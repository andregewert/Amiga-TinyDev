/**
 * @file pascal_parser.h
 * @brief Interface of the Free Pascal parser of Scout: the analysis of one
 *        source and the handling of the requests of the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_PASCAL_PARSER_H
#define TINYDEV_PASCAL_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the source (1-based lines and byte columns, end exclusive). */
typedef struct PasRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} PasRange;

/** Where a symbol can be seen. */
typedef enum PasScope {
    PAS_SCOPE_GLOBAL,   /**< also by other files: the interface of a unit, an include file */
    PAS_SCOPE_FILE,     /**< in its own file */
    PAS_SCOPE_LOCAL     /**< in its routine, record or class */
} PasScope;

/** A symbol defined in the source. */
typedef struct PasSymbol {
    char *name;         /**< its name as written (allocated) */
    char *detail;       /**< its parameters or type, as text (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_ value */
    PasScope scope;     /**< where it can be seen */
    long parent;        /**< index of the routine, record or class it belongs to, or -1 */
    int declaration;    /**< only declared here (interface, forward, external, in a class) */
    PasRange range;     /**< the definition */
    PasRange selection; /**< its name */
} PasSymbol;

/** A problem found in the source. */
typedef struct PasDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    PasRange range;     /**< where it is */
} PasDiagnostic;

/** A unit named by uses, or a file named by \$I. */
typedef struct PasReference {
    char *target;       /**< the unit's name, or the file (allocated) */
    int unit;           /**< a unit by its name: the file's extension is still to be found */
    int import;         /**< from uses (otherwise an include) */
    PasRange range;     /**< where it is written */
} PasReference;

/** The result of analysing a source. */
typedef struct PasResult {
    PasSymbol *symbols;         /**< the symbols */
    long symbol_count;          /**< number of symbols */
    PasDiagnostic *diagnostics; /**< the diagnostics, sorted by place */
    long diagnostic_count;      /**< number of diagnostics */
    PasReference *references;   /**< the units and includes, in the order of the source */
    long reference_count;       /**< number of references */
    long *tokens;               /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;           /**< number of tokens (values / 4) */
    int out_of_memory;          /**< memory ran out; the result is incomplete */
    int stopped;                /**< pascal_parser_should_stop() asked to stop; the result is incomplete */
} PasResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (PasResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*pascal_parser_should_stop)(void);

/** @name Words of the lexical rules (NULL-terminated lists) */
/** @{ */
extern const char *const pascal_parser_types[];     /**< predefined types (class "type") */
extern const char *const pascal_parser_builtins[];  /**< predefined routines (class "builtin") */
extern const char *const pascal_parser_constants[]; /**< predefined constants (class "constant") */
/** @} */

/**
 * @brief The name of a keyword, for the lexical rules (class "keyword").
 * @param index Its index, from 0.
 * @return Its name in lower case, or NULL after the last.
 */
const char *pascal_parser_keyword_name(long index);

/**
 * @brief Analyse a Pascal source.
 *
 * Case does not matter. The analysis reads the source once into tokens and
 * makes two passes over them: the first finds what is defined and the block
 * structure, the second makes the tokens, so a name may be used before its
 * definition. Expressions are not parsed; a '\$I' file is not read. What
 * follows the final "end." is ignored, as by the compiler.
 *
 * Symbols: the program or unit, routines and methods (in a class and at
 * their definition), their parameters and what they declare, labels, const,
 * type (records, objects, classes, interfaces with their fields, methods and
 * properties; enumerations with their members; other types) and var, and
 * the names of \$DEFINE. Names of a unit's interface can be seen by other
 * files.
 *
 * Diagnostics: errors for blocks that do not match (BEGIN, CASE, TRY, ASM
 * and END, REPEAT and UNTIL, EXCEPT or FINALLY without TRY), IF without
 * THEN, WHILE, FOR or WITH without DO, CASE without OF, a ';' that seems
 * missing at the end of a line, a ';' before ELSE, '=' for ':=' at the start
 * of a statement, brackets, a string or comment not closed, a declaration
 * without its ':' or '=', a missing final END, a routine without a body, \$IF
 * and \$ENDIF, and a constant, type or variable defined twice at the outer
 * level. A source with \$ELSE has both branches read and an include file
 * (.inc) is a fragment, so the structure of these is not checked.
 *
 * Tokens: parameters as SCOUT_TOKEN_PARAMETER, constants and enumeration
 * members as SCOUT_TOKEN_CONSTANT, routines as SCOUT_TOKEN_FUNCTION, types
 * as SCOUT_TOKEN_TYPE, the names of \$DEFINE as SCOUT_TOKEN_MACRO.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @param is_include Non-zero for an include file (.inc).
 * @return The result (free it with pascal_result_free()), or NULL if out of
 *         memory.
 */
PasResult *pascal_parser_analyze(const char *source, size_t length, int is_include);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void pascal_result_free(PasResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void pascal_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (pascal_parser_should_stop()) and nothing
 *         was written.
 */
int pascal_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_PASCAL_PARSER_H */
