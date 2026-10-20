/**
 * @file scout_json.h
 * @brief JSON values, reader and writer, and the JSON-RPC 2.0 helpers of the Scout protocol.
 */
#ifndef TINYDEV_SCOUT_JSON_H
#define TINYDEV_SCOUT_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name JSON-RPC 2.0 error codes
 */
/** @{ */
#define SCOUT_JSONRPC_ERR_PARSE_ERROR     -32700    /**< the request is not valid JSON */
#define SCOUT_JSONRPC_ERR_INVALID_REQUEST -32600    /**< not a valid JSON-RPC request */
#define SCOUT_JSONRPC_ERR_METHOD_NOT_FOUND -32601   /**< unknown method */
#define SCOUT_JSONRPC_ERR_INVALID_PARAMS  -32602    /**< wrong parameters */
#define SCOUT_JSONRPC_ERR_INTERNAL_ERROR  -32603    /**< error inside the server */
/** @} */

/** Type of a JSON value. */
typedef enum ScoutJsonType {
    SCOUT_JSON_NULL = 0,    /**< null */
    SCOUT_JSON_BOOL,        /**< true or false (ScoutJsonValue::u bool_val) */
    SCOUT_JSON_NUMBER,      /**< a number (num_val) */
    SCOUT_JSON_STRING,      /**< a string (str_val) */
    SCOUT_JSON_ARRAY,       /**< an array (array_val) */
    SCOUT_JSON_OBJECT       /**< an object (obj_val) */
} ScoutJsonType;

/** A JSON value (a node of the tree scout_json_parse() builds). */
typedef struct ScoutJsonValue ScoutJsonValue;

/** A member of an object: key and value (a node of a singly linked list). */
typedef struct ScoutJsonMember {
    char *key;                      /**< the key (allocated) */
    ScoutJsonValue *value;          /**< the value (owned) */
    struct ScoutJsonMember *next;   /**< the next member, NULL at the end */
} ScoutJsonMember;

/** An element of an array (a node of a singly linked list). */
typedef struct ScoutJsonElement {
    ScoutJsonValue *value;          /**< the value (owned) */
    struct ScoutJsonElement *next;  /**< the next element, NULL at the end */
} ScoutJsonElement;

/** A JSON value; the member of #u that is used depends on #type. */
struct ScoutJsonValue {
    ScoutJsonType type;             /**< which kind of value it is */
    /** The value itself. */
    union {
        int bool_val;               /**< SCOUT_JSON_BOOL: 0 or 1 */
        /** SCOUT_JSON_NUMBER */
        struct {
            long int_val;           /**< the number as an integer */
            double double_val;      /**< as a floating point number (host only; 0 on the Amiga) */
            int is_float;           /**< it had a fraction or an exponent */
        } num_val;
        char *str_val;              /**< SCOUT_JSON_STRING: the string (allocated, 8-bit as read) */
        /** SCOUT_JSON_ARRAY */
        struct {
            ScoutJsonElement *first;    /**< first element */
            ScoutJsonElement *last;     /**< last element */
            size_t count;               /**< number of elements */
        } array_val;
        /** SCOUT_JSON_OBJECT */
        struct {
            ScoutJsonMember *first;     /**< first member */
            ScoutJsonMember *last;      /**< last member */
            size_t count;               /**< number of members */
        } obj_val;
    } u;
};

/**
 * @brief Parse a JSON string into a tree of values.
 *
 * Bytes above 127 in strings are kept as they are (ISO-8859-1 files);
 * \\uXXXX escapes are decoded for the ASCII range.
 *
 * @param json_str NUL-terminated JSON text.
 * @param[out] error_pos Set to the position of an error (may be NULL).
 * @return The root value (free it with scout_json_value_free()), or NULL
 *         on a syntax error or trailing text.
 */
ScoutJsonValue *scout_json_parse(const char *json_str, const char **error_pos);

/**
 * @brief Free a value and everything below it.
 * @param val The value (may be NULL).
 */
void scout_json_value_free(ScoutJsonValue *val);

/**
 * @brief Return the value of an object's member.
 * @param obj The object (another value or NULL gives NULL).
 * @param key The key.
 * @return The value (owned by @p obj), or NULL if there is none.
 */
const ScoutJsonValue *scout_json_obj_get(const ScoutJsonValue *obj, const char *key);
/**
 * @brief Return an object's member as a string.
 * @param obj The object.
 * @param key The key.
 * @param default_val Returned if the member is missing or not a string.
 * @return The string (owned by @p obj) or @p default_val.
 */
const char *scout_json_obj_get_string(const ScoutJsonValue *obj, const char *key, const char *default_val);
/**
 * @brief Return an object's member as an integer.
 * @param obj The object.
 * @param key The key.
 * @param default_val Returned if the member is missing or not a number.
 * @return The number (its integer part) or @p default_val.
 */
long scout_json_obj_get_int(const ScoutJsonValue *obj, const char *key, long default_val);
/**
 * @brief Return an object's member as a truth value.
 * @param obj The object.
 * @param key The key.
 * @param default_val Returned if the member is missing or not true/false.
 * @return 1, 0 or @p default_val.
 */
int scout_json_obj_get_bool(const ScoutJsonValue *obj, const char *key, int default_val);
/**
 * @brief Return an object's member if it is an object.
 * @param obj The object.
 * @param key The key.
 * @return The object (owned by @p obj), or NULL.
 */
const ScoutJsonValue *scout_json_obj_get_obj(const ScoutJsonValue *obj, const char *key);
/**
 * @brief Return an object's member if it is an array.
 * @param obj The object.
 * @param key The key.
 * @return The array (owned by @p obj), or NULL.
 */
const ScoutJsonValue *scout_json_obj_get_array(const ScoutJsonValue *obj, const char *key);

/**
 * @brief Return the number of elements of an array.
 * @param arr The array (another value or NULL gives 0).
 * @return The number of elements.
 */
size_t scout_json_array_length(const ScoutJsonValue *arr);
/**
 * @brief Return an element of an array (walks the list).
 * @param arr The array.
 * @param index The element's index, from 0.
 * @return The element (owned by @p arr), or NULL if there is none.
 */
const ScoutJsonValue *scout_json_array_get(const ScoutJsonValue *arr, size_t index);

/**
 * A writer that builds JSON text in a buffer, either a fixed one or one
 * that grows. Commas and spacing are written by the caller
 * (scout_json_write_raw()).
 */
typedef struct ScoutJsonWriter {
    char *buffer;       /**< the text, always NUL-terminated */
    size_t capacity;    /**< size of the buffer */
    size_t length;      /**< length of the text */
    int is_dynamic;     /**< the buffer is allocated and grows */
    int error;          /**< the text did not fit or memory ran out; writing stops */
    int need_comma;     /**< reserved (not used by the writer itself) */
} ScoutJsonWriter;

/**
 * @brief Start writing into a fixed buffer.
 * @param w The writer.
 * @param buf The buffer.
 * @param cap Its size; text that does not fit sets ScoutJsonWriter::error.
 */
void scout_json_writer_init(ScoutJsonWriter *w, char *buf, size_t cap);
/**
 * @brief Start writing into an allocated buffer that grows.
 * @param w The writer (free it with scout_json_writer_free()).
 * @param initial_cap First size of the buffer (at least 64).
 */
void scout_json_writer_init_alloc(ScoutJsonWriter *w, size_t initial_cap);
/**
 * @brief Free an allocated buffer and empty the writer.
 * @param w The writer.
 */
void scout_json_writer_free(ScoutJsonWriter *w);
/**
 * @brief Return the text written so far.
 * @param w The writer.
 * @return The text (owned by the writer), "" if there is none.
 */
const char *scout_json_writer_get(const ScoutJsonWriter *w);
/**
 * @brief Return the length of the text written so far.
 * @param w The writer.
 * @return The length.
 */
size_t scout_json_writer_length(const ScoutJsonWriter *w);

/**
 * @brief Write null.
 * @param w The writer.
 */
void scout_json_write_null(ScoutJsonWriter *w);
/**
 * @brief Write true or false.
 * @param w The writer.
 * @param val The truth value.
 */
void scout_json_write_bool(ScoutJsonWriter *w, int val);
/**
 * @brief Write an integer.
 * @param w The writer.
 * @param val The number.
 */
void scout_json_write_int(ScoutJsonWriter *w, long val);
/**
 * @brief Write a string in quotes, escaping quotes, backslashes and
 *        control characters; bytes above 127 are written as they are.
 * @param w The writer.
 * @param str The string (NULL writes null).
 */
void scout_json_write_string(ScoutJsonWriter *w, const char *str);
/**
 * @brief Write text as it is (commas, spacing, prepared JSON).
 * @param w The writer.
 * @param raw The text (NULL writes nothing).
 */
void scout_json_write_raw(ScoutJsonWriter *w, const char *raw);

/**
 * @brief Write the start of an object ("{").
 * @param w The writer.
 */
void scout_json_write_obj_start(ScoutJsonWriter *w);
/**
 * @brief Write a member's key and the colon.
 * @param w The writer.
 * @param key The key.
 */
void scout_json_write_obj_key(ScoutJsonWriter *w, const char *key);
/**
 * @brief Write the end of an object ("}").
 * @param w The writer.
 */
void scout_json_write_obj_end(ScoutJsonWriter *w);

/**
 * @brief Write the start of an array ("[").
 * @param w The writer.
 */
void scout_json_write_arr_start(ScoutJsonWriter *w);
/**
 * @brief Write the end of an array ("]").
 * @param w The writer.
 */
void scout_json_write_arr_end(ScoutJsonWriter *w);

/** A JSON-RPC 2.0 message as scout_json_rpc_parse() reads it. */
typedef struct ScoutJsonRpcMessage {
    int is_request;             /**< 1 for request, 0 for response */
    int is_notification;        /**< 1 if request has no ID */
    long id;                    /**< the ID (0 for a notification or a non-numeric ID) */
    char *method;               /**< copied method name for requests */
    const ScoutJsonValue *params; /**< borrowed reference to params in root */
    const ScoutJsonValue *result; /**< borrowed reference to result in root */
    int has_error;              /**< 1 if error object is present */
    int error_code;             /**< the error's code (SCOUT_JSONRPC_ERR_ or the server's own) */
    char *error_msg;            /**< copied error message */
    ScoutJsonValue *root;         /**< root DOM value, owned by message */
} ScoutJsonRpcMessage;

/**
 * @brief Read a JSON-RPC 2.0 request, notification or response.
 * @param json_str The message text.
 * @param[out] msg The message (free it with scout_json_rpc_free()).
 * @return 1 on success, 0 if it is not valid JSON or not JSON-RPC 2.0.
 */
int scout_json_rpc_parse(const char *json_str, ScoutJsonRpcMessage *msg);
/**
 * @brief Free what a message of scout_json_rpc_parse() holds.
 * @param msg The message (may be NULL).
 */
void scout_json_rpc_free(ScoutJsonRpcMessage *msg);

/**
 * @brief Write a JSON-RPC 2.0 request.
 * @param w The writer.
 * @param id The request's ID.
 * @param method The method.
 * @param params_json The parameters as JSON text (NULL for none).
 * @return 1 on success, 0 if the text did not fit.
 */
int scout_json_format_request(ScoutJsonWriter *w, long id, const char *method, const char *params_json);
/**
 * @brief Write a JSON-RPC 2.0 response.
 * @param w The writer.
 * @param id The ID of the request.
 * @param result_json The result as JSON text (NULL writes null).
 * @return 1 on success, 0 if the text did not fit.
 */
int scout_json_format_response(ScoutJsonWriter *w, long id, const char *result_json);
/**
 * @brief Write a JSON-RPC 2.0 error response.
 * @param w The writer.
 * @param id The ID of the request.
 * @param error_code The error's code (SCOUT_JSONRPC_ERR_ or the server's own).
 * @param error_msg The message.
 * @return 1 on success, 0 if the text did not fit.
 */
int scout_json_format_error(ScoutJsonWriter *w, long id, int error_code, const char *error_msg);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_SCOUT_JSON_H */
