/**
 * @file sprout_tmpl.c
 * @brief Portable core of Sprout: template metadata, values of the
 *        placeholders and their replacement.
 *
 * No Amiga headers (host-tested in tests/test_sprout_tmpl.c). Strings are
 * 8-bit as read (ISO-8859-1); upper and lower case follow ISO-8859-1.
 */
#include "sprout_tmpl.h"
#include "json.h"

#include <stdlib.h>
#include <string.h>

/** The built-in keys, which a template cannot define. */
static const char *const builtin_keys[] = { "PROJECT", "DRAWER", "DATE", "YEAR", NULL };
/** The standard keys: always defined, a template may declare them. */
static const char *const standard_keys[] = { "AUTHOR", "AUTHORMAIL", NULL };

/**
 * @brief Pass a message of up to three parts to the report callback.
 * @param report The callback (may be NULL).
 * @param user Passed to it.
 * @param error Non-zero for an error.
 * @param a First part.
 * @param b Second part (may be NULL).
 * @param c Third part (may be NULL).
 */
static void say(SproutReport report, void *user, int error, const char *a, const char *b, const char *c)
{
    char text[256];
    size_t n = 0;
    const char *parts[3];
    int i;
    if (report == NULL) return;
    parts[0] = a;
    parts[1] = b;
    parts[2] = c;
    for (i = 0; i < 3; ++i) {
        const char *p = parts[i];
        if (p == NULL) continue;
        while (*p != '\0' && n < sizeof(text) - 1) text[n++] = *p++;
    }
    text[n] = '\0';
    report(user, error, text);
}

/**
 * @brief Copy a string with malloc().
 * @param s The string.
 * @return The copy, or NULL if out of memory.
 */
static char *str_dup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d != NULL) memcpy(d, s, n);
    return d;
}

/**
 * @brief Copy an optional string member of a JSON object.
 * @param obj The object.
 * @param key The member's key.
 * @param[out] out The copy, or NULL if the member is missing or not a string.
 * @return 1 on success (also when missing); -1 if out of memory.
 */
static int dup_member(const JsonValue *obj, const char *key, char **out)
{
    const char *s = json_obj_get_string(obj, key, NULL);
    *out = NULL;
    if (s == NULL) return 1;
    if ((*out = str_dup(s)) == NULL) return -1;
    return 1;
}

/** The longest language name looked for in "key[language]". */
#define LANGUAGE_MAX 32

/**
 * @brief Find the text of a member that may be translated: "key[language]"
 *        for the first of the languages that has one, else "key".
 * @param obj The object.
 * @param key The member's key (short: at most 16 characters).
 * @param languages The languages, NULL-terminated (may be NULL).
 * @return The text, or NULL if there is none.
 */
static const char *localized(const JsonValue *obj, const char *key, const char *const *languages)
{
    char name[16 + LANGUAGE_MAX + 3];
    size_t klen = strlen(key), i, j;
    const char *s;
    for (i = 0; languages != NULL && languages[i] != NULL; ++i) {
        size_t llen = strlen(languages[i]);
        if (klen > 16 || llen == 0 || llen > LANGUAGE_MAX) continue;
        memcpy(name, key, klen);
        name[klen] = '[';
        /* the locale names its languages in lower case; a name given in
         * another case is matched all the same (ASCII and Latin-1) */
        for (j = 0; j < llen; ++j) {
            unsigned char ch = (unsigned char)languages[i][j];
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 0xC0 && ch <= 0xDE && ch != 0xD7)) ch = (unsigned char)(ch + 32);
            name[klen + 1 + j] = (char)ch;
        }
        name[klen + 1 + llen] = ']';
        name[klen + 2 + llen] = '\0';
        if ((s = json_obj_get_string(obj, name, NULL)) != NULL) return s;
    }
    return json_obj_get_string(obj, key, NULL);
}

/**
 * @brief Copy an optional member that may be translated (see localized()).
 * @param obj The object.
 * @param key The member's key.
 * @param languages The languages, NULL-terminated (may be NULL).
 * @param[out] out The copy, or NULL if there is no text.
 * @return 1 on success (also when missing); -1 if out of memory.
 */
static int dup_localized(const JsonValue *obj, const char *key, const char *const *languages, char **out)
{
    const char *s = localized(obj, key, languages);
    *out = NULL;
    if (s == NULL) return 1;
    if ((*out = str_dup(s)) == NULL) return -1;
    return 1;
}

/**
 * @brief Free an array of malloc()ed strings.
 * @param list The array (may be NULL).
 * @param n Number of strings.
 */
static void free_list(char **list, int n)
{
    int i;
    if (list == NULL) return;
    for (i = 0; i < n; ++i) free(list[i]);
    free(list);
}

/**
 * @brief Read a member that is a string or an array of strings into a list.
 * @param obj The object.
 * @param key The member's key.
 * @param[out] list The strings (NULL if none).
 * @param[out] n Their number.
 * @return 1 on success; 0 if the member has another type or an element is
 *         not a string; -1 if out of memory.
 */
static int string_list(const JsonValue *obj, const char *key, char ***list, int *n)
{
    const JsonValue *v = json_obj_get(obj, key);
    size_t i, count;
    *list = NULL;
    *n = 0;
    if (v == NULL) return 1;
    if (v->type == JSON_STRING) {
        if ((*list = (char **)malloc(sizeof(char *))) == NULL) return -1;
        if (((*list)[0] = str_dup(v->u.str_val)) == NULL) {
            free(*list);
            *list = NULL;
            return -1;
        }
        *n = 1;
        return 1;
    }
    if (v->type != JSON_ARRAY) return 0;
    count = json_array_length(v);
    if (count == 0) return 1;
    if ((*list = (char **)malloc(count * sizeof(char *))) == NULL) return -1;
    for (i = 0; i < count; ++i) {
        const JsonValue *e = json_array_get(v, i);
        if (e == NULL || e->type != JSON_STRING) return 0;
        if (((*list)[i] = str_dup(e->u.str_val)) == NULL) return -1;
        ++*n;
    }
    return 1;
}

int sprout_key_valid(const char *key, size_t len)
{
    size_t i;
    if (len == 0 || key[0] < 'A' || key[0] > 'Z') return 0;
    for (i = 1; i < len; ++i) {
        char c = key[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}

int sprout_key_standard(const char *key)
{
    int i;
    for (i = 0; standard_keys[i] != NULL; ++i)
        if (strcmp(key, standard_keys[i]) == 0) return 1;
    return 0;
}

int sprout_key_builtin(const char *key)
{
    int i;
    for (i = 0; builtin_keys[i] != NULL; ++i)
        if (strcmp(key, builtin_keys[i]) == 0) return 1;
    return 0;
}

/**
 * @brief Read one entry of "placeholders".
 * @param t The template so far (for duplicate keys).
 * @param e The entry.
 * @param[out] p The placeholder (zeroed first).
 * @param languages The user's languages, NULL-terminated (may be NULL).
 * @param report Receives the reason when it is not valid.
 * @param user Passed to @p report.
 * @return 1 on success; 0 if not valid; -1 if out of memory.
 */
static int parse_placeholder(const SproutTemplate *t, const JsonValue *e, SproutPlaceholder *p,
                             const char *const *languages, SproutReport report, void *user)
{
    const char *key;
    int i, r;
    memset(p, 0, sizeof(*p));
    if (e == NULL || e->type != JSON_OBJECT) {
        say(report, user, 1, "a placeholder is not an object", NULL, NULL);
        return 0;
    }
    key = json_obj_get_string(e, "key", NULL);
    if (key == NULL || !sprout_key_valid(key, strlen(key))) {
        say(report, user, 1, "placeholder without a valid key: ", key != NULL ? key : "", NULL);
        return 0;
    }
    if (sprout_key_builtin(key)) {
        say(report, user, 1, "placeholder with a built-in key: ", key, NULL);
        return 0;
    }
    for (i = 0; i < t->nph; ++i)
        if (strcmp(t->ph[i].key, key) == 0) {
            say(report, user, 1, "placeholder defined twice: ", key, NULL);
            return 0;
        }
    if ((p->key = str_dup(key)) == NULL) return -1;
    if (dup_localized(e, "label", languages, &p->label) < 0 ||
        dup_localized(e, "description", languages, &p->description) < 0 ||
        dup_localized(e, "default", languages, &p->def) < 0)
        return -1;
    p->required = json_obj_get_bool(e, "required", 0);
    if ((r = string_list(e, "choices", &p->choices, &p->nchoices)) <= 0) {
        if (r == 0) say(report, user, 1, "choices of ", key, " are not strings");
        return r;
    }
    return 1;
}

/**
 * @brief Free the strings of a placeholder.
 * @param p The placeholder.
 */
static void free_placeholder(SproutPlaceholder *p)
{
    free(p->key);
    free(p->label);
    free(p->description);
    free(p->def);
    free_list(p->choices, p->nchoices);
}

int sprout_template_parse(SproutTemplate *t, const char *json, const char *const *languages,
                          SproutReport report, void *user)
{
    JsonValue *root = json_parse(json, NULL);
    int r;
    if (root == NULL || root->type != JSON_OBJECT) {
        memset(t, 0, sizeof(*t));
        say(report, user, 1, "template.json is not a JSON object", NULL, NULL);
        json_value_free(root);
        return 0;
    }
    r = sprout_template_from_json(t, root, languages, report, user);
    json_value_free(root);
    return r;
}

int sprout_template_from_json(SproutTemplate *t, const struct JsonValue *root, const char *const *languages,
                              SproutReport report, void *user)
{
    const JsonValue *icon, *phs;
    const char *kind, *name;
    int r = 1;
    long version;

    memset(t, 0, sizeof(*t));
    if (root == NULL || root->type != JSON_OBJECT) {
        say(report, user, 1, "template.json is not a JSON object", NULL, NULL);
        return 0;
    }
    version = json_obj_get_int(root, "version", 1);
    kind = json_obj_get_string(root, "kind", "project");
    name = localized(root, "name", languages);
    if (version != 1) {
        say(report, user, 1, "unknown version of template.json", NULL, NULL);
        r = 0;
    } else if (strcmp(kind, "project") != 0 && strcmp(kind, "file") != 0) {
        say(report, user, 1, "unknown kind of template: ", kind, NULL);
        r = 0;
    } else if (name == NULL || name[0] == '\0') {
        say(report, user, 1, "template without a name", NULL, NULL);
        r = 0;
    }
    if (r > 0) {
        if ((t->kind = str_dup(kind)) == NULL || (t->name = str_dup(name)) == NULL ||
            dup_localized(root, "description", languages, &t->description) < 0 ||
            dup_localized(root, "category", languages, &t->category) < 0)
            r = -1;
        else if (t->category == NULL && (t->category = str_dup("Other")) == NULL)
            r = -1;
    }
    if (r > 0 && (icon = json_obj_get_obj(root, "icon")) != NULL) {
        if (dup_member(icon, "type", &t->icon_type) < 0 || dup_member(icon, "file", &t->icon_file) < 0)
            r = -1;
    }
    if (r > 0 && (r = string_list(root, "open", &t->open, &t->nopen)) == 0)
        say(report, user, 1, "\"open\" is not a list of strings", NULL, NULL);
    if (r > 0 && (r = string_list(root, "verbatim", &t->verbatim, &t->nverbatim)) == 0)
        say(report, user, 1, "\"verbatim\" is not a list of strings", NULL, NULL);
    if (r > 0 && (phs = json_obj_get(root, "placeholders")) != NULL) {
        size_t i, count;
        if (phs->type != JSON_ARRAY) {
            say(report, user, 1, "\"placeholders\" is not a list", NULL, NULL);
            r = 0;
        } else if ((count = json_array_length(phs)) > 0) {
            if ((t->ph = (SproutPlaceholder *)malloc(count * sizeof(SproutPlaceholder))) == NULL) r = -1;
            for (i = 0; r > 0 && i < count; ++i) {
                r = parse_placeholder(t, json_array_get(phs, i), &t->ph[t->nph], languages, report, user);
                ++t->nph;           /* freed with the rest, also when it failed */
            }
        }
    }
    if (r <= 0) sprout_template_free(t);
    return r;
}

/**
 * @brief Set a string member if the string is there.
 * @param obj The object.
 * @param key The key.
 * @param s The string (NULL: nothing is set).
 * @return 1 on success, 0 if out of memory.
 */
static int set_string(JsonValue *obj, const char *key, const char *s)
{
    return s == NULL || json_obj_set(obj, key, json_new_string(s));
}

/**
 * @brief Set a member that is a list of strings, if there are any.
 * @param obj The object.
 * @param key The key.
 * @param list The strings.
 * @param n Their number (0: nothing is set).
 * @return 1 on success, 0 if out of memory.
 */
static int set_list(JsonValue *obj, const char *key, char *const *list, int n)
{
    JsonValue *arr;
    int i;
    if (n == 0) return 1;
    if ((arr = json_new_array()) == NULL) return 0;
    for (i = 0; i < n; ++i)
        if (!json_array_append(arr, json_new_string(list[i]))) {
            json_value_free(arr);
            return 0;
        }
    return json_obj_set(obj, key, arr);
}

struct JsonValue *sprout_template_to_json(const SproutTemplate *t)
{
    JsonValue *root = json_new_object(), *icon = NULL, *phs = NULL;
    int ok, i;
    if (root == NULL) return NULL;
    ok = json_obj_set(root, "version", json_new_int(1)) && set_string(root, "kind", t->kind) &&
         set_string(root, "name", t->name) && set_string(root, "description", t->description) &&
         set_string(root, "category", t->category);
    if (ok && (t->icon_type != NULL || t->icon_file != NULL)) {
        ok = (icon = json_new_object()) != NULL && set_string(icon, "type", t->icon_type) &&
             set_string(icon, "file", t->icon_file);
        if (ok) ok = json_obj_set(root, "icon", icon);
        else json_value_free(icon);
    }
    if (ok && t->nph > 0) {
        ok = (phs = json_new_array()) != NULL;
        for (i = 0; ok && i < t->nph; ++i) {
            const SproutPlaceholder *p = &t->ph[i];
            JsonValue *o = json_new_object();
            ok = o != NULL && set_string(o, "key", p->key) && set_string(o, "label", p->label) &&
                 set_string(o, "description", p->description) && set_string(o, "default", p->def) &&
                 (!p->required || json_obj_set(o, "required", json_new_bool(1))) &&
                 set_list(o, "choices", p->choices, p->nchoices);
            if (ok) ok = json_array_append(phs, o);
            else json_value_free(o);
        }
        if (ok) ok = json_obj_set(root, "placeholders", phs);
        else json_value_free(phs);
    }
    if (ok) ok = set_list(root, "open", t->open, t->nopen) && set_list(root, "verbatim", t->verbatim, t->nverbatim);
    if (!ok) {
        json_value_free(root);
        return NULL;
    }
    return root;
}

void sprout_template_free(SproutTemplate *t)
{
    int i;
    free(t->kind);
    free(t->name);
    free(t->description);
    free(t->category);
    free(t->icon_type);
    free(t->icon_file);
    for (i = 0; i < t->nph; ++i) free_placeholder(&t->ph[i]);
    free(t->ph);
    free_list(t->open, t->nopen);
    free_list(t->verbatim, t->nverbatim);
    memset(t, 0, sizeof(*t));
}

void sprout_values_init(SproutValues *v)
{
    memset(v, 0, sizeof(*v));
}

void sprout_values_free(SproutValues *v)
{
    free_list(v->keys, v->count);
    free_list(v->vals, v->count);
    memset(v, 0, sizeof(*v));
}

int sprout_values_set(SproutValues *v, const char *key, const char *val)
{
    char *copy;
    int i;
    if ((copy = str_dup(val)) == NULL) return -1;
    for (i = 0; i < v->count; ++i)
        if (strcmp(v->keys[i], key) == 0) {
            free(v->vals[i]);
            v->vals[i] = copy;
            return 1;
        }
    if (v->count == v->cap) {
        int cap = v->cap > 0 ? v->cap * 2 : 8;
        char **keys = (char **)realloc(v->keys, (size_t)cap * sizeof(char *));
        char **vals;
        if (keys != NULL) v->keys = keys;
        vals = keys != NULL ? (char **)realloc(v->vals, (size_t)cap * sizeof(char *)) : NULL;
        if (vals == NULL) {
            free(copy);
            return -1;
        }
        v->vals = vals;
        v->cap = cap;
    }
    if ((v->keys[v->count] = str_dup(key)) == NULL) {
        free(copy);
        return -1;
    }
    v->vals[v->count++] = copy;
    return 1;
}

const char *sprout_values_get(const SproutValues *v, const char *key)
{
    int i;
    for (i = 0; i < v->count; ++i)
        if (strcmp(v->keys[i], key) == 0) return v->vals[i];
    return NULL;
}

int sprout_values_parse_json(SproutValues *v, const char *json)
{
    JsonValue *root = json_parse(json, NULL);
    const JsonMember *m;
    int r = 1;
    if (root == NULL || root->type != JSON_OBJECT) {
        json_value_free(root);
        return 0;
    }
    for (m = root->u.obj_val.first; m != NULL && r > 0; m = m->next)
        if (m->value != NULL && m->value->type == JSON_STRING)
            r = sprout_values_set(v, m->key, m->value->u.str_val);
    json_value_free(root);
    return r;
}

int sprout_values_set_arg(SproutValues *v, const char *key, size_t keylen, const char *val)
{
    char upper[64];
    size_t i;
    if (keylen >= sizeof(upper)) return 0;
    for (i = 0; i < keylen; ++i)
        upper[i] = key[i] >= 'a' && key[i] <= 'z' ? (char)(key[i] - 'a' + 'A') : key[i];
    upper[keylen] = '\0';
    if (!sprout_key_valid(upper, keylen)) return 0;
    return sprout_values_set(v, upper, val);
}

int sprout_values_parse_assign(SproutValues *v, const char *arg)
{
    const char *eq = strchr(arg, '=');
    if (eq == NULL) return 0;
    return sprout_values_set_arg(v, arg, (size_t)(eq - arg), eq + 1);
}

void sprout_date(long days, int *year, int *month, int *day)
{
    static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int y = 1978, m = 0;
    for (;;) {
        int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        long n = leap ? 366 : 365;
        if (days < n) break;
        days -= n;
        ++y;
    }
    for (;;) {
        int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        long n = mdays[m] + (m == 1 && leap ? 1 : 0);
        if (days < n) break;
        days -= n;
        ++m;
    }
    *year = y;
    *month = m + 1;
    *day = (int)days + 1;
}

/**
 * @brief Write a number with a fixed count of digits.
 * @param out The buffer (at least @p digits + 1 bytes).
 * @param n The number (not negative).
 * @param digits The count of digits.
 */
static void put_digits(char *out, int n, int digits)
{
    out[digits] = '\0';
    while (digits-- > 0) {
        out[digits] = (char)('0' + n % 10);
        n /= 10;
    }
}

void sprout_drawer_name(const char *path, char *name, size_t size)
{
    size_t n = strlen(path), i, start = 0;
    if (size == 0) return;
    while (n > 0 && path[n - 1] == '/') --n;
    if (n > 0 && path[n - 1] == ':') --n;       /* "Work:" -> "Work" */
    for (i = 0; i < n; ++i)
        if (path[i] == '/' || path[i] == ':') start = i + 1;
    n -= start;
    if (n > size - 1) n = size - 1;
    memcpy(name, path + start, n);
    name[n] = '\0';
}

int sprout_builtins(SproutValues *v, const char *project, const char *drawer, long days)
{
    char date[12], year[6];
    int y, m, d;
    sprout_date(days, &y, &m, &d);
    put_digits(year, y, 4);
    put_digits(date, y, 4);
    date[4] = '-';
    put_digits(date + 5, m, 2);
    date[7] = '-';
    put_digits(date + 8, d, 2);
    if (project == NULL || project[0] == '\0') project = drawer;
    if (sprout_values_set(v, "PROJECT", project) < 0 || sprout_values_set(v, "DRAWER", drawer) < 0 ||
        sprout_values_set(v, "DATE", date) < 0 || sprout_values_set(v, "YEAR", year) < 0)
        return -1;
    return 1;
}

int sprout_resolve(const SproutTemplate *t, const SproutValues *given, const char *drawer, long days,
                   SproutValues *out, SproutReport report, void *user)
{
    int i, j;

    sprout_values_init(out);
    if (sprout_builtins(out, sprout_values_get(given, "PROJECT"), drawer, days) < 0) goto nomem;
    /* the standard keys: given or empty (a placeholder of the template
       with such a key works its value out again below) */
    for (i = 0; standard_keys[i] != NULL; ++i) {
        const char *val = sprout_values_get(given, standard_keys[i]);
        if (sprout_values_set(out, standard_keys[i], val != NULL ? val : "") < 0) goto nomem;
    }

    for (j = 0; j < given->count; ++j) {
        const char *key = given->keys[j];
        int known = 0;
        if (strcmp(key, "PROJECT") == 0 || sprout_key_standard(key)) continue;
        if (sprout_key_builtin(key)) {
            say(report, user, 0, "built-in value cannot be set: ", key, NULL);
            continue;
        }
        for (i = 0; i < t->nph && !known; ++i) known = strcmp(t->ph[i].key, key) == 0;
        if (!known) say(report, user, 0, "the template has no placeholder ", key, NULL);
    }

    for (i = 0; i < t->nph; ++i) {
        const SproutPlaceholder *p = &t->ph[i];
        const char *val = sprout_values_get(given, p->key);
        char *expanded = NULL;
        int r;
        if (val == NULL && p->def != NULL) {
            size_t n;
            if ((expanded = sprout_expand(p->def, strlen(p->def), out, &n)) == NULL) goto nomem;
            val = expanded;
        }
        if (val == NULL) val = p->nchoices > 0 ? p->choices[0] : "";
        if (p->required && val[0] == '\0') {
            say(report, user, 1, "a value is required for ", p->key, NULL);
            free(expanded);
            sprout_values_free(out);
            return 0;
        }
        if (p->nchoices > 0) {
            for (j = 0; j < p->nchoices && strcmp(p->choices[j], val) != 0; ++j)
                ;
            if (j == p->nchoices) {
                say(report, user, 1, "value not among the choices of ", p->key, NULL);
                free(expanded);
                sprout_values_free(out);
                return 0;
            }
        }
        r = sprout_values_set(out, p->key, val);
        free(expanded);
        if (r < 0) goto nomem;
    }
    return 1;

nomem:
    sprout_values_free(out);
    return -1;
}

/** A growing output buffer of sprout_expand(). */
typedef struct Buf {
    char *data;         /**< the bytes (malloc()ed) */
    size_t len;         /**< bytes used */
    size_t cap;         /**< bytes allocated */
    int failed;         /**< out of memory */
} Buf;

/**
 * @brief Append bytes to a buffer.
 * @param b The buffer.
 * @param s The bytes.
 * @param n Their number.
 */
static void put(Buf *b, const char *s, size_t n)
{
    if (b->failed) return;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap * 2;
        char *data;
        if (cap < b->len + n + 1) cap = b->len + n + 1;
        if ((data = (char *)realloc(b->data, cap)) == NULL) {
            b->failed = 1;
            return;
        }
        b->data = data;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

/**
 * @brief Upper case of an ISO-8859-1 character.
 * @param c The character.
 * @return Its upper case (or itself).
 */
static unsigned char to_upper(unsigned char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return (unsigned char)(c - 0x20);
    return c;
}

/**
 * @brief Lower case of an ISO-8859-1 character.
 * @param c The character.
 * @return Its lower case (or itself).
 */
static unsigned char to_lower(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (unsigned char)(c + 0x20);
    return c;
}

/** The modifiers of a placeholder. */
enum Modifier { MOD_NONE, MOD_UPPER, MOD_LOWER, MOD_IDENT, MOD_UNKNOWN };

/**
 * @brief Recognise a modifier.
 * @param s Its name.
 * @param n Its length (0: none).
 * @return The modifier.
 */
static enum Modifier modifier(const char *s, size_t n)
{
    if (n == 0) return MOD_NONE;
    if (n == 5 && memcmp(s, "upper", 5) == 0) return MOD_UPPER;
    if (n == 5 && memcmp(s, "lower", 5) == 0) return MOD_LOWER;
    if (n == 5 && memcmp(s, "ident", 5) == 0) return MOD_IDENT;
    return MOD_UNKNOWN;
}

/**
 * @brief Append a value with a modifier applied.
 * @param b The buffer.
 * @param val The value.
 * @param mod The modifier (not MOD_UNKNOWN).
 */
static void put_value(Buf *b, const char *val, enum Modifier mod)
{
    const unsigned char *p = (const unsigned char *)val;
    if (mod == MOD_NONE) {
        put(b, val, strlen(val));
        return;
    }
    if (mod == MOD_IDENT && (*p == '\0' || (*p >= '0' && *p <= '9'))) put(b, "_", 1);
    for (; *p != '\0'; ++p) {
        unsigned char c = *p;
        if (mod == MOD_UPPER) c = to_upper(c);
        else if (mod == MOD_LOWER) c = to_lower(c);
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            c = '_';
        put(b, (const char *)&c, 1);
    }
}

char *sprout_expand(const char *text, size_t len, const SproutValues *v, size_t *outlen)
{
    Buf b;
    size_t i = 0, start = 0;

    b.cap = len + 1;
    b.len = 0;
    b.failed = 0;
    if ((b.data = (char *)malloc(b.cap)) == NULL) return NULL;
    while (i < len) {
        size_t dollars, k, key, keylen, mod, modlen;
        if (text[i] != '{' || i + 1 >= len || text[i + 1] != '$') {
            ++i;
            continue;
        }
        /* {, a run of $, KEY, optionally :modifier, } */
        k = i + 1;
        while (k < len && text[k] == '$') ++k;
        dollars = k - (i + 1);
        key = k;
        while (k < len && ((text[k] >= 'A' && text[k] <= 'Z') || (text[k] >= '0' && text[k] <= '9') ||
                           text[k] == '_'))
            ++k;
        keylen = k - key;
        mod = modlen = 0;
        if (k < len && text[k] == ':') {
            mod = ++k;
            while (k < len && text[k] >= 'a' && text[k] <= 'z') ++k;
            modlen = k - mod;
            if (modlen == 0) keylen = 0;            /* "{$KEY:}" is no placeholder */
        }
        if (keylen == 0 || !sprout_key_valid(text + key, keylen) || k >= len || text[k] != '}') {
            ++i;
            continue;
        }
        if (dollars > 1) {
            /* escaped: drop one $ */
            put(&b, text + start, i + 1 - start);
            start = i + 2;
            i = k + 1;
            continue;
        }
        {
            char name[64];
            const char *val = NULL;
            enum Modifier m = modifier(text + mod, modlen);
            if (keylen < sizeof(name) && m != MOD_UNKNOWN) {
                memcpy(name, text + key, keylen);
                name[keylen] = '\0';
                val = sprout_values_get(v, name);
            }
            if (val == NULL) {          /* not defined: left as it is */
                i = k + 1;
                continue;
            }
            put(&b, text + start, i - start);
            put_value(&b, val, m);
            i = start = k + 1;
        }
    }
    put(&b, text + start, len - start);
    if (b.failed) {
        free(b.data);
        return NULL;
    }
    b.data[b.len] = '\0';
    if (outlen != NULL) *outlen = b.len;
    return b.data;
}

/**
 * @brief Tell whether one part of a path is safe.
 * @param s The part.
 * @param n Its length.
 * @param colon Non-zero to allow ':' (in a part before replacement, where
 *        it may belong to a modifier).
 * @return Non-zero if it is not empty, not "." or ".." and has no '/'
 *         (nor ':' unless allowed).
 */
static int part_safe(const char *s, size_t n, int colon)
{
    size_t i;
    if (n == 0 || (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return 0;
    for (i = 0; i < n; ++i)
        if (s[i] == '/' || (s[i] == ':' && !colon) || s[i] == '\0') return 0;
    return 1;
}

/**
 * @brief Check the parts of a path.
 * @param path The path ('/' separated).
 * @param colon Non-zero to allow ':' in the parts.
 * @return Non-zero if every part is safe.
 */
static int parts_safe(const char *path, int colon)
{
    const char *p = path;
    for (;;) {
        const char *slash = strchr(p, '/');
        size_t n = slash != NULL ? (size_t)(slash - p) : strlen(p);
        if (!part_safe(p, n, colon)) return 0;
        if (slash == NULL) return 1;
        p = slash + 1;
    }
}

int sprout_path_safe(const char *path)
{
    return parts_safe(path, 0);
}

/**
 * @brief Tell whether a condition {?KEY=a|b} or {?KEY!=a|b} holds.
 * @param c The text between "{?" and "}".
 * @param n Its length.
 * @param v The values (an undefined key counts as "").
 * @return 1 if it holds, 0 if not, -1 if it is malformed.
 */
static int condition(const char *c, size_t n, const SproutValues *v)
{
    char key[64];
    const char *val, *p, *end = c + n;
    size_t k = 0, len;
    int neg, match = 0;
    while (k < n && c[k] != '=' && c[k] != '!') ++k;
    if (k == 0 || k >= sizeof(key) || k == n || !sprout_key_valid(c, k)) return -1;
    memcpy(key, c, k);
    key[k] = '\0';
    neg = c[k] == '!';
    p = c + k + (neg ? 2 : 1);
    if (neg && (k + 1 >= n || c[k + 1] != '=')) return -1;
    if ((val = sprout_values_get(v, key)) == NULL) val = "";
    len = strlen(val);
    for (;;) {
        const char *bar = p;
        while (bar < end && *bar != '|') ++bar;
        if ((size_t)(bar - p) == len && memcmp(p, val, len) == 0) match = 1;
        if (bar == end) break;
        p = bar + 1;
    }
    return match != neg;
}

/**
 * @brief Take the conditions out of a part of a path.
 * @param part The part (need not be terminated).
 * @param n Its length.
 * @param v The values.
 * @param[out] out The part without them (appended).
 * @return 1 if all hold, 0 if one does not, -1 if one is malformed.
 */
static int strip_conditions(const char *part, size_t n, const SproutValues *v, Buf *out)
{
    size_t i = 0, start = 0;
    int all = 1;
    while (i + 1 < n) {
        if (part[i] == '{' && part[i + 1] == '?') {
            const char *close = memchr(part + i, '}', n - i);
            int r;
            if (close == NULL) return -1;
            if ((r = condition(part + i + 2, (size_t)(close - part) - i - 2, v)) < 0) return -1;
            if (r == 0) all = 0;
            put(out, part + start, i - start);
            i = start = (size_t)(close - part) + 1;
        } else {
            ++i;
        }
    }
    put(out, part + start, n - start);
    return all;
}

char *sprout_expand_path(const char *path, const SproutValues *v, int *bad)
{
    Buf b, plain;
    const char *p = path;

    *bad = SPROUT_PATH_OK;
    /* ':' is checked after the replacement ({$NAME:lower}) */
    if (!parts_safe(path, 1)) {
        *bad = SPROUT_PATH_UNSAFE;
        return NULL;
    }
    b.cap = plain.cap = strlen(path) + 1;
    b.len = plain.len = 0;
    b.failed = plain.failed = 0;
    if ((b.data = (char *)malloc(b.cap)) == NULL) return NULL;
    if ((plain.data = (char *)malloc(plain.cap)) == NULL) {
        free(b.data);
        return NULL;
    }
    for (;;) {
        const char *slash = strchr(p, '/');
        size_t n = slash != NULL ? (size_t)(slash - p) : strlen(p), m;
        char *part;
        int r;
        plain.len = 0;
        r = strip_conditions(p, n, v, &plain);
        if (r <= 0 || plain.failed) {
            *bad = r < 0 ? SPROUT_PATH_BADCOND : r == 0 ? SPROUT_PATH_LEFTOUT : SPROUT_PATH_OK;
            free(plain.data);
            free(b.data);
            return NULL;
        }
        if ((part = sprout_expand(plain.data, plain.len, v, &m)) == NULL) {
            free(plain.data);
            free(b.data);
            return NULL;
        }
        if (!part_safe(part, m, 0)) {
            free(part);
            free(plain.data);
            free(b.data);
            *bad = SPROUT_PATH_UNSAFE;
            return NULL;
        }
        if (b.len > 0) put(&b, "/", 1);
        put(&b, part, m);
        free(part);
        if (slash == NULL) break;
        p = slash + 1;
    }
    free(plain.data);
    if (b.failed) {
        free(b.data);
        return NULL;
    }
    b.data[b.len] = '\0';
    return b.data;
}

int sprout_is_text(const char *data, size_t len)
{
    return len <= (size_t)SPROUT_TEXT_MAX && memchr(data, '\0', len) == NULL;
}
