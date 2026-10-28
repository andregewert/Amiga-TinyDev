/**
 * @file c_parser.h
 * @brief Interface of the C parser of Scout: the analysis of one C source
 *        (symbols, diagnostics, includes, highlighting tokens) and the
 *        handling of the requests of the Scout protocol.
 *
 * The analysis is heuristic: it does not preprocess, does not know the
 * included headers and takes the first branch of an \#if as the code (the
 * other branches only get highlighting tokens). It is portable C without
 * Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_C_PARSER_H
#define TINYDEV_C_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Visibility of a symbol ("scope" in the protocol). */
typedef enum CScope {
    C_SCOPE_GLOBAL,     /**< visible to other files */
    C_SCOPE_FILE,       /**< only in this file */
    C_SCOPE_LOCAL       /**< inside another symbol (field, parameter, local) */
} CScope;

/** A range in the source (1-based lines and byte columns, end exclusive). */
typedef struct CRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} CRange;

/** A symbol defined (or declared) in the source. */
typedef struct CSymbol {
    char *name;         /**< its name (allocated) */
    char *detail;       /**< its declaration as text (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_ value */
    CScope scope;       /**< its visibility */
    long parent;        /**< index of the symbol it belongs to, -1 for none */
    int declaration;    /**< only declared here (prototype, extern) */
    CRange range;       /**< the whole definition */
    CRange selection;   /**< its name */
} CSymbol;

/** A problem found in the source. */
typedef struct CDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    CRange range;       /**< where it is */
} CDiagnostic;

/** A file the source includes. */
typedef struct CReference {
    char *target;       /**< the name as written, without quotes or brackets (allocated) */
    int system;         /**< written with <...> */
    CRange range;       /**< where the name is, quotes or brackets included */
} CReference;

/** The result of analysing a C source. */
typedef struct CParseResult {
    CSymbol *symbols;           /**< the symbols, in the order found (a parent before its members) */
    long symbol_count;          /**< number of symbols */
    CDiagnostic *diagnostics;   /**< the diagnostics, in the order found */
    long diagnostic_count;      /**< number of diagnostics */
    CReference *references;     /**< the includes */
    long reference_count;       /**< number of includes */
    long *tokens;               /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;           /**< number of tokens (values / 4) */
    int out_of_memory;          /**< memory ran out; the result is incomplete */
    int stopped;                /**< c_parser_should_stop() asked to stop; the result is incomplete */
} CParseResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (CParseResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*c_parser_should_stop)(void);

/** @name Words of the lexical rules (NULL-terminated lists) */
/** @{ */
extern const char *const c_parser_keywords[];   /**< keywords (class "keyword") */
extern const char *const c_parser_types[];      /**< type keywords (class "type") */
extern const char *const c_parser_constants[];  /**< constants (class "constant") */
/** @} */

/**
 * @brief Analyse C source text.
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @param is_header Whether it is a header: its macros, types and
 *                  enumerations are then visible to other files
 *                  (C_SCOPE_GLOBAL), otherwise only in the file.
 * @return The result (free it with c_parse_result_free()), or NULL if out
 *         of memory.
 */
CParseResult *c_parser_analyze(const char *source, size_t length, int is_header);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void c_parse_result_free(CParseResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void c_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (c_parser_should_stop()) and nothing was
 *         written.
 */
int c_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_C_PARSER_H */
