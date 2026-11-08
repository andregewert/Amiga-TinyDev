/**
 * @file test_arexx_parser.c
 * @brief Host-native tests of the ARexx parser: the analysis (symbols,
 *        diagnostics, tokens) and the Scout protocol, with the sample in
 *        tests/data/arexx_parser/.
 */
#include "arexx_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample programs (tests run from the project root). */
#define DATA "tests/data/arexx_parser/"

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
static const ArexxSymbol *symbol(const ArexxResult *r, int kind, const char *name)
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
 * @param text The start of the message.
 * @return The diagnostic, or NULL.
 */
static const ArexxDiagnostic *diagnostic(const ArexxResult *r, long line, const char *text)
{
    long i;
    for (i = 0; i < r->diagnostic_count; i++) {
        const ArexxDiagnostic *d = &r->diagnostics[i];
        if (d->range.start_line == line && strncmp(d->message, text, strlen(text)) == 0) return d;
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
static int has_token(const ArexxResult *r, long line, long col, long len, long kind)
{
    long i;
    for (i = 0; i < r->token_count; i++) {
        const long *t = &r->tokens[i * 4];
        if (t[0] == line && t[1] == col && t[2] == len && t[3] == kind) return 1;
    }
    return 0;
}

/**
 * @brief Analyse a text.
 * @param text The text (terminated).
 * @return The result.
 */
static ArexxResult *analyze(const char *text)
{
    return arexx_parser_analyze(text, strlen(text));
}

/**
 * @brief Tell whether a text gets exactly one diagnostic, with this
 *        severity and start of message, on line 2.
 * @param text The program, whose first line is a comment.
 * @param severity SCOUT_SEVERITY_ value.
 * @param message The start of the message.
 * @return Non-zero if so.
 */
static int only(const char *text, int severity, const char *message)
{
    ArexxResult *r = analyze(text);
    const ArexxDiagnostic *d = r ? diagnostic(r, 2, message) : NULL;
    int ok = r && r->diagnostic_count == 1 && d && d->severity == severity;
    if (!ok && r) {
        long i;
        printf("got %ld diagnostics for: %s\n", r->diagnostic_count, text);
        for (i = 0; i < r->diagnostic_count; i++) {
            printf("  %ld: %s\n", r->diagnostics[i].range.start_line, r->diagnostics[i].message);
        }
    }
    arexx_result_free(r);
    return ok;
}

/**
 * @brief Tell whether a text gets no diagnostic at all.
 * @param text The program.
 * @return Non-zero if so.
 */
static int clean(const char *text)
{
    ArexxResult *r = analyze(text);
    int ok = r && r->diagnostic_count == 0;
    if (!ok && r) {
        long i;
        printf("not clean: %s\n", text);
        for (i = 0; i < r->diagnostic_count; i++) {
            printf("  %ld: %s\n", r->diagnostics[i].range.start_line, r->diagnostics[i].message);
        }
    }
    arexx_result_free(r);
    return ok;
}

/**
 * @brief The sample program: its symbols and tokens, and no diagnostics.
 */
static void test_sample(void)
{
    size_t length = 0;
    char *text = load(DATA "sample.rexx", &length);
    ArexxResult *r;
    const ArexxSymbol *add, *s;
    long i;
    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = arexx_parser_analyze(text, length);
    TEST_ASSERT(r != NULL);
    if (!r) {
        free(text);
        return;
    }
    TEST_ASSERT(r->diagnostic_count == 0);
    for (i = 0; i < r->diagnostic_count; i++) {
        printf("  %ld: %s\n", r->diagnostics[i].range.start_line, r->diagnostics[i].message);
    }

    add = symbol(r, SCOUT_SYMBOL_PROCEDURE, "Add");
    TEST_ASSERT(add && add->range.start_line == 34 && add->selection.start_col == 1 && add->selection.end_col == 4);
    TEST_ASSERT(add && add->detail && strcmp(add->detail, "PROCEDURE EXPOSE sum") == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "Report") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "SYNTAX") != NULL);

    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "total") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "names.") != NULL);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "i");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "DO") == 0);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "file");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "ARG") == 0 && s->parent == -1);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "second") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "long") != NULL);

    /* local to Add, except what it exposes */
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "local");
    TEST_ASSERT(s && add && s->parent == (long)(add - r->symbols));
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "a");
    TEST_ASSERT(s && add && s->parent == (long)(add - r->symbols));
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "sum");
    TEST_ASSERT(s && s->parent == -1);
    /* RETURN ended Add: Report's argument is not local to it */
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "n");
    TEST_ASSERT(s && s->parent == -1);

    TEST_ASSERT(has_token(r, 4, 11, 4, SCOUT_TOKEN_PARAMETER));     /* file */
    TEST_ASSERT(has_token(r, 5, 4, 4, SCOUT_TOKEN_PARAMETER));      /* its use */
    TEST_ASSERT(has_token(r, 11, 9, 3, SCOUT_TOKEN_FUNCTION));      /* Add( */
    TEST_ASSERT(has_token(r, 12, 10, 2, SCOUT_TOKEN_CONSTANT));     /* TO */
    TEST_ASSERT(has_token(r, 34, 1, 3, SCOUT_TOKEN_FUNCTION));      /* the label */
    TEST_ASSERT(has_token(r, 34, 16, 6, SCOUT_TOKEN_CONSTANT));     /* EXPOSE */
    TEST_ASSERT(has_token(r, 30, 9, 7, SCOUT_TOKEN_CONSTANT));      /* COMMAND */

    for (i = 1; i < r->token_count; i++) {
        const long *a = &r->tokens[(i - 1) * 4];
        const long *b = &r->tokens[i * 4];
        TEST_ASSERT(a[0] < b[0] || (a[0] == b[0] && a[1] + a[2] <= b[1]));
    }
    arexx_result_free(r);
    free(text);
}

/**
 * @brief Everything the analysis reports.
 */
static void test_diagnostics(void)
{
    ArexxResult *r;

    TEST_ASSERT(only("/**/\nDO\nSAY 1\n", SCOUT_SEVERITY_ERROR, "DO without END"));
    TEST_ASSERT(only("/**/\nSELECT\nWHEN a THEN b\n", SCOUT_SEVERITY_ERROR, "SELECT without END"));
    TEST_ASSERT(only("/**/\nEND\n", SCOUT_SEVERITY_ERROR, "END without"));
    /* "==" compares: no assignment, so END is the instruction */
    TEST_ASSERT(only("/**/\nend == 1\n", SCOUT_SEVERITY_ERROR, "END without"));
    TEST_ASSERT(only("/**/\nDO i = 1 TO 2; END j\n", SCOUT_SEVERITY_ERROR, "END j does not match"));
    TEST_ASSERT(only("/**/\nIF a\nSAY 1\n", SCOUT_SEVERITY_ERROR, "IF without THEN"));
    TEST_ASSERT(only("/**/\nTHEN SAY 1\n", SCOUT_SEVERITY_ERROR, "THEN without"));
    TEST_ASSERT(only("/**/\nELSE SAY 1\n", SCOUT_SEVERITY_ERROR, "ELSE without IF"));
    TEST_ASSERT(only("/**/\nWHEN a THEN b\n", SCOUT_SEVERITY_ERROR, "WHEN outside"));
    TEST_ASSERT(only("/**/\nOTHERWISE\n", SCOUT_SEVERITY_ERROR, "OTHERWISE outside"));
    TEST_ASSERT(only("/**/\nLEAVE\n", SCOUT_SEVERITY_ERROR, "LEAVE outside"));
    TEST_ASSERT(only("/**/\nDO; ITERATE; END\n", SCOUT_SEVERITY_ERROR, "ITERATE outside"));
    TEST_ASSERT(only("/**/\nPROCEDURE\n", SCOUT_SEVERITY_ERROR, "PROCEDURE must follow"));
    TEST_ASSERT(only("/**/\nSIGNAL nowhere\n", SCOUT_SEVERITY_ERROR, "SIGNAL to a label"));
    TEST_ASSERT(only("/**/\nSAY (1\n", SCOUT_SEVERITY_ERROR, "'(' is not closed"));
    TEST_ASSERT(only("/**/\nSAY 1)\n", SCOUT_SEVERITY_ERROR, "')' without"));
    TEST_ASSERT(only("/**/\nx = 'GG'x\n", SCOUT_SEVERITY_ERROR, "invalid hexadecimal"));
    TEST_ASSERT(only("/**/\nx = '12'b\n", SCOUT_SEVERITY_ERROR, "invalid binary"));
    TEST_ASSERT(only("/**/\n/* open\n", SCOUT_SEVERITY_ERROR, "comment is not closed"));
    TEST_ASSERT(only("/**/\nSAY 'open\n", SCOUT_SEVERITY_WARNING, "string is not closed"));
    TEST_ASSERT(only("/**/\nSIGNAL ON ERROR\n", SCOUT_SEVERITY_WARNING, "SIGNAL ON ERROR has no label"));
    TEST_ASSERT(only("/**/\nCALL nothing\n", SCOUT_SEVERITY_HINT, "nothing is neither"));
    TEST_ASSERT(only("/**/\nx = nothing(1)\n", SCOUT_SEVERITY_HINT, "nothing is neither"));

    r = analyze("/**/\nx:\nx:\n");
    TEST_ASSERT(r && r->diagnostic_count == 1 && diagnostic(r, 3, "label 'x' is defined twice"));
    arexx_result_free(r);

    r = analyze("SAY 1\n");
    TEST_ASSERT(r && r->diagnostic_count == 1 && diagnostic(r, 1, "an ARexx program must begin"));
    arexx_result_free(r);

    /* an IF on a line of its own after the ELSE of another one */
    r = analyze("/**/\nIF a THEN b\nELSE c\nELSE d\n");
    TEST_ASSERT(r && r->diagnostic_count == 1 && diagnostic(r, 4, "ELSE without IF"));
    arexx_result_free(r);

    /* an instruction between the branch and the ELSE */
    r = analyze("/**/\nIF a THEN b\nSAY 1\nELSE d\n");
    TEST_ASSERT(r && r->diagnostic_count == 1 && diagnostic(r, 4, "ELSE without IF"));
    arexx_result_free(r);
}

/**
 * @brief What looks like a mistake, but is not.
 */
static void test_correct(void)
{
    TEST_ASSERT(clean(""));
    TEST_ASSERT(clean("  \n\n"));
    /* forward jumps and calls */
    TEST_ASSERT(clean("/**/\nSIGNAL later\nCALL sub\nlater: sub: EXIT\n"));
    /* a name of an instruction as a variable or label */
    TEST_ASSERT(clean("/**/\nend = 1\nselect = end\nif = 2; do = if\n"));
    TEST_ASSERT(clean("/**/\nCALL else\nEXIT\nelse: RETURN\n"));
    /* the parts of an IF on lines of their own */
    TEST_ASSERT(clean("/**/\nIF a\nTHEN b\nELSE\nc\n"));
    TEST_ASSERT(clean("/**/\nIF a THEN\n  b\nELSE\n  c\n"));
    TEST_ASSERT(clean("/**/\nIF a THEN DO\nb\nEND\nELSE DO\nc\nEND\n"));
    /* blanks and comments do not separate an IF from its ELSE */
    TEST_ASSERT(clean("/**/\nIF a THEN b\n\n/* c */\nELSE d\n"));
    TEST_ASSERT(clean("/**/\nIF a THEN b; ; ELSE d\n"));
    TEST_ASSERT(clean("/**/\nIF a THEN IF b THEN c; ELSE d; ELSE e\n"));
    TEST_ASSERT(clean("/**/\nIF a THEN b; ELSE IF c THEN d; ELSE e\n"));
    /* a comma ends the line only as a continuation; the comment after it counts as blank */
    TEST_ASSERT(clean("/**/\nIF a, /* more */\n b THEN c\n"));
    TEST_ASSERT(clean("/**/\nSAY MAX(1,\n 2)\n"));
    /* LEAVE in a SELECT inside a loop */
    TEST_ASSERT(clean("/**/\nDO i = 1 TO 2\nSELECT\nWHEN i = 1 THEN LEAVE\nOTHERWISE NOP\nEND\nEND\n"));
    /* the forms of DO */
    TEST_ASSERT(clean("/**/\nDO FOREVER; LEAVE; END\nDO 3; LEAVE; END\nDO WHILE a; ITERATE; END\n"));
    /* a name followed by a blank is no call */
    TEST_ASSERT(clean("/**/\nSAY unknown (1)\n"));
    /* a string naming a function is not looked up */
    TEST_ASSERT(clean("/**/\nSAY 'external'(1)\n"));
    /* functions of the language and of rexxsupport.library */
    TEST_ASSERT(clean("/**/\nSAY LEFT('a', 1) showdir('c:')\n"));
    /* libraries bring functions the parser cannot know */
    TEST_ASSERT(clean("/**/\nCALL ADDLIB('x.library', 0, -30)\nSAY frob(1)\n"));
    /* a double quote inside single quotes, a doubled quote */
    TEST_ASSERT(clean("/**/\nSAY 'a\"b' \"c'd\" 'it''s'\n"));
    /* quoted parentheses, comments in strings */
    TEST_ASSERT(clean("/**/\nSAY '(' \"/*\"\n"));
    /* PROCEDURE after a label, also on the next line or after a blank clause */
    TEST_ASSERT(clean("/**/\na: PROCEDURE\nRETURN\nb:\n;\nPROCEDURE\nRETURN\n"));
    /* the line ends of other systems */
    TEST_ASSERT(clean("/**/\r\nDO\r\nEND\r\n"));
    TEST_ASSERT(clean("/**/\rDO\rEND\r"));
}

/**
 * @brief The Scout protocol: describe, analyze, errors.
 */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang, *kw;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(arexx_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "arexx") == 0);
    kw = json_obj_get(json_obj_get(lang, "syntax"), "keywords");
    TEST_ASSERT(kw && json_obj_get_array(kw, "keyword") != NULL);
    TEST_ASSERT(kw && json_obj_get_array(kw, "function") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(arexx_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                            "\"params\":{\"path\":\"" DATA "sample.rexx\",\"want\":[\"symbols\"]}}",
                                            &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    TEST_ASSERT(result && json_obj_get_array(result, "symbols") != NULL);
    TEST_ASSERT(result && json_obj_get(result, "tokens") == NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"scope\":\"local\",\"parent\":") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(arexx_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                            "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(arexx_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                            "\"params\":{\"path\":\"" DATA "missing.rexx\"}}", &out));
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
    test_diagnostics();
    test_correct();
    test_rpc();
    printf("%d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
