/**
 * @file json.h
 * @brief JSON values, reader and writer (portable, ISO-8859-1 strings kept as
 *        they are).
 */
#ifndef TINYDEV_JSON_H
#define TINYDEV_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Type of a JSON value. */
typedef enum JsonType {
    JSON_NULL = 0,    /**< null */
    JSON_BOOL,        /**< true or false (JsonValue::u bool_val) */
    JSON_NUMBER,      /**< a number (num_val) */
    JSON_STRING,      /**< a string (str_val) */
    JSON_ARRAY,       /**< an array (array_val) */
    JSON_OBJECT       /**< an object (obj_val) */
} JsonType;

/** A JSON value (a node of the tree json_parse() builds). */
typedef struct JsonValue JsonValue;

/** A member of an object: key and value (a node of a singly linked list). */
typedef struct JsonMember {
    char *key;                      /**< the key (allocated) */
    JsonValue *value;          /**< the value (owned) */
    struct JsonMember *next;   /**< the next member, NULL at the end */
} JsonMember;

/** An element of an array (a node of a singly linked list). */
typedef struct JsonElement {
    JsonValue *value;          /**< the value (owned) */
    struct JsonElement *next;  /**< the next element, NULL at the end */
} JsonElement;

/** A JSON value; the member of #u that is used depends on #type. */
struct JsonValue {
    JsonType type;             /**< which kind of value it is */
    /** The value itself. */
    union {
        int bool_val;               /**< JSON_BOOL: 0 or 1 */
        /** JSON_NUMBER */
        struct {
            long int_val;           /**< the number as an integer */
            double double_val;      /**< as a floating point number (host only; 0 on the Amiga) */
            int is_float;           /**< it had a fraction or an exponent */
            char *text;             /**< the number as read (allocated; NULL if made with json_new_int()), written back as it is */
        } num_val;
        char *str_val;              /**< JSON_STRING: the string (allocated, 8-bit as read) */
        /** JSON_ARRAY */
        struct {
            JsonElement *first;    /**< first element */
            JsonElement *last;     /**< last element */
            size_t count;               /**< number of elements */
        } array_val;
        /** JSON_OBJECT */
        struct {
            JsonMember *first;     /**< first member */
            JsonMember *last;      /**< last member */
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
 * @return The root value (free it with json_value_free()), or NULL
 *         on a syntax error or trailing text.
 */
JsonValue *json_parse(const char *json_str, const char **error_pos);

/**
 * @brief Free a value and everything below it.
 * @param val The value (may be NULL).
 */
void json_value_free(JsonValue *val);

/**
 * @brief Return the value of an object's member.
 * @param obj The object (another value or NULL gives NULL).
 * @param key The key.
 * @return The value (owned by @p obj), or NULL if there is none.
 */
const JsonValue *json_obj_get(const JsonValue *obj, const char *key);
/**
 * @brief Return an object's member as a string.
 * @param obj The object.
 * @param key The key.
 * @param default_val Returned if the member is missing or not a string.
 * @return The string (owned by @p obj) or @p default_val.
 */
const char *json_obj_get_string(const JsonValue *obj, const char *key, const char *default_val);
/**
 * @brief Return an object's member as an integer.
 * @param obj The object.
 * @param key The key.
 * @param default_val Returned if the member is missing or not a number.
 * @return The number (its integer part) or @p default_val.
 */
long json_obj_get_int(const JsonValue *obj, const char *key, long default_val);
/**
 * @brief Return an object's member as a truth value.
 * @param obj The object.
 * @param key The key.
 * @param default_val Returned if the member is missing or not true/false.
 * @return 1, 0 or @p default_val.
 */
int json_obj_get_bool(const JsonValue *obj, const char *key, int default_val);
/**
 * @brief Return an object's member if it is an object.
 * @param obj The object.
 * @param key The key.
 * @return The object (owned by @p obj), or NULL.
 */
const JsonValue *json_obj_get_obj(const JsonValue *obj, const char *key);
/**
 * @brief Return an object's member if it is an array.
 * @param obj The object.
 * @param key The key.
 * @return The array (owned by @p obj), or NULL.
 */
const JsonValue *json_obj_get_array(const JsonValue *obj, const char *key);

/**
 * @brief Return the number of elements of an array.
 * @param arr The array (another value or NULL gives 0).
 * @return The number of elements.
 */
size_t json_array_length(const JsonValue *arr);
/**
 * @brief Return an element of an array (walks the list).
 * @param arr The array.
 * @param index The element's index, from 0.
 * @return The element (owned by @p arr), or NULL if there is none.
 */
const JsonValue *json_array_get(const JsonValue *arr, size_t index);

/**
 * @brief Make an empty object.
 * @return The value (free it with json_value_free()), or NULL if out of memory.
 */
JsonValue *json_new_object(void);
/**
 * @brief Make an empty array.
 * @return The value (free it with json_value_free()), or NULL if out of memory.
 */
JsonValue *json_new_array(void);
/**
 * @brief Make a truth value.
 * @param val The truth value.
 * @return The value (free it with json_value_free()), or NULL if out of memory.
 */
JsonValue *json_new_bool(int val);
/**
 * @brief Make an integer.
 * @param val The number.
 * @return The value (free it with json_value_free()), or NULL if out of memory.
 */
JsonValue *json_new_int(long val);
/**
 * @brief Make a string (copied).
 * @param str The string (NULL gives NULL).
 * @return The value (free it with json_value_free()), or NULL if out of memory.
 */
JsonValue *json_new_string(const char *str);

/**
 * @brief Append a value to an array.
 *
 * Takes the value over: it belongs to the array, or is freed if it cannot
 * be added. A NULL value (a failed json_new_...()) fails, so calls can be
 * nested.
 *
 * @param arr The array.
 * @param val The value.
 * @return 1 on success, 0 if out of memory or @p arr is no array.
 */
int json_array_append(JsonValue *arr, JsonValue *val);
/**
 * @brief Set a member of an object: an existing one gets the new value in
 *        its place, otherwise the member is added at the end.
 *
 * Takes the value over like json_array_append().
 *
 * @param obj The object.
 * @param key The key (copied).
 * @param val The value.
 * @return 1 on success, 0 if out of memory or @p obj is no object.
 */
int json_obj_set(JsonValue *obj, const char *key, JsonValue *val);
/**
 * @brief Remove a member of an object and free its value.
 * @param obj The object.
 * @param key The key.
 * @return 1 if it was there, 0 otherwise.
 */
int json_obj_remove(JsonValue *obj, const char *key);

/**
 * A writer that builds JSON text in a buffer, either a fixed one or one
 * that grows. Commas and spacing are written by the caller
 * (json_write_raw()).
 */
typedef struct JsonWriter {
    char *buffer;       /**< the text, always NUL-terminated */
    size_t capacity;    /**< size of the buffer */
    size_t length;      /**< length of the text */
    int is_dynamic;     /**< the buffer is allocated and grows */
    int error;          /**< the text did not fit or memory ran out; writing stops */
    int need_comma;     /**< reserved (not used by the writer itself) */
} JsonWriter;

/**
 * @brief Start writing into a fixed buffer.
 * @param w The writer.
 * @param buf The buffer.
 * @param cap Its size; text that does not fit sets JsonWriter::error.
 */
void json_writer_init(JsonWriter *w, char *buf, size_t cap);
/**
 * @brief Start writing into an allocated buffer that grows.
 * @param w The writer (free it with json_writer_free()).
 * @param initial_cap First size of the buffer (at least 64).
 */
void json_writer_init_alloc(JsonWriter *w, size_t initial_cap);
/**
 * @brief Free an allocated buffer and empty the writer.
 * @param w The writer.
 */
void json_writer_free(JsonWriter *w);
/**
 * @brief Return the text written so far.
 * @param w The writer.
 * @return The text (owned by the writer), "" if there is none.
 */
const char *json_writer_get(const JsonWriter *w);
/**
 * @brief Return the length of the text written so far.
 * @param w The writer.
 * @return The length.
 */
size_t json_writer_length(const JsonWriter *w);

/**
 * @brief Write null.
 * @param w The writer.
 */
void json_write_null(JsonWriter *w);
/**
 * @brief Write true or false.
 * @param w The writer.
 * @param val The truth value.
 */
void json_write_bool(JsonWriter *w, int val);
/**
 * @brief Write an integer.
 * @param w The writer.
 * @param val The number.
 */
void json_write_int(JsonWriter *w, long val);
/**
 * @brief Write a string in quotes, escaping quotes, backslashes and
 *        control characters; bytes above 127 are written as they are.
 * @param w The writer.
 * @param str The string (NULL writes null).
 */
void json_write_string(JsonWriter *w, const char *str);
/**
 * @brief Write text as it is (commas, spacing, prepared JSON).
 * @param w The writer.
 * @param raw The text (NULL writes nothing).
 */
void json_write_raw(JsonWriter *w, const char *raw);

/**
 * @brief Write a value and everything below it.
 *
 * Indented (for files people may edit): objects and arrays one member per
 * line, two spaces per depth, except below the top those that hold no
 * objects (and no arrays that do), which go on one line, e.g.
 * { "name": "Build", "command": "make" }. Numbers are written as they
 * were read; no newline at the end.
 *
 * @param w The writer.
 * @param v The value (NULL writes null).
 * @param depth The depth the value starts at (0 for the top); a negative
 *              depth writes everything on one line.
 */
void json_write_value(JsonWriter *w, const JsonValue *v, int depth);

/**
 * @brief Write the start of an object ("{").
 * @param w The writer.
 */
void json_write_obj_start(JsonWriter *w);
/**
 * @brief Write a member's key and the colon.
 * @param w The writer.
 * @param key The key.
 */
void json_write_obj_key(JsonWriter *w, const char *key);
/**
 * @brief Write the end of an object ("}").
 * @param w The writer.
 */
void json_write_obj_end(JsonWriter *w);

/**
 * @brief Write the start of an array ("[").
 * @param w The writer.
 */
void json_write_arr_start(JsonWriter *w);
/**
 * @brief Write the end of an array ("]").
 * @param w The writer.
 */
void json_write_arr_end(JsonWriter *w);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_JSON_H */
