/**
 * @file json.c
 * @brief Small JSON reader and writer (values, parser, accessors, writer).
 */
#include "json.h"

#include <stdlib.h>
#include <string.h>

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
 * @return The value (json_value_free()), or NULL on error.
 */
static JsonValue *parse_val(const char **p, const char **error_pos);

/**
 * @brief Read an object.
 * @param p The position (at "{"), moved behind the object.
 * @param[out] error_pos Set to the error position on failure (may be NULL).
 * @return The object, or NULL on error.
 */
static JsonValue *parse_obj(const char **p, const char **error_pos)
{
    if (**p != '{') {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    (*p)++; /* skip '{' */

    JsonValue *obj = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!obj) {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    obj->type = JSON_OBJECT;

    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return obj;
    }

    while (**p) {
        skip_ws(p);
        if (**p != '"') {
            if (error_pos) *error_pos = *p;
            json_value_free(obj);
            return NULL;
        }

        char *key = parse_string_literal(p, error_pos);
        if (!key) {
            json_value_free(obj);
            return NULL;
        }

        skip_ws(p);
        if (**p != ':') {
            if (error_pos) *error_pos = *p;
            free(key);
            json_value_free(obj);
            return NULL;
        }
        (*p)++; /* skip ':' */

        skip_ws(p);
        JsonValue *child = parse_val(p, error_pos);
        if (!child) {
            free(key);
            json_value_free(obj);
            return NULL;
        }

        JsonMember *member = (JsonMember *)malloc(sizeof(JsonMember));
        if (!member) {
            free(key);
            json_value_free(child);
            json_value_free(obj);
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
            json_value_free(obj);
            return NULL;
        }
    }

    if (error_pos) *error_pos = *p;
    json_value_free(obj);
    return NULL;
}

/**
 * @brief Read an array.
 * @param p The position (at "["), moved behind the array.
 * @param[out] error_pos Set to the error position on failure (may be NULL).
 * @return The array, or NULL on error.
 */
static JsonValue *parse_arr(const char **p, const char **error_pos)
{
    if (**p != '[') {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    (*p)++; /* skip '[' */

    JsonValue *arr = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (!arr) {
        if (error_pos) *error_pos = *p;
        return NULL;
    }
    arr->type = JSON_ARRAY;

    skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return arr;
    }

    while (**p) {
        skip_ws(p);
        JsonValue *child = parse_val(p, error_pos);
        if (!child) {
            json_value_free(arr);
            return NULL;
        }

        JsonElement *elem = (JsonElement *)malloc(sizeof(JsonElement));
        if (!elem) {
            json_value_free(child);
            json_value_free(arr);
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
            json_value_free(arr);
            return NULL;
        }
    }

    if (error_pos) *error_pos = *p;
    json_value_free(arr);
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

static JsonValue *parse_val(const char **p, const char **error_pos)
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
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        if (!v) { free(s); return NULL; }
        v->type = JSON_STRING;
        v->u.str_val = s;
        return v;
    }
    if (strncmp(*p, "true", 4) == 0) {
        *p += 4;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        if (!v) return NULL;
        v->type = JSON_BOOL;
        v->u.bool_val = 1;
        return v;
    }
    if (strncmp(*p, "false", 5) == 0) {
        *p += 5;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        if (!v) return NULL;
        v->type = JSON_BOOL;
        v->u.bool_val = 0;
        return v;
    }
    if (strncmp(*p, "null", 4) == 0) {
        *p += 4;
        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        if (!v) return NULL;
        v->type = JSON_NULL;
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

        JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
        if (!v) return NULL;
        v->type = JSON_NUMBER;
        v->u.num_val.is_float = is_float;
        v->u.num_val.int_val = strtol(num_start, NULL, 10);
        v->u.num_val.text = (char *)malloc((size_t)(*p - num_start) + 1);
        if (!v->u.num_val.text) { free(v); return NULL; }
        memcpy(v->u.num_val.text, num_start, (size_t)(*p - num_start));
        v->u.num_val.text[*p - num_start] = '\0';
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

JsonValue *json_parse(const char *json_str, const char **error_pos)
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
    JsonValue *val = parse_val(&p, error_pos);
    if (!val) return NULL;
    skip_ws(&p);
    if (*p != '\0') {
        /* Extra trailing content */
        if (error_pos) *error_pos = p;
        json_value_free(val);
        return NULL;
    }
    return val;
}

void json_value_free(JsonValue *val)
{
    if (!val) return;
    switch (val->type) {
        case JSON_STRING:
            if (val->u.str_val) free(val->u.str_val);
            break;
        case JSON_NUMBER:
            free(val->u.num_val.text);
            break;
        case JSON_ARRAY: {
            JsonElement *elem = val->u.array_val.first;
            while (elem) {
                JsonElement *next = elem->next;
                json_value_free(elem->value);
                free(elem);
                elem = next;
            }
            break;
        }
        case JSON_OBJECT: {
            JsonMember *m = val->u.obj_val.first;
            while (m) {
                JsonMember *next = m->next;
                if (m->key) free(m->key);
                json_value_free(m->value);
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

const JsonValue *json_obj_get(const JsonValue *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    JsonMember *m = obj->u.obj_val.first;
    while (m) {
        if (m->key && strcmp(m->key, key) == 0) {
            return m->value;
        }
        m = m->next;
    }
    return NULL;
}

const char *json_obj_get_string(const JsonValue *obj, const char *key, const char *default_val)
{
    const JsonValue *v = json_obj_get(obj, key);
    if (v && v->type == JSON_STRING && v->u.str_val) {
        return v->u.str_val;
    }
    return default_val;
}

long json_obj_get_int(const JsonValue *obj, const char *key, long default_val)
{
    const JsonValue *v = json_obj_get(obj, key);
    if (v && v->type == JSON_NUMBER) {
        return v->u.num_val.int_val;
    }
    return default_val;
}

int json_obj_get_bool(const JsonValue *obj, const char *key, int default_val)
{
    const JsonValue *v = json_obj_get(obj, key);
    if (v && v->type == JSON_BOOL) {
        return v->u.bool_val;
    }
    return default_val;
}

const JsonValue *json_obj_get_obj(const JsonValue *obj, const char *key)
{
    const JsonValue *v = json_obj_get(obj, key);
    if (v && v->type == JSON_OBJECT) {
        return v;
    }
    return NULL;
}

const JsonValue *json_obj_get_array(const JsonValue *obj, const char *key)
{
    const JsonValue *v = json_obj_get(obj, key);
    if (v && v->type == JSON_ARRAY) {
        return v;
    }
    return NULL;
}

size_t json_array_length(const JsonValue *arr)
{
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->u.array_val.count;
}

const JsonValue *json_array_get(const JsonValue *arr, size_t index)
{
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    JsonElement *elem = arr->u.array_val.first;
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
static void ensure_writer_cap(JsonWriter *w, size_t needed)
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

void json_writer_init(JsonWriter *w, char *buf, size_t cap)
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

void json_writer_init_alloc(JsonWriter *w, size_t initial_cap)
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

void json_writer_free(JsonWriter *w)
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

const char *json_writer_get(const JsonWriter *w)
{
    if (!w || !w->buffer) return "";
    return w->buffer;
}

size_t json_writer_length(const JsonWriter *w)
{
    return w ? w->length : 0;
}

/**
 * @brief Append text to the writer (nothing after an error).
 * @param w The writer.
 * @param s The text.
 * @param len Its length.
 */
static void append_str(JsonWriter *w, const char *s, size_t len)
{
    if (!s || len == 0 || w->error) return;
    ensure_writer_cap(w, len);
    if (w->error) return;
    memcpy(w->buffer + w->length, s, len);
    w->length += len;
    w->buffer[w->length] = '\0';
}

void json_write_raw(JsonWriter *w, const char *raw)
{
    if (!raw) return;
    append_str(w, raw, strlen(raw));
}

void json_write_null(JsonWriter *w)
{
    append_str(w, "null", 4);
}

void json_write_bool(JsonWriter *w, int val)
{
    if (val) {
        append_str(w, "true", 4);
    } else {
        append_str(w, "false", 5);
    }
}

void json_write_int(JsonWriter *w, long val)
{
    /* By hand rather than with snprintf(), which would pull in stdio. */
    char tmp[16];
    char *p = tmp + sizeof(tmp);
    unsigned long u = val < 0 ? 0UL - (unsigned long)val : (unsigned long)val;
    do {
        *--p = (char)('0' + u % 10);
        u /= 10;
    } while (u != 0);
    if (val < 0) *--p = '-';
    append_str(w, p, (size_t)(tmp + sizeof(tmp) - p));
}

void json_write_string(JsonWriter *w, const char *str)
{
    if (!str) {
        json_write_null(w);
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
                    char hex[7] = "\\u0000";
                    hex[4] = "0123456789abcdef"[(unsigned char)*p >> 4];
                    hex[5] = "0123456789abcdef"[*p & 15];
                    append_str(w, hex, 6);
                } else {
                    append_str(w, p, 1);
                }
                break;
        }
    }
    append_str(w, "\"", 1);
}

void json_write_obj_start(JsonWriter *w)
{
    append_str(w, "{", 1);
}

void json_write_obj_key(JsonWriter *w, const char *key)
{
    json_write_string(w, key);
    append_str(w, ":", 1);
}

void json_write_obj_end(JsonWriter *w)
{
    append_str(w, "}", 1);
}

void json_write_arr_start(JsonWriter *w)
{
    append_str(w, "[", 1);
}

void json_write_arr_end(JsonWriter *w)
{
    append_str(w, "]", 1);
}

/* Building and changing trees */

/**
 * @brief Allocate a value of a type (with everything zero).
 * @param type The type.
 * @return The value, or NULL if out of memory.
 */
static JsonValue *new_value(JsonType type)
{
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) v->type = type;
    return v;
}

JsonValue *json_new_object(void)
{
    return new_value(JSON_OBJECT);
}

JsonValue *json_new_array(void)
{
    return new_value(JSON_ARRAY);
}

JsonValue *json_new_bool(int val)
{
    JsonValue *v = new_value(JSON_BOOL);
    if (v) v->u.bool_val = val != 0;
    return v;
}

JsonValue *json_new_int(long val)
{
    JsonValue *v = new_value(JSON_NUMBER);
    if (v) {
        v->u.num_val.int_val = val;
        v->u.num_val.double_val = (double)val;
    }
    return v;
}

JsonValue *json_new_string(const char *str)
{
    size_t n;
    JsonValue *v;
    if (!str) return NULL;
    n = strlen(str) + 1;
    v = new_value(JSON_STRING);
    if (!v) return NULL;
    v->u.str_val = (char *)malloc(n);
    if (!v->u.str_val) {
        free(v);
        return NULL;
    }
    memcpy(v->u.str_val, str, n);
    return v;
}

int json_array_append(JsonValue *arr, JsonValue *val)
{
    JsonElement *e;
    if (!val) return 0;
    if (!arr || arr->type != JSON_ARRAY ||
        (e = (JsonElement *)malloc(sizeof(JsonElement))) == NULL) {
        json_value_free(val);
        return 0;
    }
    e->value = val;
    e->next = NULL;
    if (arr->u.array_val.last) arr->u.array_val.last->next = e;
    else arr->u.array_val.first = e;
    arr->u.array_val.last = e;
    arr->u.array_val.count++;
    return 1;
}

int json_obj_set(JsonValue *obj, const char *key, JsonValue *val)
{
    JsonMember *m;
    size_t n;
    if (!val) return 0;
    if (!obj || obj->type != JSON_OBJECT || !key) {
        json_value_free(val);
        return 0;
    }
    for (m = obj->u.obj_val.first; m; m = m->next)
        if (strcmp(m->key, key) == 0) {
            json_value_free(m->value);
            m->value = val;
            return 1;
        }
    n = strlen(key) + 1;
    m = (JsonMember *)malloc(sizeof(JsonMember));
    if (!m || (m->key = (char *)malloc(n)) == NULL) {
        free(m);
        json_value_free(val);
        return 0;
    }
    memcpy(m->key, key, n);
    m->value = val;
    m->next = NULL;
    if (obj->u.obj_val.last) obj->u.obj_val.last->next = m;
    else obj->u.obj_val.first = m;
    obj->u.obj_val.last = m;
    obj->u.obj_val.count++;
    return 1;
}

int json_obj_remove(JsonValue *obj, const char *key)
{
    JsonMember *m, *prev = NULL;
    if (!obj || obj->type != JSON_OBJECT || !key) return 0;
    for (m = obj->u.obj_val.first; m; prev = m, m = m->next)
        if (strcmp(m->key, key) == 0) {
            if (prev) prev->next = m->next;
            else obj->u.obj_val.first = m->next;
            if (obj->u.obj_val.last == m) obj->u.obj_val.last = prev;
            obj->u.obj_val.count--;
            free(m->key);
            json_value_free(m->value);
            free(m);
            return 1;
        }
    return 0;
}

/* Writing trees */

/**
 * @brief Tell whether a container holds no objects and no arrays with
 *        containers, so it fits on one line.
 * @param v The value.
 * @return Non-zero if it is flat (scalars are flat).
 */
static int is_flat(const JsonValue *v)
{
    if (v->type == JSON_ARRAY) {
        const JsonElement *e;
        for (e = v->u.array_val.first; e; e = e->next)
            if (e->value->type == JSON_OBJECT ||
                (e->value->type == JSON_ARRAY && !is_flat(e->value))) return 0;
    } else if (v->type == JSON_OBJECT) {
        const JsonMember *m;
        for (m = v->u.obj_val.first; m; m = m->next)
            if (m->value->type == JSON_OBJECT ||
                (m->value->type == JSON_ARRAY && !is_flat(m->value))) return 0;
    }
    return 1;
}

/**
 * @brief Start a new line indented for a depth.
 * @param w The writer.
 * @param depth The depth (two spaces each).
 */
static void new_line(JsonWriter *w, int depth)
{
    append_str(w, "\n", 1);
    while (depth-- > 0) append_str(w, "  ", 2);
}

void json_write_value(JsonWriter *w, const JsonValue *v, int depth)
{
    /* depth < 0: all on one line; flat containers below the top on one line */
    int lines;
    if (!v) {
        json_write_null(w);
        return;
    }
    lines = depth >= 0 && !(depth > 0 && is_flat(v));
    switch (v->type) {
        case JSON_NULL:
            json_write_null(w);
            break;
        case JSON_BOOL:
            json_write_bool(w, v->u.bool_val);
            break;
        case JSON_NUMBER:
            if (v->u.num_val.text) json_write_raw(w, v->u.num_val.text);
            else json_write_int(w, v->u.num_val.int_val);
            break;
        case JSON_STRING:
            json_write_string(w, v->u.str_val);
            break;
        case JSON_ARRAY: {
            const JsonElement *e;
            if (!v->u.array_val.first) {
                append_str(w, "[]", 2);
                break;
            }
            append_str(w, "[", 1);
            for (e = v->u.array_val.first; e; e = e->next) {
                if (lines) new_line(w, depth + 1);
                json_write_value(w, e->value, lines ? depth + 1 : -1);
                if (e->next) append_str(w, lines ? "," : ", ", lines ? 1 : 2);
            }
            if (lines) new_line(w, depth);
            append_str(w, "]", 1);
            break;
        }
        case JSON_OBJECT: {
            const JsonMember *m;
            if (!v->u.obj_val.first) {
                append_str(w, "{}", 2);
                break;
            }
            append_str(w, lines ? "{" : "{ ", lines ? 1 : 2);
            for (m = v->u.obj_val.first; m; m = m->next) {
                if (lines) new_line(w, depth + 1);
                json_write_string(w, m->key);
                append_str(w, ": ", 2);
                json_write_value(w, m->value, lines ? depth + 1 : -1);
                if (m->next) append_str(w, lines ? "," : ", ", lines ? 1 : 2);
            }
            if (lines) new_line(w, depth);
            append_str(w, lines ? "}" : " }", lines ? 1 : 2);
            break;
        }
    }
}
