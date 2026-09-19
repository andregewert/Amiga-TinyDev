#include "c_parser.h"
#include "lsp_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_test_count = 0;
static int g_fail_count = 0;

#define TEST_ASSERT(cond) do { \
    g_test_count++; \
    if (!(cond)) { \
        printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail_count++; \
    } \
} while (0)

static void test_symbol_extraction(void)
{
    const char *c_src =
        "#define MAX_BUFFER 1024\n"
        "#define MIN(a,b) ((a)<(b)?(a):(b))\n"
        "\n"
        "struct Point {\n"
        "    int x;\n"
        "    int y;\n"
        "};\n"
        "\n"
        "typedef struct Point Point_t;\n"
        "\n"
        "enum Color {\n"
        "    RED, GREEN, BLUE\n"
        "};\n"
        "\n"
        "int add_numbers(int a, int b)\n"
        "{\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "void main_entry(void)\n"
        "{\n"
        "    Point_t p;\n"
        "}\n";

    CParseResult *res = c_parser_analyze(c_src, "file://test.c");
    TEST_ASSERT(res != NULL);
    TEST_ASSERT(res->symbol_count >= 6);

    /* Check symbols */
    const CSymbol *s_max = NULL;
    const CSymbol *s_point = NULL;
    const CSymbol *s_point_t = NULL;
    const CSymbol *s_color = NULL;
    const CSymbol *s_add = NULL;
    const CSymbol *s_main = NULL;

    for (const CSymbol *s = res->symbols; s; s = s->next) {
        if (strcmp(s->name, "MAX_BUFFER") == 0) s_max = s;
        else if (strcmp(s->name, "Point") == 0) s_point = s;
        else if (strcmp(s->name, "Point_t") == 0) s_point_t = s;
        else if (strcmp(s->name, "Color") == 0) s_color = s;
        else if (strcmp(s->name, "add_numbers") == 0) s_add = s;
        else if (strcmp(s->name, "main_entry") == 0) s_main = s;
    }

    TEST_ASSERT(s_max != NULL && s_max->kind == LSP_SYMBOL_KIND_CONSTANT && s_max->start_line == 1);
    TEST_ASSERT(s_point != NULL && s_point->kind == LSP_SYMBOL_KIND_STRUCT && s_point->start_line == 4);
    TEST_ASSERT(s_point_t != NULL && s_point_t->kind == LSP_SYMBOL_KIND_STRUCT && s_point_t->start_line == 9);
    TEST_ASSERT(s_color != NULL && s_color->kind == LSP_SYMBOL_KIND_ENUM && s_color->start_line == 11);
    TEST_ASSERT(s_add != NULL && s_add->kind == LSP_SYMBOL_KIND_FUNCTION && s_add->start_line == 15);
    TEST_ASSERT(s_main != NULL && s_main->kind == LSP_SYMBOL_KIND_FUNCTION && s_main->start_line == 20);

    c_parse_result_free(res);
}

static void test_diagnostics_detection(void)
{
    /* Test unclosed comment */
    const char *src_unclosed_comment = "int a = 10;\n/* This is an unclosed comment\n";
    CParseResult *res1 = c_parser_analyze(src_unclosed_comment, "file://err1.c");
    TEST_ASSERT(res1 != NULL);
    TEST_ASSERT(res1->diagnostic_count == 1);
    TEST_ASSERT(res1->diagnostics != NULL && strstr(res1->diagnostics->message, "comment") != NULL);
    c_parse_result_free(res1);

    /* Test unterminated string */
    const char *src_unclosed_str = "char *s = \"hello world;\nint b = 20;\n";
    CParseResult *res2 = c_parser_analyze(src_unclosed_str, "file://err2.c");
    TEST_ASSERT(res2 != NULL);
    TEST_ASSERT(res2->diagnostic_count >= 1);
    TEST_ASSERT(res2->diagnostics != NULL && strstr(res2->diagnostics->message, "string") != NULL);
    c_parse_result_free(res2);

    /* Test mismatched brace */
    const char *src_mismatched = "void foo(void) {\n    int x = (1 + 2];\n}\n";
    CParseResult *res3 = c_parser_analyze(src_mismatched, "file://err3.c");
    TEST_ASSERT(res3 != NULL);
    TEST_ASSERT(res3->diagnostic_count >= 1);
    c_parse_result_free(res3);
}

static void test_definition_and_completion(void)
{
    const char *c_src =
        "int calculate_total(int count);\n"
        "int calculate_total(int count) { return count * 2; }\n"
        "void run_process(void) {}\n";

    CParseResult *res = c_parser_analyze(c_src, "file://calc.c");
    TEST_ASSERT(res != NULL);

    /* Go to definition */
    const CSymbol *def = NULL;
    int ok = c_parser_find_definition(res, "calculate_total", 0, 0, &def);
    TEST_ASSERT(ok && def != NULL);
    TEST_ASSERT(def->start_line == 1 || def->start_line == 2);

    /* Completions */
    CCompletion *comps = NULL;
    size_t count = c_parser_get_completions(res, "calc", &comps);
    TEST_ASSERT(count >= 1);
    TEST_ASSERT(comps != NULL && strcmp(comps->label, "calculate_total") == 0);
    c_completions_free(comps);

    /* Keyword completion */
    comps = NULL;
    count = c_parser_get_completions(res, "stru", &comps);
    TEST_ASSERT(count >= 1);
    TEST_ASSERT(comps != NULL && strcmp(comps->label, "struct") == 0);
    c_completions_free(comps);

    c_parse_result_free(res);
}

static void test_jsonrpc_protocol(void)
{
    LspJsonWriter resp;
    lsp_json_writer_init_alloc(&resp, 512);

    /* Test documentSymbol request with inline text */
    const char *req_sym =
        "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"textDocument/documentSymbol\","
        "\"params\":{\"textDocument\":{\"uri\":\"file://sample.c\",\"text\":\"int sample_func(void) { return 0; }\"}}}";

    int ok = c_parser_process_jsonrpc(req_sym, &resp);
    TEST_ASSERT(ok);

    const char *out_json = lsp_json_writer_get(&resp);
    TEST_ASSERT(out_json != NULL);

    LspJsonRpcMessage msg;
    ok = lsp_json_rpc_parse(out_json, &msg);
    TEST_ASSERT(ok);
    TEST_ASSERT(msg.id == 10);
    TEST_ASSERT(msg.result != NULL);
    TEST_ASSERT(lsp_json_array_length(msg.result) >= 1);

    const LspJsonValue *sym0 = lsp_json_array_get(msg.result, 0);
    TEST_ASSERT(sym0 != NULL);
    TEST_ASSERT(strcmp(lsp_json_obj_get_string(sym0, "name", ""), "sample_func") == 0);
    TEST_ASSERT(lsp_json_obj_get_int(sym0, "kind", 0) == LSP_SYMBOL_KIND_FUNCTION);

    lsp_json_rpc_free(&msg);

    /* Test publishDiagnostics request */
    resp.length = 0;
    resp.buffer[0] = '\0';
    const char *req_diag =
        "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/publishDiagnostics\","
        "\"params\":{\"textDocument\":{\"uri\":\"file://bad.c\",\"text\":\"/* unterminated comment\"}}}";

    ok = c_parser_process_jsonrpc(req_diag, &resp);
    TEST_ASSERT(ok);

    out_json = lsp_json_writer_get(&resp);
    ok = lsp_json_rpc_parse(out_json, &msg);
    TEST_ASSERT(ok);
    TEST_ASSERT(msg.id == 11);
    TEST_ASSERT(msg.result != NULL);
    TEST_ASSERT(lsp_json_array_length(msg.result) == 1);

    const LspJsonValue *diag0 = lsp_json_array_get(msg.result, 0);
    TEST_ASSERT(diag0 != NULL);
    TEST_ASSERT(strstr(lsp_json_obj_get_string(diag0, "message", ""), "comment") != NULL);

    lsp_json_rpc_free(&msg);

    /* Test completion request */
    resp.length = 0;
    resp.buffer[0] = '\0';
    const char *req_comp =
        "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/completion\","
        "\"params\":{\"textDocument\":{\"uri\":\"file://test.c\",\"text\":\"void my_handler(void);\"},"
        "\"context\":{\"prefix\":\"my_\"}}}";

    ok = c_parser_process_jsonrpc(req_comp, &resp);
    TEST_ASSERT(ok);

    out_json = lsp_json_writer_get(&resp);
    ok = lsp_json_rpc_parse(out_json, &msg);
    TEST_ASSERT(ok);
    TEST_ASSERT(msg.id == 12);
    TEST_ASSERT(msg.result != NULL);
    TEST_ASSERT(lsp_json_array_length(msg.result) >= 1);

    const LspJsonValue *comp0 = lsp_json_array_get(msg.result, 0);
    TEST_ASSERT(comp0 != NULL);
    TEST_ASSERT(strcmp(lsp_json_obj_get_string(comp0, "label", ""), "my_handler") == 0);

    lsp_json_rpc_free(&msg);
    lsp_json_writer_free(&resp);
}

int main(void)
{
    printf("Running C Language Parser unit tests...\n");

    test_symbol_extraction();
    test_diagnostics_detection();
    test_definition_and_completion();
    test_jsonrpc_protocol();

    printf("C Parser tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
