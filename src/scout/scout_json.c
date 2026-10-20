/**
 * @file scout_json.c
 * @brief Small JSON reader and writer and the JSON-RPC 2.0 helpers of the Scout protocol.
 */
#include "scout_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Parser internal helpers */

/**
 * @brief Skip white space.
 * @param p The position, moved on.
 */
static void skip_ws(const char **p)
{
    while (**p && ((unsigned char)**p <= ' ' || **p == '\t' || **p == '\r' || **p == '\n')) {
        (*p)++;
    }
}

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

/**
 * @brief Read a string literal (with its escapes).
 * @param p The position (at the opening quote), moved behind the string.
 * @param[out] error_pos Set to the error position on failure (may be NULL).
 * @return The string (free() it), or NULL on error.
 */
static char *parse_string_literal(const char **p, const char **error_pos)
{
    if (**p != '"') {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    (*p)++; /* skip opening quote */

    const char *start = *p;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        if (error_pos) *error_pos = start;
        return NULL;
    }

    while (**p) {
        if (**p == '"') {
            (*p)++; /* skip closing quote */
            buf[len] = '\0';
            return buf;
        }
        char ch = **p;
        if (ch == '\\') {
            (*p)++;
            if (!**p) {
                if (error_pos) *error_pos = *p;
                free(buf);
                return NULL;
            }
            switch (**p) {
                case '"':  ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/':  ch = '/'; break;
                case 'b':  ch = '\b'; break;
                case 'f':  ch = '\f'; break;
                case 'n':  ch = '\n'; break;
                case 'r':  ch = '\r'; break;
                case 't':  ch = '\t'; break;
                case 'u': {
                    (*p)++;
                    /* Simple 4-hex digit unicode decoder (supports ASCII subset) */
                    unsigned int val = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = **p;
                        if (!h) {
                            if (error_pos) *error_pos = *p;
                            free(buf);
                            return NULL;
                        }
                        val <<= 4;
                        if (h >= '0' && h <= '9') val |= (unsigned int)(h - '0');
                        else if (h >= 'a' && h <= 'f') val |= (unsigned int)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') val |= (unsigned int)(h - 'A' + 10);
                        else {
                            if (error_pos) *error_pos = *p;
                            free(buf);
                            return NULL;
                        }
                        (*p)++;
                    }
                    (*p)--; /* compensate trailing (*p)++ at end of loop */
                    ch = (val < 128) ? (char)val : '?';
                    break;
                }
                default:
                    ch = **p;
                    break;
            }
        }
        (*p)++;

        if (len + 2 >= cap) {
            cap *= 2;
            char *new_buf = (char *)realloc(buf, cap);
            if (!new_buf) {
                if (error_pos) *error_pos = *p;
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }
        buf[len++] = ch;
    }

    if (error_pos) *error_pos = *p;
    free(buf);
    return NULL;
}

/**
 * @brief Read any value.
 * @param p The position, moved behind the value.
 * @param[out] error_pos Set to the error position on failure (may be NULL).
 * @return The value (scout_json_value_free()), or NULL on error.
 */
static ScoutJsonValue *parse_val(const char **p, const char **error_pos);

/**
 * @brief Read an object.
 * @param p The position (at "{"), moved behind the object.
 * @param[out] error_pos Set to the error position on failure (may be NULL).
 * @return The object, or NULL on error.
 */
static ScoutJsonValue *parse_obj(const char **p, const char **error_pos)
{
    if (**p != '{') {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    (*p)++; /* skip '{' */

    ScoutJsonValue *obj = (ScoutJsonValue *)calloc(1, sizeof(ScoutJsonValue));
    if (!obj) {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    obj->type = SCOUT_JSON_OBJECT;

    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return obj;
    }

    while (**p) {
        skip_ws(p);
        if (**p != '"') {
            if (error_pos) *error_pos = *p;
            scout_json_value_free(obj);
            return NULL;
        }

        char *key = parse_string_literal(p, error_pos);
        if (!key) {
            scout_json_value_free(obj);
            return NULL;
        }

        skip_ws(p);
        if (**p != ':') {
            if (error_pos) *error_pos = *p;
            free(key);
            scout_json_value_free(obj);
            return NULL;
        }
        (*p)++; /* skip ':' */

        skip_ws(p);
        ScoutJsonValue *child = parse_val(p, error_pos);
        if (!child) {
            free(key);
            scout_json_value_free(obj);
            return NULL;
        }

        ScoutJsonMember *member = (ScoutJsonMember *)malloc(sizeof(ScoutJsonMember));
        if (!member) {
            free(key);
            scout_json_value_free(child);
            scout_json_value_free(obj);
            return NULL;
        }
        member->key = key;
        member->value = child;
        member->next = NULL;

        if (!obj->u.obj_val.first) {
            obj->u.obj_val.first = member;
        } else {
            obj->u.obj_val.last->next = member;
        }
        obj->u.obj_val.last = member;
        obj->u.obj_val.count++;

        skip_ws(p);
        if (**p == '}') {
            (*p)++;
            return obj;
        }
        if (**p == ',') {
            (*p)++;
        } else {
            if (error_pos) *error_pos = *p;
            scout_json_value_free(obj);
            return NULL;
        }
    }

    if (error_pos) *error_pos = *p;
    scout_json_value_free(obj);
    return NULL;
}

/**
 * @brief Read an array.
 * @param p The position (at "["), moved behind the array.
 * @param[out] error_pos Set to the error position on failure (may be NULL).
 * @return The array, or NULL on error.
 */
static ScoutJsonValue *parse_arr(const char **p, const char **error_pos)
{
    if (**p != '[') {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    (*p)++; /* skip '[' */

    ScoutJsonValue *arr = (ScoutJsonValue *)calloc(1, sizeof(ScoutJsonValue));
    if (!arr) {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    arr->type = SCOUT_JSON_ARRAY;

    skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return arr;
    }

    while (**p) {
        skip_ws(p);
        ScoutJsonValue *child = parse_val(p, error_pos);
        if (!child) {
            scout_json_value_free(arr);
            return NULL;
        }

        ScoutJsonElement *elem = (ScoutJsonElement *)malloc(sizeof(ScoutJsonElement));
        if (!elem) {
            scout_json_value_free(child);
            scout_json_value_free(arr);
            return NULL;
        }
        elem->value = child;
        elem->next = NULL;

        if (!arr->u.array_val.first) {
            arr->u.array_val.first = elem;
        } else {
            arr->u.array_val.last->next = elem;
        }
        arr->u.array_val.last = elem;
        arr->u.array_val.count++;

        skip_ws(p);
        if (**p == ']') {
            (*p)++;
            return arr;
        }
        if (**p == ',') {
            (*p)++;
        } else {
            if (error_pos) *error_pos = *p;
            scout_json_value_free(arr);
            return NULL;
        }
    }

    if (error_pos) *error_pos = *p;
    scout_json_value_free(arr);
    return NULL;
}

#ifndef __amigaos__
/**
 * @brief Read a floating point number (host only: the Amiga build has no
 *        floating point numbers).
 * @param s The text.
 * @return The number.
 */
static double simple_strtod(const char *s)
{
    if (!s) return 0.0;
    while (*s && ((unsigned char)*s <= ' ' || *s == '\t')) s++;
    double sign = 1.0;
    if (*s == '-') {
        sign = -1.0;
        s++;
    } else if (*s == '+') {
        s++;
    }
    double int_part = 0.0;
    while (*s >= '0' && *s <= '9') {
        int_part = int_part * 10.0 + (double)(*s - '0');
        s++;
    }
    double frac_part = 0.0;
    double divisor = 1.0;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac_part = frac_part * 10.0 + (double)(*s - '0');
            divisor *= 10.0;
            s++;
        }
    }
    double val = sign * (int_part + (frac_part / divisor));
    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1;
        if (*s == '-') { exp_sign = -1; s++; }
        else if (*s == '+') { s++; }
        int exp_val = 0;
        while (*s >= '0' && *s <= '9') {
            exp_val = exp_val * 10 + (*s - '0');
            s++;
        }
        for (int i = 0; i < exp_val; i++) {
            if (exp_sign > 0) val *= 10.0;
            else val /= 10.0;
        }
    }
    return val;
}
#endif

static ScoutJsonValue *parse_val(const char **p, const char **error_pos)
{
    skip_ws(p);
    if (!**p) {
        if (error_pos) *error_pos = *p;
        return NULL;
    }

    if (**p == '{') {
        return parse_obj(p, error_pos);
    }
    if (**p == '[') {
        return parse_arr(p, error_pos);
    }
    if (**p == '"') {
        char *s = parse_string_literal(p, error_pos);
        if (!s) return NULL;
        ScoutJsonValue *v = (ScoutJsonValue *)calloc(1, sizeof(ScoutJsonValue));
        if (!v) { free(s); return NULL; }
        v->type = SCOUT_JSON_STRING;
        v->u.str_val = s;
        return v;
    }
    if (strncmp(*p, "true", 4) == 0) {
        *p += 4;
        ScoutJsonValue *v = (ScoutJsonValue *)calloc(1, sizeof(ScoutJsonValue));
        if (!v) return NULL;
        v->type = SCOUT_JSON_BOOL;
        v->u.bool_val = 1;
        return v;
    }
    if (strncmp(*p, "false", 5) == 0) {
        *p += 5;
        ScoutJsonValue *v = (ScoutJsonValue *)calloc(1, sizeof(ScoutJsonValue));
        if (!v) return NULL;
        v->type = SCOUT_JSON_BOOL;
        v->u.bool_val = 0;
        return v;
    }
    if (strncmp(*p, "null", 4) == 0) {
        *p += 4;
        ScoutJsonValue *v = (ScoutJsonValue *)calloc(1, sizeof(ScoutJsonValue));
        if (!v) return NULL;
        v->type = SCOUT_JSON_NULL;
        return v;
    }

    /* Number */
    if (**p == '-' || (**p >= '0' && **p <= '9')) {
        const char *num_start = *p;
        int is_float = 0;
        if (**p == '-') (*p)++;
        while (**p >= '0' && **p <= '9') (*p)++;
        if (**p == '.') {
            is_float = 1;
            (*p)++;
            while (**p >= '0' && **p <= '9') (*p)++;
        }
        if (**p == 'e' || **p == 'E') {
            is_float = 1;
            (*p)++;
            if (**p == '+' || **p == '-') (*p)++;
            while (**p >= '0' && **p <= '9') (*p)++;
        }

        ScoutJsonValue *v = (ScoutJsonValue *)calloc(1, sizeof(ScoutJsonValue));
        if (!v) return NULL;
        v->type = SCOUT_JSON_NUMBER;
        v->u.num_val.is_float = is_float;
        v->u.num_val.int_val = strtol(num_start, NULL, 10);
#ifndef __amigaos__
        if (is_float) {
            v->u.num_val.double_val = simple_strtod(num_start);
            v->u.num_val.int_val = (long)v->u.num_val.double_val;
        } else {
            v->u.num_val.double_val = (double)v->u.num_val.int_val;
        }
#endif
        return v;
    }

    if (error_pos) *error_pos = *p;
    return NULL;
}

ScoutJsonValue *scout_json_parse(const char *json_str, const char **error_pos)
{
    if (!json_str) {
        if (error_pos) *error_pos = NULL;
        return NULL;
    }
    const char *p = json_str;
    skip_ws(&p);
    if (!*p) {
        if (error_pos) *error_pos = p;
        return NULL;
    }
    ScoutJsonValue *val = parse_val(&p, error_pos);
    if (!val) return NULL;
    skip_ws(&p);
    if (*p != '\0') {
        /* Extra trailing content */
        if (error_pos) *error_pos = p;
        scout_json_value_free(val);
        return NULL;
    }
    return val;
}

void scout_json_value_free(ScoutJsonValue *val)
{
    if (!val) return;
    switch (val->type) {
        case SCOUT_JSON_STRING:
            if (val->u.str_val) free(val->u.str_val);
            break;
        case SCOUT_JSON_ARRAY: {
            ScoutJsonElement *elem = val->u.array_val.first;
            while (elem) {
                ScoutJsonElement *next = elem->next;
                scout_json_value_free(elem->value);
                free(elem);
                elem = next;
            }
            break;
        }
        case SCOUT_JSON_OBJECT: {
            ScoutJsonMember *m = val->u.obj_val.first;
            while (m) {
                ScoutJsonMember *next = m->next;
                if (m->key) free(m->key);
                scout_json_value_free(m->value);
                free(m);
                m = next;
            }
            break;
        }
        default:
            break;
    }
    free(val);
}

const ScoutJsonValue *scout_json_obj_get(const ScoutJsonValue *obj, const char *key)
{
    if (!obj || obj->type != SCOUT_JSON_OBJECT || !key) return NULL;
    ScoutJsonMember *m = obj->u.obj_val.first;
    while (m) {
        if (m->key && strcmp(m->key, key) == 0) {
            return m->value;
        }
        m = m->next;
    }
    return NULL;
}

const char *scout_json_obj_get_string(const ScoutJsonValue *obj, const char *key, const char *default_val)
{
    const ScoutJsonValue *v = scout_json_obj_get(obj, key);
    if (v && v->type == SCOUT_JSON_STRING && v->u.str_val) {
        return v->u.str_val;
    }
    return default_val;
}

long scout_json_obj_get_int(const ScoutJsonValue *obj, const char *key, long default_val)
{
    const ScoutJsonValue *v = scout_json_obj_get(obj, key);
    if (v && v->type == SCOUT_JSON_NUMBER) {
        return v->u.num_val.int_val;
    }
    return default_val;
}

int scout_json_obj_get_bool(const ScoutJsonValue *obj, const char *key, int default_val)
{
    const ScoutJsonValue *v = scout_json_obj_get(obj, key);
    if (v && v->type == SCOUT_JSON_BOOL) {
        return v->u.bool_val;
    }
    return default_val;
}

const ScoutJsonValue *scout_json_obj_get_obj(const ScoutJsonValue *obj, const char *key)
{
    const ScoutJsonValue *v = scout_json_obj_get(obj, key);
    if (v && v->type == SCOUT_JSON_OBJECT) {
        return v;
    }
    return NULL;
}

const ScoutJsonValue *scout_json_obj_get_array(const ScoutJsonValue *obj, const char *key)
{
    const ScoutJsonValue *v = scout_json_obj_get(obj, key);
    if (v && v->type == SCOUT_JSON_ARRAY) {
        return v;
    }
    return NULL;
}

size_t scout_json_array_length(const ScoutJsonValue *arr)
{
    if (!arr || arr->type != SCOUT_JSON_ARRAY) return 0;
    return arr->u.array_val.count;
}

const ScoutJsonValue *scout_json_array_get(const ScoutJsonValue *arr, size_t index)
{
    if (!arr || arr->type != SCOUT_JSON_ARRAY) return NULL;
    ScoutJsonElement *elem = arr->u.array_val.first;
    size_t i = 0;
    while (elem) {
        if (i == index) return elem->value;
        elem = elem->next;
        i++;
    }
    return NULL;
}

/* JSON Writer Implementation */

/**
 * @brief Make room in the writer's buffer; a fixed buffer that is too
 *        small (or a failed allocation) sets the writer's error.
 * @param w The writer.
 * @param needed Number of characters to add.
 */
static void ensure_writer_cap(ScoutJsonWriter *w, size_t needed)
{
    if (w->error) return;
    if (w->length + needed + 1 > w->capacity) {
        if (!w->is_dynamic) {
            w->error = 1;
            return;
        }
        size_t new_cap = w->capacity ? w->capacity * 2 : 128;
        while (new_cap < w->length + needed + 1) new_cap *= 2;
        char *new_buf = (char *)realloc(w->buffer, new_cap);
        if (!new_buf) {
            w->error = 1;
            return;
        }
        w->buffer = new_buf;
        w->capacity = new_cap;
    }
}

void scout_json_writer_init(ScoutJsonWriter *w, char *buf, size_t cap)
{
    if (!w) return;
    w->buffer = buf;
    w->capacity = cap;
    w->length = 0;
    w->is_dynamic = 0;
    w->error = 0;
    w->need_comma = 0;
    if (w->buffer && w->capacity > 0) {
        w->buffer[0] = '\0';
    }
}

void scout_json_writer_init_alloc(ScoutJsonWriter *w, size_t initial_cap)
{
    if (!w) return;
    if (initial_cap < 64) initial_cap = 64;
    w->buffer = (char *)malloc(initial_cap);
    w->capacity = initial_cap;
    w->length = 0;
    w->is_dynamic = 1;
    w->error = (w->buffer == NULL);
    w->need_comma = 0;
    if (w->buffer) {
        w->buffer[0] = '\0';
    }
}

void scout_json_writer_free(ScoutJsonWriter *w)
{
    if (!w) return;
    if (w->is_dynamic && w->buffer) {
        free(w->buffer);
        w->buffer = NULL;
    }
    w->capacity = 0;
    w->length = 0;
    w->error = 0;
}

const char *scout_json_writer_get(const ScoutJsonWriter *w)
{
    if (!w || !w->buffer) return "";
    return w->buffer;
}

size_t scout_json_writer_length(const ScoutJsonWriter *w)
{
    return w ? w->length : 0;
}

/**
 * @brief Append text to the writer (nothing after an error).
 * @param w The writer.
 * @param s The text.
 * @param len Its length.
 */
static void append_str(ScoutJsonWriter *w, const char *s, size_t len)
{
    if (!s || len == 0 || w->error) return;
    ensure_writer_cap(w, len);
    if (w->error) return;
    memcpy(w->buffer + w->length, s, len);
    w->length += len;
    w->buffer[w->length] = '\0';
}

void scout_json_write_raw(ScoutJsonWriter *w, const char *raw)
{
    if (!raw) return;
    append_str(w, raw, strlen(raw));
}

void scout_json_write_null(ScoutJsonWriter *w)
{
    append_str(w, "null", 4);
}

void scout_json_write_bool(ScoutJsonWriter *w, int val)
{
    if (val) {
        append_str(w, "true", 4);
    } else {
        append_str(w, "false", 5);
    }
}

void scout_json_write_int(ScoutJsonWriter *w, long val)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%ld", val);
    append_str(w, tmp, strlen(tmp));
}

void scout_json_write_string(ScoutJsonWriter *w, const char *str)
{
    if (!str) {
        scout_json_write_null(w);
        return;
    }
    append_str(w, "\"", 1);
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  append_str(w, "\\\"", 2); break;
            case '\\': append_str(w, "\\\\", 2); break;
            case '\b': append_str(w, "\\b", 2); break;
            case '\f': append_str(w, "\\f", 2); break;
            case '\n': append_str(w, "\\n", 2); break;
            case '\r': append_str(w, "\\r", 2); break;
            case '\t': append_str(w, "\\t", 2); break;
            default:
                if ((unsigned char)*p < 32) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)*p);
                    append_str(w, hex, 6);
                } else {
                    append_str(w, p, 1);
                }
                break;
        }
    }
    append_str(w, "\"", 1);
}

void scout_json_write_obj_start(ScoutJsonWriter *w)
{
    append_str(w, "{", 1);
}

void scout_json_write_obj_key(ScoutJsonWriter *w, const char *key)
{
    scout_json_write_string(w, key);
    append_str(w, ":", 1);
}

void scout_json_write_obj_end(ScoutJsonWriter *w)
{
    append_str(w, "}", 1);
}

void scout_json_write_arr_start(ScoutJsonWriter *w)
{
    append_str(w, "[", 1);
}

void scout_json_write_arr_end(ScoutJsonWriter *w)
{
    append_str(w, "]", 1);
}

/* JSON-RPC 2.0 Helpers */

int scout_json_rpc_parse(const char *json_str, ScoutJsonRpcMessage *msg)
{
    if (!json_str || !msg) return 0;
    memset(msg, 0, sizeof(ScoutJsonRpcMessage));

    const char *err = NULL;
    ScoutJsonValue *root = scout_json_parse(json_str, &err);
    if (!root || root->type != SCOUT_JSON_OBJECT) {
        scout_json_value_free(root);
        return 0;
    }
    msg->root = root;

    const char *ver = scout_json_obj_get_string(root, "jsonrpc", NULL);
    if (!ver || strcmp(ver, "2.0") != 0) {
        scout_json_rpc_free(msg);
        return 0;
    }

    const ScoutJsonValue *id_val = scout_json_obj_get(root, "id");
    if (id_val) {
        if (id_val->type == SCOUT_JSON_NUMBER) {
            msg->id = id_val->u.num_val.int_val;
        } else {
            msg->id = 0;
        }
        msg->is_notification = 0;
    } else {
        msg->id = 0;
        msg->is_notification = 1;
    }

    const char *method = scout_json_obj_get_string(root, "method", NULL);
    if (method) {
        msg->is_request = 1;
        msg->method = my_strdup(method);
        msg->params = scout_json_obj_get(root, "params");
    } else {
        msg->is_request = 0;
        msg->result = scout_json_obj_get(root, "result");
        const ScoutJsonValue *err_obj = scout_json_obj_get(root, "error");
        if (err_obj && err_obj->type == SCOUT_JSON_OBJECT) {
            msg->has_error = 1;
            msg->error_code = (int)scout_json_obj_get_int(err_obj, "code", 0);
            const char *em = scout_json_obj_get_string(err_obj, "message", "");
            msg->error_msg = my_strdup(em);
        }
    }

    return 1;
}

void scout_json_rpc_free(ScoutJsonRpcMessage *msg)
{
    if (!msg) return;
    if (msg->method) free(msg->method);
    if (msg->error_msg) free(msg->error_msg);
    if (msg->root) scout_json_value_free(msg->root);
    memset(msg, 0, sizeof(ScoutJsonRpcMessage));
}

int scout_json_format_request(ScoutJsonWriter *w, long id, const char *method, const char *params_json)
{
    if (!w || !method) return 0;
    scout_json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    scout_json_write_int(w, id);
    scout_json_write_raw(w, ",\"method\":");
    scout_json_write_string(w, method);
    if (params_json && *params_json) {
        scout_json_write_raw(w, ",\"params\":");
        scout_json_write_raw(w, params_json);
    }
    scout_json_write_raw(w, "}");
    return !w->error;
}

int scout_json_format_response(ScoutJsonWriter *w, long id, const char *result_json)
{
    if (!w) return 0;
    scout_json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    scout_json_write_int(w, id);
    scout_json_write_raw(w, ",\"result\":");
    if (result_json && *result_json) {
        scout_json_write_raw(w, result_json);
    } else {
        scout_json_write_null(w);
    }
    scout_json_write_raw(w, "}");
    return !w->error;
}

int scout_json_format_error(ScoutJsonWriter *w, long id, int error_code, const char *error_msg)
{
    if (!w) return 0;
    scout_json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    scout_json_write_int(w, id);
    scout_json_write_raw(w, ",\"error\":{\"code\":");
    scout_json_write_int(w, error_code);
    scout_json_write_raw(w, ",\"message\":");
    scout_json_write_string(w, error_msg ? error_msg : "");
    scout_json_write_raw(w, "}}");
    return !w->error;
}
