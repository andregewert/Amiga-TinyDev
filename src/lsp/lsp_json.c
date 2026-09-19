#include "lsp_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Parser internal helpers */

static void skip_ws(const char **p)
{
    while (**p && ((unsigned char)**p <= ' ' || **p == '\t' || **p == '\r' || **p == '\n')) {
        (*p)++;
    }
}

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

static LspJsonValue *parse_val(const char **p, const char **error_pos);

static LspJsonValue *parse_obj(const char **p, const char **error_pos)
{
    if (**p != '{') {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    (*p)++; /* skip '{' */

    LspJsonValue *obj = (LspJsonValue *)calloc(1, sizeof(LspJsonValue));
    if (!obj) {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    obj->type = LSP_JSON_OBJECT;

    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return obj;
    }

    while (**p) {
        skip_ws(p);
        if (**p != '"') {
            if (error_pos) *error_pos = *p;
            lsp_json_value_free(obj);
            return NULL;
        }

        char *key = parse_string_literal(p, error_pos);
        if (!key) {
            lsp_json_value_free(obj);
            return NULL;
        }

        skip_ws(p);
        if (**p != ':') {
            if (error_pos) *error_pos = *p;
            free(key);
            lsp_json_value_free(obj);
            return NULL;
        }
        (*p)++; /* skip ':' */

        skip_ws(p);
        LspJsonValue *child = parse_val(p, error_pos);
        if (!child) {
            free(key);
            lsp_json_value_free(obj);
            return NULL;
        }

        LspJsonMember *member = (LspJsonMember *)malloc(sizeof(LspJsonMember));
        if (!member) {
            free(key);
            lsp_json_value_free(child);
            lsp_json_value_free(obj);
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
            lsp_json_value_free(obj);
            return NULL;
        }
    }

    if (error_pos) *error_pos = *p;
    lsp_json_value_free(obj);
    return NULL;
}

static LspJsonValue *parse_arr(const char **p, const char **error_pos)
{
    if (**p != '[') {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    (*p)++; /* skip '[' */

    LspJsonValue *arr = (LspJsonValue *)calloc(1, sizeof(LspJsonValue));
    if (!arr) {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    arr->type = LSP_JSON_ARRAY;

    skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return arr;
    }

    while (**p) {
        skip_ws(p);
        LspJsonValue *child = parse_val(p, error_pos);
        if (!child) {
            lsp_json_value_free(arr);
            return NULL;
        }

        LspJsonElement *elem = (LspJsonElement *)malloc(sizeof(LspJsonElement));
        if (!elem) {
            lsp_json_value_free(child);
            lsp_json_value_free(arr);
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
            lsp_json_value_free(arr);
            return NULL;
        }
    }

    if (error_pos) *error_pos = *p;
    lsp_json_value_free(arr);
    return NULL;
}

#ifndef __amigaos__
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

static LspJsonValue *parse_val(const char **p, const char **error_pos)
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
        LspJsonValue *v = (LspJsonValue *)calloc(1, sizeof(LspJsonValue));
        if (!v) { free(s); return NULL; }
        v->type = LSP_JSON_STRING;
        v->u.str_val = s;
        return v;
    }
    if (strncmp(*p, "true", 4) == 0) {
        *p += 4;
        LspJsonValue *v = (LspJsonValue *)calloc(1, sizeof(LspJsonValue));
        if (!v) return NULL;
        v->type = LSP_JSON_BOOL;
        v->u.bool_val = 1;
        return v;
    }
    if (strncmp(*p, "false", 5) == 0) {
        *p += 5;
        LspJsonValue *v = (LspJsonValue *)calloc(1, sizeof(LspJsonValue));
        if (!v) return NULL;
        v->type = LSP_JSON_BOOL;
        v->u.bool_val = 0;
        return v;
    }
    if (strncmp(*p, "null", 4) == 0) {
        *p += 4;
        LspJsonValue *v = (LspJsonValue *)calloc(1, sizeof(LspJsonValue));
        if (!v) return NULL;
        v->type = LSP_JSON_NULL;
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

        LspJsonValue *v = (LspJsonValue *)calloc(1, sizeof(LspJsonValue));
        if (!v) return NULL;
        v->type = LSP_JSON_NUMBER;
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

LspJsonValue *lsp_json_parse(const char *json_str, const char **error_pos)
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
    LspJsonValue *val = parse_val(&p, error_pos);
    if (!val) return NULL;
    skip_ws(&p);
    if (*p != '\0') {
        /* Extra trailing content */
        if (error_pos) *error_pos = p;
        lsp_json_value_free(val);
        return NULL;
    }
    return val;
}

void lsp_json_value_free(LspJsonValue *val)
{
    if (!val) return;
    switch (val->type) {
        case LSP_JSON_STRING:
            if (val->u.str_val) free(val->u.str_val);
            break;
        case LSP_JSON_ARRAY: {
            LspJsonElement *elem = val->u.array_val.first;
            while (elem) {
                LspJsonElement *next = elem->next;
                lsp_json_value_free(elem->value);
                free(elem);
                elem = next;
            }
            break;
        }
        case LSP_JSON_OBJECT: {
            LspJsonMember *m = val->u.obj_val.first;
            while (m) {
                LspJsonMember *next = m->next;
                if (m->key) free(m->key);
                lsp_json_value_free(m->value);
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

const LspJsonValue *lsp_json_obj_get(const LspJsonValue *obj, const char *key)
{
    if (!obj || obj->type != LSP_JSON_OBJECT || !key) return NULL;
    LspJsonMember *m = obj->u.obj_val.first;
    while (m) {
        if (m->key && strcmp(m->key, key) == 0) {
            return m->value;
        }
        m = m->next;
    }
    return NULL;
}

const char *lsp_json_obj_get_string(const LspJsonValue *obj, const char *key, const char *default_val)
{
    const LspJsonValue *v = lsp_json_obj_get(obj, key);
    if (v && v->type == LSP_JSON_STRING && v->u.str_val) {
        return v->u.str_val;
    }
    return default_val;
}

long lsp_json_obj_get_int(const LspJsonValue *obj, const char *key, long default_val)
{
    const LspJsonValue *v = lsp_json_obj_get(obj, key);
    if (v && v->type == LSP_JSON_NUMBER) {
        return v->u.num_val.int_val;
    }
    return default_val;
}

int lsp_json_obj_get_bool(const LspJsonValue *obj, const char *key, int default_val)
{
    const LspJsonValue *v = lsp_json_obj_get(obj, key);
    if (v && v->type == LSP_JSON_BOOL) {
        return v->u.bool_val;
    }
    return default_val;
}

const LspJsonValue *lsp_json_obj_get_obj(const LspJsonValue *obj, const char *key)
{
    const LspJsonValue *v = lsp_json_obj_get(obj, key);
    if (v && v->type == LSP_JSON_OBJECT) {
        return v;
    }
    return NULL;
}

const LspJsonValue *lsp_json_obj_get_array(const LspJsonValue *obj, const char *key)
{
    const LspJsonValue *v = lsp_json_obj_get(obj, key);
    if (v && v->type == LSP_JSON_ARRAY) {
        return v;
    }
    return NULL;
}

size_t lsp_json_array_length(const LspJsonValue *arr)
{
    if (!arr || arr->type != LSP_JSON_ARRAY) return 0;
    return arr->u.array_val.count;
}

const LspJsonValue *lsp_json_array_get(const LspJsonValue *arr, size_t index)
{
    if (!arr || arr->type != LSP_JSON_ARRAY) return NULL;
    LspJsonElement *elem = arr->u.array_val.first;
    size_t i = 0;
    while (elem) {
        if (i == index) return elem->value;
        elem = elem->next;
        i++;
    }
    return NULL;
}

/* JSON Writer Implementation */

static void ensure_writer_cap(LspJsonWriter *w, size_t needed)
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

void lsp_json_writer_init(LspJsonWriter *w, char *buf, size_t cap)
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

void lsp_json_writer_init_alloc(LspJsonWriter *w, size_t initial_cap)
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

void lsp_json_writer_free(LspJsonWriter *w)
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

const char *lsp_json_writer_get(const LspJsonWriter *w)
{
    if (!w || !w->buffer) return "";
    return w->buffer;
}

size_t lsp_json_writer_length(const LspJsonWriter *w)
{
    return w ? w->length : 0;
}

static void append_str(LspJsonWriter *w, const char *s, size_t len)
{
    if (!s || len == 0 || w->error) return;
    ensure_writer_cap(w, len);
    if (w->error) return;
    memcpy(w->buffer + w->length, s, len);
    w->length += len;
    w->buffer[w->length] = '\0';
}

void lsp_json_write_raw(LspJsonWriter *w, const char *raw)
{
    if (!raw) return;
    append_str(w, raw, strlen(raw));
}

void lsp_json_write_null(LspJsonWriter *w)
{
    append_str(w, "null", 4);
}

void lsp_json_write_bool(LspJsonWriter *w, int val)
{
    if (val) {
        append_str(w, "true", 4);
    } else {
        append_str(w, "false", 5);
    }
}

void lsp_json_write_int(LspJsonWriter *w, long val)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%ld", val);
    append_str(w, tmp, strlen(tmp));
}

void lsp_json_write_string(LspJsonWriter *w, const char *str)
{
    if (!str) {
        lsp_json_write_null(w);
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

void lsp_json_write_obj_start(LspJsonWriter *w)
{
    append_str(w, "{", 1);
}

void lsp_json_write_obj_key(LspJsonWriter *w, const char *key)
{
    lsp_json_write_string(w, key);
    append_str(w, ":", 1);
}

void lsp_json_write_obj_end(LspJsonWriter *w)
{
    append_str(w, "}", 1);
}

void lsp_json_write_arr_start(LspJsonWriter *w)
{
    append_str(w, "[", 1);
}

void lsp_json_write_arr_end(LspJsonWriter *w)
{
    append_str(w, "]", 1);
}

/* JSON-RPC 2.0 Helpers */

int lsp_json_rpc_parse(const char *json_str, LspJsonRpcMessage *msg)
{
    if (!json_str || !msg) return 0;
    memset(msg, 0, sizeof(LspJsonRpcMessage));

    const char *err = NULL;
    LspJsonValue *root = lsp_json_parse(json_str, &err);
    if (!root || root->type != LSP_JSON_OBJECT) {
        lsp_json_value_free(root);
        return 0;
    }
    msg->root = root;

    const char *ver = lsp_json_obj_get_string(root, "jsonrpc", NULL);
    if (!ver || strcmp(ver, "2.0") != 0) {
        lsp_json_rpc_free(msg);
        return 0;
    }

    const LspJsonValue *id_val = lsp_json_obj_get(root, "id");
    if (id_val) {
        if (id_val->type == LSP_JSON_NUMBER) {
            msg->id = id_val->u.num_val.int_val;
        } else {
            msg->id = 0;
        }
        msg->is_notification = 0;
    } else {
        msg->id = 0;
        msg->is_notification = 1;
    }

    const char *method = lsp_json_obj_get_string(root, "method", NULL);
    if (method) {
        msg->is_request = 1;
        msg->method = my_strdup(method);
        msg->params = lsp_json_obj_get(root, "params");
    } else {
        msg->is_request = 0;
        msg->result = lsp_json_obj_get(root, "result");
        const LspJsonValue *err_obj = lsp_json_obj_get(root, "error");
        if (err_obj && err_obj->type == LSP_JSON_OBJECT) {
            msg->has_error = 1;
            msg->error_code = (int)lsp_json_obj_get_int(err_obj, "code", 0);
            const char *em = lsp_json_obj_get_string(err_obj, "message", "");
            msg->error_msg = my_strdup(em);
        }
    }

    return 1;
}

void lsp_json_rpc_free(LspJsonRpcMessage *msg)
{
    if (!msg) return;
    if (msg->method) free(msg->method);
    if (msg->error_msg) free(msg->error_msg);
    if (msg->root) lsp_json_value_free(msg->root);
    memset(msg, 0, sizeof(LspJsonRpcMessage));
}

int lsp_json_format_request(LspJsonWriter *w, long id, const char *method, const char *params_json)
{
    if (!w || !method) return 0;
    lsp_json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    lsp_json_write_int(w, id);
    lsp_json_write_raw(w, ",\"method\":");
    lsp_json_write_string(w, method);
    if (params_json && *params_json) {
        lsp_json_write_raw(w, ",\"params\":");
        lsp_json_write_raw(w, params_json);
    }
    lsp_json_write_raw(w, "}");
    return !w->error;
}

int lsp_json_format_response(LspJsonWriter *w, long id, const char *result_json)
{
    if (!w) return 0;
    lsp_json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    lsp_json_write_int(w, id);
    lsp_json_write_raw(w, ",\"result\":");
    if (result_json && *result_json) {
        lsp_json_write_raw(w, result_json);
    } else {
        lsp_json_write_null(w);
    }
    lsp_json_write_raw(w, "}");
    return !w->error;
}

int lsp_json_format_error(LspJsonWriter *w, long id, int error_code, const char *error_msg)
{
    if (!w) return 0;
    lsp_json_write_raw(w, "{\"jsonrpc\":\"2.0\",\"id\":");
    lsp_json_write_int(w, id);
    lsp_json_write_raw(w, ",\"error\":{\"code\":");
    lsp_json_write_int(w, error_code);
    lsp_json_write_raw(w, ",\"message\":");
    lsp_json_write_string(w, error_msg ? error_msg : "");
    lsp_json_write_raw(w, "}}");
    return !w->error;
}
