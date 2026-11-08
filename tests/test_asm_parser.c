/**
 * @file test_asm_parser.c
 * @brief Host-native tests of the assembler parser: the analysis (symbols,
 *        diagnostics, tokens) and the Scout protocol, with the sample in
 *        tests/data/asm_parser/.
 */
#include "asm_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample sources (tests run from the project root). */
#define DATA "tests/data/asm_parser/"

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
static const AsmSymbol *symbol(const AsmResult *r, int kind, const char *name)
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
static const AsmDiagnostic *diagnostic(const AsmResult *r, long line, const char *text)
{
    long i;
    for (i = 0; i < r->diagnostic_count; i++) {
        const AsmDiagnostic *d = &r->diagnostics[i];
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
static int has_token(const AsmResult *r, long line, long col, long len, long kind)
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
static AsmResult *analyze(const char *text)
{
    return asm_parser_analyze(text, strlen(text), 0);
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
    AsmResult *r = analyze(text);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        const AsmDiagnostic *d = diagnostic(r, line, message);
        if (r->diagnostic_count != 1 || d == NULL || d->severity != severity)
            printf("  in: %s  got %ld, first: %s\n", text, r->diagnostic_count,
                   r->diagnostic_count ? r->diagnostics[0].message : "");
        TEST_ASSERT(r->diagnostic_count == 1);
        TEST_ASSERT(d != NULL && d->severity == severity);
    }
    asm_result_free(r);
}

/**
 * @brief Check that a text has no diagnostics.
 * @param text The text.
 */
static void clean(const char *text)
{
    AsmResult *r = analyze(text);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        if (r->diagnostic_count) printf("  in: %s  got: %s\n", text, r->diagnostics[0].message);
        TEST_ASSERT(r->diagnostic_count == 0);
    }
    asm_result_free(r);
}

/** @brief The sample: symbols, the INCLUDE and tokens. */
static void test_sample(void)
{
    size_t length = 0;
    char *text = load(DATA "sample.asm", &length);
    AsmResult *r;
    const AsmSymbol *s, *start;
    TEST_ASSERT(text != NULL);
    if (text == NULL) return;
    r = asm_parser_analyze(text, length, 0);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        TEST_ASSERT(r->diagnostic_count == 0);
        if (r->diagnostic_count) printf("  got %ld:%s\n", r->diagnostics[0].range.start_line, r->diagnostics[0].message);
        s = symbol(r, SCOUT_SYMBOL_CONSTANT, "Version");
        TEST_ASSERT(s && s->detail && strcmp(s->detail, "equ 37") == 0);
        TEST_ASSERT(s && s->selection.start_line == 7 && s->selection.end_col == 8);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "Limit") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "Regs") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "Ptr") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "Count") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "item_Next") != NULL);
        s = symbol(r, SCOUT_SYMBOL_MACRO, "PUSH");
        TEST_ASSERT(s && s->range.start_line == 18 && s->range.end_line == 21);
        start = symbol(r, SCOUT_SYMBOL_LABEL, "_Start");
        TEST_ASSERT(start && start->scope == ASM_SCOPE_GLOBAL && start->parent == -1);
        s = symbol(r, SCOUT_SYMBOL_LABEL, ".loop");
        TEST_ASSERT(s && start && s->parent >= 0 && r->symbols + s->parent == start && s->scope == ASM_SCOPE_LOCAL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "Other") && symbol(r, SCOUT_SYMBOL_LABEL, "Other")->scope == ASM_SCOPE_FILE);
        s = symbol(r, SCOUT_SYMBOL_LABEL, "1$");
        TEST_ASSERT(s && s->parent >= 0 && strcmp(r->symbols[s->parent].name, "Other") == 0);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "lab") == NULL);       /* inside a macro */
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "this") == NULL);      /* after END */
        TEST_ASSERT(r->reference_count == 1);
        if (r->reference_count == 1) {
            TEST_ASSERT(strcmp(r->references[0].target, "exec/types.i") == 0);
            TEST_ASSERT(r->references[0].range.start_line == 4 && r->references[0].range.start_col == 11);
            TEST_ASSERT(r->references[0].range.end_col == 23);
        }
        TEST_ASSERT(has_token(r, 7, 1, 7, SCOUT_TOKEN_CONSTANT));
        TEST_ASSERT(has_token(r, 18, 1, 4, SCOUT_TOKEN_MACRO));
        TEST_ASSERT(has_token(r, 19, 10, 2, SCOUT_TOKEN_PARAMETER));    /* \1 */
        TEST_ASSERT(has_token(r, 20, 4, 2, SCOUT_TOKEN_PARAMETER));     /* \@ in a label */
        TEST_ASSERT(has_token(r, 20, 14, 2, SCOUT_TOKEN_PARAMETER));    /* \1 in a string */
        TEST_ASSERT(has_token(r, 26, 2, 4, SCOUT_TOKEN_MACRO));
        TEST_ASSERT(has_token(r, 27, 9, 7, SCOUT_TOKEN_CONSTANT));
        TEST_ASSERT(has_token(r, 28, 1, 5, SCOUT_TOKEN_PARAMETER));
        TEST_ASSERT(has_token(r, 29, 8, 5, SCOUT_TOKEN_PARAMETER));
    }
    asm_result_free(r);
    free(text);
}

/** @brief What is reported. */
static void test_diagnostics(void)
{
    only(" endc\n", 1, SCOUT_SEVERITY_ERROR, "endc without a conditional");
    only(" else\n", 1, SCOUT_SEVERITY_ERROR, "else without a conditional");
    only(" ifeq 1\n else\n else\n endc\n", 3, SCOUT_SEVERITY_ERROR, "else after the ELSE");
    only(" ifeq 1\n nop\n", 1, SCOUT_SEVERITY_ERROR, "conditional without ENDC");
    only(" endm\n", 1, SCOUT_SEVERITY_ERROR, "endm without MACRO");
    only("m macro\n nop\n", 1, SCOUT_SEVERITY_ERROR, "MACRO without ENDM");
    only(" rept 2\n nop\n", 1, SCOUT_SEVERITY_ERROR, "REPT without ENDR");
    only(" endr\n", 1, SCOUT_SEVERITY_ERROR, "endr without REPT");
    only("x: nop\nx: nop\n", 2, SCOUT_SEVERITY_ERROR, "'x' is defined twice");
    only("x equ 1\nx equ 2\n", 2, SCOUT_SEVERITY_ERROR, "'x' is defined twice");
    only("a:\n.x: nop\n.x: nop\n", 3, SCOUT_SEVERITY_ERROR, "local label '.x' is defined twice");
    only("a:\n.x: nop\nb:\n bra .x\n", 4, SCOUT_SEVERITY_ERROR, "local label '.x' is not defined");
    only("a:\n1$: nop\nb:\n dbra d0,1$\n", 4, SCOUT_SEVERITY_ERROR, "local label '1$' is not defined");
    only(" move.q d0,d1\n", 1, SCOUT_SEVERITY_ERROR, "this instruction has no size 'q'");
    only(" lea.b 0,a0\n", 1, SCOUT_SEVERITY_ERROR, "this instruction has no size 'b'");
    only(" swap.l d0\n", 1, SCOUT_SEVERITY_ERROR, "this instruction has no size 'l'");
    only(" move.long d0,d1\n", 1, SCOUT_SEVERITY_ERROR, "this instruction has no size");
    only(" move.l (a0,d0\n", 1, SCOUT_SEVERITY_ERROR, "')' is missing");
    only(" move.l d0),d1\n", 1, SCOUT_SEVERITY_ERROR, "')' without '('");
    only(" dc.b 'abc\n", 1, SCOUT_SEVERITY_WARNING, "string is not closed");
    only(" foobar d0\n", 1, SCOUT_SEVERITY_HINT, "unknown instruction or macro 'foobar'");
}

/** @brief Correct sources, and those whose reading is easy to get wrong. */
static void test_correct(void)
{
    clean("");
    clean("* only a comment\n; another\n\n");
    clean("x set 1\nx set 2\n");
    clean(" ifd a\nx: nop\n else\nx: nop\n endc\n");
    clean(" rept 2\nx: nop\n endr\n");
    clean(" ifeq 1\n nop\n elif 2\n nop\n elseif 3\n nop\n else\n nop\n endif\n");
    /* the part of a local label */
    clean("a:\n.x: nop\nb equ 1\nc set 2\nd rs.l 1\ne reg d0\n bra .x\n");
    clean("a:\n bra .x\n ifd z\nb:\n endc\n.x: nop\n");
    clean("a:\n.x: nop\nb:\n.x: nop\n bra .x\n");
    clean("a:\n.Xy: nop\n bra .xy\n");
    clean("a:\nx$: nop\n bra x$\n");
    clean("a:\n bra .later\n.later equ *\n");
    clean(" bsr .first\n.first: rts\n");
    /* case and forms of labels */
    clean("x: nop\nX: nop\n");
    clean("x:: nop\n lab: nop\n  foo equ 1\ny=1\n z = 2\nadd: nop\n bra add\nstop equ 1\n");
    /* mnemonics in the first column */
    clean("rts\nrts\nmove.l d0,d1\nadd d0,d1\n");
    /* what follows the operands is a comment */
    clean(" move.l d0,d1 comment (with 'quote\n");
    clean(" rts that's (all\n nop don't (\n");
    clean(" move.l d0, d1 and (a comment\n");
    clean(" move.l (a0, d0),d1\n");
    clean(" cmp.b #';',d0 ; (\n");
    clean(" cmp.b #\";\",d0 ; '\n");
    clean(" dc.b 'it''s',0\n");
    clean(" dc.b 'a\\'b\n");
    clean(" dc.b \"a\\\"b\",0\n");
    /* operands */
    clean(" MOVE.L D0,D1\n BSR.S x\n Bne.B x\n");
    clean(" move.l #$ff,(a0,d0.w)\n move.w foo.w,bar.l\n move.b %101,@7\n jmp (foo).l\n");
    clean(" move.l #4*(2+3),d0\n dc.l *+4\n");
    /* macros are not checked inside, and bring what they define */
    clean("m macro\n bra .nothing\n move.q d0,(\n dc.b 'x\n endm\n m\n m.l d0\n");
    clean("m macro\n endm\nm d0\n");
    clean(" macro n\n endm\n n\n");
    clean("m macro\nl\\@: bra l\\@\n endm\n");
    clean("o macro\ni macro\n endm\n endm\n o\n");
    /* directives with text */
    clean(" section \"a (\",code\n ttl a title (\n fail bad (\n printt \"x\n idnt a(b\n");
    clean(" ifc '\\1','x'\n endc\n");
    /* REM and END */
    clean(" rem\n junk ((\n erem\n nop\n");
    clean(" nop\n end\n garbage ((\n");
    clean(" ifeq 1\n end\n endc\n");
    /* an INCLUDE may bring macros in */
    clean(" include \"x.i\"\n foobar d0\n");
    clean(" include x.i\n foobar d0\n");
    /* GNU syntax */
    clean(".text\n .globl x\n foo ((\n");
    clean("#NO_APP\n foo ((\n");
    clean(" move.l %d0,%d1\n foo ((\n");
    /* line ends and odd bytes */
    clean("x: nop\r\n.y: bra .y\r\n");
    clean("x: nop\r.y: bra .y\r");
    {
        static const char odd[] = " nop\0 ((\n\0";
        AsmResult *r = asm_parser_analyze(odd, sizeof(odd) - 1, 0);
        TEST_ASSERT(r != NULL);
        asm_result_free(r);
    }
}

/** @brief The tokens are in order and do not overlap. */
static void test_tokens(void)
{
    AsmResult *r = analyze("Sz equ 4\nm macro\nl\\@: move.\\0 \\1,\\2\n endm\nlab: m.l Sz,lab\n.a: bra.s .a\n dc.l .a-lab,Sz*2\n");
    long i;
    TEST_ASSERT(r != NULL);
    if (r == NULL) return;
    for (i = 1; i < r->token_count; i++) {
        const long *a = &r->tokens[(i - 1) * 4], *b = &r->tokens[i * 4];
        TEST_ASSERT(a[0] < b[0] || (a[0] == b[0] && a[1] + a[2] <= b[1]));
    }
    TEST_ASSERT(has_token(r, 3, 2, 2, SCOUT_TOKEN_PARAMETER));      /* \@ */
    TEST_ASSERT(has_token(r, 3, 11, 2, SCOUT_TOKEN_PARAMETER));     /* \0 as the size */
    TEST_ASSERT(has_token(r, 5, 6, 1, SCOUT_TOKEN_MACRO));
    TEST_ASSERT(has_token(r, 5, 10, 2, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 5, 13, 3, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(has_token(r, 7, 7, 2, SCOUT_TOKEN_PARAMETER));
    asm_result_free(r);
}

/** @brief What the macros of the NDK define, and macros making names. */
static void test_ndk_macros(void)
{
    static const char include[] = " ULONG f\nX equ 1\nl:\n.m: nop\n";
    AsmResult *r = analyze(" STRUCTURE Item,LN_SIZE\n"
                           " ULONG it_Next\n"
                           " UWORD it_Flags ; flags\n"
                           " STRUCT it_Name,32\n"
                           " ALIGNLONG\n"
                           " LABEL it_SIZEOF\n"
                           "\n"
                           " BITDEF ITEM,DIRTY,3\n"
                           " ENUM 1\n"
                           " EITEM ONE\n"
                           " FUNCDEF Open\n"
                           " DEVCMD CMD_X\n"
                           "LIBENT macro\n"
                           "_LVO\\1 equ count\n"
                           " endm\n"
                           " LIBENT Close\n"
                           " move.l it_Next(a0),d0\n"
                           " move.l #ITEMF_DIRTY,d1\n"
                           " jsr _LVOClose(a6)\n"
                           " jsr _LVOOpen(a6)\n");
    const AsmSymbol *st, *s;
    TEST_ASSERT(r != NULL);
    if (r == NULL) return;
    TEST_ASSERT(r->diagnostic_count == 0);
    st = symbol(r, SCOUT_SYMBOL_STRUCT, "Item");
    TEST_ASSERT(st && st->range.start_line == 1 && st->range.end_line == 6 && st->selection.start_col == 12);
    s = symbol(r, SCOUT_SYMBOL_FIELD, "it_Next");
    TEST_ASSERT(s && st && r->symbols + s->parent == st && s->scope == ASM_SCOPE_FILE);
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "ULONG") == 0);
    s = symbol(r, SCOUT_SYMBOL_FIELD, "it_Name");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "STRUCT 32") == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "it_SIZEOF") != NULL);
    s = symbol(r, SCOUT_SYMBOL_CONSTANT, "ITEMB_DIRTY");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "bit 3") == 0);
    TEST_ASSERT(s && s->selection.start_line == 8 && s->selection.start_col == 14 && s->selection.end_col == 19);
    s = symbol(r, SCOUT_SYMBOL_CONSTANT, "ITEMF_DIRTY");
    TEST_ASSERT(s && s->detail && strcmp(s->detail, "1 << 3") == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "ONE") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "_LVOOpen") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "CMD_X") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "_LVOClose") != NULL);
    TEST_ASSERT(has_token(r, 1, 2, 9, SCOUT_TOKEN_MACRO));
    TEST_ASSERT(has_token(r, 1, 12, 4, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 2, 2, 5, SCOUT_TOKEN_MACRO));
    TEST_ASSERT(has_token(r, 2, 8, 7, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 17, 9, 7, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 18, 10, 11, SCOUT_TOKEN_CONSTANT));    /* a name made from operands */
    TEST_ASSERT(has_token(r, 19, 6, 9, SCOUT_TOKEN_CONSTANT));
    asm_result_free(r);

    /* in an include file everything but local labels is seen by others */
    r = asm_parser_analyze(include, sizeof(include) - 1, 1);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "f") && symbol(r, SCOUT_SYMBOL_FIELD, "f")->scope == ASM_SCOPE_GLOBAL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "X") && symbol(r, SCOUT_SYMBOL_CONSTANT, "X")->scope == ASM_SCOPE_GLOBAL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "l") && symbol(r, SCOUT_SYMBOL_LABEL, "l")->scope == ASM_SCOPE_GLOBAL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, ".m") && symbol(r, SCOUT_SYMBOL_LABEL, ".m")->scope == ASM_SCOPE_LOCAL);
    }
    asm_result_free(r);

    /* a macro of the source goes first; a label may be named like a macro */
    r = analyze("ULONG macro\n nop\n endm\n ULONG x\nLABEL: nop\nWORD nop\n");
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        TEST_ASSERT(r->diagnostic_count == 0);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "x") == NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "LABEL") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "WORD") != NULL);
    }
    asm_result_free(r);

    /* a word in the first column named like a mnemonic is a label if a mnemonic follows */
    r = analyze("text dc.b 1\nstop rts\nlist equ 1\nm macro\n endm\ncode m\n dc.l text,list\nrts\n");
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        TEST_ASSERT(r->diagnostic_count == 0);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "text") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "stop") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "code") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "list") != NULL);
        TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "rts") == NULL);
    }
    asm_result_free(r);

    only(" ULONG a\n ULONG a\n", 2, SCOUT_SEVERITY_ERROR, "'a' is defined twice");
    only(" BITDEF M,A,0\n EITEM MB_A\n", 2, SCOUT_SEVERITY_ERROR, "'MB_A' is defined twice");
    /* incomplete calls define nothing and do no harm */
    clean(" ULONG\n BITDEF\n BITDEF M\n BITDEF M,A\n FUNCDEF\n STRUCTURE\n STRUCT a\n");
    clean(" ifd z\n ULONG a\n else\n ULONG a\n endc\n");
}

/** @brief The answers to the requests of the protocol. */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang, *syntax, *kw;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(asm_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "asm") == 0);
    syntax = json_obj_get(lang, "syntax");
    TEST_ASSERT(syntax && json_obj_get_array(syntax, "firstColumnComments") != NULL);
    kw = json_obj_get(syntax, "keywords");
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "builtin")) > 300);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "keyword")) > 100);
    TEST_ASSERT(kw && json_obj_get_array(kw, "type") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(asm_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"" DATA "sample.asm\",\"want\":[\"symbols\",\"references\"]}}",
                                          &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    TEST_ASSERT(result && json_obj_get_array(result, "symbols") != NULL);
    TEST_ASSERT(result && json_obj_get_array(result, "references") != NULL);
    TEST_ASSERT(result && json_obj_get(result, "tokens") == NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"scope\":\"global\"") != NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"scope\":\"local\",\"parent\":") != NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "{\"kind\":\"include\",\"target\":\"exec/types.i\"") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(asm_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(asm_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                          "\"params\":{\"path\":\"" DATA "missing.asm\"}}", &out));
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
    test_tokens();
    test_ndk_macros();
    test_rpc();
    printf("%d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
