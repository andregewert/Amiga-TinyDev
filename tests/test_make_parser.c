/**
 * @file test_make_parser.c
 * @brief Host-native tests of the Makefile parser: the analysis (symbols,
 *        diagnostics, references, tokens) and the Scout protocol, with the
 *        sample Makefile in tests/data/make_parser/.
 */
#include "make_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample Makefiles (tests run from the project root). */
#define DATA "tests/data/make_parser/"

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
static const MakeSymbol *symbol(const MakeParseResult *r, int kind, const char *name)
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
static const MakeDiagnostic *diagnostic(const MakeParseResult *r, long line, const char *message)
{
    long i;
    for (i = 0; i < r->diagnostic_count; i++) {
        const MakeDiagnostic *d = &r->diagnostics[i];
        if (d->range.start_line == line && strncmp(d->message, message, strlen(message)) == 0) return d;
    }
    return NULL;
}

/**
 * @brief Tell whether a token is in the result.
 * @param r The result.
 * @param line Its line.
 * @param col Its column.
 * @param len Its length.
 * @param kind Its SCOUT_TOKEN_ kind.
 * @return Non-zero if it is.
 */
static int token(const MakeParseResult *r, long line, long col, long len, long kind)
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
 * @return The result (never NULL in the tests).
 */
static MakeParseResult *analyze(const char *text)
{
    return make_parser_analyze(text, strlen(text));
}

/**
 * @brief Analyse a text and tell whether it has exactly one diagnostic,
 *        on a line and beginning with a message.
 * @param text The text.
 * @param line The line.
 * @param message The beginning of the message.
 * @return Non-zero if so.
 */
static int only_diagnostic(const char *text, long line, const char *message)
{
    MakeParseResult *r = analyze(text);
    int ok = r && r->diagnostic_count == 1 && diagnostic(r, line, message) != NULL;
    if (r && !ok) {
        long i;
        for (i = 0; i < r->diagnostic_count; i++)
            printf("  got %ld: %s\n", r->diagnostics[i].range.start_line, r->diagnostics[i].message);
    }
    make_parse_result_free(r);
    return ok;
}

/**
 * @brief Analyse a text and tell whether it has no diagnostics.
 * @param text The text.
 * @return Non-zero if so.
 */
static int no_diagnostics(const char *text)
{
    MakeParseResult *r = analyze(text);
    int ok = r && r->diagnostic_count == 0;
    if (r && !ok) {
        long i;
        for (i = 0; i < r->diagnostic_count; i++)
            printf("  got %ld: %s\n", r->diagnostics[i].range.start_line, r->diagnostics[i].message);
    }
    make_parse_result_free(r);
    return ok;
}

/** @brief The sample Makefile: symbols, references, tokens, no diagnostics. */
static void test_sample(void)
{
    size_t length = 0;
    char *text = load(DATA "Makefile", &length);
    MakeParseResult *r;
    const MakeSymbol *s;
    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = make_parser_analyze(text, length);
    free(text);
    TEST_ASSERT(r != NULL);
    if (!r) return;
    TEST_ASSERT(r->diagnostic_count == 0);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "CC") != NULL);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "CFLAGS");
    TEST_ASSERT(s && s->range.start_line == 3 && s->selection.start_col == 1 && s->selection.end_col == 7);
    TEST_ASSERT(s && strcmp(s->detail, "CFLAGS := -O2 -c99") == 0);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "PREFIX");
    TEST_ASSERT(s && s->selection.start_col == 8);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_VARIABLE, "SRCS") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FUNCTION, "all") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FUNCTION, "hello") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FUNCTION, "%.o") != NULL);
    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "clean");
    TEST_ASSERT(s && s->range.start_line == 31);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FUNCTION, ".PHONY") == NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_MACRO, "banner") != NULL);
    TEST_ASSERT(r->symbol_count == 10);
    TEST_ASSERT(r->reference_count == 2);
    TEST_ASSERT(r->reference_count == 2 && strcmp(r->references[0].target, "config.mk") == 0 &&
                r->references[0].range.start_line == 28 && r->references[0].range.start_col == 9);
    TEST_ASSERT(r->reference_count == 2 && strcmp(r->references[1].target, "local.mk") == 0);
    TEST_ASSERT(token(r, 2, 1, 2, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 9, 1, 3, SCOUT_TOKEN_FUNCTION));
    TEST_ASSERT(token(r, 11, 8, 7, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 12, 2, 5, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 12, 11, 2, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 12, 14, 2, SCOUT_TOKEN_PARAMETER));
    /* a continuation line keeps its own positions */
    TEST_ASSERT(token(r, 15, 21, 2, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 16, 6, 2, SCOUT_TOKEN_PARAMETER));
    /* inside define nothing is looked at */
    TEST_ASSERT(!token(r, 25, 17, 4, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 32, 9, 7, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 34, 10, 8, SCOUT_TOKEN_FUNCTION));
    TEST_ASSERT(token(r, 34, 26, 8, SCOUT_TOKEN_FUNCTION));
    /* directives, where they are directives */
    TEST_ASSERT(token(r, 5, 1, 6, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 18, 1, 5, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 20, 1, 4, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 22, 1, 5, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 24, 1, 6, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 24, 8, 6, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 26, 1, 5, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 28, 1, 7, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 29, 1, 8, SCOUT_TOKEN_TYPE));
    make_parse_result_free(r);
}

/** @brief The diagnostics, and lines that must not give one. */
static void test_diagnostics(void)
{
    TEST_ASSERT(only_diagnostic("\techo x\nall:\n", 1, "Recipe before the first target"));
    TEST_ASSERT(only_diagnostic("all:\n    echo x\n", 2, "Recipe line indented with spaces"));
    TEST_ASSERT(only_diagnostic("A = 1\nfoo bar\n", 2, "Missing separator"));
    TEST_ASSERT(only_diagnostic("hello: hello.c\n $(CC) -o hello hello.c\n", 2, "Recipe line indented with spaces"));
    TEST_ASSERT(only_diagnostic("all:\n  $(info x)\n", 2, "Recipe line indented with spaces"));
    TEST_ASSERT(only_diagnostic("$(CC) -o hello hello.c\n", 1, "Missing separator"));
    TEST_ASSERT(only_diagnostic("endif\n", 1, "endif without if"));
    TEST_ASSERT(only_diagnostic("else\n", 1, "else without if"));
    TEST_ASSERT(only_diagnostic("ifeq (a,b)\nelse\nelse\nendif\n", 3, "else after the last else"));
    TEST_ASSERT(only_diagnostic("endef\n", 1, "endef without define"));
    TEST_ASSERT(only_diagnostic("A = 1\nifdef X\nB = 2\n", 2, "Conditional without endif"));
    TEST_ASSERT(only_diagnostic("define X\nfoo\n", 1, "define without endef"));
    TEST_ASSERT(only_diagnostic("A = $(B\n", 1, "Unterminated variable reference"));
    TEST_ASSERT(only_diagnostic("A = $(foo x)\n", 1, "Unknown function foo"));
    TEST_ASSERT(only_diagnostic("a:\n\techo 1\na:\n\techo 2\n", 4, "Target a has a recipe already"));
    TEST_ASSERT(only_diagnostic("a: ; echo 1\na: ; echo 2\n", 2, "Target a has a recipe already"));

    TEST_ASSERT(no_diagnostics("a::\n\techo 1\na::\n\techo 2\n"));
    TEST_ASSERT(no_diagnostics("a: b\na: c\n\techo\n"));
    TEST_ASSERT(no_diagnostics("ifdef X\na:\n\techo 1\nelse\na:\n\techo 2\nendif\n"));
    TEST_ASSERT(no_diagnostics("%.o: %.c\n\tvc $<\n%.o: %.s\n\tvasm $<\n.c.o:\n\tvc $<\n"));
    TEST_ASSERT(no_diagnostics("export CC CFLAGS\n.include <bsd.port.mk>\n.if defined(X)\n.endif\n"));
    TEST_ASSERT(no_diagnostics("a: CFLAGS = -g\na:\n\techo $(CFLAGS)\n"));
    TEST_ASSERT(no_diagnostics("A = 1\r\n\r\nall:\r\n\techo\r\n"));
    TEST_ASSERT(no_diagnostics("ifeq ($(A),1)\nB = 1\nelse ifeq ($(A),2)\nB = 2\nelse\nB = 3\nendif\n"));
    TEST_ASSERT(no_diagnostics("all:\n\techo 1\n# a comment\n\n\techo 2\n"));
    TEST_ASSERT(no_diagnostics("all:\n\techo 1\nifdef X\n\techo 2\nendif\n"));
    TEST_ASSERT(no_diagnostics("$(info hello)\n$(eval $(call f,x))\n"));
    TEST_ASSERT(no_diagnostics("O = $(SRCS:.c=.o) $(@D) $(subst a,b,$(X)) $$HOME\n"));
    TEST_ASSERT(no_diagnostics("A = a \\\n    b \\\n    c\nall: ; @echo $(A)\n"));
    TEST_ASSERT(no_diagnostics("X = a\\#b # comment\nvpath %.c src\nexport\nunexport X\n"));
    TEST_ASSERT(no_diagnostics("override define V =\n  text\nendef\n"));
    TEST_ASSERT(no_diagnostics("a: b | c\n\t$(CC) $< > $@\n"));
    TEST_ASSERT(no_diagnostics(""));
    TEST_ASSERT(no_diagnostics("\n\n"));
}

/** @brief A directive's word in a path is no directive. */
static void test_directive_words(void)
{
    MakeParseResult *r = analyze("include/x.h: a\n\t$(CC) -I$(D)/include\nelse ifeq (a,b)\n");
    TEST_ASSERT(token(r, 1, 1, 11, SCOUT_TOKEN_FUNCTION));
    TEST_ASSERT(!token(r, 1, 1, 7, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 2, 2, 5, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(!token(r, 2, 15, 7, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 3, 1, 4, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(token(r, 3, 6, 4, SCOUT_TOKEN_TYPE));
    make_parse_result_free(r);

    /* the same for a function's name */
    r = analyze("all:\n\tdir $(dir $@) ${sort x} $(file)\n");
    TEST_ASSERT(!token(r, 2, 2, 3, SCOUT_TOKEN_FUNCTION));
    TEST_ASSERT(token(r, 2, 8, 3, SCOUT_TOKEN_FUNCTION));
    TEST_ASSERT(token(r, 2, 12, 2, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(token(r, 2, 18, 4, SCOUT_TOKEN_FUNCTION));
    TEST_ASSERT(token(r, 2, 26, 7, SCOUT_TOKEN_PARAMETER));     /* no arguments: a variable */
    make_parse_result_free(r);
}

/** @brief The Scout protocol: describe and analyze. */
static void test_rpc(void)
{
    JsonWriter out;
    JsonValue *v;
    const JsonValue *result, *lang, *refs;
    json_writer_init_alloc(&out, 256);

    TEST_ASSERT(make_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "make") == 0);
    TEST_ASSERT(json_array_length(json_obj_get_array(lang, "filenames")) == 4);
    TEST_ASSERT(json_obj_get(json_obj_get(lang, "syntax"), "keywords") != NULL);
    /* not highlighted by the rules: the words also occur in paths */
    TEST_ASSERT(json_obj_get(json_obj_get(json_obj_get(lang, "syntax"), "keywords"), "keyword") == NULL);
    TEST_ASSERT(json_obj_get(json_obj_get(json_obj_get(lang, "syntax"), "keywords"), "directive") != NULL);
    TEST_ASSERT(json_obj_get(json_obj_get(json_obj_get(lang, "syntax"), "keywords"), "builtin") == NULL);
    TEST_ASSERT(json_obj_get(json_obj_get(json_obj_get(lang, "syntax"), "keywords"), "function") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(make_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                           "\"params\":{\"path\":\"" DATA "Makefile\",\"want\":[\"references\"]}}",
                                           &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    refs = json_obj_get_array(result, "references");
    TEST_ASSERT(json_array_length(refs) == 2);
    TEST_ASSERT(strcmp(json_obj_get_string(json_array_get(refs, 0), "kind", ""), "include") == 0);
    TEST_ASSERT(result && json_obj_get(result, "symbols") == NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(make_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                           "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(make_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                           "\"params\":{\"path\":\"" DATA "missing\"}}", &out));
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
    test_directive_words();
    test_rpc();
    printf("make_parser: %d tests, %d failures\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
