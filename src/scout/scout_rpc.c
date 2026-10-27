/**
 * @file scout_rpc.c
 * @brief The JSON-RPC 2.0 helpers of the Scout protocol.
 */
#include "scout_rpc.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Copy a string with malloc().
 * @param s The string (may be NULL).
 * @return The copy, or NULL.
 */
static char *my_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

int scout_rpc_parse(const char *json_str, ScoutRpcMessage *msg)
{
    if (!json_str || !msg) return 0;
    memset(msg, 0, sizeof(ScoutRpcMessage));

    const char *err = NULL;
    JsonValue *root = json_parse(json_str, &err);
    if (!root || root->type != JSON_OBJECT) {
        json_value_free(root);
        return 0;
    }
    msg->root = root;

    const char *ver = json_obj_get_string(root, "jsonrpc", NULL);
    if (!ver || strcmp(ver, "2.0") != 0) {
        scout_rpc_free(msg);
        return 0;
    }

    const JsonValue *id_val = json_obj_get(root, "id");
    if (id_val) {
        if (id_val->type == JSON_NUMBER) {
            msg->id = id_val->u.num_val.int_val;
        } else {
            msg->id = 0;
        }
        msg->is_notification = 0;
    } else {
        msg->id = 0;
        msg->is_notification = 1;
    }

    const char *method = json_obj_get_string(root, "method", NULL);
    if (method) {
        msg->is_request = 1;
        msg->method = my_strdup(method);
        msg->params = json_obj_get(root, "params");
    } else {
        msg->is_request = 0;
        msg->result = json_obj_get(root, "result");
        const JsonValue *err_obj = json_obj_get(root, "error");
        if (err_obj && err_obj->type == JSON_OBJECT) {
            msg->has_error = 1;
            msg->error_code = (int)json_obj_get_int(err_obj, "code", 0);
            const char *em = json_obj_get_string(err_obj, "message", "");
            msg->error_msg = my_strdup(em);
        }
    }

    return 1;
}

void scout_rpc_free(ScoutRpcMessage *msg)
{
    if (!msg) return;
    if (msg->method) free(msg->method);
    if (msg->error_msg) free(msg->error_msg);
    if (msg->root) json_value_free(msg->root);
    memset(msg, 0, sizeof(ScoutRpcMessage));
}

int scout_rpc_format_request(JsonWriter *w, long id, const char *method, const char *params_json)
{
    if (!w || !method) return 0;
    json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    json_write_int(w, id);
    json_write_raw(w, ",\"method\":");
    json_write_string(w, method);
    if (params_json && *params_json) {
        json_write_raw(w, ",\"params\":");
        json_write_raw(w, params_json);
    }
    json_write_raw(w, "}");
    return !w->error;
}

int scout_rpc_format_response(JsonWriter *w, long id, const char *result_json)
{
    if (!w) return 0;
    json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    json_write_int(w, id);
    json_write_raw(w, ",\"result\":");
    if (result_json && *result_json) {
        json_write_raw(w, result_json);
    } else {
        json_write_null(w);
    }
    json_write_raw(w, "}");
    return !w->error;
}

int scout_rpc_format_error(JsonWriter *w, long id, int error_code, const char *error_msg)
{
    if (!w) return 0;
    json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    json_write_int(w, id);
    json_write_raw(w, ",\"error\":{\"code\":");
    json_write_int(w, error_code);
    json_write_raw(w, ",\"message\":");
    json_write_string(w, error_msg ? error_msg : "");
    json_write_raw(w, "}}");
    return !w->error;
}
