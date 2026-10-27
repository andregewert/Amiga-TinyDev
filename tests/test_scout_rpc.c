/**
 * @file test_scout_rpc.c
 * @brief Host-native tests of Scout's JSON-RPC code.
 */
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/**
 * Number of checks made.
 */
static int g_test_count = 0;
/**
 * Number of checks that failed.
 */
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
 * @brief Reading and writing JSON-RPC messages.
 */
static void test_json_rpc_messages(void)
{
    /* Test Request formatting and parsing */
    JsonWriter w;
    json_writer_init_alloc(&w, 128);

    int ok = scout_rpc_format_request(&w, 1, "textDocument/documentSymbol", "{\"textDocument\":{\"uri\":\"file://test.c\"}}");
    TEST_ASSERT(ok);

    const char *req_str = json_writer_get(&w);
    ScoutRpcMessage req;
    ok = scout_rpc_parse(req_str, &req);
    TEST_ASSERT(ok);
    TEST_ASSERT(req.is_request == 1);
    TEST_ASSERT(req.id == 1);
    TEST_ASSERT(strcmp(req.method, "textDocument/documentSymbol") == 0);
    TEST_ASSERT(req.params != NULL);

    const JsonValue *td = json_obj_get_obj(req.params, "textDocument");
    TEST_ASSERT(td != NULL);
    TEST_ASSERT(strcmp(json_obj_get_string(td, "uri", ""), "file://test.c") == 0);

    scout_rpc_free(&req);
    json_writer_free(&w);

    /* Test Response formatting and parsing */
    json_writer_init_alloc(&w, 128);
    ok = scout_rpc_format_response(&w, 1, "[{\"name\":\"main\",\"kind\":12}]");
    TEST_ASSERT(ok);

    const char *resp_str = json_writer_get(&w);
    ScoutRpcMessage resp;
    ok = scout_rpc_parse(resp_str, &resp);
    TEST_ASSERT(ok);
    TEST_ASSERT(resp.is_request == 0);
    TEST_ASSERT(resp.id == 1);
    TEST_ASSERT(resp.has_error == 0);
    TEST_ASSERT(resp.result != NULL);
    TEST_ASSERT(json_array_length(resp.result) == 1);

    const JsonValue *item = json_array_get(resp.result, 0);
    TEST_ASSERT(item != NULL);
    TEST_ASSERT(strcmp(json_obj_get_string(item, "name", ""), "main") == 0);
    TEST_ASSERT(json_obj_get_int(item, "kind", 0) == 12);

    scout_rpc_free(&resp);
    json_writer_free(&w);

    /* Test Error formatting and parsing */
    json_writer_init_alloc(&w, 128);
    ok = scout_rpc_format_error(&w, 2, SCOUT_RPC_ERR_METHOD_NOT_FOUND, "Method not found");
    TEST_ASSERT(ok);

    const char *err_str = json_writer_get(&w);
    ScoutRpcMessage err_msg;
    ok = scout_rpc_parse(err_str, &err_msg);
    TEST_ASSERT(ok);
    TEST_ASSERT(err_msg.is_request == 0);
    TEST_ASSERT(err_msg.id == 2);
    TEST_ASSERT(err_msg.has_error == 1);
    TEST_ASSERT(err_msg.error_code == SCOUT_RPC_ERR_METHOD_NOT_FOUND);
    TEST_ASSERT(strcmp(err_msg.error_msg, "Method not found") == 0);

    scout_rpc_free(&err_msg);
    json_writer_free(&w);
}

/**
 * @brief Run the JSON-RPC tests.
 * @return 0 if all passed, 1 otherwise.
 */
int main(void)
{
    printf("Running Scout JSON-RPC unit tests...\n");

    test_json_rpc_messages();

    printf("Scout JSON-RPC tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
