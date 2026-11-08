/**
 * @file arexx_parser.h
 * @brief Interface of the ARexx parser of Scout: the analysis of one
 *        program (labels, procedures, variables, block structure, jumps,
 *        highlighting tokens) and the handling of the requests of the
 *        Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_AREXX_PARSER_H
#define TINYDEV_AREXX_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the program (1-based lines and byte columns, end exclusive). */
typedef struct ArexxRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} ArexxRange;

/** A symbol defined in the program. */
typedef struct ArexxSymbol {
    char *name;         /**< its name as first written; a stem with its dot, without the tail (allocated) */
    char *detail;       /**< what defines it, as text (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_LABEL, _PROCEDURE or _VARIABLE */
    long parent;        /**< index of the procedure a variable is local to, or -1 */
    ArexxRange range;       /**< the definition */
    ArexxRange selection;   /**< its name */
} ArexxSymbol;

/** A problem found in the program. */
typedef struct ArexxDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    ArexxRange range;   /**< where it is */
} ArexxDiagnostic;

/** The result of analysing a program. */
typedef struct ArexxResult {
    ArexxSymbol *symbols;           /**< the symbols, in the order found */
    long symbol_count;              /**< number of symbols */
    ArexxDiagnostic *diagnostics;   /**< the diagnostics, sorted by place */
    long diagnostic_count;          /**< number of diagnostics */
    long *tokens;                   /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;               /**< number of tokens (values / 4) */
    int out_of_memory;              /**< memory ran out; the result is incomplete */
    int stopped;                    /**< arexx_parser_should_stop() asked to stop; the result is incomplete */
} ArexxResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (ArexxResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*arexx_parser_should_stop)(void);

/** @name Words of the lexical rules (NULL-terminated lists, lower case) */
/** @{ */
extern const char *const arexx_parser_keywords[];       /**< the instructions (class "keyword") */
extern const char *const arexx_parser_constants[];      /**< the special variables (class "constant") */
extern const char *const arexx_parser_functions[];      /**< built-in functions and those of rexxsupport.library (class "function": completion only) */
extern const char *const arexx_parser_subkeywords[];    /**< words with a meaning only inside an instruction (class "subkeyword": completion only) */
/** @} */

/**
 * @brief Analyse an ARexx program.
 *
 * Splits the text into clauses (a line end or ';' ends one, a comma at the
 * end of a line continues it) and looks at each as ARexx does: a symbol and
 * ':' is a label, a symbol and '=' an assignment, even if the symbol is
 * the name of an instruction, a known first word an instruction, anything
 * else a command for the host. The expressions are not parsed, only their
 * parentheses and function calls are looked at.
 *
 * Symbols: labels (those followed by PROCEDURE as procedures) and
 * variables at their first assignment, also through DO, ARG, PULL and
 * PARSE; a compound symbol counts as its stem. A variable assigned between
 * a label with PROCEDURE and the RETURN or EXIT that ends it at the
 * outermost level is local to it, unless it is exposed.
 *
 * Diagnostics: errors for a comment not closed, an invalid hexadecimal
 * or binary string, unbalanced parentheses in a clause, DO or SELECT
 * without END, END without either or with another loop variable, IF or WHEN
 * without THEN, THEN and ELSE without IF, WHEN and OTHERWISE outside SELECT,
 * LEAVE and ITERATE outside a loop, PROCEDURE not directly after a label,
 * and SIGNAL to a label that is missing. Warnings for a label defined
 * twice, SIGNAL ON without its label, a string not closed on its line and
 * a program that does not begin with a comment. A hint for a called
 * name that is neither a label nor a known function, as it may be an
 * external program; none in a program using ADDLIB, whose libraries bring
 * functions the parser cannot know.
 *
 * Tokens: labels, jumps and function calls as SCOUT_TOKEN_FUNCTION,
 * arguments of ARG and PARSE ARG, also where they are used, as
 * SCOUT_TOKEN_PARAMETER, the words with a meaning only inside an
 * instruction (TO, WITH, EXPOSE, the name of the host, ...) as
 * SCOUT_TOKEN_CONSTANT. These names are also common as variables, so the
 * lexical rules cannot mark them.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @return The result (free it with arexx_result_free()), or NULL if out
 *         of memory.
 */
ArexxResult *arexx_parser_analyze(const char *source, size_t length);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void arexx_result_free(ArexxResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void arexx_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (arexx_parser_should_stop()) and nothing
 *         was written.
 */
int arexx_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_AREXX_PARSER_H */
