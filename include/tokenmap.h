/**
 * @file tokenmap.h
 * @brief The highlighting tokens of a document from TinyDev-Scout, found
 *        again by the text of a line.
 *
 * The highlighter hook gets the text of a line but not its number, and
 * lines move while the user types. Tokens are therefore kept per line of
 * the text they were made for and found by a checksum of the line's text
 * (and its length): a line that has not changed gets its tokens wherever
 * it is now; a changed one gets none until the next analysis.
 *
 * Portable C without Amiga headers (host-tested). Lookups only read the
 * map and allocate nothing (they run in the highlighter hook).
 */
#ifndef TINYDEV_TOKENMAP_H
#define TINYDEV_TOKENMAP_H

#include <stddef.h>
#include "syntax.h"

/** A token of a line. */
typedef struct TokenSpan {
    unsigned short col;         /**< first column (from 0) */
    unsigned short len;         /**< length */
    unsigned char style;        /**< SyntaxStyle */
} TokenSpan;

/** A line with tokens. */
typedef struct TokenLine {
    unsigned long hash;         /**< checksum of its text */
    unsigned long length;       /**< length of its text */
    long first;                 /**< its first token in TokenMap::spans */
    long count;                 /**< number of its tokens */
} TokenLine;

/** The tokens of a document. */
typedef struct TokenMap {
    TokenLine *lines;           /**< the lines with tokens */
    long line_count;            /**< number of them */
    TokenSpan *spans;           /**< the tokens, by line */
    long span_count;            /**< number of them */
    long *slots;                /**< hash table of the lines (index, -1) */
    unsigned long mask;         /**< size of the table minus one */
} TokenMap;

/**
 * @brief Read the numbers of the "tokens" array of Scout's TOKENS answer
 *        (without building a JSON tree: there can be many).
 * @param json The answer.
 * @param[out] count Number of values read (four per token).
 * @return The values (free() them), or NULL if there are none or memory
 *         ran out.
 */
long *tokenmap_read(const char *json, long *count);

/**
 * @brief Build the map of a text's tokens.
 *
 * Token kinds without a colour (variables, parameters, fields, labels) are
 * left out.
 *
 * @param text The text the tokens were made for.
 * @param length Its length.
 * @param values The tokens: line, column, length, kind (SCOUT_TOKEN_), four
 *               values each, sorted.
 * @param count Number of values.
 * @return The map (free it with tokenmap_free()), or NULL if out of memory
 *         or there are no tokens.
 */
TokenMap *tokenmap_build(const char *text, size_t length, const long *values, long count);

/**
 * @brief Free a map.
 * @param map The map (may be NULL).
 */
void tokenmap_free(TokenMap *map);

/**
 * @brief Find the tokens of a line by its text.
 * @param map The map.
 * @param text The line's text.
 * @param length Its length.
 * @param[out] count Number of tokens.
 * @return The tokens, or NULL.
 */
const TokenSpan *tokenmap_find(const TokenMap *map, const char *text, size_t length, long *count);

#endif
