/**
 * @file test_e_parser.c
 * @brief Host-native tests of the AmigaE parser: the analysis (symbols,
 *        diagnostics, tokens), compiled modules (built here byte by byte)
 *        and the Scout protocol, with the sample in tests/data/e_parser/.
 */
#include "e_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample sources (tests run from the project root). */
#define DATA "tests/data/e_parser/"

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
static const ESymbol *symbol(const EResult *r, int kind, const char *name)
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
static const EDiagnostic *diagnostic(const EResult *r, long line, const char *text)
{
    long i;
    for (i = 0; i < r->diagnostic_count; i++) {
        const EDiagnostic *d = &r->diagnostics[i];
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
static int has_token(const EResult *r, long line, long col, long len, long kind)
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
static EResult *analyze(const char *text)
{
    return e_parser_analyze(text, strlen(text));
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
    EResult *r = analyze(text);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        const EDiagnostic *d = diagnostic(r, line, message);
        if (r->diagnostic_count != 1 || d == NULL || d->severity != severity)
            printf("  in: %s  got %ld, first: %s\n", text, r->diagnostic_count,
                   r->diagnostic_count ? r->diagnostics[0].message : "");
        TEST_ASSERT(r->diagnostic_count == 1);
        TEST_ASSERT(d != NULL && d->severity == severity);
    }
    e_result_free(r);
}

/**
 * @brief Check that a text has no diagnostics.
 * @param text The text.
 */
static void clean(const char *text)
{
    EResult *r = analyze(text);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        if (r->diagnostic_count) printf("  in: %s  got: %s\n", text, r->diagnostics[0].message);
        TEST_ASSERT(r->diagnostic_count == 0);
    }
    e_result_free(r);
}

/** @brief The sample: symbols, references, diagnostics and tokens. */
static void test_sample(void)
{
    size_t length = 0;
    char *text = load(DATA "sample.e", &length);
    EResult *r;
    const ESymbol *s, *o;
    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = e_parser_analyze(text, length);
    TEST_ASSERT(r != NULL);
    if (!r) {
        free(text);
        return;
    }
    if (r->diagnostic_count) printf("  sample: line %ld: %s\n", r->diagnostics[0].range.start_line, r->diagnostics[0].message);
    TEST_ASSERT(r->diagnostic_count == 0);
    TEST_ASSERT(!r->binary);

    TEST_ASSERT(r->reference_count == 3);
    if (r->reference_count == 3) {
        TEST_ASSERT(strcmp(r->references[0].target, "intuition/intuition") == 0 && !r->references[0].local);
        TEST_ASSERT(r->references[0].range.start_line == 4 && r->references[0].range.start_col == 8);
        TEST_ASSERT(strcmp(r->references[2].target, "helper") == 0 && r->references[2].local);
    }

    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "MAX_ITEMS") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "BIG") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "ER_FILE") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "FLAG_B") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_MACRO, "TWICE") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "done") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "data") != NULL);

    o = symbol(r, SCOUT_SYMBOL_STRUCT, "point");
    TEST_ASSERT(o && o->scope == E_SCOPE_FILE && o->range.start_line == 13 && o->range.end_line == 16);
    s = symbol(r, SCOUT_SYMBOL_FIELD, "name");
    TEST_ASSERT(s && s->scope == E_SCOPE_LOCAL && s->detail && strcmp(s->detail, "ARRAY OF CHAR") == 0);
    TEST_ASSERT(s && o && &r->symbols[s->parent] == o);
    o = symbol(r, SCOUT_SYMBOL_STRUCT, "circle");
    TEST_ASSERT(o && o->scope == E_SCOPE_GLOBAL && o->detail && strcmp(o->detail, "OF point") == 0);
    s = symbol(r, SCOUT_SYMBOL_FIELD, "next");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "PTR TO circle") == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "radius") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "a") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "b") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "c") != NULL);

    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "count");
    TEST_ASSERT(s && s->scope == E_SCOPE_FILE);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "buffer");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "STRING") == 0);

    s = symbol(r, SCOUT_SYMBOL_METHOD, "area");
    TEST_ASSERT(s && o && &r->symbols[s->parent] == o);
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "(r=1) OF circle") == 0);
    TEST_ASSERT(s && s->range.start_line == 33 && s->range.end_line == 33);
    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "twice");
    TEST_ASSERT(s && s->scope == E_SCOPE_FILE && s->range.end_line == 35);
    o = symbol(r, SCOUT_SYMBOL_FUNCTION, "sum");
    TEST_ASSERT(o && o->scope == E_SCOPE_GLOBAL && o->range.start_line == 37 && o->range.end_line == 67);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "total");
    TEST_ASSERT(s && o && s->scope == E_SCOPE_LOCAL && &r->symbols[s->parent] == o);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "b");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "PTR TO LONG") == 0);
    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "main");
    TEST_ASSERT(s && s->range.start_line == 69 && s->range.end_line == 77);

    TEST_ASSERT(has_token(r, 7, 7, 9, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 13, 8, 5, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 18, 25, 5, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 33, 11, 1, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(has_token(r, 35, 6, 5, SCOUT_TOKEN_FUNCTION));
    TEST_ASSERT(has_token(r, 35, 22, 5, SCOUT_TOKEN_MACRO));
    TEST_ASSERT(has_token(r, 42, 16, 9, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 43, 7, 5, SCOUT_TOKEN_FUNCTION));
    TEST_ASSERT(has_token(r, 43, 13, 6, SCOUT_TOKEN_CONSTANT));
    /* nothing is made of inline assembly */
    TEST_ASSERT(!has_token(r, 73, 3, 4, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(!has_token(r, 73, 10, 2, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(!has_token(r, 74, 3, 3, SCOUT_TOKEN_CONSTANT));
    e_result_free(r);
    free(text);
}

/** @brief Errors of the structure, each the only diagnostic of its text. */
static void test_diagnostics(void)
{
    only("PROC main()\n  IF a\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "IF without ENDIF");
    only("PROC main()\n  ENDIF\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "ENDIF without");
    only("PROC main()\n  FOR i:=0 TO 1\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "FOR without ENDFOR");
    only("PROC main()\n  WHILE a\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "WHILE without");
    only("PROC main()\n  REPEAT\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "REPEAT without UNTIL");
    only("PROC main()\n  UNTIL a\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "UNTIL without");
    only("PROC main()\n  LOOP\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "LOOP without");
    only("PROC main()\n  SELECT a\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "SELECT without");
    only("PROC main()\n  CASE 1\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "CASE without");
    only("PROC main()\n  ELSE\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "ELSE without");
    only("PROC main()\n  TRY\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "TRY without");
    /* an inner block left open is what is reported, the outer one closes */
    only("PROC main()\n  IF a\n    FOR i:=0 TO 1\n  ENDIF\nENDPROC\n", 3, SCOUT_SEVERITY_ERROR, "FOR without");

    only("ENDPROC\nPROC main()\nENDPROC\n", 1, SCOUT_SEVERITY_ERROR, "ENDPROC without PROC");
    only("PROC main()\n", 1, SCOUT_SEVERITY_ERROR, "PROC without ENDPROC");
    only("OPT MODULE\nPROC a()\nPROC b()\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "PROC without ENDPROC");
    only("OBJECT a\n  x\n", 1, SCOUT_SEVERITY_ERROR, "OBJECT without ENDOBJECT");
    only("ENDOBJECT\nPROC main()\nENDPROC\n", 1, SCOUT_SEVERITY_ERROR, "ENDOBJECT without");
    only("PROC main()\nEXCEPT\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "EXCEPT without");

    only("PROC main()\n  a:=(1\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "'(' not closed");
    only("PROC main()\n  a:=1)\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "')' without");
    only("/* open\nPROC main()\nENDPROC\n", 1, SCOUT_SEVERITY_ERROR, "comment not closed");
    only("PROC main()\n  a:='x\nENDPROC\n", 2, SCOUT_SEVERITY_WARNING, "string not closed");
    only("#ifdef A\nPROC main()\nENDPROC\n", 1, SCOUT_SEVERITY_ERROR, "#ifdef without #endif");
    only("#endif\nPROC main()\nENDPROC\n", 1, SCOUT_SEVERITY_ERROR, "#endif without");

    only("PROC a()\nENDPROC\nPROC main()\nENDPROC\nPROC a()\nENDPROC\n", 5, SCOUT_SEVERITY_ERROR, "'a' is defined twice");
    only("OBJECT a\nENDOBJECT\nOBJECT a\nENDOBJECT\nPROC main()\nENDPROC\n", 3, SCOUT_SEVERITY_ERROR, "'a' is");
    only("CONST A=1,A=2\nPROC main()\nENDPROC\n", 1, SCOUT_SEVERITY_ERROR, "'A' is");
    only("PROC main(a)\n  DEF a\nENDPROC\n", 2, SCOUT_SEVERITY_ERROR, "'a' is");

    only("PROC x()\nENDPROC\n", 1, SCOUT_SEVERITY_HINT, "no PROC main");
}

/** @brief Correct sources that must not be reported. */
static void test_correct(void)
{
    /* one-line forms open no block */
    clean("PROC main()\n  IF a THEN b() ELSE c()\n  FOR i:=0 TO 1 DO b()\n  WHILE a DO b()\nENDPROC\n");
    clean("PROC main()\n  IF a THEN\n    b()\nENDPROC\n");
    /* IF as an expression */
    clean("PROC main()\n  x:=IF a THEN 1 ELSE 2\nENDPROC\n");
    /* the rest after ELSE and after a label is a statement of its own */
    clean("PROC main()\n  IF a\n    b()\n  ELSE IF c\n    d()\n  ENDIF\n  ENDIF\nENDPROC\n");
    clean("PROC main()\nl: IF a\n  ENDIF\nENDPROC\n");
    /* separators */
    clean("PROC main(); IF a; b(); ENDIF; ENDPROC\n");
    /* continuation lines */
    clean("PROC main()\n  IF a AND\n     b THEN c()\n  x:=1+\n    2\n  y:=[1,\n     2,\n     3]\nENDPROC\n");
    clean("DEF a,\n    b\nPROC main()\nENDPROC\n");
    clean("PROC main()\n  x:=f(1,\n    -> a comment between the lines\n\n    2)\nENDPROC\n");
    /* after x++ the statement is over */
    clean("PROC main()\n  x++\n  IF a THEN b()\nENDPROC\n");
    /* the forms of a PROC */
    clean("OPT MODULE\nPROC a() IS 1\nPROC b(x) RETURN x\nPROC c(x=[1,2]:LONG) HANDLE\nEXCEPT\nENDPROC\n");
    clean("OPT MODULE\nPROC a() HANDLE\nEXCEPT DO\n  IF exception THEN b()\nENDPROC 1,2 WITH c\n");
    clean("OBJECT o\nENDOBJECT\nPROC a() OF o\nENDPROC\nOBJECT p\nENDOBJECT\nPROC a() OF p\nENDPROC\nPROC main()\nENDPROC\n");
    /* other blocks */
    clean("PROC main()\n  WHILE a\n  ELSEWHILE b\n  ALWAYS\n  ENDWHILE\n  TRY\n  CATCH 1\n  ENDTRY\nENDPROC\n");
    clean("PROC main()\n  IFN a\n  ELSEIFN b\n  ENDIF\n  WHILEN c\n  ENDWHILE\n  REPEAT\n  UNTILN d\nENDPROC\n");
    clean("PROC main()\n  SELECT 10 OF x\n    CASE 1 TO 3\n      IF a\n      ENDIF\n    DEFAULT\n  ENDSELECT\nENDPROC\n");
    /* strings and comments hold anything */
    clean("PROC main()\n  a:='ENDIF ( \\' -> x'\n  b:=\"(\" -> ENDIF (\n  c:=1 // ENDIF (\n /* ENDIF\n ( */\nENDPROC\n");
    /* inline assembly */
    clean("PROC main()\n  MOVE.L D0,(A1)+\n  IF a THEN b()\n  AND.L D1,D0\n  JMP (A0)\nlab:\n  NOP\nENDPROC\n");
    /* the preprocessor */
    clean("#define A(x) (x\n#ifdef B\n#define C \\\n  ENDIF (\n#endif\nPROC main()\nENDPROC\n");
    /* both branches are read, with what they do to the structure */
    clean("#ifdef A\nPROC main()\n#else\nPROC main(x)\n#endif\nENDPROC\n");
    clean("#ifdef A\nPROC a()\nENDPROC\n#endif\n#ifndef A\nPROC a()\nENDPROC\n#endif\nPROC main()\nENDPROC\n");
    /* modules */
    clean("OPT MODULE\nPROC a()\nENDPROC\n");
    clean("OPT MODULE, EXPORT\nCONST A=1\n");
    /* line ends */
    clean("PROC main()\r\n  IF a\r\n  ENDIF\r\nENDPROC\r\n");
    clean("");
}

/** @brief Scope of names and what becomes a token. */
static void test_scopes_tokens(void)
{
    EResult *r = analyze("OPT MODULE, EXPORT\nCONST A=1\nPROC f()\nENDPROC\n");
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_CONSTANT, "A")->scope == E_SCOPE_GLOBAL);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_FUNCTION, "f")->scope == E_SCOPE_GLOBAL);
    e_result_free(r);

    r = analyze("EXPORT CONST A=1\nCONST B=2\nEXPORT DEF c\nDEF d\n");
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_CONSTANT, "A")->scope == E_SCOPE_GLOBAL);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_CONSTANT, "B")->scope == E_SCOPE_FILE);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_VARIABLE, "c")->scope == E_SCOPE_GLOBAL);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_VARIABLE, "d")->scope == E_SCOPE_FILE);
    e_result_free(r);

    /* used before their definition; x.Y and y.z are left alone */
    r = analyze("PROC main()\n  later(FOO_BAR, x.Y, Open(1))\n  y.z()\nENDPROC\nPROC later(p)\n  p()\nENDPROC\n");
    TEST_ASSERT(r != NULL);
    if (r) {
        TEST_ASSERT(has_token(r, 2, 3, 5, SCOUT_TOKEN_FUNCTION));
        TEST_ASSERT(has_token(r, 2, 9, 7, SCOUT_TOKEN_CONSTANT));
        TEST_ASSERT(!has_token(r, 2, 20, 1, SCOUT_TOKEN_CONSTANT));
        TEST_ASSERT(has_token(r, 2, 23, 4, SCOUT_TOKEN_FUNCTION));
        TEST_ASSERT(!has_token(r, 3, 5, 1, SCOUT_TOKEN_FUNCTION));
        /* a parameter, even when it is called */
        TEST_ASSERT(has_token(r, 6, 3, 1, SCOUT_TOKEN_PARAMETER));
    }
    e_result_free(r);

    /* the parameters are valid up to the end of ENDPROC's values, not beyond */
    r = analyze("PROC f(a)\nENDPROC a\nDEF g\nPROC h()\n  a()\nENDPROC\n");
    TEST_ASSERT(r != NULL);
    if (r) {
        TEST_ASSERT(has_token(r, 2, 9, 1, SCOUT_TOKEN_PARAMETER));
        TEST_ASSERT(has_token(r, 5, 3, 1, SCOUT_TOKEN_FUNCTION));
    }
    e_result_free(r);

    /* types */
    r = analyze("DEF a:foo, b:PTR TO bar, c:LONG\nPROC main()\n  x:=y::baz.z\n  w:=[1,2]:qux\nENDPROC\n");
    TEST_ASSERT(r != NULL);
    if (r) {
        TEST_ASSERT(has_token(r, 1, 7, 3, SCOUT_TOKEN_TYPE));
        TEST_ASSERT(has_token(r, 1, 21, 3, SCOUT_TOKEN_TYPE));
        TEST_ASSERT(has_token(r, 3, 9, 3, SCOUT_TOKEN_TYPE));
        TEST_ASSERT(has_token(r, 4, 12, 3, SCOUT_TOKEN_TYPE));
    }
    e_result_free(r);

    /* tokens are sorted and do not overlap, however a statement is spread */
    r = analyze("PROC main()\n  foo(AA,\n      BB) ; bar(CC)\nENDPROC\n");
    TEST_ASSERT(r != NULL);
    if (r) {
        long i;
        for (i = 1; i < r->token_count; i++) {
            const long *a = &r->tokens[(i - 1) * 4], *b = &r->tokens[i * 4];
            TEST_ASSERT(a[0] < b[0] || (a[0] == b[0] && a[1] + a[2] <= b[1]));
        }
        TEST_ASSERT(has_token(r, 3, 7, 2, SCOUT_TOKEN_CONSTANT));
    }
    e_result_free(r);
}

/** What the module being built has written so far. */
static unsigned char g_mod[1024];
/** Its length. */
static size_t g_mod_len = 0;

/**
 * @brief Write a 16-bit value to the module.
 * @param v The value.
 */
static void mod_int(int v)
{
    g_mod[g_mod_len++] = (unsigned char)((v >> 8) & 255);
    g_mod[g_mod_len++] = (unsigned char)(v & 255);
}

/**
 * @brief Write bytes to the module.
 * @param text The bytes.
 * @param len How many.
 */
static void mod_bytes(const char *text, size_t len)
{
    memcpy(g_mod + g_mod_len, text, len);
    g_mod_len += len;
}

/**
 * @brief Write zeros to the module.
 * @param len How many.
 */
static void mod_zero(size_t len)
{
    memset(g_mod + g_mod_len, 0, len);
    g_mod_len += len;
}

/**
 * @brief Write the chunk with the module format.
 * @param version The format.
 */
static void mod_header(int version)
{
    g_mod_len = 0;
    mod_bytes("EMOD", 4);
    mod_int(5);
    mod_zero(16);
    mod_int(version);
    if (version > 10) mod_zero(4);
    mod_zero(4);
}

/** @brief Compiled modules: all chunks with symbols, as the format has grown. */
static void test_modules(void)
{
    EResult *r;
    const ESymbol *o, *s;
    size_t i;

    mod_header(15);
    mod_int(1); mod_zero(4);                        /* constants */
    mod_int(2); mod_zero(4); mod_bytes("AB", 2);
    mod_int(-256); mod_int(4); mod_bytes("CDE\0", 4); mod_int(2); mod_bytes("xy\0\0", 4);
    mod_int(0);
    mod_int(2); mod_zero(4); mod_zero(2);           /* an object with members and methods */
    mod_int(4); mod_zero(4); mod_bytes("obj\0", 4);
    mod_int(2); mod_zero(4); mod_bytes("ab", 2);
    mod_zero(2); mod_zero(2); mod_int(1); mod_zero(2); mod_int(0);
    mod_int(-1); mod_zero(4);                       /* private */
    mod_zero(2); mod_zero(2); mod_int(0); mod_int(-1); mod_int(2); mod_zero(2);
    mod_int(0);
    mod_zero(2);
    mod_int(1); mod_zero(4); mod_int(2); mod_zero(2); mod_zero(4);
    mod_int(0); mod_zero(2); mod_int(4); mod_bytes("draw", 4); mod_int(1); mod_int(1); mod_zero(4);
    mod_int(-32768); mod_int(2); mod_zero(2);
    mod_int(-1);
    mod_int(0); mod_zero(4);
    mod_int(-1);
    mod_int(4);                                     /* procedures and labels */
    mod_int(4); mod_bytes("func", 4); mod_zero(4); mod_int(1); mod_zero(4); mod_int(1); mod_zero(4); mod_int(2); mod_zero(2);
    mod_int(4); mod_bytes("lab\0", 4); mod_zero(4); mod_int(2);
    mod_int(0);
    mod_int(8);                                     /* globals */
    mod_int(0);
    mod_int(4); mod_bytes("glob", 4); mod_zero(10); mod_int(-1);
    mod_zero(4);
    mod_int(-1);
    mod_int(11);                                    /* macros */
    mod_int(4); mod_bytes("MAC1", 4); mod_zero(4); mod_int(2); mod_zero(2);
    mod_int(0);
    mod_int(9);                                     /* what it uses: skipped */
    mod_zero(4); mod_int(2); mod_zero(2); mod_int(2); mod_int(2); mod_zero(2); mod_zero(2); mod_int(1); mod_zero(4);
    mod_int(1); mod_int(2); mod_zero(2); mod_int(1); mod_zero(6);
    mod_int(0); mod_int(0);
    mod_int(12); mod_zero(2); mod_int(2); mod_zero(2);
    mod_int(1); mod_zero(4); mod_int(2); mod_zero(4); mod_bytes("ZZ", 2); mod_int(0); /* after the skipped ones */
    mod_int(0);

    r = e_parser_analyze((const char *)g_mod, g_mod_len);
    TEST_ASSERT(r != NULL && r->binary);
    if (r) {
        TEST_ASSERT(r->diagnostic_count == 0 && r->token_count == 0);
        s = symbol(r, SCOUT_SYMBOL_CONSTANT, "AB");
        TEST_ASSERT(s && s->scope == E_SCOPE_GLOBAL && s->range.start_line == 1);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "CDE") != NULL);
        o = symbol(r, SCOUT_SYMBOL_STRUCT, "obj");
        TEST_ASSERT(o != NULL);
        s = symbol(r, SCOUT_SYMBOL_FIELD, "ab");
        TEST_ASSERT(s && o && s->scope == E_SCOPE_LOCAL && &r->symbols[s->parent] == o);
        s = symbol(r, SCOUT_SYMBOL_METHOD, "draw");
        TEST_ASSERT(s && o && &r->symbols[s->parent] == o);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FUNCTION, "func") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "lab") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "glob") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_MACRO, "MAC1") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "ZZ") != NULL);
        TEST_ASSERT(r->symbol_count == 10);
    }
    e_result_free(r);

    /* a module cut off anywhere gives what it can */
    for (i = 0; i < g_mod_len; i++) {
        r = e_parser_analyze((const char *)g_mod, i);
        TEST_ASSERT(r != NULL);
        e_result_free(r);
    }

    /* the older format has fewer fields in an object */
    mod_header(5);
    mod_int(2);
    mod_int(4); mod_zero(4); mod_bytes("old\0", 4);
    mod_int(2); mod_zero(4); mod_bytes("ab", 2);
    mod_int(0);
    mod_zero(2);
    mod_int(0);
    r = e_parser_analyze((const char *)g_mod, g_mod_len);
    TEST_ASSERT(r != NULL && r->symbol_count == 2);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_FIELD, "ab") != NULL);
    e_result_free(r);

    /* a library's functions */
    mod_header(10);
    mod_int(6);
    mod_bytes("dos.library\0dosbase\0", 20);
    mod_bytes("Open\1\2\020", 7);           /* 16 follows: an empty slot */
    mod_bytes("Close\20", 6);               /* no arguments */
    mod_bytes("Read\1\2\3", 7);
    mod_bytes("\xFF", 1);
    r = e_parser_analyze((const char *)g_mod, g_mod_len);
    TEST_ASSERT(r != NULL && r->symbol_count == 3);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_FUNCTION, "Open") != NULL);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_FUNCTION, "Close") != NULL);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_FUNCTION, "Read") != NULL);
    e_result_free(r);

    /* damage in a library's list ends the walk: a blank where a name starts */
    mod_header(10);
    mod_int(6);
    mod_bytes("a\0b\0 ", 5);
    r = e_parser_analyze((const char *)g_mod, g_mod_len);
    TEST_ASSERT(r != NULL && r->symbol_count == 0);
    e_result_free(r);

    /* a format newer than known is left alone */
    mod_header(99);
    mod_int(1); mod_zero(4); mod_int(2); mod_zero(4); mod_bytes("AB", 2); mod_int(0); mod_int(0);
    r = e_parser_analyze((const char *)g_mod, g_mod_len);
    TEST_ASSERT(r != NULL && r->binary && r->symbol_count == 0);
    e_result_free(r);
}

/** @brief The answers to the requests of the protocol. */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang, *syntax, *kw;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(e_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "e") == 0);
    TEST_ASSERT(lang && json_array_length(json_obj_get_array(lang, "extensions")) == 2);
    syntax = json_obj_get(lang, "syntax");
    TEST_ASSERT(syntax && json_obj_get_bool(syntax, "caseSensitive", 0));
    TEST_ASSERT(syntax && json_array_length(json_obj_get_array(syntax, "lineComments")) == 2);
    TEST_ASSERT(syntax && json_array_length(json_obj_get_array(syntax, "blockComments")) == 1);
    TEST_ASSERT(syntax && json_obj_get(syntax, "preprocessor") != NULL);
    kw = json_obj_get(syntax, "keywords");
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "builtin")) > 180);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "keyword")) > 100);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "type")) == 9);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "constant")) > 10);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "variable")) > 10);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(e_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                        "\"params\":{\"path\":\"" DATA "sample.e\",\"want\":[\"symbols\",\"references\"]}}",
                                        &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    TEST_ASSERT(result && json_obj_get_array(result, "symbols") != NULL);
    TEST_ASSERT(result && json_obj_get_array(result, "references") != NULL);
    TEST_ASSERT(result && json_obj_get(result, "tokens") == NULL);
    TEST_ASSERT(result && !json_obj_get_bool(result, "binary", 0));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"target\":\"intuition/intuition.m\",\"system\":true") != NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"target\":\"helper.m\",\"range\"") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(e_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                        "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(e_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                        "\"params\":{\"path\":\"" DATA "missing.e\"}}", &out));
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
    test_modules();
    test_rpc();
    printf("%d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
