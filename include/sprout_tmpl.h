/**
 * @file sprout_tmpl.h
 * @brief Portable core of Sprout: a template's metadata (template.json),
 *        the values of its placeholders and their replacement in names
 *        and text.
 *
 * No Amiga headers, so the host tests can use it; TinyDev's wizard links
 * it too, to show the defaults Sprout would use. The format is described
 * in src/sprout/docs/Sprout.md.
 *
 * A placeholder is written {$KEY} or {$KEY:modifier} (modifiers upper,
 * lower, ident); keys are A-Z, 0-9 and _, starting with a letter. Only
 * defined keys are replaced; a placeholder with one more $ ({$$KEY}) is
 * written without that $ instead. Names of files may also hold conditions
 * ({?KEY=value}, see sprout_expand_path()).
 */
#ifndef SPROUT_TMPL_H
#define SPROUT_TMPL_H

#include <stddef.h>

struct JsonValue;

/** The largest file whose contents placeholders are replaced in (1 MB). */
#define SPROUT_TEXT_MAX (1024L * 1024L)

/**
 * @brief Receives a message of the core.
 * @param user The pointer given with the callback.
 * @param error Non-zero for an error, zero for a warning.
 * @param text The message (English, without line end).
 */
typedef void (*SproutReport)(void *user, int error, const char *text);

/** A placeholder of a template. All strings are malloc()ed. */
typedef struct SproutPlaceholder {
    char *key;          /**< its key */
    char *label;        /**< label in the wizard; NULL: the key */
    char *description;  /**< help text; NULL if none */
    char *def;          /**< default (may contain placeholders); NULL if none */
    int required;       /**< non-zero: the value must not be empty */
    char **choices;     /**< the allowed values; NULL if any value is */
    int nchoices;       /**< number of choices */
} SproutPlaceholder;

/** A template's metadata. All strings are malloc()ed. */
typedef struct SproutTemplate {
    char *kind;                 /**< "project" or "file" */
    char *name;                 /**< shown name */
    char *description;          /**< description; NULL if none */
    char *category;             /**< category; "Other" if none was given */
    char *icon_type;            /**< icon type; NULL if none */
    char *icon_file;            /**< icon file in the template; NULL if none */
    SproutPlaceholder *ph;      /**< the placeholders, in their order */
    int nph;                    /**< number of placeholders */
    char **open;                /**< files to open after creating */
    int nopen;                  /**< number of files to open */
    char **verbatim;            /**< AmigaDOS patterns of files copied unchanged */
    int nverbatim;              /**< number of patterns */
} SproutTemplate;

/** Values of placeholders: keys with their strings (malloc()ed). */
typedef struct SproutValues {
    char **keys;        /**< the keys */
    char **vals;        /**< the values, parallel to keys */
    int count;          /**< number of values */
    int cap;            /**< allocated entries */
} SproutValues;

/**
 * @brief Read a template's metadata.
 *
 * The texts shown to the user (name, description, category, and label,
 * description and default of a placeholder) may be translated: a member
 * "name[deutsch]" beside "name" holds the text for that language. The
 * first of @p languages with a translation wins, otherwise the member
 * without a language is taken.
 *
 * @param[out] t The metadata (always initialised; sprout_template_free() it).
 * @param json The text of template.json.
 * @param languages The user's languages, most preferred first, named as
 *                  by the Amiga's locale ("deutsch"), NULL-terminated;
 *                  NULL for none.
 * @param report Receives the reason when the metadata is not valid.
 * @param user Passed to @p report.
 * @return 1 on success; 0 if not valid (reported); -1 if out of memory.
 */
int sprout_template_parse(SproutTemplate *t, const char *json, const char *const *languages,
                          SproutReport report, void *user);

/**
 * @brief Read a template's metadata from a JSON tree (an object as in
 *        template.json, or an entry of Sprout's LIST output).
 * Translated texts are chosen as with sprout_template_parse().
 *
 * @param[out] t The metadata (always initialised; sprout_template_free() it).
 * @param root The object.
 * @param languages The user's languages (as with sprout_template_parse()),
 *                  NULL for none.
 * @param report Receives the reason when the metadata is not valid.
 * @param user Passed to @p report.
 * @return 1 on success; 0 if not valid (reported); -1 if out of memory.
 */
int sprout_template_from_json(SproutTemplate *t, const struct JsonValue *root, const char *const *languages,
                              SproutReport report, void *user);

/**
 * @brief Write a template's metadata as a JSON tree, in the form of
 *        template.json (keys without a value left out; the texts in the
 *        language chosen when reading, without translations).
 * @param t The metadata.
 * @return The object (json_value_free() it), or NULL if out of memory.
 */
struct JsonValue *sprout_template_to_json(const SproutTemplate *t);

/**
 * @brief Free a template's metadata (it stays valid and empty).
 * @param t The metadata.
 */
void sprout_template_free(SproutTemplate *t);

/**
 * @brief Tell whether a key is valid: A-Z, 0-9, _, starting with a letter.
 * @param key The key.
 * @param len Its length.
 * @return Non-zero if valid.
 */
int sprout_key_valid(const char *key, size_t len);

/**
 * @brief Tell whether a key is a built-in one (PROJECT, DRAWER, DATE,
 *        YEAR).
 * @param key The key.
 * @return Non-zero if built-in.
 */
int sprout_key_builtin(const char *key);

/**
 * @brief Tell whether a key is a standard one (AUTHOR, AUTHORMAIL): always
 *        defined (empty unless given), and a template may declare it as a
 *        placeholder to ask for it.
 * @param key The key.
 * @return Non-zero if standard.
 */
int sprout_key_standard(const char *key);

/**
 * @brief Initialise an empty set of values.
 * @param[out] v The values.
 */
void sprout_values_init(SproutValues *v);

/**
 * @brief Free a set of values (it stays valid and empty).
 * @param v The values.
 */
void sprout_values_free(SproutValues *v);

/**
 * @brief Set a value, replacing one of the same key.
 * @param v The values.
 * @param key The key (copied).
 * @param val The value (copied).
 * @return 1 on success; -1 if out of memory.
 */
int sprout_values_set(SproutValues *v, const char *key, const char *val);

/**
 * @brief Look up a value.
 * @param v The values.
 * @param key The key.
 * @return The value, or NULL if it is not set.
 */
const char *sprout_values_get(const SproutValues *v, const char *key);

/**
 * @brief Add the values of a JSON object ({ "KEY": "value", ... }).
 *
 * Members whose value is not a string are left out.
 *
 * @param v The values.
 * @param json The text.
 * @return 1 on success; 0 if the text is not a JSON object; -1 if out of
 *         memory.
 */
int sprout_values_parse_json(SproutValues *v, const char *json);

/**
 * @brief Add a value given on the command line: the key may be written in
 *        any case (it is stored in upper case).
 * @param v The values.
 * @param key The key (need not be terminated).
 * @param keylen Its length.
 * @param val The value.
 * @return 1 on success; 0 if the key is not valid; -1 if out of memory.
 */
int sprout_values_set_arg(SproutValues *v, const char *key, size_t keylen, const char *val);

/**
 * @brief Add a value given as KEY=value (the first '=' separates them),
 *        with sprout_values_set_arg().
 * @param v The values.
 * @param arg The argument.
 * @return 1 on success; 0 if there is no '=' or the key is not valid;
 *         -1 if out of memory.
 */
int sprout_values_parse_assign(SproutValues *v, const char *arg);

/**
 * @brief Turn a number of days since 1 January 1978 (the Amiga's epoch)
 *        into a date.
 * @param days The days (not negative).
 * @param[out] year The year.
 * @param[out] month The month, 1-12.
 * @param[out] day The day, 1-31.
 */
void sprout_date(long days, int *year, int *month, int *day);

/**
 * @brief The name of a project drawer (DRAWER, and PROJECT unless one is
 *        given): the last part of its path, "Work:" giving "Work".
 * @param path The drawer's path (trailing '/' ignored).
 * @param[out] name The name.
 * @param size The size of @p name.
 */
void sprout_drawer_name(const char *path, char *name, size_t size);

/**
 * @brief Set the built-in values: PROJECT (the given one, else the
 *        drawer's name), DRAWER, DATE as YYYY-MM-DD and YEAR.
 * @param v The values (replacing ones of the same keys).
 * @param project The project's name; NULL or "" for the drawer's name.
 * @param drawer The name of the project drawer.
 * @param days Today, as days since 1 January 1978.
 * @return 1 on success; -1 if out of memory.
 */
int sprout_builtins(SproutValues *v, const char *project, const char *drawer, long days);

/**
 * @brief Work out the values of all placeholders of a template.
 *
 * The built-in keys come first: PROJECT (the given one, else the drawer's
 * name), DRAWER (the drawer's name), DATE as YYYY-MM-DD and YEAR; then
 * the standard keys AUTHOR and AUTHORMAIL (given, else empty); then
 * each placeholder in its order: the given value, else its default
 * (expanded with the values so far), else the first choice, else empty.
 * Errors: a required value empty, a value not among the choices. Given
 * values for unknown keys or for DRAWER, DATE and YEAR are warnings and
 * left out.
 *
 * @param t The template.
 * @param given The values given (VALUES file, SET).
 * @param drawer The name of the project drawer.
 * @param days Today, as days since 1 January 1978.
 * @param[out] out The values (initialised here; sprout_values_free() it).
 * @param report Receives errors and warnings.
 * @param user Passed to @p report.
 * @return 1 on success (possibly with warnings); 0 on an error (reported);
 *         -1 if out of memory.
 */
int sprout_resolve(const SproutTemplate *t, const SproutValues *given, const char *drawer, long days,
                   SproutValues *out, SproutReport report, void *user);

/**
 * @brief Replace the placeholders in a text.
 * @param text The text (need not be terminated; may contain NUL bytes).
 * @param len Its length.
 * @param v The values.
 * @param[out] outlen The length of the result (without its terminator).
 * @return The result, NUL-terminated (free() it); NULL if out of memory.
 */
char *sprout_expand(const char *text, size_t len, const SproutValues *v, size_t *outlen);

/**
 * @brief Tell whether a relative path from a template is safe: not empty,
 *        no ':', no leading '/', no empty part, no part "." or "..".
 * @param path The path ('/' separated).
 * @return Non-zero if safe.
 */
int sprout_path_safe(const char *path);

/** @name Results of sprout_expand_path() (its @p bad) */
/** @{ */
#define SPROUT_PATH_OK      0   /**< expanded (or out of memory if NULL is returned) */
#define SPROUT_PATH_UNSAFE  1   /**< the path, or an expanded part, is not safe */
#define SPROUT_PATH_LEFTOUT 2   /**< a condition does not hold: the file is left out */
#define SPROUT_PATH_BADCOND 3   /**< a condition is malformed */
/** @} */

/**
 * @brief Replace the placeholders in a relative path, part by part, and
 *        take out its conditions.
 *
 * A part may hold conditions {?KEY=value}, {?KEY=a|b} (one of the values)
 * or {?KEY!=a|b} (none of them); values are compared exactly, an undefined
 * key counts as "". The conditions are removed from the name; if one does
 * not hold, the path is left out (a condition on a drawer leaves out all
 * below it, as their paths contain it).
 * Fails if the path or an expanded part is not safe, e.g. because a value
 * contains '/' or ':'. A ':' before the replacement is allowed (it may
 * belong to a modifier); one left after it is not.
 * @param path The path ('/' separated).
 * @param v The values.
 * @param[out] bad Why NULL was returned: #SPROUT_PATH_UNSAFE,
 *                 #SPROUT_PATH_LEFTOUT, #SPROUT_PATH_BADCOND, or
 *                 #SPROUT_PATH_OK when out of memory.
 * @return The result (free() it); NULL otherwise.
 */
char *sprout_expand_path(const char *path, const SproutValues *v, int *bad);

/**
 * @brief Tell whether a file's contents get their placeholders replaced:
 *        at most #SPROUT_TEXT_MAX bytes and no NUL byte.
 * @param data The contents.
 * @param len Their length.
 * @return Non-zero if it is a text file.
 */
int sprout_is_text(const char *data, size_t len);

#endif /* SPROUT_TMPL_H */
