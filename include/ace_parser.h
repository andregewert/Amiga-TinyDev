/**
 * @file ace_parser.h
 * @brief Interface of the ACE BASIC parser of Scout: the analysis of one
 *        source and the handling of the requests of the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_ACE_PARSER_H
#define TINYDEV_ACE_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the source (1-based lines and byte columns, end exclusive). */
typedef struct AceRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} AceRange;

/** Where a symbol can be seen. */
typedef enum AceScope {
    ACE_SCOPE_GLOBAL,   /**< also by other files: EXTERNAL, or in an include file */
    ACE_SCOPE_FILE,     /**< in its own file */
    ACE_SCOPE_LOCAL     /**< in its SUB, METHOD, STRUCT or CLASS */
} AceScope;

/** A symbol defined in the source. */
typedef struct AceSymbol {
    char *name;         /**< its name as written (allocated) */
    char *detail;       /**< its parameters or type, as text (allocated, may be NULL) */
    int kind;           /**< SCOUT_SYMBOL_ value */
    AceScope scope;     /**< where it can be seen */
    long parent;        /**< index of the SUB or STRUCT it belongs to, or -1 */
    int declaration;    /**< only declared here (DECLARE, EXTERNAL) */
    AceRange range;     /**< the definition */
    AceRange selection; /**< its name */
} AceSymbol;

/** A problem found in the source. */
typedef struct AceDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    AceRange range;     /**< where it is */
} AceDiagnostic;

/** A file named by \#include. */
typedef struct AceReference {
    char *target;       /**< the name as written (allocated) */
    int system;         /**< written in <>: looked up in the include paths only */
    AceRange range;     /**< where it is written */
} AceReference;

/** The result of analysing a source. */
typedef struct AceResult {
    AceSymbol *symbols;         /**< the symbols, in the order of the source */
    long symbol_count;          /**< number of symbols */
    AceDiagnostic *diagnostics; /**< the diagnostics, sorted by place */
    long diagnostic_count;      /**< number of diagnostics */
    AceReference *references;   /**< the includes, in the order of the source */
    long reference_count;       /**< number of references */
    long *tokens;               /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;           /**< number of tokens (values / 4) */
    int out_of_memory;          /**< memory ran out; the result is incomplete */
    int stopped;                /**< ace_parser_should_stop() asked to stop; the result is incomplete */
} AceResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (AceResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*ace_parser_should_stop)(void);

/** @name Classes of reserved words, for ace_parser_word_name() */
/** @{ */
#define ACE_WORD_KEYWORD    0   /**< class "keyword" of the rules */
#define ACE_WORD_TYPE       1   /**< class "type" of the rules */
#define ACE_WORD_FUNCTION   2   /**< class "builtin" of the rules */
/** @} */

/** The constants of ACE's standard include file (class "constant"; NULL-terminated). */
extern const char *const ace_parser_constants[];

/**
 * @brief The name of a reserved word, for the lexical rules.
 * @param word_class The class (ACE_WORD_ value).
 * @param index Its index in the class, from 0.
 * @return Its name in capitals, or NULL after the last.
 */
const char *ace_parser_word_name(int word_class, long index);

/**
 * @brief Analyse an ACE source.
 *
 * Case does not matter. A statement ends at a ':' or a line end; a '~' at
 * the end of a line continues it. The analysis makes two passes: the first
 * finds what is defined and the block structure, the second makes the
 * tokens, so a name may be used before its definition.
 *
 * Symbols: SUB, DEF FN, GENERIC and declared functions as functions, METHODs
 * as methods, STRUCT and CLASS with their members as local fields, CONST as
 * constants, labels, \#define macros, the variables of SHORTINT, LONGINT,
 * ADDRESS, SINGLE, STRING, ATOM, DIM, GLOBAL, COMMON and DECLARE STRUCT.
 * Variables that come into being by being used are not listed. Parameters
 * and what a SUB declares are local to it; what an include file (.h)
 * defines and what is EXTERNAL can be seen by other files.
 *
 * Diagnostics: errors for blocks that do not match (IF ... END IF, FOR ...
 * NEXT, WHILE ... WEND, REPEAT ... UNTIL, CASE, TYPECASE, BLOCK, GENERIC,
 * ASSEM, SUB, METHOD, STRUCT, CLASS), IF without THEN or GOTO, brackets, a
 * comment not closed, \#ifdef and \#endif, a SUB, type, constant or label
 * defined twice; a warning for a string not closed. A source with \#else has
 * both branches read, so its block structure is not checked. The lines
 * between ASSEM and END ASSEM are left alone.
 *
 * Tokens: parameters as SCOUT_TOKEN_PARAMETER, constants and atom literals
 * as SCOUT_TOKEN_CONSTANT, known functions as SCOUT_TOKEN_FUNCTION,
 * structures and classes as SCOUT_TOKEN_TYPE, macros as SCOUT_TOKEN_MACRO.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @param is_include Non-zero for an include file (.h).
 * @return The result (free it with ace_result_free()), or NULL if out of
 *         memory.
 */
AceResult *ace_parser_analyze(const char *source, size_t length, int is_include);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void ace_result_free(AceResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void ace_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (ace_parser_should_stop()) and nothing was
 *         written.
 */
int ace_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_ACE_PARSER_H */
