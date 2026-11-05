/**
 * @file test_json_parser.c
 * @brief Host-native tests of the JSON parser: the check (syntax errors,
 *        keys defined twice), symbols, tokens and the Scout protocol, with
 *        the sample in tests/data/json_parser/.
 */
#include "json_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the samples (tests run from the project root). */
#define DATA "tests/data/json_parser/"

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
 * @brief Check a text and return its only error.
 * @param text The text.
 * @param[out] line The error's line (0 if there is none).
 * @param[out] col Its column.
 * @return The message (static copy), or "" if there is no error.
 */
static const char *error_of(const char *text, long *line, long *col)
{
    static char message[200];
    JsonDocResult *r = json_parser_analyze(text, strlen(text));
    long i;
    message[0] = '\0';
    *line = *col = 0;
    for (i = 0; r && i < r->diagnostic_count; i++) {
        if (r->diagnostics[i].severity == SCOUT_SEVERITY_ERROR) {
            snprintf(message, sizeof(message), "%s", r->diagnostics[i].message);
            *line = r->diagnostics[i].range.start_line;
            *col = r->diagnostics[i].range.start_col;
            break;
        }
    }
    json_doc_result_free(r);
    return message;
}

/**
 * @brief Tell whether a text has the error expected, at a place.
 * @param text The text.
 * @param message The message expected.
 * @param line Its line.
 * @param col Its column.
 * @return Non-zero if it has.
 */
static int error_is(const char *text, const char *message, long line, long col)
{
    long l, c;
    const char *m = error_of(text, &l, &c);
    if (strcmp(m, message) != 0 || l != line || c != col) {
        printf("  \"%s\": got \"%s\" at %ld:%ld\n", text, m, l, c);
        return 0;
    }
    return 1;
}

/** @brief The sample: symbols, tokens, the key defined twice. */
static void test_sample(void)
{
    size_t length = 0;
    char *text = read_all(DATA "sample.json", &length);
    JsonDocResult *r;
    const JsonDocSymbol *s;

    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = json_parser_analyze(text, length);
    free(text);
    TEST_ASSERT(r != NULL && !r->out_of_memory && !r->stopped);
    if (!r) return;

    TEST_ASSERT(r->diagnostic_count == 1);
    if (r->diagnostic_count == 1) {
        TEST_ASSERT(r->diagnostics[0].severity == SCOUT_SEVERITY_WARNING);
        TEST_ASSERT(strcmp(r->diagnostics[0].message, "key defined twice in this object: name") == 0);
        TEST_ASSERT(r->diagnostics[0].range.start_line == 11 && r->diagnostics[0].range.start_col == 3);
    }

    /* the keys of the root and of the objects that are their values */
    TEST_ASSERT(r->symbol_count == 11);
    if (r->symbol_count == 11) {
        s = r->symbols;
        TEST_ASSERT(strcmp(s[0].name, "version") == 0 && strcmp(s[0].detail, "1") == 0 && s[0].parent == -1);
        TEST_ASSERT(s[0].kind == SCOUT_SYMBOL_KEY);
        TEST_ASSERT(s[0].selection.start_line == 2 && s[0].selection.start_col == 4 && s[0].selection.end_col == 11);
        TEST_ASSERT(strcmp(s[1].name, "Gr\xfc\xdf" "e") != 0 && strcmp(s[1].name, "name") == 0);
        TEST_ASSERT(strcmp(s[1].detail, "\"Gr\xfc\xdf" "e aus \\\"Amiga\\\"\"") == 0);
        TEST_ASSERT(strcmp(s[2].name, "window") == 0 && strcmp(s[2].detail, "{...}") == 0);
        TEST_ASSERT(s[2].range.start_line == 4 && s[2].range.end_line == 9 && s[2].range.end_col == 4);
        TEST_ASSERT(strcmp(s[3].name, "left") == 0 && s[3].parent == 2);
        TEST_ASSERT(strcmp(s[4].detail, "-2.5e3") == 0);
        TEST_ASSERT(strlen(s[5].detail) == 60 && strcmp(s[5].detail + 57, "...") == 0);
        TEST_ASSERT(strcmp(s[6].name, "flags") == 0 && strcmp(s[6].detail, "[...]") == 0);
        TEST_ASSERT(strcmp(s[7].name, "deep") == 0 && s[7].parent == 2);
        TEST_ASSERT(strcmp(s[8].name, "list") == 0 && s[8].parent == -1);
        TEST_ASSERT(strcmp(s[9].name, "name") == 0 && strcmp(s[9].detail, "\"twice\"") == 0);
        TEST_ASSERT(strcmp(s[10].name, "empty") == 0 && s[10].range.end_line == 12 && s[10].range.end_col == 14);
    }

    /* every key is a token with its quotes, "hidden" and "inArray" too */
    TEST_ASSERT(r->token_count == 13);
    if (r->token_count == 13) {
        TEST_ASSERT(r->tokens[0] == 2 && r->tokens[1] == 3 && r->tokens[2] == 9 && r->tokens[3] == SCOUT_TOKEN_TYPE);
        TEST_ASSERT(r->tokens[8 * 4] == 8 && r->tokens[8 * 4 + 1] == 15 && r->tokens[8 * 4 + 2] == 8);
    }
    json_doc_result_free(r);
}

/** @brief Syntax errors: only the first, at its place. */
static void test_errors(void)
{
    long l, c;
    TEST_ASSERT(error_is("", "", 0, 0));
    TEST_ASSERT(error_is(" \r\n\t\n", "", 0, 0));
    TEST_ASSERT(error_is("[1, 2,\n 3]", "", 0, 0));
    TEST_ASSERT(error_is("{\"a\": 1 \"b\": 2}", "comma missing before this key", 1, 9));
    TEST_ASSERT(error_is("[1 2]", "comma missing before this value", 1, 4));
    TEST_ASSERT(error_is("{\"a\": 1,\n}", "comma before '}'", 1, 8));
    TEST_ASSERT(error_is("[1, 2, ]", "comma before ']'", 1, 6));
    TEST_ASSERT(error_is("{\n  \"a\": [1,\n", "'[' is not closed", 2, 8));
    TEST_ASSERT(error_is("{\"a\": 1", "'{' is not closed", 1, 1));
    TEST_ASSERT(error_is("{\"a\": \"b}", "string is not closed", 1, 7));
    TEST_ASSERT(error_is("{\"a\": \"b\n\"}", "string is not closed", 1, 7));
    TEST_ASSERT(error_is("[\n{\"a\": 1]", "']' does not close the '{' of line 2", 2, 8));
    TEST_ASSERT(error_is("[1]]", "']' without an opening '['", 1, 4));
    TEST_ASSERT(error_is("{} {}", "text after the end of the JSON value", 1, 4));
    TEST_ASSERT(error_is("{\"a\" 1}", "':' expected after the key", 1, 6));
    TEST_ASSERT(error_is("{a: 1}", "not a JSON value: a (strings need double quotes)", 1, 2));
    TEST_ASSERT(error_is("{1: 1}", "key expected (a string in double quotes)", 1, 2));
    TEST_ASSERT(error_is("{'a': 1}", "strings need double quotes", 1, 2));
    TEST_ASSERT(error_is("[True]", "not a JSON value: True (strings need double quotes)", 1, 2));
    TEST_ASSERT(error_is("// note\n{}", "comments are not allowed in JSON", 1, 1));
    TEST_ASSERT(error_is("[1, /* x */ 2]", "comments are not allowed in JSON", 1, 5));
    TEST_ASSERT(error_is("[01]", "invalid number: 01", 1, 2));
    TEST_ASSERT(error_is("[1.]", "invalid number: 1.", 1, 2));
    TEST_ASSERT(error_is("[-]", "invalid number: -", 1, 2));
    TEST_ASSERT(error_is("[+1]", "unexpected character: +", 1, 2));
    TEST_ASSERT(error_is("[1e5, -0.5E-3, 0]", "", 0, 0));
    TEST_ASSERT(error_is("[\"a\\x\"]", "invalid escape in a string: \\x", 1, 4));
    TEST_ASSERT(error_is("[\"\\u00e9\\n\\/\"]", "", 0, 0));
    TEST_ASSERT(error_is("[\"\\u12\"]", "invalid escape in a string: \\u", 1, 3));
    TEST_ASSERT(error_is("[\"a\tb\"]", "tab in a string (write \\t)", 1, 4));
    TEST_ASSERT(error_is("[\"\xe9\"]", "", 0, 0));
    TEST_ASSERT(error_is(":", "value expected", 1, 1));
    TEST_ASSERT(error_is("{\"a\":}", "value expected", 1, 6));
    TEST_ASSERT(error_is("{\"a\":", "'{' is not closed", 1, 1));
    TEST_ASSERT(error_is("{\"a\" : 1 ]", "']' does not close the '{' of line 1", 1, 10));
    TEST_ASSERT(error_is("\r\n[\r\n1,,2]", "value expected", 3, 3));
    /* the first error ends the check; the warnings before it stay */
    {
        const char *text = "{\"a\": 1, \"a\": 2, \"b\" 3, \"c\": x}";
        JsonDocResult *r = json_parser_analyze(text, strlen(text));
        TEST_ASSERT(r && r->diagnostic_count == 2);
        if (r && r->diagnostic_count == 2) {
            TEST_ASSERT(r->diagnostics[0].severity == SCOUT_SEVERITY_WARNING);
            TEST_ASSERT(strcmp(r->diagnostics[1].message, "':' expected after the key") == 0);
        }
        json_doc_result_free(r);
    }
    /* keys of different objects may repeat */
    TEST_ASSERT(strcmp(error_of("[{\"a\": 1}, {\"a\": 2}]", &l, &c), "") == 0);
    {
        const char *text = "[{\"a\": 1}, {\"a\": 2}]";
        JsonDocResult *r = json_parser_analyze(text, strlen(text));
        TEST_ASSERT(r && r->diagnostic_count == 0 && r->symbol_count == 0 && r->token_count == 2);
        json_doc_result_free(r);
    }
    {   /* deeply nested: an error, no crash */
        static char deep[2100];
        memset(deep, '[', 2050);
        deep[2050] = '\0';
        TEST_ASSERT(strcmp(error_of(deep, &l, &c), "nested too deeply") == 0 && c == 1001);
    }
}

/** @brief The Scout protocol: describe and analyze. */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang, *symbols;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(json_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "json") == 0);
    TEST_ASSERT(json_array_length(json_obj_get_array(lang, "extensions")) == 1);
    TEST_ASSERT(json_obj_get_array(json_obj_get(lang, "syntax"), "strings") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(json_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                           "\"params\":{\"path\":\"" DATA "sample.json\",\"want\":[\"symbols\"]}}",
                                           &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    symbols = json_obj_get_array(result, "symbols");
    TEST_ASSERT(json_array_length(symbols) == 11);
    TEST_ASSERT(json_obj_get_int(json_array_get(symbols, 3), "kind", 0) == SCOUT_SYMBOL_KEY);
    TEST_ASSERT(strcmp(json_obj_get_string(json_array_get(symbols, 3), "scope", ""), "file") == 0);
    TEST_ASSERT(json_obj_get_int(json_array_get(symbols, 3), "parent", -1) == 2);
    TEST_ASSERT(json_obj_get(result, "diagnostics") == NULL && json_obj_get(result, "tokens") == NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(json_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                           "\"params\":{\"path\":\"" DATA "sample.json\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "key defined twice in this object: name") != NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"tokens\":[2,3,9,1,") != NULL);

    out.length = 0;
    TEST_ASSERT(json_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                           "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(json_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"scout/analyze\","
                                           "\"params\":{\"path\":\"" DATA "missing.json\"}}", &out));
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
    test_errors();
    test_rpc();
    printf("json_parser: %d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
