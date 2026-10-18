#ifndef TINYDEV_SCOUT_JSON_H
#define TINYDEV_SCOUT_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard JSON-RPC 2.0 Error Codes */
#define SCOUT_JSONRPC_ERR_PARSE_ERROR     -32700
#define SCOUT_JSONRPC_ERR_INVALID_REQUEST -32600
#define SCOUT_JSONRPC_ERR_METHOD_NOT_FOUND -32601
#define SCOUT_JSONRPC_ERR_INVALID_PARAMS  -32602
#define SCOUT_JSONRPC_ERR_INTERNAL_ERROR  -32603

typedef enum ScoutJsonType {
    SCOUT_JSON_NULL = 0,
    SCOUT_JSON_BOOL,
    SCOUT_JSON_NUMBER,
    SCOUT_JSON_STRING,
    SCOUT_JSON_ARRAY,
    SCOUT_JSON_OBJECT
} ScoutJsonType;

typedef struct ScoutJsonValue ScoutJsonValue;

typedef struct ScoutJsonMember {
    char *key;
    ScoutJsonValue *value;
    struct ScoutJsonMember *next;
} ScoutJsonMember;

typedef struct ScoutJsonElement {
    ScoutJsonValue *value;
    struct ScoutJsonElement *next;
} ScoutJsonElement;

struct ScoutJsonValue {
    ScoutJsonType type;
    union {
        int bool_val;
        struct {
            long int_val;
            double double_val;
            int is_float;
        } num_val;
        char *str_val;
        struct {
            ScoutJsonElement *first;
            ScoutJsonElement *last;
            size_t count;
        } array_val;
        struct {
            ScoutJsonMember *first;
            ScoutJsonMember *last;
            size_t count;
        } obj_val;
    } u;
};

/**
 * @brief Parse a JSON string into a DOM value structure.
 *
 * @param json_str NUL-terminated JSON string.
 * @param error_pos Optional pointer to receive the error location pointer on failure.
 * @return Parsed root value or NULL on parse error.
 */
ScoutJsonValue *scout_json_parse(const char *json_str, const char **error_pos);

/**
 * @brief Recursively free a JSON value DOM structure.
 *
 * @param val Pointer to root value (safe if NULL).
 */
void scout_json_value_free(ScoutJsonValue *val);

/* Accessor helpers */
const ScoutJsonValue *scout_json_obj_get(const ScoutJsonValue *obj, const char *key);
const char *scout_json_obj_get_string(const ScoutJsonValue *obj, const char *key, const char *default_val);
long scout_json_obj_get_int(const ScoutJsonValue *obj, const char *key, long default_val);
int scout_json_obj_get_bool(const ScoutJsonValue *obj, const char *key, int default_val);
const ScoutJsonValue *scout_json_obj_get_obj(const ScoutJsonValue *obj, const char *key);
const ScoutJsonValue *scout_json_obj_get_array(const ScoutJsonValue *obj, const char *key);

size_t scout_json_array_length(const ScoutJsonValue *arr);
const ScoutJsonValue *scout_json_array_get(const ScoutJsonValue *arr, size_t index);

/* JSON Writer / Serializer */
typedef struct ScoutJsonWriter {
    char *buffer;
    size_t capacity;
    size_t length;
    int is_dynamic;
    int error;
    int need_comma;
} ScoutJsonWriter;

void scout_json_writer_init(ScoutJsonWriter *w, char *buf, size_t cap);
void scout_json_writer_init_alloc(ScoutJsonWriter *w, size_t initial_cap);
void scout_json_writer_free(ScoutJsonWriter *w);
const char *scout_json_writer_get(const ScoutJsonWriter *w);
size_t scout_json_writer_length(const ScoutJsonWriter *w);

void scout_json_write_null(ScoutJsonWriter *w);
void scout_json_write_bool(ScoutJsonWriter *w, int val);
void scout_json_write_int(ScoutJsonWriter *w, long val);
void scout_json_write_string(ScoutJsonWriter *w, const char *str);
void scout_json_write_raw(ScoutJsonWriter *w, const char *raw);

void scout_json_write_obj_start(ScoutJsonWriter *w);
void scout_json_write_obj_key(ScoutJsonWriter *w, const char *key);
void scout_json_write_obj_end(ScoutJsonWriter *w);

void scout_json_write_arr_start(ScoutJsonWriter *w);
void scout_json_write_arr_end(ScoutJsonWriter *w);

/* JSON-RPC 2.0 Helpers */

typedef struct ScoutJsonRpcMessage {
    int is_request;             /* 1 for request, 0 for response */
    int is_notification;        /* 1 if request has no ID */
    long id;
    char *method;               /* copied method name for requests */
    const ScoutJsonValue *params; /* borrowed reference to params in root */
    const ScoutJsonValue *result; /* borrowed reference to result in root */
    int has_error;              /* 1 if error object is present */
    int error_code;
    char *error_msg;            /* copied error message */
    ScoutJsonValue *root;         /* root DOM value, owned by message */
} ScoutJsonRpcMessage;

int scout_json_rpc_parse(const char *json_str, ScoutJsonRpcMessage *msg);
void scout_json_rpc_free(ScoutJsonRpcMessage *msg);

int scout_json_format_request(ScoutJsonWriter *w, long id, const char *method, const char *params_json);
int scout_json_format_response(ScoutJsonWriter *w, long id, const char *result_json);
int scout_json_format_error(ScoutJsonWriter *w, long id, int error_code, const char *error_msg);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_SCOUT_JSON_H */
