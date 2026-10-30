/**
 * @file test_dos_parser.c
 * @brief Host-native tests of the AmigaDOS script parser: the analysis
 *        (symbols, diagnostics, tokens) and the Scout protocol, with the
 *        sample script in tests/data/dos_parser/.
 */
#include "dos_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample scripts (tests run from the project root). */
#define DATA "tests/data/dos_parser/"

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

/**
 * @brief Read a whole file.
 * @param path The file.
 * @param[out] length Its length.
 * @return Its contents, terminated (free() it), or NULL.
 */
static char *load(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long size;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)size + 1);
    if (buf) {
        *length = fread(buf, 1, (size_t)size, f);
        buf[*length] = '\0';
    }
    fclose(f);
    return buf;
}

/**
 * @brief Find a symbol by kind and name.
 * @param r The result.
 * @param kind SCOUT_SYMBOL_ value.
 * @param name The name.
 * @return The symbol, or NULL.
 */
static const DosSymbol *symbol(const DosParseResult *r, int kind, const char *name)
{
    long i;
    for (i = 0; i < r->symbol_count; i++) {
        if (r->symbols[i].kind == kind && strcmp(r->symbols[i].name, name) == 0) return &r->symbols[i];
    }
    return NULL;
}

/**
 * @brief Find a diagnostic by line and the start of its message.
 * @param r The result.
 * @param line The line.
 * @param message The beginning of the message.
 * @return The diagnostic, or NULL.
 */
static const DosDiagnostic *diagnostic(const DosParseResult *r, long line, const char *message)
{
    long i;
    for (i = 0; i < r->diagnostic_count; i++) {
        const DosDiagnostic *d = &r->diagnostics[i];
        if (d->range.start_line == line && strncmp(d->message, message, strlen(message)) == 0) return d;
    }
    return NULL;
}

/**
 * @brief Tell whether there is a token.
 * @param r The result.
 * @param line Its line.
 * @param col Its column.
 * @param len Its length.
 * @param kind SCOUT_TOKEN_ value.
 * @return Non-zero if there is.
 */
static int token(const DosParseResult *r, long line, long col, long len, long kind)
{
    long i;
    for (i = 0; i < r->token_count; i++) {
        const long *t = &r->tokens[i * 4];
        if (t[0] == line && t[1] == col && t[2] == len && t[3] == kind) return 1;
    }
    return 0;
}

/**
 * @brief Analyse a script given as a string.
 * @param text The script.
 * @return The result.
 */
static DosParseResult *analyze(const char *text)
{
    return dos_parser_analyze(text, strlen(text));
}

/**
 * @brief The sample script: symbols, diagnostics and tokens.
 */
static void test_sample(void)
{
    size_t length = 0;
    char *src = load(DATA "sample.script", &length);
    DosParseResult *r;
    const DosSymbol *s;
    long i;
    TEST_ASSERT(src != NULL);
    if (!src) return;
    r = dos_parser_analyze(src, length);
    free(src);
    TEST_ASSERT(r != NULL);
    if (!r) return;
    TEST_ASSERT(!r->out_of_memory && !r->stopped);

    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "FROM");
    TEST_ASSERT(s && s->range.start_line == 1 && s->selection.start_col == 6 && s->selection.end_col == 10);
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "FROM/A") == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "TO") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "QUIET") != NULL);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "zaehler");
    TEST_ASSERT(s && !s->global && s->range.start_line == 13);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "Editor");
    TEST_ASSERT(s && s->global);
    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "ll");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "Alias ll List {LFORMAT=%N}") == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "oben") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "weiter") != NULL);
    s = symbol(r, SCOUT_SYMBOL_LABEL, "ende");
    TEST_ASSERT(s && s->range.start_line == 25);

    /* the diagnostics */
    TEST_ASSERT(diagnostic(r, 17, "NAME is not a parameter") != NULL);
    TEST_ASSERT(diagnostic(r, 23, "no label fehlt") != NULL);
    TEST_ASSERT(diagnostic(r, 24, "label weiter is below") == NULL);
    TEST_ASSERT(diagnostic(r, 26, "label ende is defined twice") != NULL);
    TEST_ASSERT(diagnostic(r, 27, "unknown dot command .UNKNOWN") != NULL);
    TEST_ASSERT(diagnostic(r, 28, "ENDIF without IF") != NULL);
    TEST_ASSERT(diagnostic(r, 29, "ELSE without IF") != NULL);
    TEST_ASSERT(diagnostic(r, 30, "the string is not closed") != NULL);
    TEST_ASSERT(diagnostic(r, 31, "IF without ENDIF") != NULL);
    /* the escaped quotes in line 8 close nothing; SKIP ende finds it below */
    TEST_ASSERT(diagnostic(r, 8, "the string") == NULL);
    TEST_ASSERT(diagnostic(r, 9, "") == NULL);
    TEST_ASSERT(diagnostic(r, 20, "") == NULL);
    for (i = 1; i < r->diagnostic_count; i++)
        TEST_ASSERT(r->diagnostics[i - 1].range.start_line <= r->diagnostics[i].range.start_line);

    /* the tokens */
    TEST_ASSERT(token(r, 1, 6, 4, SCOUT_TOKEN_PARAMETER));     /* FROM in the template */
    TEST_ASSERT(token(r, 2, 10, 2, SCOUT_TOKEN_PARAMETER));    /* .DEFAULT TO */
    TEST_ASSERT(token(r, 7, 15, 6, SCOUT_TOKEN_PARAMETER));    /* {FROM} */
    TEST_ASSERT(token(r, 11, 17, 9, SCOUT_TOKEN_PARAMETER));  /* {TO$RAM:} */
    TEST_ASSERT(token(r, 9, 8, 4, SCOUT_TOKEN_LABEL));         /* SKIP ende */
    TEST_ASSERT(token(r, 16, 5, 4, SCOUT_TOKEN_LABEL));        /* LAB oben */
    TEST_ASSERT(token(r, 17, 12, 8, SCOUT_TOKEN_PARAMETER));   /* $zaehler */
    TEST_ASSERT(token(r, 17, 25, 9, SCOUT_TOKEN_PARAMETER));   /* ${Editor} */
    TEST_ASSERT(!token(r, 6, 1, 6, SCOUT_TOKEN_PARAMETER));
    for (i = 1; i < r->token_count; i++) {
        const long *a = &r->tokens[(i - 1) * 4], *b = &r->tokens[i * 4];
        TEST_ASSERT(a[0] < b[0] || (a[0] == b[0] && a[1] + a[2] <= b[1]));
    }
    dos_parse_result_free(r);
}

/**
 * @brief Without .KEY, angle brackets are redirections; comments and
 *        changed dot characters.
 */
static void test_lines(void)
{
    DosParseResult *r = analyze("Echo >RAM:x <foo> ; $nicht\nList \"a;b\" $ja\n");
    TEST_ASSERT(r && r->diagnostic_count == 0);
    TEST_ASSERT(r && r->token_count == 1 && token(r, 2, 12, 3, SCOUT_TOKEN_PARAMETER));
    dos_parse_result_free(r);

    r = analyze(".DOT #\n#KEY a\n.ignored as a command\n#DEF a 1\n");
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_VARIABLE, "a") != NULL);
    TEST_ASSERT(r && r->diagnostic_count == 0);
    dos_parse_result_free(r);

    r = analyze(". a comment\n.\n.KEY x\n.KEY y\n.DEF z\n.BRA\n");
    TEST_ASSERT(r && diagnostic(r, 4, "second .KEY") != NULL);
    TEST_ASSERT(r && diagnostic(r, 5, "z is not a parameter") != NULL);
    TEST_ASSERT(r && diagnostic(r, 6, "this dot command needs") != NULL);
    TEST_ASSERT(r && r->diagnostic_count == 3);
    dos_parse_result_free(r);

    r = analyze("lab x\r\nskip X\r\nlab x2\r\nskip x back\r\n");
    TEST_ASSERT(r && r->diagnostic_count == 1 && diagnostic(r, 2, "label X is above") != NULL);
    dos_parse_result_free(r);

    r = analyze("");
    TEST_ASSERT(r && r->symbol_count == 0 && r->diagnostic_count == 0 && r->token_count == 0);
    dos_parse_result_free(r);
}

/**
 * @brief The Scout protocol: describe, analyze, errors.
 */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(dos_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "amigados") == 0);
    TEST_ASSERT(json_obj_get(json_obj_get(lang, "syntax"), "keywords") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(dos_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"" DATA "sample.script\",\"want\":[\"symbols\"]}}",
                                          &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    TEST_ASSERT(result && json_obj_get_array(result, "symbols") != NULL);
    TEST_ASSERT(result && json_obj_get(result, "tokens") == NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(dos_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(dos_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"" DATA "missing.script\"}}", &out));
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
    test_lines();
    test_rpc();
    printf("%d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
