/**
 * @file filetypes.c
 * @brief Portable reader and writer of the settings per file type.
 *
 * Reads and writes the "filetypes" of PROGDIR:TinyDev.json (with json.c);
 * the format is described in filetypes.h. No Amiga headers, so the host
 * tests can use it.
 */
#include "filetypes.h"
#include "json.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Copy a string with malloc().
 * @param text The string.
 * @return The copy, or NULL if out of memory.
 */
static char *copy(const char *text)
{
    size_t n = strlen(text) + 1;
    char *c = malloc(n);
    if (c != NULL) memcpy(c, text, n);
    return c;
}

/**
 * @brief Bring a value into a range.
 * @param v The value.
 * @param lo The smallest value.
 * @param hi The largest value.
 * @return The value within [lo, hi].
 */
static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/**
 * @brief Bring settings into their ranges.
 * @param o The settings.
 */
static void normalise(FileTypeOptions *o)
{
    o->tab_size = clamp(o->tab_size, 1, FILETYPES_MAX_WIDTH);
    o->indent = clamp(o->indent, 0, FILETYPES_MAX_WIDTH);
    o->auto_indent = o->auto_indent != 0;
    o->tab_after = o->tab_after != 0;
    o->line_numbers = o->line_numbers != 0;
    o->cut_copy_line = o->cut_copy_line != 0;
}

void filetypes_defaults(FileTypeOptions *o)
{
    o->tab_size = 8;
    o->indent = 4;
    o->auto_indent = 1;
    o->tab_after = 1;
    o->line_numbers = 1;
    o->cut_copy_line = 0;
}

void filetypes_init(FileTypes *t)
{
    filetypes_defaults(&t->standard);
    t->entries = NULL;
    t->count = 0;
}

void filetypes_clear(FileTypes *t)
{
    long i;
    for (i = 0; i < t->count; ++i) free(t->entries[i].id);
    free(t->entries);
    filetypes_init(t);
}

int filetypes_copy(FileTypes *dst, const FileTypes *src)
{
    long i;
    filetypes_init(dst);
    dst->standard = src->standard;
    if (src->count == 0) return 1;
    if ((dst->entries = malloc((size_t)src->count * sizeof(FileTypeEntry))) == NULL) return 0;
    for (i = 0; i < src->count; ++i) {
        if ((dst->entries[i].id = copy(src->entries[i].id)) == NULL) {
            filetypes_clear(dst);
            return 0;
        }
        dst->entries[i].options = src->entries[i].options;
        dst->count = i + 1;
    }
    return 1;
}

/**
 * @brief Read the keys of an entry over settings; missing or wrong values
 *        leave theirs.
 * @param e The entry (a JSON object).
 * @param[in,out] o The settings.
 */
static void read_entry(const JsonValue *e, FileTypeOptions *o)
{
    const JsonValue *v;
    if ((v = json_obj_get(e, "tabsize")) != NULL && v->type == JSON_NUMBER &&
        v->u.num_val.int_val >= 1 && v->u.num_val.int_val <= FILETYPES_MAX_WIDTH)
        o->tab_size = (int)v->u.num_val.int_val;
    if ((v = json_obj_get(e, "indent")) != NULL) {
        if (v->type == JSON_NUMBER && v->u.num_val.int_val >= 1 &&
            v->u.num_val.int_val <= FILETYPES_MAX_WIDTH)
            o->indent = (int)v->u.num_val.int_val;
        else if (v->type == JSON_STRING && strcmp(v->u.str_val, "tab") == 0)
            o->indent = 0;
    }
    if ((v = json_obj_get(e, "autoindent")) != NULL && v->type == JSON_BOOL)
        o->auto_indent = v->u.bool_val;
    if ((v = json_obj_get(e, "tabkey")) != NULL && v->type == JSON_STRING) {
        if (strcmp(v->u.str_val, "line") == 0) o->tab_after = 0;
        else if (strcmp(v->u.str_val, "after") == 0) o->tab_after = 1;
    }
    if ((v = json_obj_get(e, "linenumbers")) != NULL && v->type == JSON_BOOL)
        o->line_numbers = v->u.bool_val;
    if ((v = json_obj_get(e, "cutcopyline")) != NULL && v->type == JSON_BOOL)
        o->cut_copy_line = v->u.bool_val;
}

int filetypes_parse(FileTypes *t, const char *json)
{
    JsonValue *root = json_parse(json, NULL);
    const JsonValue *ft, *e, *langs;
    const JsonMember *m;
    FileTypes n;
    int rc = 1;

    if (root == NULL || root->type != JSON_OBJECT) {
        json_value_free(root);
        return 0;
    }
    filetypes_init(&n);
    if ((ft = json_obj_get_obj(root, "filetypes")) != NULL) {
        if ((e = json_obj_get_obj(ft, "default")) != NULL) read_entry(e, &n.standard);
        if ((langs = json_obj_get_obj(ft, "languages")) != NULL)
            for (m = langs->u.obj_val.first; m != NULL && rc > 0; m = m->next) {
                FileTypeOptions o = n.standard;
                if (m->value->type != JSON_OBJECT || m->key[0] == '\0') continue;
                read_entry(m->value, &o);
                if (filetypes_set(&n, m->key, &o) < 0) rc = -1;
            }
    }
    json_value_free(root);
    filetypes_clear(t);
    if (rc > 0) *t = n;
    else filetypes_clear(&n);
    return rc;
}

/**
 * @brief Return the index of a language's entry.
 * @param t The settings.
 * @param id The language's identifier.
 * @return The index, or -1.
 */
static long find_index(const FileTypes *t, const char *id)
{
    long i;
    for (i = 0; i < t->count; ++i)
        if (strcmp(t->entries[i].id, id) == 0) return i;
    return -1;
}

const FileTypeOptions *filetypes_find(const FileTypes *t, const char *id)
{
    long i = id != NULL ? find_index(t, id) : -1;
    return i >= 0 ? &t->entries[i].options : NULL;
}

const FileTypeOptions *filetypes_get(const FileTypes *t, const char *id)
{
    const FileTypeOptions *o = id != NULL && id[0] != '\0' ? filetypes_find(t, id) : NULL;
    return o != NULL ? o : &t->standard;
}

int filetypes_set(FileTypes *t, const char *id, const FileTypeOptions *o)
{
    FileTypeEntry *grown;
    long i;
    if (id == NULL || id[0] == '\0') {
        if (o != NULL) {
            t->standard = *o;
            normalise(&t->standard);
        }
        return 1;
    }
    i = find_index(t, id);
    if (o == NULL) {
        if (i >= 0) {
            free(t->entries[i].id);
            memmove(&t->entries[i], &t->entries[i + 1], (size_t)(t->count - i - 1) * sizeof(FileTypeEntry));
            --t->count;
        }
        return 1;
    }
    if (i < 0) {
        char *c = copy(id);
        if (c == NULL) return -1;
        grown = realloc(t->entries, (size_t)(t->count + 1) * sizeof(FileTypeEntry));
        if (grown == NULL) {
            free(c);
            return -1;
        }
        t->entries = grown;
        i = t->count++;
        t->entries[i].id = c;
    }
    t->entries[i].options = *o;
    normalise(&t->entries[i].options);
    return 1;
}

/**
 * @brief Return a member of an object for changing it, made as an empty
 *        object if it is missing or no object.
 * @param obj The object.
 * @param key The member's key.
 * @return The member, or NULL if out of memory.
 */
static JsonValue *child_object(JsonValue *obj, const char *key)
{
    const JsonValue *v = json_obj_get(obj, key);
    if (v != NULL && v->type == JSON_OBJECT) return (JsonValue *)v;
    if (!json_obj_set(obj, key, json_new_object())) return NULL;
    return (JsonValue *)json_obj_get(obj, key);
}

/**
 * @brief Write settings into an entry, keeping its other keys.
 * @param e The entry (a JSON object).
 * @param o The settings.
 * @return 1 on success, 0 if out of memory.
 */
static int write_entry(JsonValue *e, const FileTypeOptions *o)
{
    return json_obj_set(e, "tabsize", json_new_int(o->tab_size)) &&
           json_obj_set(e, "indent", o->indent > 0 ? json_new_int(o->indent) : json_new_string("tab")) &&
           json_obj_set(e, "autoindent", json_new_bool(o->auto_indent)) &&
           json_obj_set(e, "tabkey", json_new_string(o->tab_after ? "after" : "line")) &&
           json_obj_set(e, "linenumbers", json_new_bool(o->line_numbers)) &&
           json_obj_set(e, "cutcopyline", json_new_bool(o->cut_copy_line));
}

char *filetypes_format(const FileTypes *t, const char *old_json)
{
    JsonValue *root, *ft, *langs, *e;
    JsonMember *m;
    JsonWriter w;
    char *text = NULL;
    int ok;
    long i;

    if (old_json != NULL && old_json[0] != '\0') {
        root = json_parse(old_json, NULL);
        if (root != NULL && root->type != JSON_OBJECT) {
            json_value_free(root);
            return NULL;
        }
    } else {
        root = json_new_object();
        if (root != NULL && !json_obj_set(root, "version", json_new_int(1))) {
            json_value_free(root);
            root = NULL;
        }
    }
    if (root == NULL) return NULL;

    ok = (ft = child_object(root, "filetypes")) != NULL &&
         (e = child_object(ft, "default")) != NULL && write_entry(e, &t->standard) &&
         (langs = child_object(ft, "languages")) != NULL;
    if (ok) {
        /* languages without settings of their own lose their entries */
        for (m = langs->u.obj_val.first; m != NULL; ) {
            JsonMember *next = m->next;
            if (find_index(t, m->key) < 0) json_obj_remove(langs, m->key);
            m = next;
        }
        for (i = 0; ok && i < t->count; ++i)
            ok = (e = child_object(langs, t->entries[i].id)) != NULL &&
                 write_entry(e, &t->entries[i].options);
    }
    if (ok) {
        json_writer_init_alloc(&w, 512);
        json_write_value(&w, root, 0);
        json_write_raw(&w, "\n");
        if (!w.error) text = copy(json_writer_get(&w));
        json_writer_free(&w);
    }
    json_value_free(root);
    return text;
}
