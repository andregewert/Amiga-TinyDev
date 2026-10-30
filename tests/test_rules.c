/**
 * @file test_rules.c
 * @brief Host-native tests of the generic scanner with lexical rules (the
 *        C parser's, the AmigaDOS parser's and rules of a made-up
 *        language) and of the token map.
 */
#include "rules.h"
#include "tokenmap.h"
#include "syntax.h"
#include "c_parser.h"
#include "dos_parser.h"
#include "scout_protocol.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Number of checks made. */
static int g_test_count = 0;
/** Number of checks that failed. */
static int g_fail_count = 0;

/**
 * @brief Check a condition and report it if it fails (counting both).
 * @param cond The condition.
 */
#define TEST_ASSERT(cond) do { \
    g_test_count++; \
    if (!(cond)) { \
        printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail_count++; \
    } \
} while (0)

/** Spans emitted during one scan. */
typedef struct Capture {
    SyntaxSpan spans[32];   /**< the spans */
    int count;              /**< number of them */
} Capture;

/**
 * @brief SyntaxEmit callback that records the spans.
 * @param context The Capture.
 * @param span The span.
 */
static void capture(void *context, const SyntaxSpan *span)
{
    Capture *c = (Capture *)context;
    if (c->count < 32) c->spans[c->count++] = *span;
}

/**
 * @brief Tell whether a span was emitted.
 * @param c The capture.
 * @param start Its start.
 * @param end Its end.
 * @param style Its style.
 * @return Non-zero if it was.
 */
static int has(const Capture *c, size_t start, size_t end, SyntaxStyle style)
{
    int i;
    for (i = 0; i < c->count; i++)
        if (c->spans[i].start == start && c->spans[i].end == end && c->spans[i].style == style) return 1;
    return 0;
}

/**
 * @brief Scan a line with rules into a fresh capture.
 * @param r The rules.
 * @param text The line.
 * @param state The previous state.
 * @param c The capture (emptied first).
 * @return The state at the end.
 */
static unsigned long scan(const SyntaxRules *r, const char *text, unsigned long state, Capture *c)
{
    c->count = 0;
    return rules_scan_line(r, text, state, capture, c);
}

/**
 * @brief Return the "syntax" object of a parser's description as text.
 * @param describe The parser's function writing scout/describe.
 * @return The text (free() it).
 */
static char *parser_syntax(void (*describe)(JsonWriter *))
{
    JsonWriter w, out;
    JsonValue *root;
    const JsonValue *lang;
    char *text = NULL;
    json_writer_init_alloc(&w, 2048);
    describe(&w);
    root = json_parse(json_writer_get(&w), NULL);
    lang = root != NULL ? json_array_get(json_obj_get_array(root, "languages"), 0) : NULL;
    if (lang != NULL) {
        json_writer_init_alloc(&out, 2048);
        json_write_value(&out, json_obj_get_obj(lang, "syntax"), -1);
        text = malloc(json_writer_length(&out) + 1);
        if (text != NULL) memcpy(text, json_writer_get(&out), json_writer_length(&out) + 1);
        json_writer_free(&out);
    }
    json_value_free(root);
    json_writer_free(&w);
    return text;
}

/** @brief The C parser's rules: keywords, types, constants, numbers, comments, strings, preprocessor. */
static void test_c_rules(void)
{
    char *json = parser_syntax(c_parser_describe);
    SyntaxRules *r = json != NULL ? rules_parse(json) : NULL;
    Capture c;
    unsigned long st;
    TEST_ASSERT(r != NULL);
    free(json);
    if (r == NULL) return;
    TEST_ASSERT(r->case_sensitive && r->block_count == 1 && r->string_count == 2 && r->line_comment_count == 1);

    st = scan(r, "int x = 0x1F; return NULL; // done", 0, &c);
    TEST_ASSERT(st == 0);
    TEST_ASSERT(has(&c, 0, 3, SYNTAX_TYPE));
    TEST_ASSERT(has(&c, 8, 12, SYNTAX_CONSTANT));
    TEST_ASSERT(has(&c, 14, 20, SYNTAX_KEYWORD));
    TEST_ASSERT(has(&c, 21, 25, SYNTAX_CONSTANT));
    TEST_ASSERT(has(&c, 27, 34, SYNTAX_COMMENT));
    TEST_ASSERT(c.count == 5);

    /* a digit in a word is no number; a string with an escaped quote */
    st = scan(r, "x2 = \"a\\\"b\"; c = 'q';", 0, &c);
    TEST_ASSERT(c.count == 2 && has(&c, 5, 11, SYNTAX_STRING) && has(&c, 17, 20, SYNTAX_STRING));

    /* block comments over lines */
    st = scan(r, "a /* one", 0, &c);
    TEST_ASSERT(st != 0 && has(&c, 2, 8, SYNTAX_COMMENT));
    st = scan(r, "two", st, &c);
    TEST_ASSERT(st != 0 && has(&c, 0, 3, SYNTAX_COMMENT));
    st = scan(r, "three */ while", st, &c);
    TEST_ASSERT(st == 0 && has(&c, 0, 8, SYNTAX_COMMENT) && has(&c, 9, 14, SYNTAX_KEYWORD));

    /* a preprocessor line as a whole, its comment over it, continued */
    st = scan(r, "  #define X 1 /* c */ \\", 0, &c);
    TEST_ASSERT(has(&c, 0, 23, SYNTAX_PREPROCESSOR) && has(&c, 14, 21, SYNTAX_COMMENT));
    TEST_ASSERT(!has(&c, 12, 13, SYNTAX_CONSTANT));
    st = scan(r, "  + 2", st, &c);
    TEST_ASSERT(st == 0 && has(&c, 0, 5, SYNTAX_PREPROCESSOR) && c.count == 1);

    /* an unterminated string ends at the line end (not multiline) */
    st = scan(r, "s = \"open", 0, &c);
    TEST_ASSERT(st == 0 && has(&c, 4, 9, SYNTAX_STRING));
    rules_free(r);
}

/** @brief The AmigaDOS parser's rules: case-insensitive words, ";" comments, "*" escapes, dot lines. */
static void test_dos_rules(void)
{
    char *json = parser_syntax(dos_parser_describe);
    SyntaxRules *r = json != NULL ? rules_parse(json) : NULL;
    Capture c;
    unsigned long st;
    TEST_ASSERT(r != NULL);
    free(json);
    if (r == NULL) return;
    TEST_ASSERT(!r->case_sensitive && r->string_count == 1 && r->line_comment_count == 1);
    st = scan(r, "IF NOT EXISTS ram:x ; test", 0, &c);
    TEST_ASSERT(st == 0 && has(&c, 0, 2, SYNTAX_KEYWORD) && has(&c, 3, 6, SYNTAX_CONSTANT));
    TEST_ASSERT(has(&c, 7, 13, SYNTAX_CONSTANT) && has(&c, 20, 26, SYNTAX_COMMENT) && c.count == 4);
    st = scan(r, "Copy \"a*\"b;\" TO RAM:", 0, &c);
    TEST_ASSERT(has(&c, 0, 4, SYNTAX_FUNCTION) && has(&c, 5, 12, SYNTAX_STRING) && c.count == 2);
    st = scan(r, ".KEY FROM/A", 0, &c);
    TEST_ASSERT(has(&c, 0, 11, SYNTAX_PREPROCESSOR));
    rules_free(r);
}

/** @brief Rules of a made-up language: case-insensitive keywords, nested comments, multi-line strings, $ numbers. */
static void test_other_rules(void)
{
    const char *json =
        "{\"caseSensitive\":false,\"identifier\":{\"start\":\"A-Za-z_\",\"part\":\"A-Za-z0-9_\"},"
        "\"lineComments\":[\"--\",\"-\"],"
        "\"blockComments\":[{\"open\":\"(*\",\"close\":\"*)\",\"nested\":true}],"
        "\"strings\":[{\"open\":\"[[\",\"close\":\"]]\",\"multiline\":true}],"
        "\"numbers\":{\"prefixes\":[\"$\"]},"
        "\"keywords\":{\"keyword\":[\"BEGIN\",\"end\"],\"builtin\":[\"WriteLn\"]}}";
    SyntaxRules *r = rules_parse(json);
    Capture c;
    unsigned long st;
    TEST_ASSERT(r != NULL);
    if (r == NULL) return;
    st = scan(r, "Begin writeln($FF) END -- x", 0, &c);
    TEST_ASSERT(st == 0 && has(&c, 0, 5, SYNTAX_KEYWORD) && has(&c, 6, 13, SYNTAX_FUNCTION));
    TEST_ASSERT(has(&c, 14, 17, SYNTAX_CONSTANT) && has(&c, 19, 22, SYNTAX_KEYWORD) && has(&c, 23, 27, SYNTAX_COMMENT));
    /* the longest delimiter wins: "--" not "-" */
    TEST_ASSERT(c.count == 5);
    st = scan(r, "(* a (* b *) c", 0, &c);
    TEST_ASSERT(st != 0 && has(&c, 0, 14, SYNTAX_COMMENT));
    st = scan(r, "*) begin", st, &c);
    TEST_ASSERT(st == 0 && has(&c, 0, 2, SYNTAX_COMMENT) && has(&c, 3, 8, SYNTAX_KEYWORD));
    st = scan(r, "x = [[ long", 0, &c);
    TEST_ASSERT(st != 0 && has(&c, 4, 11, SYNTAX_STRING));
    st = scan(r, "text ]] end", st, &c);
    TEST_ASSERT(st == 0 && has(&c, 0, 7, SYNTAX_STRING) && has(&c, 8, 11, SYNTAX_KEYWORD));
    rules_free(r);
    TEST_ASSERT(rules_parse("[1]") == NULL);
    TEST_ASSERT(rules_parse("not json") == NULL);
    r = rules_parse("{}");
    TEST_ASSERT(r != NULL && r->word_count == 0);
    if (r != NULL) {
        st = scan(r, "anything 12", 0, &c);
        TEST_ASSERT(st == 0 && c.count == 1 && has(&c, 9, 11, SYNTAX_CONSTANT));
    }
    rules_free(r);
}

/** @brief The token map: reading TOKENS, building, finding lines by text; syntax_scan() with tokens. */
static void test_tokens(void)
{
    const char *text = "Point p;\r\nint f(void);\nPoint p;\n\nx = MAX;\n";
    const char *answer = "{\"file\":\"a.c\",\"version\":3,\"tokens\" : [ 1,1,5,1, 2,5,1,2,\n 3,1,5,1, 5,5,3,3, 5,1,1,5 ]}";
    long count = 0, n = 0;
    long *values = tokenmap_read(answer, &count);
    TokenMap *map;
    const TokenSpan *s;
    Capture c;
    TEST_ASSERT(values != NULL && count == 20);
    map = tokenmap_build(text, strlen(text), values, count);
    free(values);
    TEST_ASSERT(map != NULL);
    if (map == NULL) return;
    /* the variable (kind 5) has no colour; line 3 has the same text as line 1 */
    TEST_ASSERT(map->span_count == 4);
    s = tokenmap_find(map, "Point p;", 8, &n);
    TEST_ASSERT(s != NULL && n == 1 && s[0].col == 0 && s[0].len == 5 && s[0].style == SYNTAX_TYPE);
    s = tokenmap_find(map, "int f(void);", 12, &n);
    TEST_ASSERT(s != NULL && n == 1 && s[0].col == 4 && s[0].style == SYNTAX_FUNCTION);
    s = tokenmap_find(map, "x = MAX;", 8, &n);
    TEST_ASSERT(s != NULL && n == 1 && s[0].col == 4 && s[0].style == SYNTAX_MACRO);
    s = tokenmap_find(map, "Point q;", 8, &n);
    TEST_ASSERT(s == NULL && n == 0);

    /* without rules: the tokens alone */
    c.count = 0;
    syntax_scan(NULL, map, "x = MAX;", 0, capture, &c);
    TEST_ASSERT(c.count == 1 && has(&c, 4, 7, SYNTAX_MACRO));
    c.count = 0;
    syntax_scan(NULL, map, "int f(void);", 0, capture, &c);
    TEST_ASSERT(c.count == 1 && has(&c, 4, 5, SYNTAX_FUNCTION));
    tokenmap_free(map);

    TEST_ASSERT(tokenmap_read("{\"tokens\":[]}", &count) == NULL && count == 0);
    TEST_ASSERT(tokenmap_build("x", 1, NULL, 0) == NULL);
}

/**
 * @brief Run the tests of the rules and the token map.
 * @return 0 when all checks pass, 1 otherwise.
 */
int main(void)
{
    printf("Running rules tests...\n");
    test_c_rules();
    test_dos_rules();
    test_other_rules();
    test_tokens();
    printf("Rules tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
