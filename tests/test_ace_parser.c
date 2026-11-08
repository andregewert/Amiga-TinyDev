/**
 * @file test_ace_parser.c
 * @brief Host-native tests of the ACE BASIC parser: the analysis (symbols,
 *        diagnostics, tokens) and the Scout protocol, with the sample in
 *        tests/data/ace_parser/.
 */
#include "ace_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample sources (tests run from the project root). */
#define DATA "tests/data/ace_parser/"

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
static const AceSymbol *symbol(const AceResult *r, int kind, const char *name)
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
static const AceDiagnostic *diagnostic(const AceResult *r, long line, const char *text)
{
    long i;
    for (i = 0; i < r->diagnostic_count; i++) {
        const AceDiagnostic *d = &r->diagnostics[i];
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
static int has_token(const AceResult *r, long line, long col, long len, long kind)
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
 * @param text The text.
 * @return The result.
 */
static AceResult *analyze(const char *text)
{
    return ace_parser_analyze(text, strlen(text), 0);
}

/**
 * @brief Check that a text has exactly one diagnostic.
 * @param text The text.
 * @param line The line of the diagnostic.
 * @param severity Its SCOUT_SEVERITY_ value.
 * @param message The start of its message.
 */
static void only(const char *text, long line, int severity, const char *message)
{
    AceResult *r = analyze(text);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        const AceDiagnostic *d = diagnostic(r, line, message);
        if (r->diagnostic_count != 1 || d == NULL || d->severity != severity)
            printf("  in: %s  got %ld, first: %s\n", text, r->diagnostic_count,
                   r->diagnostic_count ? r->diagnostics[0].message : "");
        TEST_ASSERT(r->diagnostic_count == 1);
        TEST_ASSERT(d != NULL && d->severity == severity);
    }
    ace_result_free(r);
}

/**
 * @brief Check that a text has no diagnostics.
 * @param text The text.
 */
static void clean(const char *text)
{
    AceResult *r = analyze(text);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        if (r->diagnostic_count) printf("  in: %s  got: %s\n", text, r->diagnostics[0].message);
        TEST_ASSERT(r->diagnostic_count == 0);
    }
    ace_result_free(r);
}

/**
 * @brief The sample: symbols, references, tokens, no diagnostics.
 */
static void test_sample(void)
{
    size_t len;
    char *text = load(DATA "sample.b", &len);
    AceResult *r;
    const AceSymbol *s;
    long i, helpers = 0;
    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = ace_parser_analyze(text, len, 0);
    TEST_ASSERT(r != NULL);
    if (!r) {
        free(text);
        return;
    }
    TEST_ASSERT(r->diagnostic_count == 0);
    TEST_ASSERT(r->reference_count == 2);
    TEST_ASSERT(r->reference_count == 2 && strcmp(r->references[0].target, "ace/acedef.h") == 0 && r->references[0].system);
    TEST_ASSERT(r->reference_count == 2 && strcmp(r->references[1].target, "local.h") == 0 && !r->references[1].system);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_MACRO, "LIMIT") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "maxitems") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "minitems") != NULL);

    /* declared in a library: without its qualifier */
    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "AllocMem");
    TEST_ASSERT(s && s->declaration && strcmp(s->detail, "(LONGINT size, LONGINT flags)") == 0);
    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "Peer");
    TEST_ASSERT(s && s->declaration && s->scope == ACE_SCOPE_GLOBAL);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "origin");
    TEST_ASSERT(s && strcmp(s->detail, "Point") == 0);

    /* a word of the compiler can name a structure */
    s = symbol(r, SCOUT_SYMBOL_STRUCT, "Point");
    TEST_ASSERT(s && s->range.start_line == 19 && s->range.end_line == 23);
    s = symbol(r, SCOUT_SYMBOL_FIELD, "title");
    TEST_ASSERT(s && s->scope == ACE_SCOPE_LOCAL && strcmp(s->detail, "STRING") == 0);
    s = symbol(r, SCOUT_SYMBOL_CLASS, "Shape");
    TEST_ASSERT(s && strcmp(s->detail, "EXTENDS Point") == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "area") != NULL);
    s = symbol(r, SCOUT_SYMBOL_METHOD, "Area");
    TEST_ASSERT(s && s->range.end_line == 31);

    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "total");
    TEST_ASSERT(s && s->scope == ACE_SCOPE_FILE && strcmp(s->detail, "LONGINT") == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "buffer") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "table") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "kind") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FUNCTION, "FNtwice") != NULL);

    for (i = 0; i < r->symbol_count; i++) {
        if (strcmp(r->symbols[i].name, "Helper") == 0) helpers++;
    }
    TEST_ASSERT(helpers == 2);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "n%");
    TEST_ASSERT(s && s->scope == ACE_SCOPE_LOCAL && s->parent >= 0 &&
                strcmp(r->symbols[s->parent].name, "Helper") == 0 && !r->symbols[s->parent].declaration);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "i");
    TEST_ASSERT(s && s->scope == ACE_SCOPE_LOCAL);

    /* the same name with another qualifier is another SUB */
    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "Twin&");
    TEST_ASSERT(s && s->scope == ACE_SCOPE_GLOBAL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FUNCTION, "Twin$") != NULL);

    s = symbol(r, SCOUT_SYMBOL_LABEL, "start");
    TEST_ASSERT(s && s->range.start_line == 62 && s->range.start_col == 4);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "other") != NULL);
    TEST_ASSERT(has_token(r, 7, 7, 8, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 36, 13, 8, SCOUT_TOKEN_CONSTANT));     /* #:circle */
    TEST_ASSERT(has_token(r, 43, 16, 2, SCOUT_TOKEN_PARAMETER));    /* n% */
    TEST_ASSERT(has_token(r, 44, 12, 5, SCOUT_TOKEN_MACRO));        /* LIMIT */
    TEST_ASSERT(has_token(r, 49, 23, 7, SCOUT_TOKEN_FUNCTION));     /* FNtwice */
    ace_result_free(r);
    free(text);
}

/**
 * @brief What is wrong is reported in the right place.
 */
static void test_diagnostics(void)
{
    only("IF a THEN\nPRINT a\n", 1, SCOUT_SEVERITY_ERROR, "IF without END IF");
    only("END IF\n", 1, SCOUT_SEVERITY_ERROR, "END IF without IF");
    only("IF a\nPRINT a\n", 1, SCOUT_SEVERITY_ERROR, "IF without THEN");
    only("ELSE\n", 1, SCOUT_SEVERITY_ERROR, "ELSE without IF");
    only("ELSEIF a THEN\n", 1, SCOUT_SEVERITY_ERROR, "ELSEIF without IF");
    only("FOR i = 1 TO 2\n", 1, SCOUT_SEVERITY_ERROR, "FOR without NEXT");
    only("NEXT\n", 1, SCOUT_SEVERITY_ERROR, "NEXT without FOR");
    only("WHILE a\n", 1, SCOUT_SEVERITY_ERROR, "WHILE without WEND");
    only("WEND\n", 1, SCOUT_SEVERITY_ERROR, "WEND without WHILE");
    only("REPEAT\n", 1, SCOUT_SEVERITY_ERROR, "REPEAT without UNTIL");
    only("UNTIL a\n", 1, SCOUT_SEVERITY_ERROR, "UNTIL without REPEAT");
    only("CASE\n", 1, SCOUT_SEVERITY_ERROR, "CASE without END CASE");
    only("END CASE\n", 1, SCOUT_SEVERITY_ERROR, "END CASE without CASE");
    only("TYPECASE a\n", 1, SCOUT_SEVERITY_ERROR, "TYPECASE without END TYPECASE");
    only("BLOCK\n", 1, SCOUT_SEVERITY_ERROR, "BLOCK without END BLOCK");
    only("SUB a\n", 1, SCOUT_SEVERITY_ERROR, "SUB without END SUB");
    only("END SUB\n", 1, SCOUT_SEVERITY_ERROR, "END SUB without SUB");
    only("METHOD a(b)\n", 1, SCOUT_SEVERITY_ERROR, "METHOD without END METHOD");
    only("STRUCT a\n x y\n", 1, SCOUT_SEVERITY_ERROR, "STRUCT without END STRUCT");
    only("END CLASS\n", 1, SCOUT_SEVERITY_ERROR, "END CLASS without CLASS");
    only("ASSEM\n move d0,d1\n", 1, SCOUT_SEVERITY_ERROR, "ASSEM without END ASSEM");
    only("a = (1\n", 1, SCOUT_SEVERITY_ERROR, "'(' not closed");
    only("a = 1)\n", 1, SCOUT_SEVERITY_ERROR, "')' without an opening bracket");
    only("{ a\n", 1, SCOUT_SEVERITY_ERROR, "comment not closed");
    only("/* a\n", 1, SCOUT_SEVERITY_ERROR, "comment not closed");
    only("PRINT \"a\n", 1, SCOUT_SEVERITY_WARNING, "string not closed");
    only("#endif\n", 1, SCOUT_SEVERITY_ERROR, "#endif without #ifdef");
    only("#ifdef a\n", 1, SCOUT_SEVERITY_ERROR, "#ifdef without #endif");
    only("SUB a\nEND SUB\nsub A\nEND SUB\n", 3, SCOUT_SEVERITY_ERROR, "'A' is defined twice");
    only("CONST a = 1\nCONST a = 2\n", 2, SCOUT_SEVERITY_ERROR, "'a' is defined twice");
    only("STRUCT a\nEND STRUCT\nSTRUCT a\nEND STRUCT\n", 3, SCOUT_SEVERITY_ERROR, "'a' is defined twice");
    only("x:\nx:\n", 2, SCOUT_SEVERITY_ERROR, "'x' is defined twice");
    /* the block that is left open is the one reported */
    only("WHILE a\nIF b THEN\nWEND\n", 2, SCOUT_SEVERITY_ERROR, "IF without END IF");
}

/**
 * @brief Correct code of all forms stays free of diagnostics.
 */
static void test_correct(void)
{
    clean("if a then print 1 else print 2\n");
    clean("IF a THEN x = 1 : y = 2\nPRINT\n");
    clean("IF a GOTO b\nb:\n");
    clean("IF a THEN b ELSE c\nIF d THEN e\n");
    clean("IF a THEN\nb\nELSEIF c THEN\nd\nELSE\ne\nEND IF\n");
    clean("FOR i = 1 TO 2 : PRINT i : NEXT\n");
    clean("WHILE a : WEND\n");
    clean("REM IF a THEN\n");
    clean("rem IF a THEN\n");
    clean("' IF a THEN\n");
    clean("a = 1 ' FOR\n");
    clean("a = 1 : REM FOR\n");
    clean("{ IF { a }\n");
    clean("/* IF /* a */ b */\n");
    clean("10 PRINT\n20 GOTO 10\n");
    clean("CASE\n a=1 : b\n ELSE c\nEND CASE\n");
    clean("TYPECASE a\n CASE b\n  c\n CASE ELSE\n  d\nEND TYPECASE\n");
    clean("BLOCK\nEND BLOCK\n");
    clean("a = b ~\n + (c ~\n)\n");
    clean("DECLARE FUNCTION STRUCTPTR f( STRUCTPTR a, \\\n STRUCTPTR b ) LIBRARY x\n");
    clean("#define A 1 /* a comment\n going on ) */\n");
    clean("DATA 1, 2), (3\n");
    clean("class = 1\nstruct = 2\n");
    clean("SUB ASSERT(a)\nEND SUB\n");
    clean("DECLARE SUB a(b)\nSUB a(b)\nEND SUB\n");
    clean("SUB a&\nEND SUB\nSUB a$\nEND SUB\n");
    clean("ASSEM\n  IF ( ; not BASIC\nEND ASSEM\n");
    clean("GENERIC METHOD a(b)\n  ON c\nEND GENERIC\n");
    clean("STRUCT a\n  STRING if SIZE 2\n  b *c, d\nEND STRUCT\n");
    clean("PRINT#1, a\nOPEN \"I\",#1,\"x\"\n");
    clean("x = a->b : y := 2\n");
    clean("#ifdef A\nIF b THEN\n#else\nIF c THEN\n#endif\nEND IF\n");
}

/**
 * @brief Where names can be seen, and the tokens of the uses.
 */
static void test_scopes_tokens(void)
{
    AceResult *r;
    const char *text = "#include \"a.h\"\nSUB s\nEND SUB\nCONST c = 1\n";
    r = ace_parser_analyze(text, strlen(text), 1);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_FUNCTION, "s")->scope == ACE_SCOPE_GLOBAL);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_CONSTANT, "c")->scope == ACE_SCOPE_GLOBAL);
    ace_result_free(r);
    r = analyze("SUB s\nEND SUB\nCONST c = 1\n");
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_FUNCTION, "s")->scope == ACE_SCOPE_FILE);
    ace_result_free(r);

    /* case does not matter, nor a qualifier; a use may come first */
    r = analyze("PRINT VALUE\nCONST value = 1\nx = a->value\n");
    TEST_ASSERT(r && has_token(r, 1, 7, 5, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(r && !has_token(r, 3, 8, 5, SCOUT_TOKEN_CONSTANT));
    ace_result_free(r);

    /* a parameter hides a constant, in its SUB only */
    r = analyze("CONST a = 1\nSUB s(a)\n x = a\nEND SUB\ny = a\n");
    TEST_ASSERT(r && has_token(r, 3, 6, 1, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(r && has_token(r, 5, 5, 1, SCOUT_TOKEN_CONSTANT));
    ace_result_free(r);

    /* the variables of a SUB are its own */
    r = analyze("SUB s\n LONGINT a\nEND SUB\nLONGINT b\n");
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_VARIABLE, "a")->scope == ACE_SCOPE_LOCAL);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_VARIABLE, "b")->scope == ACE_SCOPE_FILE);
    ace_result_free(r);

    /* a word and a colon calling a known SUB, and a CASE branch, are no labels */
    r = analyze("SUB s\nEND SUB\ns:\nCASE\n b : c\nEND CASE\n");
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_LABEL, "s") == NULL);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_LABEL, "b") == NULL);
    ace_result_free(r);

    /* the word after SIZEOF is a structure */
    r = analyze("x = SIZEOF(thing)\n");
    TEST_ASSERT(r && has_token(r, 1, 12, 5, SCOUT_TOKEN_TYPE));
    ace_result_free(r);
}

/**
 * @brief scout/describe and scout/analyze.
 */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang, *syntax, *kw;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(ace_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "ace") == 0);
    TEST_ASSERT(lang && json_array_length(json_obj_get_array(lang, "extensions")) == 2);
    syntax = json_obj_get(lang, "syntax");
    TEST_ASSERT(syntax && !json_obj_get_bool(syntax, "caseSensitive", 1));
    TEST_ASSERT(syntax && json_array_length(json_obj_get_array(syntax, "lineCommentWords")) == 1);
    TEST_ASSERT(syntax && json_array_length(json_obj_get_array(syntax, "blockComments")) == 2);
    kw = json_obj_get(syntax, "keywords");
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "builtin")) > 60);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "keyword")) > 150);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "type")) == 7);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "constant")) == 3);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(ace_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"" DATA "sample.b\",\"want\":[\"symbols\",\"references\"]}}",
                                          &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    TEST_ASSERT(result && json_obj_get_array(result, "symbols") != NULL);
    TEST_ASSERT(result && json_obj_get(result, "tokens") == NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"language\":\"ace\",\"target\":\"ace/acedef.h\",\"system\":true") != NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"declaration\":true") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(ace_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(ace_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"" DATA "missing.b\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "cannot read") != NULL);
    json_writer_free(&out);
}

/**
 * @brief Run the tests.
 * @return 0 if all passed.
 */
int main(void)
{
    test_sample();
    test_diagnostics();
    test_correct();
    test_scopes_tokens();
    test_rpc();
    printf("%d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
