/**
 * @file bmstore.c
 * @brief Portable store of the bookmarks of a project.
 *
 * Reads and writes \<project\>/.tinydev/bookmarks.json (with Scout's JSON
 * code) and finds the line of a bookmark again in a changed text. Used by
 * bookmarks.c; no Amiga headers, so the host tests can use it.
 */
#include "bmstore.h"
#include "json.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Fold a character for comparing file names (ASCII and Latin-1).
 * @param c The character.
 * @return Its lower-case form.
 */
static unsigned char fold(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return (unsigned char)(c + 32);
    return c;
}

int bmstore_same_file(const char *a, const char *b)
{
    while (*a != '\0' && fold((unsigned char)*a) == fold((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

int bmstore_compare_files(const char *a, const char *b)
{
    while (*a != '\0' && fold((unsigned char)*a) == fold((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return (int)fold((unsigned char)*a) - (int)fold((unsigned char)*b);
}

void bmstore_init(BmStore *s)
{
    s->entries = NULL;
    s->count = s->capacity = 0;
}

void bmstore_clear(BmStore *s)
{
    long i;
    for (i = 0; i < s->count; ++i) {
        free(s->entries[i].file);
        free(s->entries[i].text);
    }
    free(s->entries);
    bmstore_init(s);
}

/**
 * @brief Copy a string of a given length.
 * @param text The string (need not be terminated).
 * @param length Its length.
 * @return The copy (free() it), or NULL.
 */
static char *copy(const char *text, long length)
{
    char *c = malloc((size_t)length + 1);
    if (c == NULL) return NULL;
    memcpy(c, text, (size_t)length);
    c[length] = '\0';
    return c;
}

int bmstore_add(BmStore *s, const char *file, long line, const char *text, long length)
{
    BmEntry e;
    long i;
    if (line < 0) return 1;
    if (text == NULL) length = 0;
    if (length > BMSTORE_TEXT_MAX) length = BMSTORE_TEXT_MAX;
    e.line = line;
    e.file = copy(file, (long)strlen(file));
    e.text = copy(text != NULL ? text : "", length);
    if (e.file == NULL || e.text == NULL) {
        free(e.file);
        free(e.text);
        return 0;
    }
    for (i = 0; i < s->count; ++i)
        if (s->entries[i].line == line && bmstore_same_file(s->entries[i].file, file)) {
            free(s->entries[i].file);
            free(s->entries[i].text);
            s->entries[i] = e;
            return 1;
        }
    if (s->count == s->capacity) {
        long n = s->capacity ? s->capacity * 2 : 32;
        BmEntry *grown = realloc(s->entries, (size_t)n * sizeof(*grown));
        if (grown == NULL) {
            free(e.file);
            free(e.text);
            return 0;
        }
        s->entries = grown;
        s->capacity = n;
    }
    s->entries[s->count++] = e;
    return 1;
}

long bmstore_remove_file(BmStore *s, const char *file)
{
    long i, k = 0, removed = 0;
    for (i = 0; i < s->count; ++i) {
        if (bmstore_same_file(s->entries[i].file, file)) {
            free(s->entries[i].file);
            free(s->entries[i].text);
            ++removed;
        } else s->entries[k++] = s->entries[i];
    }
    s->count = k;
    return removed;
}

int bmstore_remove(BmStore *s, const char *file, long line)
{
    long i;
    for (i = 0; i < s->count; ++i)
        if (s->entries[i].line == line && bmstore_same_file(s->entries[i].file, file)) {
            free(s->entries[i].file);
            free(s->entries[i].text);
            s->entries[i] = s->entries[--s->count];
            return 1;
        }
    return 0;
}

int bmstore_parse(BmStore *s, const char *json)
{
    BmStore add;
    JsonValue *root = json_parse(json, NULL);
    const JsonValue *list;
    size_t i, n;
    int ok = 1;

    if (root == NULL || root->type != JSON_OBJECT ||
        (list = json_obj_get_array(root, "bookmarks")) == NULL) {
        json_value_free(root);
        return 0;
    }
    bmstore_init(&add);
    n = json_array_length(list);
    for (i = 0; i < n && ok; ++i) {
        const JsonValue *e = json_array_get(list, i);
        const char *file, *text;
        long line;
        if (e == NULL || e->type != JSON_OBJECT) continue;
        file = json_obj_get_string(e, "file", NULL);
        line = json_obj_get_int(e, "line", 0);
        text = json_obj_get_string(e, "text", "");
        if (file == NULL || file[0] == '\0' || line < 1) continue;
        ok = bmstore_add(&add, file, line - 1, text, (long)strlen(text));
    }
    json_value_free(root);
    for (i = 0; ok && i < (size_t)add.count; ++i) {
        BmEntry *e = &add.entries[i];
        ok = bmstore_add(s, e->file, e->line, e->text, (long)strlen(e->text));
    }
    bmstore_clear(&add);
    return ok;
}

/**
 * @brief qsort() order of the bookmarks: by file, then by line.
 * @param a A bookmark.
 * @param b Another bookmark.
 * @return <0, 0 or >0.
 */
static int order(const void *a, const void *b)
{
    const BmEntry *x = a, *y = b;
    int c = bmstore_compare_files(x->file, y->file);
    if (c != 0) return c;
    return x->line < y->line ? -1 : x->line > y->line;
}

char *bmstore_format(BmStore *s)
{
    JsonWriter w;
    char *result;
    long i;

    if (s->count > 1) qsort(s->entries, (size_t)s->count, sizeof(*s->entries), order);
    json_writer_init_alloc(&w, 256);
    json_write_raw(&w, "{\n  \"version\": 1,\n  \"bookmarks\": [");
    for (i = 0; i < s->count; ++i) {
        json_write_raw(&w, i > 0 ? ",\n    { " : "\n    { ");
        json_write_obj_key(&w, "file");
        json_write_raw(&w, " ");
        json_write_string(&w, s->entries[i].file);
        json_write_raw(&w, ", ");
        json_write_obj_key(&w, "line");
        json_write_raw(&w, " ");
        json_write_int(&w, s->entries[i].line + 1);
        json_write_raw(&w, ", ");
        json_write_obj_key(&w, "text");
        json_write_raw(&w, " ");
        json_write_string(&w, s->entries[i].text);
        json_write_raw(&w, " }");
    }
    json_write_raw(&w, s->count > 0 ? "\n  ]\n}\n" : "]\n}\n");
    if (w.error) {
        json_writer_free(&w);
        return NULL;
    }
    result = copy(json_writer_get(&w), (long)json_writer_length(&w));
    json_writer_free(&w);
    return result;
}

/**
 * @brief Tell whether a line has the text stored for a bookmark (which may
 *        be cut to BMSTORE_TEXT_MAX characters).
 * @param line The line (not terminated).
 * @param length Its length.
 * @param stored The stored text.
 * @return Non-zero if it matches.
 */
static int line_matches(const char *line, long length, const char *stored)
{
    long n = (long)strlen(stored);
    if (n < BMSTORE_TEXT_MAX && length != n) return 0;
    if (length < n) return 0;
    return memcmp(line, stored, (size_t)n) == 0;
}

long bmstore_relocate(const char *text, long size, long line, const char *line_text)
{
    long n = 0, offset = 0, end, best = -1, best_distance = 0, distance;
    int any = line_text == NULL || line_text[0] == '\0';
    for (;;) {
        end = offset;
        while (end < size && text[end] != '\n' && text[end] != '\r') ++end;
        if (!any && line_matches(text + offset, end - offset, line_text)) {
            distance = n > line ? n - line : line - n;
            if (best < 0 || distance < best_distance) {
                best = n;
                best_distance = distance;
            }
        }
        if (end >= size) break;
        offset = text[end] == '\r' && end + 1 < size && text[end + 1] == '\n' ? end + 2 : end + 1;
        ++n;
    }
    if (best >= 0) return best;
    return line > n ? n : line;
}
