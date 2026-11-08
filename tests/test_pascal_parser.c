/**
 * @file test_pascal_parser.c
 * @brief Host-native tests of the Free Pascal parser: the analysis (symbols,
 *        diagnostics, tokens) and the Scout protocol, with the sample in
 *        tests/data/pascal_parser/.
 */
#include "pascal_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the sample sources (tests run from the project root). */
#define DATA "tests/data/pascal_parser/"

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
static const PasSymbol *symbol(const PasResult *r, int kind, const char *name)
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
static const PasDiagnostic *diagnostic(const PasResult *r, long line, const char *text)
{
    long i;
    for (i = 0; i < r->diagnostic_count; i++) {
        const PasDiagnostic *d = &r->diagnostics[i];
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
static int has_token(const PasResult *r, long line, long col, long len, long kind)
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
static PasResult *analyze(const char *text)
{
    return pascal_parser_analyze(text, strlen(text), 0);
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
    PasResult *r = analyze(text);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        const PasDiagnostic *d = diagnostic(r, line, message);
        if (r->diagnostic_count != 1 || d == NULL || d->severity != severity)
            printf("  in: %s  got %ld, first: %s\n", text, r->diagnostic_count,
                   r->diagnostic_count ? r->diagnostics[0].message : "");
        TEST_ASSERT(r->diagnostic_count == 1);
        TEST_ASSERT(d != NULL && d->severity == severity);
    }
    pascal_result_free(r);
}

/**
 * @brief Check that a text has no diagnostics.
 * @param text The text.
 */
static void clean(const char *text)
{
    PasResult *r = analyze(text);
    TEST_ASSERT(r != NULL);
    if (r != NULL) {
        if (r->diagnostic_count) printf("  in: %s  got: %s\n", text, r->diagnostics[0].message);
        TEST_ASSERT(r->diagnostic_count == 0);
    }
    pascal_result_free(r);
}

/**
 * @brief The sample: symbols, references, tokens, no diagnostics.
 */
static void test_sample(void)
{
    size_t len;
    char *text = load(DATA "sample.pas", &len);
    PasResult *r;
    const PasSymbol *s;
    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = pascal_parser_analyze(text, len, 0);
    TEST_ASSERT(r != NULL);
    if (!r) {
        free(text);
        return;
    }
    TEST_ASSERT(r->diagnostic_count == 0);
    TEST_ASSERT(r->reference_count == 4);
    TEST_ASSERT(r->reference_count == 4 && strcmp(r->references[0].target, "parts.inc") == 0 && !r->references[0].import);
    TEST_ASSERT(r->reference_count == 4 && strcmp(r->references[1].target, "SysUtils") == 0 && r->references[1].unit);
    TEST_ASSERT(r->reference_count == 4 && strcmp(r->references[3].target, "lib/other.pas") == 0 && !r->references[3].unit);

    s = symbol(r, SCOUT_SYMBOL_MODULE, "sample");
    TEST_ASSERT(s != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_MACRO, "FEATURE") != NULL);
    s = symbol(r, SCOUT_SYMBOL_CONSTANT, "MaxItems");
    TEST_ASSERT(s && s->scope == PAS_SCOPE_GLOBAL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_CONSTANT, "Title") != NULL);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_ENUM, "TColor") != NULL);
    s = symbol(r, SCOUT_SYMBOL_ENUM_MEMBER, "green");
    TEST_ASSERT(s && s->scope == PAS_SCOPE_GLOBAL);
    s = symbol(r, SCOUT_SYMBOL_TYPE_ALIAS, "TName");
    TEST_ASSERT(s && strcmp(s->detail, "string[20]") == 0);

    s = symbol(r, SCOUT_SYMBOL_STRUCT, "TPoint");
    TEST_ASSERT(s && s->range.end_line == 26);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "y") != NULL);
    s = symbol(r, SCOUT_SYMBOL_FIELD, "note");          /* of a variant */
    TEST_ASSERT(s && strcmp(s->detail, "Word") == 0);

    s = symbol(r, SCOUT_SYMBOL_CLASS, "TBase");
    TEST_ASSERT(s && strcmp(s->detail, "(TObject)") == 0 && s->range.start_line == 28 && s->range.end_line == 37);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_FIELD, "FValue") != NULL);
    s = symbol(r, SCOUT_SYMBOL_PROPERTY, "Current");
    TEST_ASSERT(s && s->scope == PAS_SCOPE_LOCAL);
    s = symbol(r, SCOUT_SYMBOL_CONSTRUCTOR, "Create");  /* first the declaration in the class */
    TEST_ASSERT(s && s->declaration && s->scope == PAS_SCOPE_LOCAL);

    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "Twice");
    TEST_ASSERT(s && s->declaration && s->scope == PAS_SCOPE_GLOBAL && strcmp(s->detail, "(a: Integer): Integer") == 0);
    s = symbol(r, SCOUT_SYMBOL_PROCEDURE, "Log");
    TEST_ASSERT(s && s->declaration);
    TEST_ASSERT(symbol(r, SCOUT_SYMBOL_LABEL, "done") != NULL);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "Counter");
    TEST_ASSERT(s && s->scope == PAS_SCOPE_GLOBAL && strcmp(s->detail, "LongInt") == 0);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "Hidden");
    TEST_ASSERT(s && s->scope == PAS_SCOPE_FILE);

    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "local");
    TEST_ASSERT(s && s->scope == PAS_SCOPE_LOCAL && s->parent >= 0 && strcmp(r->symbols[s->parent].name, "Twice") == 0 &&
                !r->symbols[s->parent].declaration);
    s = symbol(r, SCOUT_SYMBOL_FUNCTION, "Inner");
    TEST_ASSERT(s && s->scope == PAS_SCOPE_LOCAL && strcmp(r->symbols[s->parent].name, "Twice") == 0);
    TEST_ASSERT(s && s->range.end_line == 82);
    s = symbol(r, SCOUT_SYMBOL_VARIABLE, "b");
    TEST_ASSERT(s && strcmp(r->symbols[s->parent].name, "Inner") == 0);

    TEST_ASSERT(has_token(r, 13, 3, 8, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 20, 3, 6, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 86, 14, 8, SCOUT_TOKEN_CONSTANT));     /* MaxItems */
    TEST_ASSERT(has_token(r, 85, 18, 1, SCOUT_TOKEN_PARAMETER));    /* a */
    TEST_ASSERT(has_token(r, 85, 12, 5, SCOUT_TOKEN_FUNCTION));     /* Inner */
    pascal_result_free(r);
    free(text);
}

/** Start of the programs below. */
#define HEAD "program p;\n"

/**
 * @brief What is wrong is reported in the right place.
 */
static void test_diagnostics(void)
{
    PasResult *r;
    only(HEAD "begin\n  if a then begin\n  end;\n", 2, SCOUT_SEVERITY_ERROR, "BEGIN without END");
    r = analyze(HEAD "begin\n  case a of\n  try\n");
    TEST_ASSERT(r && r->diagnostic_count == 3 && diagnostic(r, 3, "CASE without END") && diagnostic(r, 4, "TRY without END"));
    pascal_result_free(r);
    only(HEAD "begin\n  repeat\n  end.\n", 3, SCOUT_SEVERITY_ERROR, "REPEAT without UNTIL");
    only(HEAD "begin\n  until a;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "UNTIL without REPEAT");
    only(HEAD "begin\n  except\nend.\n", 3, SCOUT_SEVERITY_ERROR, "EXCEPT without TRY");
    only(HEAD "begin\n  finally\nend.\n", 3, SCOUT_SEVERITY_ERROR, "FINALLY without TRY");
    only(HEAD "end.\n", 2, SCOUT_SEVERITY_ERROR, "END without BEGIN");
    only(HEAD "begin\n  if a b;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "IF without THEN");
    only(HEAD "begin\n  while a b;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "WHILE without DO");
    only(HEAD "begin\n  for i := 1 to 2 b;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "FOR without DO");
    only(HEAD "begin\n  with a b;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "WITH without DO");
    only(HEAD "begin\n  case a\n  end;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "CASE without OF");
    only(HEAD "begin\n  a := 1\n  b := 2;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "';' expected");
    only(HEAD "begin\n  if a then b;\n  else c;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "';' before ELSE");
    only(HEAD "begin\n  a = 1;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "'=' where ':=' is meant");
    only(HEAD "begin\n  a(1;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "'(' not closed");
    only(HEAD "begin\n  a := 1);\nend.\n", 3, SCOUT_SEVERITY_ERROR, "')' without an opening bracket");
    only(HEAD "begin\n  a := (b];\nend.\n", 3, SCOUT_SEVERITY_ERROR, "']' does not match its bracket");
    only(HEAD "begin\n  a := 'b;\nend.\n", 3, SCOUT_SEVERITY_ERROR, "string not closed");
    only(HEAD "{ a\n", 2, SCOUT_SEVERITY_ERROR, "comment not closed");
    only(HEAD "(* a\n", 2, SCOUT_SEVERITY_ERROR, "comment not closed");
    only(HEAD "begin\nend;\n", 3, SCOUT_SEVERITY_ERROR, "'.' expected after the final END");
    only(HEAD "var a: integer;\n", 2, SCOUT_SEVERITY_ERROR, "the final END is missing");
    only(HEAD "procedure a;\nvar b: integer;\n", 2, SCOUT_SEVERITY_ERROR, "routine without a body");
    only(HEAD "var a integer;\nbegin\nend.\n", 2, SCOUT_SEVERITY_ERROR, "':' expected");
    only(HEAD "var a: integer\n    b: byte;\nbegin\nend.\n", 2, SCOUT_SEVERITY_ERROR, "';' expected");
    only(HEAD "const a 1;\nbegin\nend.\n", 2, SCOUT_SEVERITY_ERROR, "'=' expected");
    only(HEAD "type a integer;\nbegin\nend.\n", 2, SCOUT_SEVERITY_ERROR, "'=' expected");
    only(HEAD "type a = record\n", 2, SCOUT_SEVERITY_ERROR, "'record' without END");
    only(HEAD "procedure a(b: integer)\nbegin\nend;\nbegin\nend.\n", 2, SCOUT_SEVERITY_ERROR, "';' expected");
    only(HEAD "uses a\nbegin\nend.\n", 2, SCOUT_SEVERITY_ERROR, "';' expected");
    only(HEAD "{$ENDIF}\nbegin\nend.\n", 2, SCOUT_SEVERITY_ERROR, "$ENDIF without $IF");
    only(HEAD "{$IFDEF a}\nbegin\nend.\n", 2, SCOUT_SEVERITY_ERROR, "$IF without $ENDIF");
    only(HEAD "const a = 1;\n  A = 2;\nbegin\nend.\n", 3, SCOUT_SEVERITY_ERROR, "'A' is defined twice");
    only(HEAD "var a: byte;\ntype a = byte;\nbegin\nend.\n", 3, SCOUT_SEVERITY_ERROR, "'a' is defined twice");
    /* inside a conditional, where the compiler may skip it */
    only(HEAD "{$IFDEF a}\n  x := 'b;\n{$ENDIF}\nbegin\nend.\n", 3, SCOUT_SEVERITY_WARNING, "string not closed");
}

/**
 * @brief Correct code of all forms stays free of diagnostics.
 */
static void test_correct(void)
{
    clean(HEAD "BEGIN\nEND.\n");
    clean(HEAD "begin\nend.\nnot Pascal, and a ' quote\n");
    clean("library l;\nbegin\nend.\n");
    clean("unit u;\ninterface\nimplementation\nend.\n");
    clean("unit u;\ninterface\nimplementation\nbegin\nend.\n");
    clean("unit u;\ninterface\nprocedure a;\nimplementation\nprocedure a;\nbegin\nend;\nend.\n");
    clean("unit a.b;\ninterface\nimplementation\ninitialization\n x;\nfinalization\n y;\nend.\n");
    clean(HEAD "uses a, b.c, d in 'd.pas';\nbegin\nend.\n");
    clean("program p(input, output);\nbegin\nend.\n");
    clean(HEAD "begin\n  if a then b else c;\n  if a then b;\nend.\n");
    clean(HEAD "begin\n  if a then\n    b\n  else\n    c\nend.\n");
    clean(HEAD "begin\n  a :=\n    b\n    + c;\n  d(e,\n    f);\nend.\n");
    clean(HEAD "begin\n  case a of\n    1: b;\n  else\n    c;\n  end;\nend.\n");
    clean(HEAD "begin\n  try\n    a;\n  except\n    on b: c do d;\n  else\n    e;\n  end;\nend.\n");
    clean(HEAD "begin\n  repeat\n    begin\n    end;\n  until a;\nend.\n");
    clean(HEAD "begin\n  if a = b then c;\n  d := e = f;\nend.\n");
    clean(HEAD "begin\n  asm\n    move.l d0,d1 ; it's ( not Pascal\n  end;\nend.\n");
    clean(HEAD "procedure a; assembler; asm\n  rts\nend;\nbegin\nend.\n");
    clean(HEAD "{ a { nested } b }\n(* a (* nested *) b *)\n// a ' b\nbegin\nend.\n");
    clean(HEAD "{$MODE DELPHI}\n{ a { b }\nbegin\nend.\n");
    clean(HEAD "{$IFDEF a { b }\nbegin\nend.\n{$ENDIF}\n");
    clean(HEAD "const a = 'it''s';\n  b = #13#10;\n  c = #$0D;\n  d = $FF + %101 + &77;\n  e = 1.5e3;\nbegin\nend.\n");
    clean(HEAD "type a = 1..2;\nvar b: array[1..2] of byte;\nbegin\n  c := [1..2];\nend.\n");
    clean(HEAD "var class, try: byte;\nbegin\n  class := 1;\n  try := class;\nend.\n");
    clean(HEAD "var a: integer; external name 'b';\n  c: byte; cvar; public;\nbegin\nend.\n");
    clean(HEAD "function a: integer; forward;\nfunction a: integer;\nbegin\nend;\nbegin\nend.\n");
    clean(HEAD "procedure a; external 'b' name 'c';\nbegin\nend.\n");
    clean(HEAD "function a(b: longint location 'd0'): longint; syscall base 30;\nbegin\nend.\n");
    clean(HEAD "function a: byte; inline($5a/$eb);\nbegin\nend.\n");
    clean(HEAD "procedure a; cdecl; public name 'b';\nbegin\nend;\nbegin\nend.\n");
    clean(HEAD "procedure a; [public, alias: 'b'];\nbegin\nend;\nbegin\nend.\n");
    clean(HEAD "operator + (a, b: t) c: t;\nbegin\nend;\nbegin\nend.\n");
    clean(HEAD "type a = class;\n  a = class\n  end;\nbegin\nend.\n");
    clean(HEAD "type a = class of b;\n  c = class(d);\n  e = object(f)\n    procedure g; virtual;\n  end;\nbegin\nend.\n");
    clean(HEAD "type a = interface ['{x}']\n  procedure b;\nend;\nbegin\nend.\n");
    clean(HEAD "type generic a<t> = class\n  b: t;\nend;\nbegin\nend.\n");
    clean(HEAD "type a = function(b: byte): byte;\n  c = procedure of object;\nbegin\nend.\n");
    clean(HEAD "var a: record b: byte; end;\nbegin\nend.\n");
    clean(HEAD "const a: array[0..1] of byte = (1, 2);\n  b: t = (c: 1; d: 2);\nbegin\nend.\n");
    clean(HEAD "{$IFDEF a}\nbegin\n{$ELSE}\nbegin\n{$ENDIF}\nend.\n");
}

/**
 * @brief Where names can be seen, and the tokens of the uses.
 */
static void test_scopes_tokens(void)
{
    PasResult *r;
    const char *text = "begin\n  if a b;\nend;\nprocedure c;\nbegin\nend;\nconst d = 1;\n";

    /* an include file is a fragment: no structure, its names for all */
    r = pascal_parser_analyze(text, strlen(text), 1);
    TEST_ASSERT(r && r->diagnostic_count == 0);
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_PROCEDURE, "c") && symbol(r, SCOUT_SYMBOL_PROCEDURE, "c")->scope == PAS_SCOPE_GLOBAL);
    pascal_result_free(r);

    /* a routine only in the implementation stays in its file */
    r = analyze("unit u;\ninterface\nprocedure a;\nimplementation\nprocedure b;\nbegin\nend;\nprocedure a;\nbegin\nend;\nend.\n");
    TEST_ASSERT(r && symbol(r, SCOUT_SYMBOL_PROCEDURE, "b")->scope == PAS_SCOPE_FILE);
    TEST_ASSERT(r && r->symbol_count == 4 && r->symbols[3].scope == PAS_SCOPE_GLOBAL && !r->symbols[3].declaration);
    pascal_result_free(r);

    /* case does not matter, a use may come first, a field is not coloured */
    r = analyze(HEAD "procedure a;\nbegin\n  x := B;\n  y := c.b;\nend;\nconst b = 1;\nbegin\nend.\n");
    TEST_ASSERT(r && has_token(r, 4, 8, 1, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(r && !has_token(r, 5, 10, 1, SCOUT_TOKEN_CONSTANT));
    pascal_result_free(r);

    /* a parameter hides a constant, in its routine only */
    r = analyze(HEAD "const a = 1;\nprocedure s(a: byte);\nbegin\n  x := a;\nend;\nbegin\n  y := a;\nend.\n");
    TEST_ASSERT(r && has_token(r, 5, 8, 1, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(r && has_token(r, 8, 8, 1, SCOUT_TOKEN_CONSTANT));
    pascal_result_free(r);
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

    TEST_ASSERT(pascal_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}", &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    lang = json_array_get(json_obj_get_array(result, "languages"), 0);
    TEST_ASSERT(lang && strcmp(json_obj_get_string(lang, "id", ""), "pascal") == 0);
    TEST_ASSERT(lang && json_array_length(json_obj_get_array(lang, "extensions")) == 5);
    syntax = json_obj_get(lang, "syntax");
    TEST_ASSERT(syntax && !json_obj_get_bool(syntax, "caseSensitive", 1));
    TEST_ASSERT(syntax && json_array_length(json_obj_get_array(syntax, "blockComments")) == 2);
    kw = json_obj_get(syntax, "keywords");
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "keyword")) > 80);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "type")) > 40);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "builtin")) > 70);
    TEST_ASSERT(kw && json_array_length(json_obj_get_array(kw, "constant")) == 4);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(pascal_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\","
                                             "\"params\":{\"path\":\"" DATA "sample.pas\",\"want\":[\"symbols\",\"references\"]}}",
                                             &out));
    v = json_parse(json_writer_get(&out), NULL);
    result = json_obj_get(v, "result");
    TEST_ASSERT(result && json_obj_get_array(result, "symbols") != NULL);
    TEST_ASSERT(result && json_obj_get(result, "tokens") == NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"kind\":\"import\",\"target\":\"SysUtils.pas\"") != NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"kind\":\"import\",\"target\":\"Helper.pp\"") != NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"language\":\"pascal\",\"target\":\"parts.inc\"") != NULL);
    TEST_ASSERT(strstr(json_writer_get(&out), "\"declaration\":true") != NULL);
    json_value_free(v);

    out.length = 0;
    TEST_ASSERT(pascal_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\","
                                             "\"params\":{\"path\":\"x\",\"language\":\"c\"}}", &out));
    TEST_ASSERT(strstr(json_writer_get(&out), "\"error\"") != NULL);

    out.length = 0;
    TEST_ASSERT(pascal_parser_handle_request("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\","
                                             "\"params\":{\"path\":\"" DATA "missing.pas\"}}", &out));
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
