/**
 * @file filetypes.h
 * @brief Portable reader and writer of TinyDev's settings per file type
 *        (PROGDIR:TinyDev.json): tabs, indentation, line numbers.
 */
#ifndef TINYDEV_FILETYPES_H
#define TINYDEV_FILETYPES_H

/*
 * The editor settings of each file type, as kept in PROGDIR:TinyDev.json.
 * No Amiga headers, so the host tests can use it.
 *
 * A file type is a language of TinyDev-Scout (its identifier, e.g. "c");
 * "default" holds the settings of plain text and of every language
 * without an entry of its own. The file (ISO-8859-1, indented):
 *
 *   {
 *     "version": 1,
 *     "filetypes": {
 *       "default": { "tabsize": 8, "indent": 4, "autoindent": true,
 *                    "tabkey": "after", "linenumbers": true,
 *                    "cutcopyline": false },
 *       "languages": {
 *         "amigados": { "indent": "tab" }
 *       }
 *     }
 *   }
 *
 * - "tabsize": the columns of a tab character (1 to 16);
 * - "indent": the spaces of a level of indentation (1 to 16), or "tab"
 *   to indent with tab characters;
 * - "autoindent": a new line takes the indentation of the line before;
 * - "tabkey": what the Tab key does with the cursor inside a line's text:
 *   "line" indents the line, "after" the text after the cursor;
 * - "linenumbers": line numbers in the editor's left bar;
 * - "cutcopyline": Cut and Copy take the cursor's line when nothing is
 *   selected.
 * A language's entry may leave keys out: they come from "default". Wrong
 * values are ignored. Other keys, in the file, in "filetypes" and in the
 * entries, are kept by filetypes_format().
 */

/** The largest tab size and indentation. */
#define FILETYPES_MAX_WIDTH 16

/** The editor settings of a file type. */
typedef struct FileTypeOptions {
    int tab_size;       /**< columns of a tab character (1 to FILETYPES_MAX_WIDTH) */
    int indent;         /**< spaces per level of indentation, 0 for tab characters */
    int auto_indent;    /**< a new line takes the indentation of the line before */
    int tab_after;      /**< Tab inside the text indents the text after the cursor (else the line) */
    int line_numbers;   /**< show line numbers */
    int cut_copy_line;  /**< Cut and Copy take the line when nothing is selected */
} FileTypeOptions;

/** The settings of a language that has its own. */
typedef struct FileTypeEntry {
    char *id;                   /**< the language's identifier (malloc()) */
    FileTypeOptions options;    /**< its settings */
} FileTypeEntry;

/** The settings of all file types. */
typedef struct FileTypes {
    FileTypeOptions standard;   /**< plain text and languages without an entry ("default") */
    FileTypeEntry *entries;     /**< the languages with settings of their own (malloc()) */
    long count;                 /**< number of entries */
} FileTypes;

/**
 * @brief Set the built-in settings: tab size 8, indentation 4 spaces,
 *        automatic indentation, Tab indents the text after the cursor,
 *        line numbers, no line cut or copy.
 * @param[out] o The settings.
 */
void filetypes_defaults(FileTypeOptions *o);

/**
 * @brief Initialise the settings: the built-in ones for all file types.
 * @param[out] t The settings.
 */
void filetypes_init(FileTypes *t);

/**
 * @brief Free the entries; the settings are those of filetypes_init() then.
 * @param t The settings.
 */
void filetypes_clear(FileTypes *t);

/**
 * @brief Copy settings.
 * @param[out] dst The copy (initialised here; filetypes_clear() it).
 * @param src The settings.
 * @return 1 on success, 0 if out of memory (@p dst is initialised then).
 */
int filetypes_copy(FileTypes *dst, const FileTypes *src);

/**
 * @brief Read the settings from the JSON text of the file, replacing
 *        those in @p t.
 * @param t The settings.
 * @param json The text.
 * @return 1 on success (also without "filetypes"); 0 if the text is not a
 *         JSON object (@p t is left unchanged then); -1 if out of memory
 *         (@p t is initialised then).
 */
int filetypes_parse(FileTypes *t, const char *json);

/**
 * @brief Return the entry of a language.
 * @param t The settings.
 * @param id The language's identifier.
 * @return Its settings, or NULL if it has none of its own.
 */
const FileTypeOptions *filetypes_find(const FileTypes *t, const char *id);

/**
 * @brief Return the settings a file type uses.
 * @param t The settings.
 * @param id The language's identifier; NULL or "" for plain text.
 * @return The language's own settings, otherwise the default ones.
 */
const FileTypeOptions *filetypes_get(const FileTypes *t, const char *id);

/**
 * @brief Set the settings of a file type, or remove a language's entry.
 *
 * The values are brought into their ranges.
 *
 * @param t The settings.
 * @param id The language's identifier; NULL or "" for the default ones.
 * @param o The settings; NULL removes the language's entry (it uses the
 *        default ones then).
 * @return 1 on success, -1 if out of memory (nothing changes).
 */
int filetypes_set(FileTypes *t, const char *id, const FileTypeOptions *o);

/**
 * @brief Write the settings as the JSON text of the file.
 *
 * "filetypes" gets the settings of @p t; everything else in @p old_json
 * stays, in its place, and so do the other keys of "filetypes" and of
 * the entries. Every entry is written whole. A new file starts with
 * "version": 1.
 *
 * @param t The settings.
 * @param old_json The text of the file as it is now (NULL or "" for a new
 *        file); it must be a text filetypes_parse() accepts.
 * @return The text (free() it), or NULL if out of memory or @p old_json
 *         is not a JSON object.
 */
char *filetypes_format(const FileTypes *t, const char *old_json);

#endif /* TINYDEV_FILETYPES_H */
