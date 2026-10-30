/**
 * @file dos_parser.h
 * @brief Interface of the AmigaDOS script parser of Scout: the analysis of
 *        one script (labels, variables, template parameters, aliases,
 *        IF/ELSE/ENDIF and SKIP checks, highlighting tokens) and the
 *        handling of the requests of the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_DOS_PARSER_H
#define TINYDEV_DOS_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the script (1-based lines and byte columns, end exclusive). */
typedef struct DosRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} DosRange;

/** A symbol defined in the script. */
typedef struct DosSymbol {
    char *name;         /**< its name (allocated) */
    char *detail;       /**< what defines it, as text (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_ value */
    int global;         /**< visible to other scripts (an environment variable) */
    DosRange range;     /**< the line that defines it */
    DosRange selection; /**< its name */
} DosSymbol;

/** A problem found in the script. */
typedef struct DosDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    DosRange range;     /**< where it is */
} DosDiagnostic;

/** The result of analysing a script. */
typedef struct DosParseResult {
    DosSymbol *symbols;         /**< the symbols, in the order found */
    long symbol_count;          /**< number of symbols */
    DosDiagnostic *diagnostics; /**< the diagnostics, in the order of their lines */
    long diagnostic_count;      /**< number of diagnostics */
    long *tokens;               /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;           /**< number of tokens (values / 4) */
    int out_of_memory;          /**< memory ran out; the result is incomplete */
    int stopped;                /**< dos_parser_should_stop() asked to stop; the result is incomplete */
} DosParseResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (DosParseResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*dos_parser_should_stop)(void);

/** @name Words of the lexical rules (NULL-terminated lists, lower case) */
/** @{ */
extern const char *const dos_parser_keywords[];     /**< script control (class "keyword") */
extern const char *const dos_parser_commands[];     /**< commands of C: and the Shell (class "builtin") */
extern const char *const dos_parser_constants[];    /**< words of IF and SKIP (class "constant") */
/** @} */

/**
 * @brief Analyse an AmigaDOS script.
 *
 * Symbols: labels (LAB), the parameters of the .KEY template, local and
 * environment variables (SET, SETENV) and aliases (ALIAS), each at its
 * first definition. Diagnostics: ELSE and ENDIF without IF, IF without
 * ENDIF, a second ELSE, SKIP to a label that is missing or on the wrong
 * side (without or with BACK), a label defined twice, an unterminated
 * string, an unknown dot command, a parameter used in angle brackets that
 * the template does not name. Tokens: template parameters and Shell
 * variables as SCOUT_TOKEN_PARAMETER, labels as SCOUT_TOKEN_LABEL.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @return The result (free it with dos_parse_result_free()), or NULL if
 *         out of memory.
 */
DosParseResult *dos_parser_analyze(const char *source, size_t length);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void dos_parse_result_free(DosParseResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void dos_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (dos_parser_should_stop()) and nothing was
 *         written.
 */
int dos_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_DOS_PARSER_H */
