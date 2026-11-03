/**
 * @file test_markdown_parser.c
 * @brief Host-native tests of the Markdown parser: the analysis (headings,
 *        links, tokens) and the Scout protocol, with the samples in
 *        tests/data/markdown_parser/.
 */
#include "markdown_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the samples (tests run from the project root). */
#define DATA "tests/data/markdown_parser/"

/** Number of checks made. */
static int g_test_count = 0;
/** Number of checks that failed. */
static int g_fail_count = 0;

/** Check a condition; on failure print its line and text. */
#define TEST_ASSERT(cond) do { \
        g_test_count++; \
        if (!(cond)) { \
            g_fail_count++; \
            printf("%s:%d: failed: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/**
 * @brief Read a whole file.
 * @param path The file.
 * @param[out] length Its length.
 * @return Its contents (free() it), or NULL.
 */
static char *read_all(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long size;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)size + 1);
    if (buf) *length = fread(buf, 1, (size_t)size, f);
    fclose(f);
    return buf;
}

/**
 * @brief Tell whether a file exists.
 * @param path The file.
 * @return Non-zero if it does.
 */
static int exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/** The file system for the tests. */
static const MarkdownFiles g_files = { exists, read_all };

/**
 * @brief Analyse a text without access to other files.
 * @param text The text.
 * @return The result.
 */
static MarkdownParseResult *analyze_text(const char *text)
{
    return markdown_parser_analyze(text, strlen(text), NULL, NULL);
}

/**
 * @brief Find a diagnostic by its message.
 * @param r The result.
 * @param message The message.
 * @return The diagnostic, or NULL.
 */
static const MarkdownDiagnostic *diagnostic(const MarkdownParseResult *r, const char *message)
{
    long i;
    for (i = 0; i < r->diagnostic_count; i++) {
        if (strcmp(r->diagnostics[i].message, message) == 0) return &r->diagnostics[i];
    }
    return NULL;
}

/**
 * @brief Tell whether there is a token.
 * @param r The result.
 * @param line Its line.
 * @param col Its column.
 * @param len Its length.
 * @param kind Its kind.
 * @return Non-zero if there is.
 */
static int has_token(const MarkdownParseResult *r, long line, long col, long len, long kind)
{
    long i;
    for (i = 0; i < r->token_count; i++) {
        const long *t = r->tokens + i * 4;
        if (t[0] == line && t[1] == col && t[2] == len && t[3] == kind) return 1;
    }
    return 0;
}

/**
 * @brief Tell how many headings a text has.
 * @param text The text.
 * @return Their number.
 */
static long heading_count(const char *text)
{
    MarkdownParseResult *r = analyze_text(text);
    long n = r->symbol_count;
    markdown_parse_result_free(r);
    return n;
}

/**
 * @brief Tell whether a text gets no diagnostics (without other files).
 * @param text The text.
 * @return Non-zero if it gets none.
 */
static int no_diagnostics(const char *text)
{
    MarkdownParseResult *r = analyze_text(text);
    int ok = r->diagnostic_count == 0;
    markdown_parse_result_free(r);
    return ok;
}

/** @brief The sample: headings, links and tokens. */
static void test_sample(void)
{
    size_t length = 0;
    char *text = read_all(DATA "sample.md", &length);
    MarkdownParseResult *r;
    const MarkdownDiagnostic *d;
    long i, tokens_sorted = 1;

    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = markdown_parser_analyze(text, length, DATA "sample.md", &g_files);
    free(text);
    TEST_ASSERT(r != NULL && !r->out_of_memory && !r->stopped);
    if (!r) return;

    /* headings: code, comments and paragraphs make none */
    TEST_ASSERT(r->symbol_count == 5);
    if (r->symbol_count == 5) {
        TEST_ASSERT(strcmp(r->symbols[0].name, "Title of the Sample") == 0);
        TEST_ASSERT(strcmp(r->symbols[0].detail, "# Title of the Sample") == 0);
        TEST_ASSERT(r->symbols[0].kind == SCOUT_SYMBOL_HEADING);
        TEST_ASSERT(r->symbols[0].parent == -1);
        TEST_ASSERT(r->symbols[0].selection.start_col == 3 && r->symbols[0].selection.end_col == 24);
        TEST_ASSERT(strcmp(r->symbols[1].name, "Setext heading") == 0);
        TEST_ASSERT(r->symbols[1].level == 2 && r->symbols[1].parent == 0);
        TEST_ASSERT(r->symbols[1].range.start_line == 6 && r->symbols[1].range.end_line == 7);
        TEST_ASSERT(strcmp(r->symbols[2].name, "Gr\xf6\xdf" "e & snake_case") == 0);
        TEST_ASSERT(r->symbols[2].parent == 0);
        TEST_ASSERT(strcmp(r->symbols[3].detail, "### Repeated") == 0 && r->symbols[3].parent == 2);
        TEST_ASSERT(strcmp(r->symbols[4].name, "Repeated") == 0 && r->symbols[4].parent == 2);
    }

    /* what is wrong, and nothing else */
    TEST_ASSERT((d = diagnostic(r, "anchor not found: #nowhere")) != NULL);
    if (d) TEST_ASSERT(d->range.start_line == 3 && d->severity == SCOUT_SEVERITY_WARNING);
    TEST_ASSERT(diagnostic(r, "anchor not found in that file: #missing") != NULL);
    TEST_ASSERT(diagnostic(r, "file not found: gone.md") != NULL);
    TEST_ASSERT(diagnostic(r, "file not found: missing.iff") != NULL);
    TEST_ASSERT(diagnostic(r, "file not found: gone-too.md") != NULL);
    TEST_ASSERT(diagnostic(r, "file not found: gone-list.md") != NULL);
    TEST_ASSERT(diagnostic(r, "anchor not found: #repeated-2") != NULL);
    TEST_ASSERT(diagnostic(r, "link has no target") != NULL);
    TEST_ASSERT(diagnostic(r, "reference not defined: nothing") != NULL);
    TEST_ASSERT(diagnostic(r, "code fence is not closed") != NULL);
    TEST_ASSERT(r->diagnostic_count == 10);

    /* tokens: heading, code, target */
    TEST_ASSERT(has_token(r, 1, 1, 23, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 6, 1, 14, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 7, 1, 14, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 11, 22, 8, SCOUT_TOKEN_MACRO));
    TEST_ASSERT(has_token(r, 4, 5, 29, SCOUT_TOKEN_CONSTANT));
    for (i = 1; i < r->token_count; i++) {
        const long *a = r->tokens + (i - 1) * 4, *b = r->tokens + i * 4;
        if (a[0] > b[0] || (a[0] == b[0] && a[1] + a[2] > b[1])) tokens_sorted = 0;
    }
    TEST_ASSERT(tokens_sorted);
    markdown_parse_result_free(r);
}

/** @brief Markers, tables and raw HTML. */
static void test_markers(void)
{
    MarkdownParseResult *r;
    long f = SCOUT_TOKEN_FUNCTION, h = SCOUT_TOKEN_PARAMETER;

    r = analyze_text("- a\n  12. b\n+ [x] c\n* [ ] d\n-no\n\n> - [X] e\n\n- - -\n");
    TEST_ASSERT(has_token(r, 1, 1, 1, f));
    TEST_ASSERT(has_token(r, 2, 3, 3, f));
    TEST_ASSERT(has_token(r, 3, 1, 1, f) && has_token(r, 3, 3, 3, f));
    TEST_ASSERT(has_token(r, 4, 1, 1, f) && has_token(r, 4, 3, 3, f));
    TEST_ASSERT(has_token(r, 7, 1, 1, f) && has_token(r, 7, 3, 1, f) && has_token(r, 7, 5, 3, f));
    TEST_ASSERT(has_token(r, 9, 1, 5, f));
    TEST_ASSERT(r->token_count == 10);
    markdown_parse_result_free(r);

    r = analyze_text("| a | `b|c` \\| |\n|:--|--:|\n| d | e |\nf | g\n\nh | i\n");
    TEST_ASSERT(has_token(r, 1, 1, 1, f) && has_token(r, 1, 5, 1, f) && has_token(r, 1, 16, 1, f));
    TEST_ASSERT(has_token(r, 1, 7, 5, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 2, 1, 9, f));
    TEST_ASSERT(has_token(r, 3, 1, 1, f) && has_token(r, 3, 5, 1, f) && has_token(r, 3, 9, 1, f));
    TEST_ASSERT(has_token(r, 4, 3, 1, f));
    TEST_ASSERT(r->token_count == 9);
    markdown_parse_result_free(r);

    r = analyze_text("a **b** *c* _d_ ~~e~~ ***f*** g\n");
    TEST_ASSERT(r->token_count == 10);
    TEST_ASSERT(has_token(r, 1, 3, 2, f) && has_token(r, 1, 6, 2, f));
    TEST_ASSERT(has_token(r, 1, 9, 1, f) && has_token(r, 1, 11, 1, f));
    TEST_ASSERT(has_token(r, 1, 17, 2, f) && has_token(r, 1, 20, 2, f));
    TEST_ASSERT(has_token(r, 1, 23, 3, f) && has_token(r, 1, 27, 3, f));
    markdown_parse_result_free(r);

    /* marks that are none, and nested ones */
    r = analyze_text("a * b * c, snake_case_name, 2 ** 3, **open, `*x*`, \\*y*\n");
    TEST_ASSERT(r->token_count == 1 && has_token(r, 1, 45, 5, SCOUT_TOKEN_CONSTANT));
    markdown_parse_result_free(r);
    r = analyze_text("*a **b** c*\n");
    TEST_ASSERT(r->token_count == 4 && has_token(r, 1, 11, 1, f) && has_token(r, 1, 7, 2, f));
    markdown_parse_result_free(r);

    r = analyze_text("<b>x</b> &auml; a & b <http://a.b> <me@a.b> 1 < 2\n");
    TEST_ASSERT(has_token(r, 1, 1, 3, h) && has_token(r, 1, 5, 4, h) && has_token(r, 1, 10, 6, h));
    TEST_ASSERT(has_token(r, 1, 23, 12, SCOUT_TOKEN_MACRO) && has_token(r, 1, 36, 8, SCOUT_TOKEN_MACRO));
    TEST_ASSERT(r->token_count == 5);
    markdown_parse_result_free(r);

    /* a heading stays one token; a list item ends a paragraph */
    r = analyze_text("# a *b* | c\n");
    TEST_ASSERT(r->token_count == 1);
    markdown_parse_result_free(r);
    TEST_ASSERT(heading_count("a\n- b\n---\n") == 0);
    r = analyze_text("a *b*\n===\n");
    TEST_ASSERT(r->token_count == 2 && has_token(r, 1, 1, 5, SCOUT_TOKEN_TYPE));
    markdown_parse_result_free(r);
}

/** @brief Headings and what only looks like one. */
static void test_headings(void)
{
    MarkdownParseResult *r;
    TEST_ASSERT(heading_count("# One\n##Two\n####### Seven\n   # Three spaces\n") == 2);
    TEST_ASSERT(heading_count("\n---\n\n===\n") == 0);
    TEST_ASSERT(heading_count("- item\n---\n") == 0);
    TEST_ASSERT(heading_count("> quote\n===\n") == 0);
    TEST_ASSERT(heading_count("a\nb\n===\n") == 1);
    TEST_ASSERT(heading_count("#\n# #\n") == 0);
    TEST_ASSERT(heading_count("a\r\n===\r\n\r\n# b\r# c\r") == 3);

    r = analyze_text("first\nsecond\n===\n");
    TEST_ASSERT(r->symbol_count == 1 && strcmp(r->symbols[0].name, "first second") == 0);
    TEST_ASSERT(has_token(r, 1, 1, 5, SCOUT_TOKEN_TYPE) && has_token(r, 2, 1, 6, SCOUT_TOKEN_TYPE));
    markdown_parse_result_free(r);

    r = analyze_text("## [Link](a.md) **bold** C# ##\n");
    TEST_ASSERT(r->symbol_count == 1 && strcmp(r->symbols[0].name, "Link bold C#") == 0);
    markdown_parse_result_free(r);

    /* levels jump: the parent is the nearest one above */
    r = analyze_text("# a\n### b\n## c\n### d\n# e\n");
    TEST_ASSERT(r->symbol_count == 5);
    if (r->symbol_count == 5) {
        TEST_ASSERT(r->symbols[1].parent == 0 && r->symbols[2].parent == 0);
        TEST_ASSERT(r->symbols[3].parent == 2 && r->symbols[4].parent == -1);
    }
    markdown_parse_result_free(r);
}

/** @brief Anchors of headings, as mdtohtml makes them. */
static void test_anchors(void)
{
    TEST_ASSERT(no_diagnostics("# Hello, World!\n[a](#hello-world)\n"));
    TEST_ASSERT(no_diagnostics("# A - B\n[a](#a---b)\n"));
    TEST_ASSERT(no_diagnostics("# snake_case *em* _em_\n[a](#snake_case-em-em)\n"));
    TEST_ASSERT(no_diagnostics("# `a_b` [c](d.md)\n[a](#a_b-c)\n"));
    TEST_ASSERT(no_diagnostics("# a_b_c _d_e_ f_ __g__\n[a](#a_b_c-d_e-f_-g)\n"));
    TEST_ASSERT(no_diagnostics("# AT&T &amp; co\n[a](#att--co)\n"));
    TEST_ASSERT(no_diagnostics("# x\n# x\n# x\n[a](#x-2)\n"));
    TEST_ASSERT(no_diagnostics("[before](#later)\n\n# Later\n"));
    TEST_ASSERT(no_diagnostics("[a](#)\n[b](#%C3%A4)\n<a id=\"\xc3\xa4\"></a>\n"));
    TEST_ASSERT(!no_diagnostics("# Hello\n[a](#Hello)\n"));
}

/** @brief Links that are none, and links without other files. */
static void test_links(void)
{
    TEST_ASSERT(no_diagnostics("[a](other.md) needs files, so no check here\n"));
    TEST_ASSERT(no_diagnostics("no](link.md) and [x]\n[^1]: a note\n"));
    TEST_ASSERT(no_diagnostics("[a [b] c](http://x/(y))\n"));
    TEST_ASSERT(no_diagnostics("[![image](a.png)](http://x)\n"));
    TEST_ASSERT(no_diagnostics("[ref][]\n\n[REF]: http://x\n"));
    TEST_ASSERT(no_diagnostics("[two words][ A  name ]\n\n[a name]: http://x\n"));
    TEST_ASSERT(no_diagnostics("``code ` [a](#b)`` <b title=\"[a](#b)\">\n"));
    TEST_ASSERT(no_diagnostics("<!-- [a](#b) --> text\n"));
    TEST_ASSERT(no_diagnostics(""));
    TEST_ASSERT(no_diagnostics("\n\n"));
    TEST_ASSERT(!no_diagnostics("<!-- c --> [a](#b)\n"));
    TEST_ASSERT(!no_diagnostics("<!-- c\n--> [a](#b)\n"));
    TEST_ASSERT(!no_diagnostics("[a\nb](#c)\n"));
    TEST_ASSERT(!no_diagnostics("[a]( )\n"));
}

/** @brief The Scout protocol: describe and analyze. */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang, *symbols;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(markdown_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "markdown") == 0);
    TEST_ASSERT(json_array_length(json_obj_get_array(lang, "extensions")) == 2);
    TEST_ASSERT(json_obj_get(lang, "syntax") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(markdown_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                               "\"params\":{\"path\":\"" DATA "sample.md\",\"want\":[\"symbols\"]}}",
                                               &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    symbols = json_obj_get_array(result, "symbols");
    TEST_ASSERT(json_array_length(symbols) == 5);
    TEST_ASSERT(json_obj_get_int(json_array_get(symbols, 1), "kind", 0) == SCOUT_SYMBOL_HEADING);
    TEST_ASSERT(strcmp(json_obj_get_string(json_array_get(symbols, 1), "scope", ""), "file") == 0);
    TEST_ASSERT(json_obj_get_int(json_array_get(symbols, 1), "parent", -1) == 0);
    TEST_ASSERT(json_obj_get(result, "diagnostics") == NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(markdown_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                               "\"params\":{\"path\":\"" DATA "sample.md\",\"want\":[\"diagnostics\"]}}",
                                               &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "file not found: gone.md") != NULL);

    out.length = 0;
    TEST_ASSERT(markdown_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                               "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(markdown_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"scout/analyze\","
                                               "\"params\":{\"path\":\"" DATA "missing.md\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "cannot read") != NULL);
    json_writer_free(&out);
}

/**
 * @brief Run the tests.
 * @return 0 if all passed, 1 otherwise.
 */
int main(void)
{
    test_sample();
    test_markers();
    test_headings();
    test_anchors();
    test_links();
    test_rpc();
    printf("markdown_parser: %d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
