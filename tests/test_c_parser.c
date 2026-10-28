/**
 * @file test_c_parser.c
 * @brief Host-native tests of the C parser: the analysis (symbols,
 *        diagnostics, includes, tokens) and the Scout protocol, with the
 *        sample sources in tests/data/c_parser/.
 */
#include "c_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample sources (tests run from the project root). */
#define DATA "tests/data/c_parser/"

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
 * @brief Find the position of the n-th occurrence of a text.
 * @param src The source (terminated).
 * @param needle The text.
 * @param nth Which occurrence (from 1).
 * @param[out] line Its line.
 * @param[out] col Its column.
 * @return Non-zero if found.
 */
static int pos_of(const char *src, const char *needle, int nth, long *line, long *col)
{
    const char *p = src, *hit = NULL, *q;
    while (nth-- > 0) {
        hit = strstr(p, needle);
        if (!hit) return 0;
        p = hit + 1;
    }
    *line = 1;
    *col = 1;
    for (q = src; q < hit; q++) {
        if (*q == '\n') {
            (*line)++;
            *col = 1;
        } else {
            (*col)++;
        }
    }
    return 1;
}

/**
 * @brief Find the n-th symbol of a name.
 * @param res The result.
 * @param name The name.
 * @param nth Which one (from 1).
 * @return Its index, or -1.
 */
static long sym(const CParseResult *res, const char *name, int nth)
{
    long i;
    for (i = 0; i < res->symbol_count; i++) {
        if (strcmp(res->symbols[i].name, name) == 0 && --nth == 0) return i;
    }
    return -1;
}

/**
 * @brief Count the symbols of a name.
 * @param res The result.
 * @param name The name.
 * @return Their number.
 */
static int sym_count(const CParseResult *res, const char *name)
{
    long i;
    int n = 0;
    for (i = 0; i < res->symbol_count; i++) {
        if (strcmp(res->symbols[i].name, name) == 0) n++;
    }
    return n;
}

/**
 * @brief Return the kind of the token at a position.
 * @param res The result.
 * @param line Its line.
 * @param col Its column.
 * @return SCOUT_TOKEN_ value, 0 if there is no token there.
 */
static long token_at(const CParseResult *res, long line, long col)
{
    long i;
    for (i = 0; i < res->token_count; i++) {
        if (res->tokens[i * 4] == line && res->tokens[i * 4 + 1] == col) return res->tokens[i * 4 + 3];
    }
    return 0;
}

/**
 * @brief Return the kind of the token at the n-th occurrence of a text.
 * @param res The result.
 * @param src The source.
 * @param needle The text (the token starts with it).
 * @param nth Which occurrence (from 1).
 * @return SCOUT_TOKEN_ value, 0 for none, -1 if the text is not found.
 */
static long token_of(const CParseResult *res, const char *src, const char *needle, int nth)
{
    long line, col;
    if (!pos_of(src, needle, nth, &line, &col)) return -1;
    return token_at(res, line, col);
}

/**
 * @brief Tell whether a result has a diagnostic.
 * @param res The result.
 * @param message Its message.
 * @param severity Its severity.
 * @param line Its line.
 * @return Non-zero if it has.
 */
static int has_diag(const CParseResult *res, const char *message, int severity, long line)
{
    long i;
    for (i = 0; i < res->diagnostic_count; i++) {
        const CDiagnostic *d = &res->diagnostics[i];
        if (strcmp(d->message, message) == 0 && d->severity == severity && d->range.start_line == line) return 1;
    }
    return 0;
}

/**
 * @brief Analyse a terminated text as a source file.
 * @param src The text.
 * @return The result.
 */
static CParseResult *analyze_text(const char *src)
{
    return c_parser_analyze(src, strlen(src), 0);
}

/**
 * @brief The header sample: macros, a struct with a typedef of the same
 *        name, an anonymous typedef'd enum, declarations, an include, the
 *        scopes of a header.
 */
static void test_header(void)
{
    size_t len = 0;
    char *src = load(DATA "sample.h", &len);
    CParseResult *res;
    long i, point, alias, colour;
    TEST_ASSERT(src != NULL);
    if (!src) return;
    res = c_parser_analyze(src, len, 1);
    TEST_ASSERT(res != NULL && !res->out_of_memory);
    if (!res) return;

    TEST_ASSERT(res->diagnostic_count == 0);

    i = sym(res, "MAX_ITEMS", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_MACRO);
    TEST_ASSERT(i >= 0 && res->symbols[i].scope == C_SCOPE_GLOBAL);
    TEST_ASSERT(i >= 0 && strcmp(res->symbols[i].detail, "MAX_ITEMS 16") == 0);
    TEST_ASSERT(i >= 0 && res->symbols[i].selection.start_line == 7 &&
                res->symbols[i].selection.start_col == 9 && res->symbols[i].selection.end_col == 18);
    TEST_ASSERT(i >= 0 && res->symbols[i].range.start_col == 1 && res->symbols[i].range.end_col == 21);
    i = sym(res, "SQUARE", 1);
    TEST_ASSERT(i >= 0 && strcmp(res->symbols[i].detail, "SQUARE(x) ((x) * (x))") == 0);
    TEST_ASSERT(sym(res, "SAMPLE_H", 1) >= 0);

    point = sym(res, "Point", 1);
    alias = sym(res, "Point", 2);
    TEST_ASSERT(point >= 0 && res->symbols[point].kind == SCOUT_SYMBOL_STRUCT);
    TEST_ASSERT(point >= 0 && res->symbols[point].scope == C_SCOPE_GLOBAL);
    TEST_ASSERT(point >= 0 && res->symbols[point].range.start_line == 10 &&
                res->symbols[point].range.end_line == 13 && res->symbols[point].range.end_col == 2);
    TEST_ASSERT(point >= 0 && strcmp(res->symbols[point].detail, "struct Point") == 0);
    i = sym(res, "x", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_FIELD && res->symbols[i].parent == point);
    TEST_ASSERT(i >= 0 && res->symbols[i].scope == C_SCOPE_LOCAL);
    TEST_ASSERT(i >= 0 && strcmp(res->symbols[i].detail, "LONG x") == 0);
    TEST_ASSERT(alias > point && res->symbols[alias].kind == SCOUT_SYMBOL_TYPE_ALIAS);
    TEST_ASSERT(alias > point && strcmp(res->symbols[alias].detail, "typedef struct Point {...} Point") == 0);

    /* typedef enum { ... } Colour: the alias comes before its members */
    colour = sym(res, "Colour", 1);
    TEST_ASSERT(colour >= 0 && res->symbols[colour].kind == SCOUT_SYMBOL_TYPE_ALIAS);
    i = sym(res, "GREEN", 1);
    TEST_ASSERT(i > colour && res->symbols[i].kind == SCOUT_SYMBOL_ENUM_MEMBER && res->symbols[i].parent == colour);
    TEST_ASSERT(i >= 0 && res->symbols[i].scope == C_SCOPE_GLOBAL);
    TEST_ASSERT(sym(res, "RED", 1) >= 0 && sym(res, "BLUE", 1) >= 0);
    TEST_ASSERT(sym_count(res, "Colour") == 1);

    /* forward declaration: no symbol */
    TEST_ASSERT(sym(res, "List", 1) < 0);

    i = sym(res, "item_count", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_VARIABLE && res->symbols[i].declaration);
    i = sym(res, "add_numbers", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_FUNCTION && res->symbols[i].declaration);
    TEST_ASSERT(i >= 0 && strcmp(res->symbols[i].detail, "int add_numbers(int a, int b)") == 0);
    /* no parameters for a prototype */
    TEST_ASSERT(sym(res, "a", 1) < 0);

    TEST_ASSERT(res->reference_count == 1);
    TEST_ASSERT(res->reference_count == 1 && strcmp(res->references[0].target, "exec/types.h") == 0 &&
                res->references[0].system && res->references[0].range.start_col == 10 &&
                res->references[0].range.end_col == 24);

    /* tokens: the uses of the header's own names */
    TEST_ASSERT(token_of(res, src, "Point {", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "Point;", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "LONG x", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "x;", 1) == SCOUT_TOKEN_FIELD);
    TEST_ASSERT(token_of(res, src, "GREEN", 1) == SCOUT_TOKEN_CONSTANT);
    TEST_ASSERT(token_of(res, src, "SQUARE", 1) == SCOUT_TOKEN_MACRO);
    TEST_ASSERT(token_of(res, src, "List", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "add_numbers", 1) == SCOUT_TOKEN_FUNCTION);
    TEST_ASSERT(token_of(res, src, "ifndef", 1) == 0);
    TEST_ASSERT(token_of(res, src, "int item", 1) == 0);

    c_parse_result_free(res);

    /* the same text as a source file: types and macros only in the file */
    res = c_parser_analyze(src, len, 0);
    TEST_ASSERT(res != NULL);
    if (res) {
        i = sym(res, "Point", 1);
        TEST_ASSERT(i >= 0 && res->symbols[i].scope == C_SCOPE_FILE);
        i = sym(res, "MAX_ITEMS", 1);
        TEST_ASSERT(i >= 0 && res->symbols[i].scope == C_SCOPE_FILE);
        i = sym(res, "add_numbers", 1);
        TEST_ASSERT(i >= 0 && res->symbols[i].scope == C_SCOPE_GLOBAL);
        c_parse_result_free(res);
    }
    free(src);
}

/**
 * @brief The source sample: variables, a function pointer, functions with
 *        parameters, locals and a label, an \#else branch left out, and
 *        the tokens of the uses.
 */
static void test_source(void)
{
    size_t len = 0;
    char *src = load(DATA "sample.c", &len);
    CParseResult *res;
    long i, helper, add;
    TEST_ASSERT(src != NULL);
    if (!src) return;
    res = c_parser_analyze(src, len, 0);
    TEST_ASSERT(res != NULL && !res->out_of_memory);
    if (!res) return;

    TEST_ASSERT(res->diagnostic_count == 0);
    TEST_ASSERT(res->reference_count == 2);
    TEST_ASSERT(res->reference_count == 2 && strcmp(res->references[0].target, "sample.h") == 0 &&
                !res->references[0].system);
    TEST_ASSERT(res->reference_count == 2 && strcmp(res->references[1].target, "stdio.h") == 0 &&
                res->references[1].system);

    i = sym(res, "counter", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_VARIABLE && res->symbols[i].scope == C_SCOPE_FILE);
    TEST_ASSERT(i >= 0 && strcmp(res->symbols[i].detail, "static int counter") == 0);
    i = sym(res, "item_count", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].scope == C_SCOPE_GLOBAL && !res->symbols[i].declaration);
    i = sym(res, "other_count", 1);
    TEST_ASSERT(i >= 0 && strcmp(res->symbols[i].detail, "int other_count") == 0);
    i = sym(res, "handler", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_VARIABLE && res->symbols[i].scope == C_SCOPE_FILE);
    TEST_ASSERT(sym(res, "code", 1) < 0);

    helper = sym(res, "helper", 1);
    TEST_ASSERT(helper >= 0 && res->symbols[helper].kind == SCOUT_SYMBOL_FUNCTION);
    TEST_ASSERT(helper >= 0 && res->symbols[helper].scope == C_SCOPE_FILE && !res->symbols[helper].declaration);
    TEST_ASSERT(helper >= 0 && res->symbols[helper].range.start_line == 10 && res->symbols[helper].range.end_line == 24);
    TEST_ASSERT(helper >= 0 && strcmp(res->symbols[helper].detail,
                                      "static int helper(Point *p, ULONG flags)") == 0);
    i = sym(res, "p", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].parent == helper && res->symbols[i].scope == C_SCOPE_LOCAL);
    TEST_ASSERT(i >= 0 && strcmp(res->symbols[i].detail, "Point *p") == 0);
    TEST_ASSERT(sym(res, "flags", 1) >= 0);
    i = sym(res, "mask", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_VARIABLE && res->symbols[i].parent == helper);
    TEST_ASSERT(sym(res, "list", 1) >= 0 && sym(res, "i", 1) >= 0 && sym(res, "k", 1) >= 0);
    i = sym(res, "c", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].parent == helper && strcmp(res->symbols[i].detail, "Colour c") == 0);
    i = sym(res, "done", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_LABEL && res->symbols[i].parent == helper);

    add = sym(res, "add_numbers", 1);
    TEST_ASSERT(add >= 0 && res->symbols[add].scope == C_SCOPE_GLOBAL);
    i = sym(res, "b", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].parent == add);

    /* the #else branch is left out */
    TEST_ASSERT(sym_count(res, "compiler") == 1);
    i = sym(res, "compiler", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].range.start_line == 33);

    /* tokens */
    TEST_ASSERT(token_of(res, src, "Point *p", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "p, ULONG", 1) == SCOUT_TOKEN_PARAMETER);
    TEST_ASSERT(token_of(res, src, "ULONG flags", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "flags & 3", 1) == SCOUT_TOKEN_PARAMETER);
    TEST_ASSERT(token_of(res, src, "mask =", 1) == SCOUT_TOKEN_VARIABLE);
    TEST_ASSERT(token_of(res, src, "mask +=", 1) == SCOUT_TOKEN_VARIABLE);
    TEST_ASSERT(token_of(res, src, "List *list", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "counter +=", 1) == SCOUT_TOKEN_VARIABLE);
    TEST_ASSERT(token_of(res, src, "p->x", 1) == SCOUT_TOKEN_PARAMETER);
    TEST_ASSERT(token_of(res, src, "x >", 1) == SCOUT_TOKEN_FIELD);
    TEST_ASSERT(token_of(res, src, "done;", 1) == SCOUT_TOKEN_LABEL);
    TEST_ASSERT(token_of(res, src, "done:", 1) == SCOUT_TOKEN_LABEL);
    TEST_ASSERT(token_of(res, src, "Colour c", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "SQUARE(i)", 1) == SCOUT_TOKEN_FUNCTION);
    TEST_ASSERT(token_of(res, src, "printf", 1) == SCOUT_TOKEN_FUNCTION);
    TEST_ASSERT(token_of(res, src, "helper(NULL", 1) == SCOUT_TOKEN_FUNCTION);
    TEST_ASSERT(token_of(res, src, "a + b", 1) == SCOUT_TOKEN_PARAMETER);
    TEST_ASSERT(token_of(res, src, "MAX_ITEMS) goto", 1) == 0);
    TEST_ASSERT(token_of(res, src, "NULL", 1) == 0);
    /* the skipped branch still gets tokens */
    TEST_ASSERT(token_of(res, src, "compiler", 2) == SCOUT_TOKEN_FUNCTION);
    /* sorted */
    for (i = 1; i < res->token_count; i++) {
        long l0 = res->tokens[(i - 1) * 4], c0 = res->tokens[(i - 1) * 4 + 1];
        long l1 = res->tokens[i * 4], c1 = res->tokens[i * 4 + 1];
        TEST_ASSERT(l0 < l1 || (l0 == l1 && c0 + res->tokens[(i - 1) * 4 + 2] <= c1));
    }

    c_parse_result_free(res);
    free(src);
}

/**
 * @brief Diagnostics: brackets, literals, comments, \#if nesting, unknown
 *        directives, \#include and \#define without an argument.
 */
static void test_diagnostics(void)
{
    CParseResult *res = analyze_text(
        "int f(void) {\n"
        "    char *s = \"open;\n"
        "    char c = 'x;\n"
        "    g(1];\n"
        "}\n"
        "#endif\n"
        "#frobnicate\n"
        "#include\n"
        "#define\n"
        "#error don't\n"
        "#if X\n"
        "int g(void) {\n"
        "/* open\n");
    TEST_ASSERT(res != NULL);
    if (!res) return;
    TEST_ASSERT(has_diag(res, "Unterminated string literal", SCOUT_SEVERITY_ERROR, 2));
    TEST_ASSERT(has_diag(res, "Unterminated character literal", SCOUT_SEVERITY_ERROR, 3));
    TEST_ASSERT(has_diag(res, "Mismatched bracket ']'", SCOUT_SEVERITY_ERROR, 4));
    TEST_ASSERT(has_diag(res, "#endif without #if", SCOUT_SEVERITY_ERROR, 6));
    TEST_ASSERT(has_diag(res, "Unknown preprocessor directive #frobnicate", SCOUT_SEVERITY_WARNING, 7));
    TEST_ASSERT(has_diag(res, "#include without a file name", SCOUT_SEVERITY_ERROR, 8));
    TEST_ASSERT(has_diag(res, "#define without a name", SCOUT_SEVERITY_ERROR, 9));
    TEST_ASSERT(has_diag(res, "#if without #endif", SCOUT_SEVERITY_ERROR, 11));
    TEST_ASSERT(has_diag(res, "Unclosed bracket '{'", SCOUT_SEVERITY_ERROR, 12));
    TEST_ASSERT(has_diag(res, "Unclosed block comment", SCOUT_SEVERITY_ERROR, 13));
    /* the '}' closes the function again: the '(' left open is reported,
       but no mismatch; "don't" in #error is no literal */
    TEST_ASSERT(has_diag(res, "Unclosed bracket '('", SCOUT_SEVERITY_ERROR, 4));
    TEST_ASSERT(res->diagnostic_count == 11);
    c_parse_result_free(res);

    res = analyze_text("int ok(void) { return (1 + [2]); }\n");
    TEST_ASSERT(res != NULL && res->diagnostic_count == 0);
    c_parse_result_free(res);
}

/**
 * @brief Positions: CR, CRLF and LF line ends, tabs, a NUL byte and
 *        Latin-1 characters count one column each.
 */
static void test_positions(void)
{
    static const char src[] = "/* \xe4 */ int a;\r\nint\tb;\rint c;\n\0int d;\n";
    CParseResult *res = c_parser_analyze(src, sizeof(src) - 1, 0);
    long i;
    TEST_ASSERT(res != NULL);
    if (!res) return;
    i = sym(res, "a", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].selection.start_line == 1 && res->symbols[i].selection.start_col == 13);
    i = sym(res, "b", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].selection.start_line == 2 && res->symbols[i].selection.start_col == 5);
    i = sym(res, "c", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].selection.start_line == 3 && res->symbols[i].selection.start_col == 5);
    i = sym(res, "d", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].selection.start_line == 4 && res->symbols[i].selection.start_col == 6);
    c_parse_result_free(res);

    /* empty text */
    res = c_parser_analyze("", 0, 0);
    TEST_ASSERT(res != NULL && res->symbol_count == 0 && res->token_count == 0 && res->diagnostic_count == 0);
    c_parse_result_free(res);
}

/**
 * @brief More declarations: nested structs, bit fields, function pointer
 *        fields and typedefs, arrays, attributes, K&R-free prototypes.
 */
static void test_declarations(void)
{
    const char *src =
        "struct Outer {\n"
        "    struct Inner { int deep; } in;\n"
        "    union { long l; char c[4]; };\n"
        "    unsigned flag : 1, other : 2;\n"
        "    void (*callback)(struct Outer *self, int n);\n"
        "};\n"
        "typedef int (*Compare)(const void *, const void *);\n"
        "static const char *names[3] = { \"a\", \"b\", \"c\" };\n"
        "int __attribute__((unused)) quiet;\n"
        "enum Mode { MODE_A = (1 << 0), MODE_B } mode;\n"
        "void run(void);\n";
    CParseResult *res = analyze_text(src);
    long outer, i;
    TEST_ASSERT(res != NULL);
    if (!res) return;
    TEST_ASSERT(res->diagnostic_count == 0);
    outer = sym(res, "Outer", 1);
    TEST_ASSERT(outer >= 0 && res->symbols[outer].kind == SCOUT_SYMBOL_STRUCT);
    i = sym(res, "Inner", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].parent == outer && res->symbols[i].scope == C_SCOPE_LOCAL);
    TEST_ASSERT(sym(res, "deep", 1) >= 0 && res->symbols[sym(res, "deep", 1)].parent == i);
    TEST_ASSERT(sym(res, "in", 1) >= 0 && res->symbols[sym(res, "in", 1)].parent == outer);
    /* members of an anonymous union belong to the outer struct */
    TEST_ASSERT(sym(res, "l", 1) >= 0 && res->symbols[sym(res, "l", 1)].parent == outer);
    TEST_ASSERT(sym(res, "c", 1) >= 0);
    TEST_ASSERT(sym(res, "flag", 1) >= 0 && sym(res, "other", 1) >= 0);
    i = sym(res, "callback", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_FIELD);
    TEST_ASSERT(sym(res, "self", 1) < 0);
    i = sym(res, "Compare", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_TYPE_ALIAS);
    i = sym(res, "names", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_VARIABLE && res->symbols[i].scope == C_SCOPE_FILE);
    TEST_ASSERT(i >= 0 && strcmp(res->symbols[i].detail, "static const char *names[3]") == 0);
    TEST_ASSERT(sym(res, "quiet", 1) >= 0 && sym(res, "unused", 1) < 0);
    i = sym(res, "Mode", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].kind == SCOUT_SYMBOL_ENUM);
    TEST_ASSERT(sym(res, "MODE_B", 1) >= 0 && res->symbols[sym(res, "MODE_B", 1)].parent == i);
    TEST_ASSERT(sym(res, "mode", 1) >= 0);
    i = sym(res, "run", 1);
    TEST_ASSERT(i >= 0 && res->symbols[i].declaration);
    TEST_ASSERT(token_of(res, src, "Outer *self", 1) == SCOUT_TOKEN_TYPE);
    TEST_ASSERT(token_of(res, src, "MODE_A", 1) == SCOUT_TOKEN_CONSTANT);
    c_parse_result_free(res);
}

/**
 * @brief Answer one request and parse the answer.
 * @param request The request.
 * @param[out] msg The answer (scout_rpc_free() it).
 * @return Non-zero if the answer is one line of valid JSON-RPC.
 */
static int ask(const char *request, ScoutRpcMessage *msg)
{
    JsonWriter w;
    int ok;
    json_writer_init_alloc(&w, 256);
    ok = c_parser_handle_request(request, &w) == 1;
    ok = ok && strchr(json_writer_get(&w), '\n') == NULL;
    ok = scout_rpc_parse(json_writer_get(&w), msg) && ok;
    json_writer_free(&w);
    return ok;
}

/**
 * @brief The protocol: scout/describe, scout/analyze with a file, a
 *        snapshot, "want", and the errors.
 */
static void test_protocol(void)
{
    ScoutRpcMessage m;
    const JsonValue *v, *lang, *syntax, *list;
    size_t i;
    int found;

    TEST_ASSERT(ask("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &m));
    TEST_ASSERT(m.id == 1 && !m.has_error && m.result);
    TEST_ASSERT(json_obj_get_int(m.result, "protocol", 0) == SCOUT_PROTOCOL_VERSION);
    TEST_ASSERT(strcmp(json_obj_get_string(m.result, "name", ""), "c_parser") == 0);
    v = json_obj_get_array(m.result, "languages");
    lang = v ? json_array_get(v, 0) : NULL;
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "c") == 0);
    list = lang ? json_obj_get_array(lang, "extensions") : NULL;
    TEST_ASSERT(list && json_array_length(list) == 2);
    syntax = lang ? json_obj_get_obj(lang, "syntax") : NULL;
    TEST_ASSERT(syntax && json_obj_get_bool(syntax, "caseSensitive", 0));
    v = syntax ? json_obj_get_array(syntax, "blockComments") : NULL;
    TEST_ASSERT(v && strcmp(json_obj_get_string(json_array_get(v, 0), "close", ""), "*/") == 0);
    v = syntax ? json_obj_get_array(syntax, "strings") : NULL;
    TEST_ASSERT(v && strcmp(json_obj_get_string(json_array_get(v, 0), "escape", ""), "\\") == 0);
    v = syntax ? json_obj_get_obj(syntax, "keywords") : NULL;
    list = v ? json_obj_get_array(v, "keyword") : NULL;
    found = 0;
    for (i = 0; list && i < json_array_length(list); i++) {
        const JsonValue *k = json_array_get(list, i);
        if (k && k->type == JSON_STRING && strcmp(k->u.str_val, "while") == 0) found = 1;
    }
    TEST_ASSERT(found);
    v = json_obj_get_obj(m.result, "capabilities");
    TEST_ASSERT(v && json_obj_get_bool(v, "tokens", 0) && json_obj_get_bool(v, "references", 0));
    scout_rpc_free(&m);

    TEST_ASSERT(ask("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                    "\"params\":{\"path\":\"" DATA "sample.c\",\"language\":\"c\"}}", &m));
    TEST_ASSERT(m.id == 2 && !m.has_error && m.result);
    TEST_ASSERT(strcmp(json_obj_get_string(m.result, "language", ""), "c") == 0);
    v = json_obj_get_array(m.result, "symbols");
    TEST_ASSERT(v && json_array_length(v) > 10);
    if (v) {
        const JsonValue *s = json_array_get(v, 0), *range;
        TEST_ASSERT(strcmp(json_obj_get_string(s, "name", ""), "counter") == 0);
        TEST_ASSERT(json_obj_get_int(s, "kind", 0) == SCOUT_SYMBOL_VARIABLE);
        TEST_ASSERT(strcmp(json_obj_get_string(s, "scope", ""), "file") == 0);
        range = json_obj_get_obj(s, "selection");
        list = range ? json_obj_get_array(range, "start") : NULL;
        TEST_ASSERT(list && json_array_get(list, 0)->u.num_val.int_val == 5 &&
                    json_array_get(list, 1)->u.num_val.int_val == 12);
    }
    TEST_ASSERT(json_obj_get_array(m.result, "diagnostics") != NULL);
    v = json_obj_get_array(m.result, "references");
    TEST_ASSERT(v && json_array_length(v) == 2 &&
                strcmp(json_obj_get_string(json_array_get(v, 0), "kind", ""), "include") == 0);
    v = json_obj_get_array(m.result, "tokens");
    TEST_ASSERT(v && json_array_length(v) > 0 && json_array_length(v) % 4 == 0);
    scout_rpc_free(&m);

    /* a snapshot: text read from "text", scopes from "path"; only symbols */
    TEST_ASSERT(ask("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                    "\"params\":{\"path\":\"Work:none/sample.h\",\"text\":\"" DATA "sample.h\","
                    "\"want\":[\"symbols\"]}}", &m));
    TEST_ASSERT(m.id == 3 && !m.has_error && m.result);
    v = json_obj_get_array(m.result, "symbols");
    TEST_ASSERT(v && strcmp(json_obj_get_string(json_array_get(v, 0), "scope", ""), "global") == 0);
    TEST_ASSERT(json_obj_get_array(m.result, "tokens") == NULL);
    TEST_ASSERT(json_obj_get_array(m.result, "diagnostics") == NULL);
    scout_rpc_free(&m);

    /* errors */
    TEST_ASSERT(ask("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                    "\"params\":{\"path\":\"" DATA "missing.c\"}}", &m));
    TEST_ASSERT(m.id == 4 && m.has_error && m.error_code == SCOUT_ERR_FILE);
    scout_rpc_free(&m);
    TEST_ASSERT(ask("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"scout/analyze\","
                    "\"params\":{\"path\":\"" DATA "sample.c\",\"language\":\"pascal\"}}", &m));
    TEST_ASSERT(m.has_error && m.error_code == SCOUT_ERR_LANGUAGE);
    scout_rpc_free(&m);
    TEST_ASSERT(ask("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"scout/analyze\",\"params\":{}}", &m));
    TEST_ASSERT(m.has_error && m.error_code == SCOUT_RPC_ERR_INVALID_PARAMS);
    scout_rpc_free(&m);
    TEST_ASSERT(ask("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"textDocument/documentSymbol\"}", &m));
    TEST_ASSERT(m.id == 7 && m.has_error && m.error_code == SCOUT_RPC_ERR_METHOD_NOT_FOUND);
    scout_rpc_free(&m);
    TEST_ASSERT(ask("not json", &m));
    TEST_ASSERT(m.has_error && m.error_code == SCOUT_RPC_ERR_PARSE_ERROR);
    scout_rpc_free(&m);
}

/** Calls of stop_soon() before it asks to stop. */
static int g_stop_after = 0;

/**
 * @brief A c_parser_should_stop() that asks to stop after a number of calls.
 * @return Non-zero to stop.
 */
static int stop_soon(void)
{
    return --g_stop_after <= 0;
}

/**
 * @brief Stopping: a long source stops when asked, and the request is not
 *        answered.
 */
static void test_stop(void)
{
    size_t n = 200000, i;
    char *src = (char *)malloc(n + 1);
    CParseResult *res;
    JsonWriter w;
    TEST_ASSERT(src != NULL);
    if (!src) return;
    for (i = 0; i < n; i++) src[i] = (i % 8 == 7) ? '\n' : 'x';
    src[n] = '\0';
    g_stop_after = 2;
    c_parser_should_stop = stop_soon;
    res = c_parser_analyze(src, n, 0);
    TEST_ASSERT(res != NULL && res->stopped);
    c_parse_result_free(res);
    free(src);

    g_stop_after = 1;
    json_writer_init_alloc(&w, 64);
    TEST_ASSERT(c_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/analyze\","
                                        "\"params\":{\"path\":\"" DATA "sample.c\"}}", &w) == 1 ||
                g_stop_after <= 0);
    json_writer_free(&w);
    c_parser_should_stop = NULL;
}

/**
 * @brief Run the tests of the C parser.
 * @return 0 when all checks pass, 1 otherwise.
 */
int main(void)
{
    printf("Running C Language Parser unit tests...\n");
    test_header();
    test_source();
    test_diagnostics();
    test_positions();
    test_declarations();
    test_protocol();
    test_stop();
    printf("C Parser tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
