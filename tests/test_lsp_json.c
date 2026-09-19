#include "lsp_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_test_count = 0;
static int g_fail_count = 0;

#define TEST_ASSERT(cond) do { \
    g_test_count++; \
    if (!(cond)) { \
        printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail_count++; \
    } \
} while (0)

static void test_json_primitives(void)
{
    const char *err = NULL;

    /* Null */
    LspJsonValue *v = lsp_json_parse("null", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == LSP_JSON_NULL);
    lsp_json_value_free(v);

    /* Bool true */
    v = lsp_json_parse("true", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == LSP_JSON_BOOL);
    TEST_ASSERT(v->u.bool_val == 1);
    lsp_json_value_free(v);

    /* Bool false */
    v = lsp_json_parse("false", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == LSP_JSON_BOOL);
    TEST_ASSERT(v->u.bool_val == 0);
    lsp_json_value_free(v);

    /* Integers */
    v = lsp_json_parse("12345", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == LSP_JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.int_val == 12345);
    lsp_json_value_free(v);

    v = lsp_json_parse("-42", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == LSP_JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.int_val == -42);
    lsp_json_value_free(v);

    /* Float */
    v = lsp_json_parse("3.1415", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == LSP_JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.double_val > 3.14 && v->u.num_val.double_val < 3.15);
    lsp_json_value_free(v);

    /* String */
    v = lsp_json_parse("\"Hello Amiga!\\n\\t\\\"\"", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == LSP_JSON_STRING);
    TEST_ASSERT(strcmp(v->u.str_val, "Hello Amiga!\n\t\"") == 0);
    lsp_json_value_free(v);

    /* Unicode escape */
    v = lsp_json_parse("\"\\u0041\\u0042\"", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == LSP_JSON_STRING);
    TEST_ASSERT(strcmp(v->u.str_val, "AB") == 0);
    lsp_json_value_free(v);
}

static void test_json_objects_and_arrays(void)
{
    const char *err = NULL;
    const char *json = "{\"name\": \"TinyDev\", \"version\": 3, \"active\": true, \"tags\": [\"amiga\", \"c\", 68000], \"nested\": {\"key\": \"value\"}}";

    LspJsonValue *root = lsp_json_parse(json, &err);
    TEST_ASSERT(root != NULL);
    TEST_ASSERT(root->type == LSP_JSON_OBJECT);

    TEST_ASSERT(strcmp(lsp_json_obj_get_string(root, "name", ""), "TinyDev") == 0);
    TEST_ASSERT(lsp_json_obj_get_int(root, "version", 0) == 3);
    TEST_ASSERT(lsp_json_obj_get_bool(root, "active", 0) == 1);
    TEST_ASSERT(lsp_json_obj_get_int(root, "non_existent", 99) == 99);

    const LspJsonValue *tags = lsp_json_obj_get_array(root, "tags");
    TEST_ASSERT(tags != NULL);
    TEST_ASSERT(lsp_json_array_length(tags) == 3);

    const LspJsonValue *t0 = lsp_json_array_get(tags, 0);
    TEST_ASSERT(t0 && t0->type == LSP_JSON_STRING && strcmp(t0->u.str_val, "amiga") == 0);

    const LspJsonValue *t1 = lsp_json_array_get(tags, 1);
    TEST_ASSERT(t1 && t1->type == LSP_JSON_STRING && strcmp(t1->u.str_val, "c") == 0);

    const LspJsonValue *t2 = lsp_json_array_get(tags, 2);
    TEST_ASSERT(t2 && t2->type == LSP_JSON_NUMBER && t2->u.num_val.int_val == 68000);

    const LspJsonValue *nested = lsp_json_obj_get_obj(root, "nested");
    TEST_ASSERT(nested != NULL);
    TEST_ASSERT(strcmp(lsp_json_obj_get_string(nested, "key", ""), "value") == 0);

    lsp_json_value_free(root);
}

static void test_json_error_handling(void)
{
    const char *err = NULL;

    /* Unterminated string */
    LspJsonValue *v = lsp_json_parse("{\"name\": \"unterminated}", &err);
    TEST_ASSERT(v == NULL);
    TEST_ASSERT(err != NULL);

    /* Unclosed object */
    v = lsp_json_parse("{\"name\": 123", &err);
    TEST_ASSERT(v == NULL);

    /* Unclosed array */
    v = lsp_json_parse("[1, 2, 3", &err);
    TEST_ASSERT(v == NULL);

    /* Extra token at end */
    v = lsp_json_parse("[1, 2] 3", &err);
    TEST_ASSERT(v == NULL);

    /* Trailing comma */
    v = lsp_json_parse("[1, 2,]", &err);
    TEST_ASSERT(v == NULL);

    /* NULL / Empty */
    v = lsp_json_parse("", &err);
    TEST_ASSERT(v == NULL);

    v = lsp_json_parse(NULL, &err);
    TEST_ASSERT(v == NULL);
}

static void test_json_writer(void)
{
    LspJsonWriter w;
    lsp_json_writer_init_alloc(&w, 64);

    lsp_json_write_obj_start(&w);
    lsp_json_write_obj_key(&w, "command");
    lsp_json_write_string(&w, "parse");
    lsp_json_write_raw(&w, ",");
    lsp_json_write_obj_key(&w, "line");
    lsp_json_write_int(&w, 42);
    lsp_json_write_raw(&w, ",");
    lsp_json_write_obj_key(&w, "items");
    lsp_json_write_arr_start(&w);
    lsp_json_write_string(&w, "first");
    lsp_json_write_raw(&w, ",");
    lsp_json_write_string(&w, "second");
    lsp_json_write_arr_end(&w);
    lsp_json_write_obj_end(&w);

    const char *out = lsp_json_writer_get(&w);
    TEST_ASSERT(out != NULL);
    TEST_ASSERT(strcmp(out, "{\"command\":\"parse\",\"line\":42,\"items\":[\"first\",\"second\"]}") == 0);

    /* Parse back what was serialized */
    const char *err = NULL;
    LspJsonValue *parsed = lsp_json_parse(out, &err);
    TEST_ASSERT(parsed != NULL);
    TEST_ASSERT(strcmp(lsp_json_obj_get_string(parsed, "command", ""), "parse") == 0);
    TEST_ASSERT(lsp_json_obj_get_int(parsed, "line", 0) == 42);
    lsp_json_value_free(parsed);

    lsp_json_writer_free(&w);
}

static void test_json_rpc_messages(void)
{
    /* Test Request formatting and parsing */
    LspJsonWriter w;
    lsp_json_writer_init_alloc(&w, 128);

    int ok = lsp_json_format_request(&w, 1, "textDocument/documentSymbol", "{\"textDocument\":{\"uri\":\"file://test.c\"}}");
    TEST_ASSERT(ok);

    const char *req_str = lsp_json_writer_get(&w);
    LspJsonRpcMessage req;
    ok = lsp_json_rpc_parse(req_str, &req);
    TEST_ASSERT(ok);
    TEST_ASSERT(req.is_request == 1);
    TEST_ASSERT(req.id == 1);
    TEST_ASSERT(strcmp(req.method, "textDocument/documentSymbol") == 0);
    TEST_ASSERT(req.params != NULL);

    const LspJsonValue *td = lsp_json_obj_get_obj(req.params, "textDocument");
    TEST_ASSERT(td != NULL);
    TEST_ASSERT(strcmp(lsp_json_obj_get_string(td, "uri", ""), "file://test.c") == 0);

    lsp_json_rpc_free(&req);
    lsp_json_writer_free(&w);

    /* Test Response formatting and parsing */
    lsp_json_writer_init_alloc(&w, 128);
    ok = lsp_json_format_response(&w, 1, "[{\"name\":\"main\",\"kind\":12}]");
    TEST_ASSERT(ok);

    const char *resp_str = lsp_json_writer_get(&w);
    LspJsonRpcMessage resp;
    ok = lsp_json_rpc_parse(resp_str, &resp);
    TEST_ASSERT(ok);
    TEST_ASSERT(resp.is_request == 0);
    TEST_ASSERT(resp.id == 1);
    TEST_ASSERT(resp.has_error == 0);
    TEST_ASSERT(resp.result != NULL);
    TEST_ASSERT(lsp_json_array_length(resp.result) == 1);

    const LspJsonValue *item = lsp_json_array_get(resp.result, 0);
    TEST_ASSERT(item != NULL);
    TEST_ASSERT(strcmp(lsp_json_obj_get_string(item, "name", ""), "main") == 0);
    TEST_ASSERT(lsp_json_obj_get_int(item, "kind", 0) == 12);

    lsp_json_rpc_free(&resp);
    lsp_json_writer_free(&w);

    /* Test Error formatting and parsing */
    lsp_json_writer_init_alloc(&w, 128);
    ok = lsp_json_format_error(&w, 2, LSP_JSONRPC_ERR_METHOD_NOT_FOUND, "Method not found");
    TEST_ASSERT(ok);

    const char *err_str = lsp_json_writer_get(&w);
    LspJsonRpcMessage err_msg;
    ok = lsp_json_rpc_parse(err_str, &err_msg);
    TEST_ASSERT(ok);
    TEST_ASSERT(err_msg.is_request == 0);
    TEST_ASSERT(err_msg.id == 2);
    TEST_ASSERT(err_msg.has_error == 1);
    TEST_ASSERT(err_msg.error_code == LSP_JSONRPC_ERR_METHOD_NOT_FOUND);
    TEST_ASSERT(strcmp(err_msg.error_msg, "Method not found") == 0);

    lsp_json_rpc_free(&err_msg);
    lsp_json_writer_free(&w);
}

int main(void)
{
    printf("Running LSP JSON and JSON-RPC unit tests...\n");

    test_json_primitives();
    test_json_objects_and_arrays();
    test_json_error_handling();
    test_json_writer();
    test_json_rpc_messages();

    printf("LSP JSON tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
