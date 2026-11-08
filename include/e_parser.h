/**
 * @file e_parser.h
 * @brief Interface of the AmigaE parser of Scout: the analysis of an E source
 *        (as E-VO reads it) or of a compiled module (.m), and the handling of
 *        the requests of the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_E_PARSER_H
#define TINYDEV_E_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the source (1-based lines and byte columns, end exclusive). */
typedef struct ERange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} ERange;

/** Where a symbol can be seen. */
typedef enum EScope {
    E_SCOPE_GLOBAL,     /**< also by other files: EXPORT, or OPT EXPORT */
    E_SCOPE_FILE,       /**< in its own file */
    E_SCOPE_LOCAL       /**< in its PROC or OBJECT */
} EScope;

/** A symbol defined in the source. */
typedef struct ESymbol {
    char *name;         /**< its name as written (allocated) */
    char *detail;       /**< what defines it, as text (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_ value */
    EScope scope;       /**< where it can be seen */
    long parent;        /**< index of the PROC or OBJECT it belongs to, or -1 */
    ERange range;       /**< the definition */
    ERange selection;   /**< its name */
} ESymbol;

/** A problem found in the source. */
typedef struct EDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    ERange range;       /**< where it is */
} EDiagnostic;

/** A module named by MODULE. */
typedef struct EReference {
    char *target;       /**< the name as written, without a leading '*' (allocated) */
    int local;          /**< it had a '*': relative to the source */
    ERange range;       /**< where it is written */
} EReference;

/** The result of analysing a source. */
typedef struct EResult {
    ESymbol *symbols;           /**< the symbols, in the order of the source */
    long symbol_count;          /**< number of symbols */
    EDiagnostic *diagnostics;   /**< the diagnostics, sorted by place */
    long diagnostic_count;      /**< number of diagnostics */
    EReference *references;     /**< the modules, in the order of the source */
    long reference_count;       /**< number of references */
    long *tokens;               /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;           /**< number of tokens (values / 4) */
    int binary;                 /**< it was a compiled module: the symbols have no place to go to */
    int out_of_memory;          /**< memory ran out; the result is incomplete */
    int stopped;                /**< e_parser_should_stop() asked to stop; the result is incomplete */
} EResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (EResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*e_parser_should_stop)(void);

/** @name Words of the lexical rules (NULL-terminated lists) */
/** @{ */
extern const char *const e_parser_constants[];  /**< the built-in constants (class "constant") */
extern const char *const e_parser_functions[];  /**< the built-in functions (class "builtin") */
extern const char *const e_parser_variables[];  /**< the built-in variables (class "variable", completion only) */
/** @} */

/**
 * @brief The name of a keyword, for the lexical rules (class "keyword").
 * @param index Its index, from 0.
 * @return Its name, or NULL after the last.
 */
const char *e_parser_keyword_name(long index);

/**
 * @brief The name of a built-in type, for the lexical rules (class "type").
 * @param index Its index, from 0.
 * @return Its name, or NULL after the last.
 */
const char *e_parser_type_name(long index);

/**
 * @brief Analyse an E source or a compiled module.
 *
 * A text is read statement by statement, in two passes: the first finds the
 * definitions and the block structure, the second makes the tokens, so a
 * name may be used before its definition. A line end ends a statement,
 * unless a bracket is open or the line ends in a comma, an operator or one
 * of AND, OR, BUT, THEN, IS; a ';' separates statements.
 *
 * Symbols: PROCs as functions and their methods (PROC x() OF y) as methods,
 * parameters and local DEFs as local variables of their PROC, global DEFs,
 * CONST, ENUM and SET as constants, OBJECTs as structures with their members
 * as local fields, labels, and the macros of \#define. A name is seen by
 * other files if it has EXPORT or the source has OPT EXPORT.
 *
 * Diagnostics: errors for blocks that do not match (IF, FOR, WHILE, REPEAT,
 * LOOP, SELECT, TRY, a one-line form with THEN or DO opens none), PROC and
 * OBJECT not closed or nested, brackets, a comment not closed, \#ifdef and
 * \#endif, names defined twice; a warning for a string not closed; a hint
 * if a source that is not a module has no PROC main. A source with \#else
 * or \#elif* has both branches read, so its block structure is not checked.
 * Inline assembly is left alone.
 *
 * Tokens: parameters as SCOUT_TOKEN_PARAMETER, constants (known, or by E's
 * rule that a name starting with two capitals is one) as
 * SCOUT_TOKEN_CONSTANT, called and known functions as SCOUT_TOKEN_FUNCTION,
 * objects and the names after PTR TO, OF or :: as SCOUT_TOKEN_TYPE, macros
 * as SCOUT_TOKEN_MACRO.
 *
 * Bytes starting with "EMOD" are a compiled module: its constants, objects
 * with their public members and methods, procedures, globals, macros and
 * library functions become global symbols on line 1 and EResult::binary
 * is set. A damaged module gives what could be read before the damage.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @return The result (free it with e_result_free()), or NULL if out of
 *         memory.
 */
EResult *e_parser_analyze(const char *source, size_t length);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void e_result_free(EResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void e_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (e_parser_should_stop()) and nothing was
 *         written.
 */
int e_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_E_PARSER_H */
