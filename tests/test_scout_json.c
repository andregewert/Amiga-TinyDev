/**
 * @file test_scout_json.c
 * @brief Host-native tests of Scout's JSON and JSON-RPC code.
 */
#include "scout_json.h"

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
 * @brief Reading null, truth values, numbers and strings.
 */
static void test_json_primitives(void)
{
    const char *err = NULL;

    /* Null */
    ScoutJsonValue *v = scout_json_parse("null", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == SCOUT_JSON_NULL);
    scout_json_value_free(v);

    /* Bool true */
    v = scout_json_parse("true", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == SCOUT_JSON_BOOL);
    TEST_ASSERT(v->u.bool_val == 1);
    scout_json_value_free(v);

    /* Bool false */
    v = scout_json_parse("false", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == SCOUT_JSON_BOOL);
    TEST_ASSERT(v->u.bool_val == 0);
    scout_json_value_free(v);

    /* Integers */
    v = scout_json_parse("12345", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == SCOUT_JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.int_val == 12345);
    scout_json_value_free(v);

    v = scout_json_parse("-42", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == SCOUT_JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.int_val == -42);
    scout_json_value_free(v);

    /* Float */
    v = scout_json_parse("3.1415", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == SCOUT_JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.double_val > 3.14 && v->u.num_val.double_val < 3.15);
    scout_json_value_free(v);

    /* String */
    v = scout_json_parse("\"Hello Amiga!\\n\\t\\\"\"", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == SCOUT_JSON_STRING);
    TEST_ASSERT(strcmp(v->u.str_val, "Hello Amiga!\n\t\"") == 0);
    scout_json_value_free(v);

    /* Unicode escape */
    v = scout_json_parse("\"\\u0041\\u0042\"", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == SCOUT_JSON_STRING);
    TEST_ASSERT(strcmp(v->u.str_val, "AB") == 0);
    scout_json_value_free(v);
}

/**
 * @brief Reading objects and arrays and their accessors.
 */
static void test_json_objects_and_arrays(void)
{
    const char *err = NULL;
    const char *json = "{\"name\": \"TinyDev\", \"version\": 3, \"active\": true, \"tags\": [\"amiga\", \"c\", 68000], \"nested\": {\"key\": \"value\"}}";

    ScoutJsonValue *root = scout_json_parse(json, &err);
    TEST_ASSERT(root != NULL);
    TEST_ASSERT(root->type == SCOUT_JSON_OBJECT);

    TEST_ASSERT(strcmp(scout_json_obj_get_string(root, "name", ""), "TinyDev") == 0);
    TEST_ASSERT(scout_json_obj_get_int(root, "version", 0) == 3);
    TEST_ASSERT(scout_json_obj_get_bool(root, "active", 0) == 1);
    TEST_ASSERT(scout_json_obj_get_int(root, "non_existent", 99) == 99);

    const ScoutJsonValue *tags = scout_json_obj_get_array(root, "tags");
    TEST_ASSERT(tags != NULL);
    TEST_ASSERT(scout_json_array_length(tags) == 3);

    const ScoutJsonValue *t0 = scout_json_array_get(tags, 0);
    TEST_ASSERT(t0 && t0->type == SCOUT_JSON_STRING && strcmp(t0->u.str_val, "amiga") == 0);

    const ScoutJsonValue *t1 = scout_json_array_get(tags, 1);
    TEST_ASSERT(t1 && t1->type == SCOUT_JSON_STRING && strcmp(t1->u.str_val, "c") == 0);

    const ScoutJsonValue *t2 = scout_json_array_get(tags, 2);
    TEST_ASSERT(t2 && t2->type == SCOUT_JSON_NUMBER && t2->u.num_val.int_val == 68000);

    const ScoutJsonValue *nested = scout_json_obj_get_obj(root, "nested");
    TEST_ASSERT(nested != NULL);
    TEST_ASSERT(strcmp(scout_json_obj_get_string(nested, "key", ""), "value") == 0);

    scout_json_value_free(root);
}

/**
 * @brief Refusing invalid JSON.
 */
static void test_json_error_handling(void)
{
    const char *err = NULL;

    /* Unterminated string */
    ScoutJsonValue *v = scout_json_parse("{\"name\": \"unterminated}", &err);
    TEST_ASSERT(v == NULL);
    TEST_ASSERT(err != NULL);

    /* Unclosed object */
    v = scout_json_parse("{\"name\": 123", &err);
    TEST_ASSERT(v == NULL);

    /* Unclosed array */
    v = scout_json_parse("[1, 2, 3", &err);
    TEST_ASSERT(v == NULL);

    /* Extra token at end */
    v = scout_json_parse("[1, 2] 3", &err);
    TEST_ASSERT(v == NULL);

    /* Trailing comma */
    v = scout_json_parse("[1, 2,]", &err);
    TEST_ASSERT(v == NULL);

    /* NULL / Empty */
    v = scout_json_parse("", &err);
    TEST_ASSERT(v == NULL);

    v = scout_json_parse(NULL, &err);
    TEST_ASSERT(v == NULL);
}

/**
 * @brief Writing JSON, fixed and growing buffers.
 */
static void test_json_writer(void)
{
    ScoutJsonWriter w;
    scout_json_writer_init_alloc(&w, 64);

    scout_json_write_obj_start(&w);
    scout_json_write_obj_key(&w, "command");
    scout_json_write_string(&w, "parse");
    scout_json_write_raw(&w, ",");
    scout_json_write_obj_key(&w, "line");
    scout_json_write_int(&w, 42);
    scout_json_write_raw(&w, ",");
    scout_json_write_obj_key(&w, "items");
    scout_json_write_arr_start(&w);
    scout_json_write_string(&w, "first");
    scout_json_write_raw(&w, ",");
    scout_json_write_string(&w, "second");
    scout_json_write_arr_end(&w);
    scout_json_write_obj_end(&w);

    const char *out = scout_json_writer_get(&w);
    TEST_ASSERT(out != NULL);
    TEST_ASSERT(strcmp(out, "{\"command\":\"parse\",\"line\":42,\"items\":[\"first\",\"second\"]}") == 0);

    /* Parse back what was serialized */
    const char *err = NULL;
    ScoutJsonValue *parsed = scout_json_parse(out, &err);
    TEST_ASSERT(parsed != NULL);
    TEST_ASSERT(strcmp(scout_json_obj_get_string(parsed, "command", ""), "parse") == 0);
    TEST_ASSERT(scout_json_obj_get_int(parsed, "line", 0) == 42);
    scout_json_value_free(parsed);

    scout_json_writer_free(&w);
}

/**
 * @brief Reading and writing JSON-RPC messages.
 */
static void test_json_rpc_messages(void)
{
    /* Test Request formatting and parsing */
    ScoutJsonWriter w;
    scout_json_writer_init_alloc(&w, 128);

    int ok = scout_json_format_request(&w, 1, "textDocument/documentSymbol", "{\"textDocument\":{\"uri\":\"file://test.c\"}}");
    TEST_ASSERT(ok);

    const char *req_str = scout_json_writer_get(&w);
    ScoutJsonRpcMessage req;
    ok = scout_json_rpc_parse(req_str, &req);
    TEST_ASSERT(ok);
    TEST_ASSERT(req.is_request == 1);
    TEST_ASSERT(req.id == 1);
    TEST_ASSERT(strcmp(req.method, "textDocument/documentSymbol") == 0);
    TEST_ASSERT(req.params != NULL);

    const ScoutJsonValue *td = scout_json_obj_get_obj(req.params, "textDocument");
    TEST_ASSERT(td != NULL);
    TEST_ASSERT(strcmp(scout_json_obj_get_string(td, "uri", ""), "file://test.c") == 0);

    scout_json_rpc_free(&req);
    scout_json_writer_free(&w);

    /* Test Response formatting and parsing */
    scout_json_writer_init_alloc(&w, 128);
    ok = scout_json_format_response(&w, 1, "[{\"name\":\"main\",\"kind\":12}]");
    TEST_ASSERT(ok);

    const char *resp_str = scout_json_writer_get(&w);
    ScoutJsonRpcMessage resp;
    ok = scout_json_rpc_parse(resp_str, &resp);
    TEST_ASSERT(ok);
    TEST_ASSERT(resp.is_request == 0);
    TEST_ASSERT(resp.id == 1);
    TEST_ASSERT(resp.has_error == 0);
    TEST_ASSERT(resp.result != NULL);
    TEST_ASSERT(scout_json_array_length(resp.result) == 1);

    const ScoutJsonValue *item = scout_json_array_get(resp.result, 0);
    TEST_ASSERT(item != NULL);
    TEST_ASSERT(strcmp(scout_json_obj_get_string(item, "name", ""), "main") == 0);
    TEST_ASSERT(scout_json_obj_get_int(item, "kind", 0) == 12);

    scout_json_rpc_free(&resp);
    scout_json_writer_free(&w);

    /* Test Error formatting and parsing */
    scout_json_writer_init_alloc(&w, 128);
    ok = scout_json_format_error(&w, 2, SCOUT_JSONRPC_ERR_METHOD_NOT_FOUND, "Method not found");
    TEST_ASSERT(ok);

    const char *err_str = scout_json_writer_get(&w);
    ScoutJsonRpcMessage err_msg;
    ok = scout_json_rpc_parse(err_str, &err_msg);
    TEST_ASSERT(ok);
    TEST_ASSERT(err_msg.is_request == 0);
    TEST_ASSERT(err_msg.id == 2);
    TEST_ASSERT(err_msg.has_error == 1);
    TEST_ASSERT(err_msg.error_code == SCOUT_JSONRPC_ERR_METHOD_NOT_FOUND);
    TEST_ASSERT(strcmp(err_msg.error_msg, "Method not found") == 0);

    scout_json_rpc_free(&err_msg);
    scout_json_writer_free(&w);
}

/**
 * @brief Run the JSON tests.
 * @return 0 if all passed, 1 otherwise.
 */
int main(void)
{
    printf("Running Scout JSON and JSON-RPC unit tests...\n");

    test_json_primitives();
    test_json_objects_and_arrays();
    test_json_error_handling();
    test_json_writer();
    test_json_rpc_messages();

    printf("Scout JSON tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
