/**
 * @file test_guide_parser.c
 * @brief Host-native tests of the AmigaGuide parser: the checks, the nodes
 *        and macros as symbols, the tokens and the Scout protocol, with the sample in tests/data/guide_parser/.
 */
#include "guide_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the samples (tests run from the project root). */
#define DATA "tests/data/guide_parser/"

/** The lines every text in the tests of errors starts with. */
#define HEAD "@database x\n@node a\n"

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
 * @brief Check a text for exactly one diagnostic.
 * @param text The text.
 * @param severity The severity it must have.
 * @param line The line it must be in.
 * @param col The column it must start in.
 * @param message A part of its message.
 * @return Non-zero if the text has just that diagnostic.
 */
static int only(const char *text, int severity, long line, long col, const char *message)
{
    GuideResult *r = guide_parser_analyze(text, strlen(text));
    int ok = r && r->diagnostic_count == 1 && r->diagnostics[0].severity == severity
             && r->diagnostics[0].range.start_line == line && r->diagnostics[0].range.start_col == col
             && strstr(r->diagnostics[0].message, message) != NULL;
    if (r && !ok) {
        long i;
        for (i = 0; i < r->diagnostic_count; i++)
            printf("  got %ld:%ld: %s\n", r->diagnostics[i].range.start_line, r->diagnostics[i].range.start_col,
                   r->diagnostics[i].message);
    }
    guide_result_free(r);
    return ok;
}

/**
 * @brief Count the diagnostics of a text.
 * @param text The text.
 * @return Their number.
 */
static long count(const char *text)
{
    GuideResult *r = guide_parser_analyze(text, strlen(text));
    long n = r ? r->diagnostic_count : -1;
    guide_result_free(r);
    return n;
}

/**
 * @brief Tell whether a result has a token.
 * @param r The result.
 * @param line Its line.
 * @param col Its column.
 * @param length Its length.
 * @param kind Its kind.
 * @return Non-zero if it is there.
 */
static int has_token(const GuideResult *r, long line, long col, long length, int kind)
{
    long i;
    for (i = 0; i < r->token_count; i++) {
        const long *t = r->tokens + i * 4;
        if (t[0] == line && t[1] == col && t[2] == length && t[3] == kind) return 1;
    }
    return 0;
}

/** @brief The sample has no problems, and its commands become tokens. */
static void test_sample(void)
{
    size_t length = 0;
    char *text = read_all(DATA "sample.guide", &length);
    GuideResult *r;
    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = guide_parser_analyze(text, length);
    TEST_ASSERT(r != NULL);
    if (r) {
        long i;
        TEST_ASSERT(r->diagnostic_count == 0);
        for (i = 0; i < r->diagnostic_count; i++)
            printf("  sample %ld: %s\n", r->diagnostics[i].range.start_line, r->diagnostics[i].message);
        TEST_ASSERT(r->symbol_count == 3);
        TEST_ASSERT(has_token(r, 1, 1, 9, SCOUT_TOKEN_FUNCTION));       /* @database */
        TEST_ASSERT(has_token(r, 9, 1, 5, SCOUT_TOKEN_FUNCTION));       /* @node */
        TEST_ASSERT(has_token(r, 9, 7, 4, SCOUT_TOKEN_LABEL));          /* Main */
        TEST_ASSERT(has_token(r, 9, 12, 15, SCOUT_TOKEN_CONSTANT));     /* its title */
        TEST_ASSERT(has_token(r, 13, 1, 2, SCOUT_TOKEN_MACRO));         /* @{ */
        TEST_ASSERT(has_token(r, 13, 3, 13, SCOUT_TOKEN_CONSTANT));     /* the label */
        TEST_ASSERT(has_token(r, 13, 17, 4, SCOUT_TOKEN_FUNCTION));     /* link */
        TEST_ASSERT(has_token(r, 13, 22, 6, SCOUT_TOKEN_LABEL));        /* the target */
        TEST_ASSERT(has_token(r, 13, 28, 1, SCOUT_TOKEN_MACRO));        /* } */
        TEST_ASSERT(!has_token(r, 7, 1, 4, SCOUT_TOKEN_FUNCTION));      /* @rem is a comment */
    }
    guide_result_free(r);
    free(text);
}

/** @brief The nodes and macros as symbols: names, details, ranges. */
static void test_symbols(void)
{
    static const char text[] =
        "@database x\n"                        /* 1 */
        "@node One \"First\"\n"                 /* 2 */
        "@title \"not used\"\n"                 /* 3 */
        "@endnode\n"                           /* 4 */
        "@node \"Two words\"\n"                 /* 5 */
        "@title \"Second\"\n"                   /* 6 */
        "text\n"                               /* 7 */
        "@node Three\n"                        /* 8 */
        "more text\n";                         /* 9 */
    GuideResult *r = guide_parser_analyze(text, sizeof(text) - 1);
    TEST_ASSERT(r != NULL);
    if (!r) return;
    TEST_ASSERT(r->symbol_count == 3);
    if (r->symbol_count == 3) {
        const GuideSymbol *s = r->symbols;
        TEST_ASSERT(strcmp(s[0].name, "One") == 0 && strcmp(s[0].detail, "First") == 0);
        TEST_ASSERT(s[0].kind == SCOUT_SYMBOL_HEADING);
        TEST_ASSERT(s[0].range.start_line == 2 && s[0].range.start_col == 1);
        TEST_ASSERT(s[0].range.end_line == 4 && s[0].range.end_col == 9);
        TEST_ASSERT(s[0].selection.start_line == 2 && s[0].selection.start_col == 7);
        TEST_ASSERT(s[0].selection.end_line == 2 && s[0].selection.end_col == 10);
        TEST_ASSERT(strcmp(s[1].name, "Two words") == 0 && strcmp(s[1].detail, "Second") == 0);
        TEST_ASSERT(s[1].selection.start_col == 8 && s[1].selection.end_col == 17);
        TEST_ASSERT(s[1].range.end_line == 7 && s[1].range.end_col == 5);   /* before the next @node */
        TEST_ASSERT(strcmp(s[2].name, "Three") == 0 && s[2].detail == NULL);
        TEST_ASSERT(s[2].range.end_line >= 9);                              /* to the end of the file */
    }
    guide_result_free(r);

    r = guide_parser_analyze("@database x\n@node\n@endnode\n", 27);
    TEST_ASSERT(r != NULL && r->symbol_count == 0);
    guide_result_free(r);

    {
        static const char macros[] =
            "@database x\n"
            "@macro Fat \"@{b}$1@{ub}\" \n"
            "@node a\n"
            "@macro plain @{i} $1 @{ui}\n"
            "@macro empty\n"
            "text\n"
            "@endnode\n";
        r = guide_parser_analyze(macros, sizeof(macros) - 1);
        TEST_ASSERT(r != NULL && r->symbol_count == 4);
        if (r && r->symbol_count == 4) {
            const GuideSymbol *s = r->symbols;
            TEST_ASSERT(strcmp(s[0].name, "Fat") == 0 && s[0].kind == SCOUT_SYMBOL_MACRO);
            TEST_ASSERT(strcmp(s[0].detail, "@{b}$1@{ub}") == 0);
            TEST_ASSERT(s[0].range.start_line == 2 && s[0].range.end_line == 2);
            TEST_ASSERT(s[0].selection.start_col == 8 && s[0].selection.end_col == 11);
            TEST_ASSERT(s[1].kind == SCOUT_SYMBOL_HEADING && s[1].range.end_line == 7);     /* a macro does not end it */
            TEST_ASSERT(strcmp(s[2].name, "plain") == 0 && strcmp(s[2].detail, "@{i} $1 @{ui}") == 0);
            TEST_ASSERT(strcmp(s[3].name, "empty") == 0 && s[3].detail == NULL);
        }
        guide_result_free(r);
    }
}

/** @brief The structure of the file: \@database, nodes. */
static void test_structure(void)
{
    TEST_ASSERT(count("") == 0);
    TEST_ASSERT(count("  \n\n") == 0);
    TEST_ASSERT(count("@DataBase x\r\n@NODE a\r\n@{b}\r\n@EndNode\r\n") == 0);
    TEST_ASSERT(only("hello\n@node a\n@endnode\n", SCOUT_SEVERITY_ERROR, 1, 1, "first line"));
    TEST_ASSERT(count("\n@database x\n@node a\n@endnode\n") == 2);
    TEST_ASSERT(only("@database x\n", SCOUT_SEVERITY_WARNING, 1, 1, "no @node"));
    TEST_ASSERT(only(HEAD, SCOUT_SEVERITY_WARNING, 2, 1, "no @endnode"));
    TEST_ASSERT(only(HEAD "@node b\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 1, "no @endnode"));
    TEST_ASSERT(only(HEAD "@endnode\n@endnode\n", SCOUT_SEVERITY_WARNING, 4, 1, "without a node"));
    TEST_ASSERT(only(HEAD "@endnode\n@node A\n@endnode\n", SCOUT_SEVERITY_WARNING, 4, 7, "defined twice"));
    TEST_ASSERT(only("@database x\n@node\n@endnode\n", SCOUT_SEVERITY_ERROR, 2, 1, "needs a name"));
}

/** @brief Line commands: names, places, arguments. */
static void test_line_commands(void)
{
    TEST_ASSERT(only(HEAD "@frobnicate\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 1, "unknown command"));
    TEST_ASSERT(only(HEAD "@width 80\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 1, "outside a node"));
    TEST_ASSERT(only("@database x\n@title \"t\"\n@node a\n@endnode\n", SCOUT_SEVERITY_WARNING, 2, 1, "inside a node"));
    TEST_ASSERT(only(HEAD "@title\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 1, "more arguments"));
    TEST_ASSERT(only(HEAD "@title \"open\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 8, "quote"));
    TEST_ASSERT(only(HEAD "@next nowhere\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 7, "no node 'nowhere'"));
    TEST_ASSERT(count(HEAD "@next other.guide/x\n@help sys:h/main\n@endnode\n") == 0);
    TEST_ASSERT(only("@database x\n@width many\n@node a\n@endnode\n", SCOUT_SEVERITY_WARNING, 2, 8, "number"));
    TEST_ASSERT(only("@database x\n@dnode a\n@node a\n@endnode\n", SCOUT_SEVERITY_HINT, 2, 1, "obsolete"));
}

/** @brief Text commands: closing, labels, arguments, macros. */
static void test_text_commands(void)
{
    TEST_ASSERT(only(HEAD "text @{b\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 6, "not closed"));
    TEST_ASSERT(only(HEAD "@{\"lab link x}\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 3, "quote"));
    TEST_ASSERT(only(HEAD "@{b @{ub}\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 6, "'{'"));
    TEST_ASSERT(only(HEAD "@{}\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 1, "empty"));
    TEST_ASSERT(only(HEAD "@{\"label\"}\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 1, "needs a command"));
    TEST_ASSERT(only(HEAD "@{nope}\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 3, "unknown text command"));
    TEST_ASSERT(only(HEAD "@{link a}\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 1, "needs a label"));
    TEST_ASSERT(only(HEAD "@{\"x\" b}\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 3, "takes no label"));
    TEST_ASSERT(only(HEAD "@{\"x\" link}\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 7, "more arguments"));
    TEST_ASSERT(only(HEAD "@{\"x\" link gone}\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 12, "no node 'gone'"));
    TEST_ASSERT(count(HEAD "@{\"x\" link A}\n@{\"y\" link b 3}\n@endnode\n@node B\n@endnode\n") == 0);
    TEST_ASSERT(only(HEAD "@{\"x\" quit}\n@endnode\n", SCOUT_SEVERITY_HINT, 3, 7, "no effect"));
    TEST_ASSERT(only(HEAD "@{img}\n@endnode\n", SCOUT_SEVERITY_ERROR, 3, 3, "more arguments"));
    TEST_ASSERT(count(HEAD "@{IMG pics/a.iff}\n@endnode\n") == 0);
    TEST_ASSERT(only(HEAD "@{fg pink}\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 6, "colour"));
    TEST_ASSERT(only(HEAD "@{apen x}\n@endnode\n", SCOUT_SEVERITY_WARNING, 3, 8, "number"));
    TEST_ASSERT(only("@database x\n@{b}\n@node a\n@endnode\n", SCOUT_SEVERITY_WARNING, 2, 1, "outside a node"));
    TEST_ASSERT(count("@database x\n@macro m \"@{b}\"\n@node a\n@{M 1}\n@endnode\n") == 0);
    TEST_ASSERT(count(HEAD "\\@{nope \\\\ me@home\n@endnode\n") == 0);
}

/** @brief The requests of the Scout protocol. */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(guide_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "amigaguide") == 0);
    TEST_ASSERT(json_array_length(json_obj_get_array(lang, "extensions")) == 1);
    TEST_ASSERT(json_array_length(json_obj_get_array(json_obj_get(lang, "syntax"), "firstColumnComments")) == 3);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(guide_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                            "\"params\":{\"path\":\"" DATA "sample.guide\",\"want\":[\"diagnostics\"]}}",
                                            &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    TEST_ASSERT(json_array_length(json_obj_get_array(result, "diagnostics")) == 0);
    TEST_ASSERT(json_obj_get(result, "tokens") == NULL && json_obj_get(result, "symbols") == NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(guide_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                            "\"params\":{\"path\":\"" DATA "sample.guide\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"tokens\":[1,1,9,2,") != NULL);
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    TEST_ASSERT(json_array_length(json_obj_get_array(result, "symbols")) == 3);
    TEST_ASSERT(strcmp(json_obj_get_string(json_array_get(json_obj_get_array(result, "symbols"), 1), "name", ""), "Main") == 0);
    TEST_ASSERT(json_obj_get_int(json_array_get(json_obj_get_array(result, "symbols"), 0), "kind", 0) == SCOUT_SYMBOL_MACRO);
    TEST_ASSERT(strcmp(json_obj_get_string(json_array_get(json_obj_get_array(result, "symbols"), 2), "name", ""), "second") == 0);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(guide_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                            "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(guide_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"scout/analyze\","
                                            "\"params\":{\"path\":\"" DATA "missing.guide\"}}", &out));
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
    test_symbols();
    test_structure();
    test_line_commands();
    test_text_commands();
    test_rpc();
    printf("guide_parser: %d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
