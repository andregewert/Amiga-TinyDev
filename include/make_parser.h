/**
 * @file make_parser.h
 * @brief Interface of the Makefile parser of Scout: the analysis of one
 *        Makefile (variables, targets, conditionals, recipes, highlighting
 *        tokens) and the handling of the requests of the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_MAKE_PARSER_H
#define TINYDEV_MAKE_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the Makefile (1-based lines and byte columns, end exclusive). */
typedef struct MakeRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} MakeRange;

/** A symbol defined in the Makefile. */
typedef struct MakeSymbol {
    char *name;         /**< its name (allocated) */
    char *detail;       /**< its definition as text (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_VARIABLE, SCOUT_SYMBOL_FUNCTION (a target) or SCOUT_SYMBOL_MACRO (define) */
    MakeRange range;    /**< the line that defines it (with its continuation lines) */
    MakeRange selection;/**< its name */
} MakeSymbol;

/** A problem found in the Makefile. */
typedef struct MakeDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    MakeRange range;    /**< where it is */
} MakeDiagnostic;

/** An included Makefile. */
typedef struct MakeReference {
    char *target;       /**< the name as written (allocated) */
    MakeRange range;    /**< where the name is */
} MakeReference;

/** The result of analysing a Makefile. */
typedef struct MakeParseResult {
    MakeSymbol *symbols;            /**< the symbols, in the order found */
    long symbol_count;              /**< number of symbols */
    MakeDiagnostic *diagnostics;    /**< the diagnostics, in the order of their lines */
    long diagnostic_count;          /**< number of diagnostics */
    MakeReference *references;      /**< the included files */
    long reference_count;           /**< number of includes */
    long *tokens;                   /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;               /**< number of tokens (values / 4) */
    int out_of_memory;              /**< memory ran out; the result is incomplete */
    int stopped;                    /**< make_parser_should_stop() asked to stop; the result is incomplete */
} MakeParseResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (MakeParseResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*make_parser_should_stop)(void);

/** @name Words of the lexical rules (NULL-terminated lists) */
/** @{ */
extern const char *const make_parser_keywords[];    /**< directives: offered by completion only (class "directive"), as the words also occur in paths; the tokens mark them where they are directives */
extern const char *const make_parser_functions[];   /**< functions of GNU make: offered by completion only (class "function"); the tokens mark them where they are called */
/** @} */

/**
 * @brief Analyse a Makefile.
 *
 * The rules are those of GNU make, which smake (SAS/C) and the make of
 * vbcc's users read alike for simple Makefiles. Symbols: variables (=,
 * :=, ::=, ?=, +=, !=, also after export or override), define blocks and
 * targets (explicit and pattern rules; special targets such as .PHONY
 * are left out), each at its first definition. References: include,
 * -include and sinclude. Diagnostics: a recipe line before the first
 * target, a recipe indented with spaces instead of a tab, a line that is
 * neither a rule, an assignment nor a directive ("missing separator"),
 * else, endif and endef without their opening line, conditionals and
 * define blocks left open, an unterminated variable reference, an unknown
 * function, a target with two recipes. Tokens: variable references,
 * automatic variables ($@, $<, ...) and defined variables as
 * SCOUT_TOKEN_PARAMETER (the kind with a colour of its own in the editor;
 * SCOUT_TOKEN_VARIABLE has none), targets and the functions of make where
 * they are called as SCOUT_TOKEN_FUNCTION, and directives where they are
 * directives as SCOUT_TOKEN_TYPE.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @return The result (free it with make_parse_result_free()), or NULL if
 *         out of memory.
 */
MakeParseResult *make_parser_analyze(const char *source, size_t length);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void make_parse_result_free(MakeParseResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void make_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (make_parser_should_stop()) and nothing was
 *         written.
 */
int make_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_MAKE_PARSER_H */
