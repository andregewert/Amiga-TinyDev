/**
 * @file appprefs.c
 * @brief Portable reader and writer of TinyDev's global settings.
 *
 * Reads and writes the "settings" of PROGDIR:TinyDev.json (with json.c);
 * the format is described in appprefs.h. No Amiga headers, so the host
 * tests can use it.
 */
#include "appprefs.h"
#include "json.h"

#include <stdlib.h>
#include <string.h>

/** Default colours of the highlighting, SettingsColour order: clearly
 *  different hues, each with a contrast of at least 4.5:1 to the editor's
 *  white background. */
const unsigned long settings_default_colours[SCOL_COUNT] = {
    0x000000,                       /* text: black */
    0x0030d0,                       /* keywords: blue (and bold) */
    0x008a00,                       /* strings: green */
    0x707070,                       /* comments: grey */
    0xb0006e,                       /* preprocessor: magenta (and bold) */
    0x00838f,                       /* types: teal */
    0x7030c0,                       /* functions: violet */
    0xc05000,                       /* macros: orange */
    0xd01010,                       /* constants and numbers: red */
    0x8a5a00,                       /* parameters and variables: brown */
};

/** Values of "toolbar"/"show", in the order of enum ToolbarMode. */
static const char *const tbmode_names[TBMODE_COUNT] = { "images", "both", "text" };

/** Keys of "colours", SettingsColour order. */
static const char *const colour_names[SCOL_COUNT] = {
    "text", "keyword", "string", "comment", "preprocessor",
    "type", "function", "macro", "constant", "variable"
};

/** Keys of "toolwindows": bookmark list, output window, symbol list, problem list. */
static const char *const toolwin_names[4] = { "bookmarks", "output", "symbols", "problems" };

/**
 * @brief Return the box and open flag of a tool window.
 * @param s The settings.
 * @param i 0 bookmarks, 1 output, 2 symbols, 3 problems.
 * @param[out] open The open flag.
 * @return The box (left, top, width, height).
 */
static long *toolwin(Settings *s, int i, int **open)
{
    switch (i) {
    case 0: *open = &s->bmlist; return s->bmwin;
    case 1: *open = &s->outlist; return s->outwin;
    case 2: *open = &s->symlist; return s->symwin;
    default: *open = &s->diaglist; return s->diagwin;
    }
}

void settings_default(Settings *s)
{
    int i, *open;
    memset(s, 0, sizeof(*s));
    s->tbmode = TBMODE_IMAGES;
    s->splash = 1;
    s->foldertree = 1;
    s->winleft = s->wintop = -1;
    for (i = 0; i < 4; ++i) {
        long *box = toolwin(s, i, &open);
        box[0] = box[1] = -1;
    }
    s->scout = 1;
    strcpy(s->tempdir, "T:");
    strcpy(s->shellwindow, "CON:");
    memcpy(s->colours, settings_default_colours, sizeof(s->colours));
}

/* -- reading --------------------------------------------------------------- */

/**
 * @brief Read a boolean member.
 * @param obj The object (may be NULL).
 * @param key The member's key.
 * @param[in,out] out The setting, unchanged if the member is missing or no boolean.
 */
static void get_bool(const JsonValue *obj, const char *key, int *out)
{
    const JsonValue *v = obj != NULL ? json_obj_get(obj, key) : NULL;
    if (v != NULL && v->type == JSON_BOOL) *out = v->u.bool_val != 0;
}

/**
 * @brief Read a number member.
 * @param obj The object (may be NULL).
 * @param key The member's key.
 * @param min The smallest allowed value.
 * @param[in,out] out The setting, unchanged if the member is missing, no
 *        number or below min.
 */
static void get_long(const JsonValue *obj, const char *key, long min, long *out)
{
    const JsonValue *v = obj != NULL ? json_obj_get(obj, key) : NULL;
    if (v != NULL && v->type == JSON_NUMBER && v->u.num_val.int_val >= min)
        *out = v->u.num_val.int_val;
}

/**
 * @brief Read a string member into a buffer (cut to its size).
 * @param obj The object.
 * @param key The member's key.
 * @param empty_ok Non-zero if "" is a valid value.
 * @param[out] out The setting, unchanged if the member is missing, no string,
 *        or empty and empty_ok is 0.
 * @param size Its size.
 */
static void get_string(const JsonValue *obj, const char *key, int empty_ok, char *out, size_t size)
{
    const JsonValue *v = json_obj_get(obj, key);
    if (v == NULL || v->type != JSON_STRING || (!empty_ok && v->u.str_val[0] == '\0')) return;
    strncpy(out, v->u.str_val, size - 1);
    out[size - 1] = '\0';
}

/**
 * @brief Value of a hex digit.
 * @param c The character.
 * @return 0..15, or -1.
 */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)(c | 0x20);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/**
 * @brief Parse "#RRGGBB" (the '#' may be left out).
 * @param v The text.
 * @param[out] rgb The colour.
 * @return Non-zero if it is a colour.
 */
static int parse_colour(const char *v, unsigned long *rgb)
{
    unsigned long c = 0;
    int i;
    if (*v == '#') ++v;
    for (i = 0; i < 6; ++i) {
        int d = hex_digit(v[i]);
        if (d < 0) return 0;
        c = c << 4 | (unsigned long)d;
    }
    if (v[6] != '\0') return 0;
    *rgb = c;
    return 1;
}

/**
 * @brief Read a tool window's entry.
 * @param e The entry (may be NULL).
 * @param[in,out] box Its box; a box is taken only as a whole and with a size.
 * @param[in,out] open Its open flag.
 */
static void get_toolwin(const JsonValue *e, long box[4], int *open)
{
    long n[4];
    int i;
    if (e == NULL) return;
    get_bool(e, "open", open);
    for (i = 0; i < 4; ++i) n[i] = box[i];
    get_long(e, "left", -1, &n[0]);
    get_long(e, "top", -1, &n[1]);
    get_long(e, "width", 0, &n[2]);
    get_long(e, "height", 0, &n[3]);
    if (n[2] > 0 && n[3] > 0) {
        for (i = 0; i < 4; ++i) box[i] = n[i];
    } else if (n[2] == 0) {                 /* automatic again */
        box[0] = box[1] = -1;
        box[2] = box[3] = 0;
    }
}

int appprefs_parse(Settings *s, const char *json)
{
    JsonValue *root = json_parse(json, NULL);
    const JsonValue *st, *o, *v;
    int i, *open;

    if (root == NULL || root->type != JSON_OBJECT) {
        json_value_free(root);
        return 0;
    }
    if ((st = json_obj_get_obj(root, "settings")) != NULL) {
        get_bool(st, "splash", &s->splash);
        get_bool(st, "scout", &s->scout);
        if ((o = json_obj_get_obj(st, "toolbar")) != NULL) {
            if ((v = json_obj_get(o, "show")) != NULL && v->type == JSON_STRING)
                for (i = 0; i < TBMODE_COUNT; ++i)
                    if (strcmp(v->u.str_val, tbmode_names[i]) == 0) s->tbmode = i;
            get_bool(o, "frames", &s->tbframes);
        }
        o = json_obj_get_obj(st, "window");
        get_long(o, "left", -1, &s->winleft);
        get_long(o, "top", -1, &s->wintop);
        get_long(o, "width", 0, &s->winwidth);
        get_long(o, "height", 0, &s->winheight);
        o = json_obj_get_obj(st, "view");
        get_bool(o, "foldertree", &s->foldertree);
        get_bool(o, "minimap", &s->minimap);
        get_bool(o, "showinfo", &s->showinfo);
        if ((o = json_obj_get_obj(st, "toolwindows")) != NULL)
            for (i = 0; i < 4; ++i) {
                long *box = toolwin(s, i, &open);
                get_toolwin(json_obj_get_obj(o, toolwin_names[i]), box, open);
            }
        get_string(st, "tempdir", 0, s->tempdir, sizeof(s->tempdir));
        get_string(st, "shellwindow", 0, s->shellwindow, sizeof(s->shellwindow));
        get_string(st, "projectdir", 1, s->projectdir, sizeof(s->projectdir));
        get_string(st, "author", 1, s->author, sizeof(s->author));
        get_string(st, "authormail", 1, s->authormail, sizeof(s->authormail));
        if ((o = json_obj_get_obj(st, "colours")) != NULL)
            for (i = 0; i < SCOL_COUNT; ++i)
                if ((v = json_obj_get(o, colour_names[i])) != NULL && v->type == JSON_STRING)
                    parse_colour(v->u.str_val, &s->colours[i]);
    }
    json_value_free(root);
    return 1;
}

/* -- writing --------------------------------------------------------------- */

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
 * @brief Write a box (left, top, width, height) into an object.
 * @param o The object.
 * @param box The box.
 * @return 1 on success, 0 if out of memory.
 */
static int set_box(JsonValue *o, const long box[4])
{
    return json_obj_set(o, "left", json_new_int(box[0])) &&
           json_obj_set(o, "top", json_new_int(box[1])) &&
           json_obj_set(o, "width", json_new_int(box[2])) &&
           json_obj_set(o, "height", json_new_int(box[3]));
}

/**
 * @brief Write the settings into the "settings" object, keeping its other keys.
 * @param st The object.
 * @param s The settings.
 * @return 1 on success, 0 if out of memory.
 */
static int write_settings(JsonValue *st, const Settings *s)
{
    Settings c = *s;                        /* toolwin() wants a changeable one */
    JsonValue *o, *tw;
    long win[4];
    int i, *open;
    char hex[8];

    if (!json_obj_set(st, "splash", json_new_bool(s->splash)) ||
        !json_obj_set(st, "scout", json_new_bool(s->scout)))
        return 0;
    if ((o = child_object(st, "toolbar")) == NULL ||
        !json_obj_set(o, "show", json_new_string(tbmode_names[s->tbmode >= 0 && s->tbmode < TBMODE_COUNT ? s->tbmode : 0])) ||
        !json_obj_set(o, "frames", json_new_bool(s->tbframes)))
        return 0;
    win[0] = s->winleft; win[1] = s->wintop; win[2] = s->winwidth; win[3] = s->winheight;
    if ((o = child_object(st, "window")) == NULL || !set_box(o, win)) return 0;
    if ((o = child_object(st, "view")) == NULL ||
        !json_obj_set(o, "foldertree", json_new_bool(s->foldertree)) ||
        !json_obj_set(o, "minimap", json_new_bool(s->minimap)) ||
        !json_obj_set(o, "showinfo", json_new_bool(s->showinfo)))
        return 0;
    if ((tw = child_object(st, "toolwindows")) == NULL) return 0;
    for (i = 0; i < 4; ++i) {
        long *box = toolwin(&c, i, &open);
        if ((o = child_object(tw, toolwin_names[i])) == NULL ||
            !json_obj_set(o, "open", json_new_bool(*open)) || !set_box(o, box))
            return 0;
    }
    if (!json_obj_set(st, "tempdir", json_new_string(s->tempdir)) ||
        !json_obj_set(st, "shellwindow", json_new_string(s->shellwindow)) ||
        !json_obj_set(st, "projectdir", json_new_string(s->projectdir)) ||
        !json_obj_set(st, "author", json_new_string(s->author)) ||
        !json_obj_set(st, "authormail", json_new_string(s->authormail)))
        return 0;
    if ((o = child_object(st, "colours")) == NULL) return 0;
    for (i = 0; i < SCOL_COUNT; ++i) {
        static const char digits[] = "0123456789ABCDEF";
        unsigned long rgb = s->colours[i];
        int k;
        hex[0] = '#';
        for (k = 0; k < 6; ++k) hex[1 + k] = digits[(rgb >> (20 - 4 * k)) & 15];
        hex[7] = '\0';
        if (!json_obj_set(o, colour_names[i], json_new_string(hex))) return 0;
    }
    return 1;
}

char *appprefs_format(const Settings *s, const char *old_json)
{
    JsonValue *root, *st;
    JsonWriter w;
    char *text = NULL;

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
    if ((st = child_object(root, "settings")) != NULL && write_settings(st, s)) {
        json_writer_init_alloc(&w, 1024);
        json_write_value(&w, root, 0);
        json_write_raw(&w, "\n");
        if (!w.error) text = copy(json_writer_get(&w));
        json_writer_free(&w);
    }
    json_value_free(root);
    return text;
}
