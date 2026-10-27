/**
 * @file test_json.c
 * @brief Host-native tests of the JSON code.
 */
#include "json.h"

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
    JsonValue *v = json_parse("null", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == JSON_NULL);
    json_value_free(v);

    /* Bool true */
    v = json_parse("true", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == JSON_BOOL);
    TEST_ASSERT(v->u.bool_val == 1);
    json_value_free(v);

    /* Bool false */
    v = json_parse("false", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == JSON_BOOL);
    TEST_ASSERT(v->u.bool_val == 0);
    json_value_free(v);

    /* Integers */
    v = json_parse("12345", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.int_val == 12345);
    json_value_free(v);

    v = json_parse("-42", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.int_val == -42);
    json_value_free(v);

    /* Float */
    v = json_parse("3.1415", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == JSON_NUMBER);
    TEST_ASSERT(v->u.num_val.double_val > 3.14 && v->u.num_val.double_val < 3.15);
    json_value_free(v);

    /* String */
    v = json_parse("\"Hello Amiga!\\n\\t\\\"\"", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == JSON_STRING);
    TEST_ASSERT(strcmp(v->u.str_val, "Hello Amiga!\n\t\"") == 0);
    json_value_free(v);

    /* Unicode escape */
    v = json_parse("\"\\u0041\\u0042\"", &err);
    TEST_ASSERT(v != NULL);
    TEST_ASSERT(v->type == JSON_STRING);
    TEST_ASSERT(strcmp(v->u.str_val, "AB") == 0);
    json_value_free(v);
}

/**
 * @brief Reading objects and arrays and their accessors.
 */
static void test_json_objects_and_arrays(void)
{
    const char *err = NULL;
    const char *json = "{\"name\": \"TinyDev\", \"version\": 3, \"active\": true, \"tags\": [\"amiga\", \"c\", 68000], \"nested\": {\"key\": \"value\"}}";

    JsonValue *root = json_parse(json, &err);
    TEST_ASSERT(root != NULL);
    TEST_ASSERT(root->type == JSON_OBJECT);

    TEST_ASSERT(strcmp(json_obj_get_string(root, "name", ""), "TinyDev") == 0);
    TEST_ASSERT(json_obj_get_int(root, "version", 0) == 3);
    TEST_ASSERT(json_obj_get_bool(root, "active", 0) == 1);
    TEST_ASSERT(json_obj_get_int(root, "non_existent", 99) == 99);

    const JsonValue *tags = json_obj_get_array(root, "tags");
    TEST_ASSERT(tags != NULL);
    TEST_ASSERT(json_array_length(tags) == 3);

    const JsonValue *t0 = json_array_get(tags, 0);
    TEST_ASSERT(t0 && t0->type == JSON_STRING && strcmp(t0->u.str_val, "amiga") == 0);

    const JsonValue *t1 = json_array_get(tags, 1);
    TEST_ASSERT(t1 && t1->type == JSON_STRING && strcmp(t1->u.str_val, "c") == 0);

    const JsonValue *t2 = json_array_get(tags, 2);
    TEST_ASSERT(t2 && t2->type == JSON_NUMBER && t2->u.num_val.int_val == 68000);

    const JsonValue *nested = json_obj_get_obj(root, "nested");
    TEST_ASSERT(nested != NULL);
    TEST_ASSERT(strcmp(json_obj_get_string(nested, "key", ""), "value") == 0);

    json_value_free(root);
}

/**
 * @brief Refusing invalid JSON.
 */
static void test_json_error_handling(void)
{
    const char *err = NULL;

    /* Unterminated string */
    JsonValue *v = json_parse("{\"name\": \"unterminated}", &err);
    TEST_ASSERT(v == NULL);
    TEST_ASSERT(err != NULL);

    /* Unclosed object */
    v = json_parse("{\"name\": 123", &err);
    TEST_ASSERT(v == NULL);

    /* Unclosed array */
    v = json_parse("[1, 2, 3", &err);
    TEST_ASSERT(v == NULL);

    /* Extra token at end */
    v = json_parse("[1, 2] 3", &err);
    TEST_ASSERT(v == NULL);

    /* Trailing comma */
    v = json_parse("[1, 2,]", &err);
    TEST_ASSERT(v == NULL);

    /* NULL / Empty */
    v = json_parse("", &err);
    TEST_ASSERT(v == NULL);

    v = json_parse(NULL, &err);
    TEST_ASSERT(v == NULL);
}

/**
 * @brief Writing JSON, fixed and growing buffers.
 */
static void test_json_writer(void)
{
    JsonWriter w;
    json_writer_init_alloc(&w, 64);

    json_write_obj_start(&w);
    json_write_obj_key(&w, "command");
    json_write_string(&w, "parse");
    json_write_raw(&w, ",");
    json_write_obj_key(&w, "line");
    json_write_int(&w, 42);
    json_write_raw(&w, ",");
    json_write_obj_key(&w, "items");
    json_write_arr_start(&w);
    json_write_string(&w, "first");
    json_write_raw(&w, ",");
    json_write_string(&w, "second");
    json_write_arr_end(&w);
    json_write_obj_end(&w);

    const char *out = json_writer_get(&w);
    TEST_ASSERT(out != NULL);
    TEST_ASSERT(strcmp(out, "{\"command\":\"parse\",\"line\":42,\"items\":[\"first\",\"second\"]}") == 0);

    /* Parse back what was serialized */
    const char *err = NULL;
    JsonValue *parsed = json_parse(out, &err);
    TEST_ASSERT(parsed != NULL);
    TEST_ASSERT(strcmp(json_obj_get_string(parsed, "command", ""), "parse") == 0);
    TEST_ASSERT(json_obj_get_int(parsed, "line", 0) == 42);
    json_value_free(parsed);

    json_writer_free(&w);
}

/**
 * @brief Run the JSON tests.
 * @return 0 if all passed, 1 otherwise.
 */
int main(void)
{
    printf("Running JSON unit tests...\n");

    test_json_primitives();
    test_json_objects_and_arrays();
    test_json_error_handling();
    test_json_writer();

    printf("JSON tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
