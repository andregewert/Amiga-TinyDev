/**
 * @file scout_rpc.h
 * @brief The JSON-RPC 2.0 helpers of the Scout protocol.
 */
#ifndef TINYDEV_SCOUT_RPC_H
#define TINYDEV_SCOUT_RPC_H

#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name JSON-RPC 2.0 error codes
 */
/** @{ */
#define SCOUT_RPC_ERR_PARSE_ERROR     -32700    /**< the request is not valid JSON */
#define SCOUT_RPC_ERR_INVALID_REQUEST -32600    /**< not a valid JSON-RPC request */
#define SCOUT_RPC_ERR_METHOD_NOT_FOUND -32601   /**< unknown method */
#define SCOUT_RPC_ERR_INVALID_PARAMS  -32602    /**< wrong parameters */
#define SCOUT_RPC_ERR_INTERNAL_ERROR  -32603    /**< error inside the server */
/** @} */

/** A JSON-RPC 2.0 message as scout_rpc_parse() reads it. */
typedef struct ScoutRpcMessage {
    int is_request;             /**< 1 for request, 0 for response */
    int is_notification;        /**< 1 if request has no ID */
    long id;                    /**< the ID (0 for a notification or a non-numeric ID) */
    char *method;               /**< copied method name for requests */
    const JsonValue *params; /**< borrowed reference to params in root */
    const JsonValue *result; /**< borrowed reference to result in root */
    int has_error;              /**< 1 if error object is present */
    int error_code;             /**< the error's code (SCOUT_RPC_ERR_ or the server's own) */
    char *error_msg;            /**< copied error message */
    JsonValue *root;         /**< root DOM value, owned by message */
} ScoutRpcMessage;

/**
 * @brief Read a JSON-RPC 2.0 request, notification or response.
 * @param json_str The message text.
 * @param[out] msg The message (free it with scout_rpc_free()).
 * @return 1 on success, 0 if it is not valid JSON or not JSON-RPC 2.0.
 */
int scout_rpc_parse(const char *json_str, ScoutRpcMessage *msg);
/**
 * @brief Free what a message of scout_rpc_parse() holds.
 * @param msg The message (may be NULL).
 */
void scout_rpc_free(ScoutRpcMessage *msg);

/**
 * @brief Write a JSON-RPC 2.0 request.
 * @param w The writer.
 * @param id The request's ID.
 * @param method The method.
 * @param params_json The parameters as JSON text (NULL for none).
 * @return 1 on success, 0 if the text did not fit.
 */
int scout_rpc_format_request(JsonWriter *w, long id, const char *method, const char *params_json);
/**
 * @brief Write a JSON-RPC 2.0 response.
 * @param w The writer.
 * @param id The ID of the request.
 * @param result_json The result as JSON text (NULL writes null).
 * @return 1 on success, 0 if the text did not fit.
 */
int scout_rpc_format_response(JsonWriter *w, long id, const char *result_json);
/**
 * @brief Write a JSON-RPC 2.0 error response.
 * @param w The writer.
 * @param id The ID of the request.
 * @param error_code The error's code (SCOUT_RPC_ERR_ or the server's own).
 * @param error_msg The message.
 * @return 1 on success, 0 if the text did not fit.
 */
int scout_rpc_format_error(JsonWriter *w, long id, int error_code, const char *error_msg);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_SCOUT_RPC_H */
