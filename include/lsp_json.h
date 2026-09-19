#ifndef TINYDEV_LSP_JSON_H
#define TINYDEV_LSP_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard JSON-RPC 2.0 Error Codes */
#define LSP_JSONRPC_ERR_PARSE_ERROR     -32700
#define LSP_JSONRPC_ERR_INVALID_REQUEST -32600
#define LSP_JSONRPC_ERR_METHOD_NOT_FOUND -32601
#define LSP_JSONRPC_ERR_INVALID_PARAMS  -32602
#define LSP_JSONRPC_ERR_INTERNAL_ERROR  -32603

typedef enum LspJsonType {
    LSP_JSON_NULL = 0,
    LSP_JSON_BOOL,
    LSP_JSON_NUMBER,
    LSP_JSON_STRING,
    LSP_JSON_ARRAY,
    LSP_JSON_OBJECT
} LspJsonType;

typedef struct LspJsonValue LspJsonValue;

typedef struct LspJsonMember {
    char *key;
    LspJsonValue *value;
    struct LspJsonMember *next;
} LspJsonMember;

typedef struct LspJsonElement {
    LspJsonValue *value;
    struct LspJsonElement *next;
} LspJsonElement;

struct LspJsonValue {
    LspJsonType type;
    union {
        int bool_val;
        struct {
            long int_val;
            double double_val;
            int is_float;
        } num_val;
        char *str_val;
        struct {
            LspJsonElement *first;
            LspJsonElement *last;
            size_t count;
        } array_val;
        struct {
            LspJsonMember *first;
            LspJsonMember *last;
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
LspJsonValue *lsp_json_parse(const char *json_str, const char **error_pos);

/**
 * @brief Recursively free a JSON value DOM structure.
 *
 * @param val Pointer to root value (safe if NULL).
 */
void lsp_json_value_free(LspJsonValue *val);

/* Accessor helpers */
const LspJsonValue *lsp_json_obj_get(const LspJsonValue *obj, const char *key);
const char *lsp_json_obj_get_string(const LspJsonValue *obj, const char *key, const char *default_val);
long lsp_json_obj_get_int(const LspJsonValue *obj, const char *key, long default_val);
int lsp_json_obj_get_bool(const LspJsonValue *obj, const char *key, int default_val);
const LspJsonValue *lsp_json_obj_get_obj(const LspJsonValue *obj, const char *key);
const LspJsonValue *lsp_json_obj_get_array(const LspJsonValue *obj, const char *key);

size_t lsp_json_array_length(const LspJsonValue *arr);
const LspJsonValue *lsp_json_array_get(const LspJsonValue *arr, size_t index);

/* JSON Writer / Serializer */
typedef struct LspJsonWriter {
    char *buffer;
    size_t capacity;
    size_t length;
    int is_dynamic;
    int error;
    int need_comma;
} LspJsonWriter;

void lsp_json_writer_init(LspJsonWriter *w, char *buf, size_t cap);
void lsp_json_writer_init_alloc(LspJsonWriter *w, size_t initial_cap);
void lsp_json_writer_free(LspJsonWriter *w);
const char *lsp_json_writer_get(const LspJsonWriter *w);
size_t lsp_json_writer_length(const LspJsonWriter *w);

void lsp_json_write_null(LspJsonWriter *w);
void lsp_json_write_bool(LspJsonWriter *w, int val);
void lsp_json_write_int(LspJsonWriter *w, long val);
void lsp_json_write_string(LspJsonWriter *w, const char *str);
void lsp_json_write_raw(LspJsonWriter *w, const char *raw);

void lsp_json_write_obj_start(LspJsonWriter *w);
void lsp_json_write_obj_key(LspJsonWriter *w, const char *key);
void lsp_json_write_obj_end(LspJsonWriter *w);

void lsp_json_write_arr_start(LspJsonWriter *w);
void lsp_json_write_arr_end(LspJsonWriter *w);

/* JSON-RPC 2.0 Helpers */

typedef struct LspJsonRpcMessage {
    int is_request;             /* 1 for request, 0 for response */
    int is_notification;        /* 1 if request has no ID */
    long id;
    char *method;               /* copied method name for requests */
    const LspJsonValue *params; /* borrowed reference to params in root */
    const LspJsonValue *result; /* borrowed reference to result in root */
    int has_error;              /* 1 if error object is present */
    int error_code;
    char *error_msg;            /* copied error message */
    LspJsonValue *root;         /* root DOM value, owned by message */
} LspJsonRpcMessage;

int lsp_json_rpc_parse(const char *json_str, LspJsonRpcMessage *msg);
void lsp_json_rpc_free(LspJsonRpcMessage *msg);

int lsp_json_format_request(LspJsonWriter *w, long id, const char *method, const char *params_json);
int lsp_json_format_response(LspJsonWriter *w, long id, const char *result_json);
int lsp_json_format_error(LspJsonWriter *w, long id, int error_code, const char *error_msg);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_LSP_JSON_H */
