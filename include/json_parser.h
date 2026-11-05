/**
 * @file json_parser.h
 * @brief Interface of the JSON parser of Scout: the check of one JSON
 *        file (its syntax up to the first error, keys defined twice), the
 *        keys of its first two levels as symbols, its keys as highlighting
 *        tokens, and the handling of the requests of the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_JSON_PARSER_H
#define TINYDEV_JSON_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the file (1-based lines and byte columns, end exclusive). */
typedef struct JsonDocRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} JsonDocRange;

/** A key of the first two levels of objects. */
typedef struct JsonDocSymbol {
    char *name;         /**< the key, its escapes decoded (allocated) */
    char *detail;       /**< its value: "{...}", "[...]" or the value as written, shortened (allocated) */
    int kind;           /**< SCOUT_SYMBOL_KEY */
    long parent;        /**< index of the key whose object it belongs to, or -1 */
    JsonDocRange range;     /**< the key and its value */
    JsonDocRange selection; /**< the key without its quotes */
} JsonDocSymbol;

/** A problem found in the file. */
typedef struct JsonDocDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    JsonDocRange range; /**< where it is */
} JsonDocDiagnostic;

/** The result of checking a file. */
typedef struct JsonDocResult {
    JsonDocSymbol *symbols;             /**< the keys of the first two levels, in the order of the file */
    long symbol_count;                  /**< number of symbols */
    JsonDocDiagnostic *diagnostics;     /**< the diagnostics, in the order of the file */
    long diagnostic_count;              /**< number of diagnostics */
    long *tokens;                       /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;                   /**< number of tokens (values / 4) */
    int out_of_memory;                  /**< memory ran out; the result is incomplete */
    int stopped;                        /**< json_parser_should_stop() asked to stop; the result is incomplete */
} JsonDocResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (JsonDocResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*json_parser_should_stop)(void);

/**
 * @brief Check a JSON file.
 *
 * The syntax is that of RFC 8259; bytes from 0x80 on are taken as they
 * are in strings (ISO-8859-1). The first syntax error is reported (an
 * error) and ends the check, as what follows a mistake in JSON is rarely
 * worth reporting; until then, a key defined twice in one object is a
 * warning. Comments are reported as errors with a message of their own.
 * An empty file (only blanks) gets no diagnostic: it is a file just
 * begun. Symbols: the keys of the root object, and the keys of objects
 * that are their values (SCOUT_SYMBOL_KEY). Tokens: every key, with its
 * quotes, as SCOUT_TOKEN_TYPE, so it covers the string the lexical rules
 * find exactly.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @return The result (free it with json_doc_result_free()), or NULL if out
 *         of memory.
 */
JsonDocResult *json_parser_analyze(const char *source, size_t length);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void json_doc_result_free(JsonDocResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void json_parser_describe(JsonWriter *w);

/**
 * @brief Answer one request of the Scout protocol.
 *
 * Knows scout/describe and scout/analyze; other methods are answered with
 * the "unknown method" error.
 *
 * @param request The request (one line of JSON).
 * @param response The writer of the answer (one line of JSON, without the
 *                 line end).
 * @return 1 if an answer was written (also an error answer); 0 if the
 *         analysis was stopped (json_parser_should_stop()) and nothing
 *         was written.
 */
int json_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_JSON_PARSER_H */
