#ifndef TINYDEV_SEARCH_H
#define TINYDEV_SEARCH_H

/*
 * Portable search engine of the find and replace window: plain text (and,
 * later, regular expressions) in 8-bit text, line by line. No Amiga
 * headers, so the host tests can use it.
 *
 * Text is a buffer of lines separated by LF, CR or CRLF; positions are
 * (line, column) pairs counted from 0, as GA_TEXTEDITOR_CursorY/CursorX of
 * texteditor.gadget. A match never spans a line break.
 */

#include <stddef.h>

/** Number of capture groups a match reports (\0 is the whole match). */
#define SEARCH_GROUPS 10

/** Search options. */
typedef struct SearchOptions {
    int case_sensitive;     /**< otherwise A-Z and the Latin-1 letters fold */
    int whole_word;         /**< the match must not touch word characters */
    int regex;              /**< regular expression (not available yet) */
} SearchOptions;

/** A compiled search. */
typedef struct SearchPattern {
    SearchOptions options;
    char *text;             /**< plain: the text, folded if case-insensitive */
    long length;            /**< its length */
} SearchPattern;

/** A match within one line (columns, end exclusive). */
typedef struct SearchMatch {
    long start;
    long end;
    long group_start[SEARCH_GROUPS];    /**< -1 for groups that did not take part */
    long group_end[SEARCH_GROUPS];
} SearchMatch;

/** A position in the text. */
typedef struct SearchPos {
    long line;
    long col;
} SearchPos;

/** A match in the text. */
typedef struct SearchHit {
    long line;              /**< line number */
    long col;               /**< first column */
    long length;            /**< length of the match */
    long line_offset;       /**< offset of the line in the text */
    long line_length;       /**< length of the line without its line break */
} SearchHit;

/** Results of search_compile(). */
enum {
    SEARCH_OK = 0,
    SEARCH_EMPTY,           /**< nothing to search for */
    SEARCH_NOMEM,           /**< out of memory */
    SEARCH_NOREGEX          /**< regular expressions are not available */
};

/**
 * @brief Prepare a search.
 * @param p The pattern to fill in; free it with search_free() (also on error).
 * @param text The text to search for.
 * @param options The options.
 * @return SEARCH_OK or an error code.
 */
int search_compile(SearchPattern *p, const char *text, const SearchOptions *options);

/**
 * @brief Free a pattern prepared by search_compile().
 * @param p The pattern.
 */
void search_free(SearchPattern *p);

/**
 * @brief Return a message for an error of search_compile().
 * @param error The error code.
 * @return The message.
 */
const char *search_error(int error);

/**
 * @brief Find the first match in a line that starts at or after a column.
 * @param p The pattern.
 * @param line The line (need not be terminated).
 * @param length Its length.
 * @param from The first column a match may start at.
 * @param m The match, if one is found.
 * @return Non-zero if a match was found.
 */
int search_line_next(const SearchPattern *p, const char *line, long length, long from,
                     SearchMatch *m);

/**
 * @brief Find the last match in a line that starts before a column.
 * @param p The pattern.
 * @param line The line (need not be terminated).
 * @param length Its length.
 * @param before Matches must start before this column.
 * @param m The match, if one is found.
 * @return Non-zero if a match was found.
 */
int search_line_prev(const SearchPattern *p, const char *line, long length, long before,
                     SearchMatch *m);

/**
 * @brief Find the next (or previous) match in a text.
 * @param p The pattern.
 * @param text The text.
 * @param size Its size.
 * @param from Forward: the first position a match may start at;
 *             backwards: matches must start before this position.
 * @param backwards Non-zero to search towards the start.
 * @param wrap Non-zero to go on at the other end of the text.
 * @param hit The match, if one is found.
 * @param wrapped Set to non-zero if the match was found after wrapping
 *                (may be NULL).
 * @return Non-zero if a match was found.
 */
int search_text(const SearchPattern *p, const char *text, long size, SearchPos from,
                int backwards, int wrap, SearchHit *hit, int *wrapped);

/**
 * @brief Find all matches in a text, from the top.
 * @param p The pattern.
 * @param text The text.
 * @param size Its size.
 * @param hits Set to a malloc()ed array of the matches (NULL if none); the
 *             caller frees it.
 * @return The number of matches, or -1 if out of memory.
 */
long search_all(const SearchPattern *p, const char *text, long size, SearchHit **hits);

/**
 * @brief Build the replacement of a match. Plain searches use the
 *        replacement as it is; regular expressions replace \0 to \9 with
 *        the groups and \\ with a backslash.
 * @param p The pattern.
 * @param replacement The replacement text.
 * @param line The line of the match.
 * @param m The match.
 * @param out The buffer for the result (may be NULL if size is 0).
 * @param size Its size; the result is always terminated if size > 0.
 * @return The length of the complete result (as snprintf()).
 */
long search_expand(const SearchPattern *p, const char *replacement, const char *line,
                   const SearchMatch *m, char *out, long size);

#endif
