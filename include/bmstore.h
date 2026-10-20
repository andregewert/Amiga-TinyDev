/**
 * @file bmstore.h
 * @brief Portable store of a project's bookmarks (.tinydev/bookmarks.json).
 */
#ifndef TINYDEV_BMSTORE_H
#define TINYDEV_BMSTORE_H

/*
 * Portable store of the bookmarks of a project, as kept in
 * <project>/.tinydev/bookmarks.json. No Amiga headers, so the host tests
 * can use it.
 *
 * The file (ISO-8859-1, like all of TinyDev's files):
 *
 *   {
 *     "version": 1,
 *     "bookmarks": [
 *       { "file": "src/ui.c", "line": 99, "text": "void tree_handle_event(...)" }
 *     ]
 *   }
 *
 * "file" is relative to the project directory, "line" counts from 1 and
 * "text" is the line's text (at most BMSTORE_TEXT_MAX characters), used to
 * find the line again when the file was changed elsewhere. In memory the
 * lines count from 0.
 */

#include <stddef.h>

/** Characters of a line kept to find it again. */
#define BMSTORE_TEXT_MAX 120

/** A bookmark. */
typedef struct BmEntry {
    char *file;             /**< relative to the project, '/' separated */
    long line;              /**< counted from 0 */
    char *text;             /**< the line's text (shortened) */
} BmEntry;

/** The bookmarks of a project. */
typedef struct BmStore {
    BmEntry *entries;   /**< the bookmarks (malloc()) */
    long count;         /**< number of them */
    long capacity;      /**< size of the array */
} BmStore;

/**
 * @brief Initialise an empty store.
 * @param s The store.
 */
void bmstore_init(BmStore *s);

/**
 * @brief Remove all bookmarks and free the memory.
 * @param s The store.
 */
void bmstore_clear(BmStore *s);

/**
 * @brief Add a bookmark (a bookmark on the same line of the file is
 *        replaced).
 * @param s The store.
 * @param file The file, relative to the project.
 * @param line The line, counted from 0.
 * @param text The line's text (need not be terminated; NULL for none).
 * @param length Its length.
 * @return Non-zero on success, zero if out of memory.
 */
int bmstore_add(BmStore *s, const char *file, long line, const char *text, long length);

/**
 * @brief Remove the bookmarks of a file.
 * @param s The store.
 * @param file The file, relative to the project (compared ignoring case,
 *             as AmigaDOS does).
 * @return The number removed.
 */
long bmstore_remove_file(BmStore *s, const char *file);

/**
 * @brief Remove one bookmark.
 * @param s The store.
 * @param file The file, relative to the project (ignoring case).
 * @param line The line, counted from 0.
 * @return Non-zero if it was there.
 */
int bmstore_remove(BmStore *s, const char *file, long line);

/**
 * @brief Compare two file names ignoring case (ASCII and Latin-1), for
 *        sorting.
 * @param a A name.
 * @param b Another name.
 * @return <0, 0 or >0.
 */
int bmstore_compare_files(const char *a, const char *b);

/**
 * @brief Tell whether two file names are the same file (ignoring case).
 * @param a A name.
 * @param b Another name.
 * @return Non-zero if they are equal.
 */
int bmstore_same_file(const char *a, const char *b);

/**
 * @brief Read bookmarks from the JSON text of a bookmarks file and add
 *        them to the store.
 * @param s The store.
 * @param json The text.
 * @return Non-zero on success, zero if the text is not a bookmarks file
 *         (the store is left unchanged then).
 */
int bmstore_parse(BmStore *s, const char *json);

/**
 * @brief Write the store as the JSON text of a bookmarks file, sorted by
 *        file and line.
 * @param s The store.
 * @return The text (free() it), or NULL if out of memory.
 */
char *bmstore_format(BmStore *s);

/**
 * @brief Find the line of a bookmark in a text that may have changed: the
 *        line itself if its text is still the same, otherwise the nearest
 *        line with that text, otherwise the line itself (or the last line).
 * @param text The text (lines separated by LF, CR or CRLF).
 * @param size Its size.
 * @param line The line of the bookmark, counted from 0.
 * @param line_text The text of the line as stored (NULL or "": any).
 * @return The line, counted from 0.
 */
long bmstore_relocate(const char *text, long size, long line, const char *line_text);

#endif
