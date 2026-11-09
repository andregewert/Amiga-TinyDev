/**
 * @file guide_parser.h
 * @brief Interface of the AmigaGuide parser of Scout: the check of one
 *        AmigaGuide file, its nodes and macros as symbols, its commands as
 *        highlighting tokens, and the handling of the requests of the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_GUIDE_PARSER_H
#define TINYDEV_GUIDE_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the file (1-based lines and byte columns, end exclusive). */
typedef struct GuideRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} GuideRange;

/** A node or macro of the file, as a symbol. */
typedef struct GuideSymbol {
    char *name;         /**< its name (allocated) */
    char *detail;       /**< a node's title or a macro's text, or NULL (allocated) */
    int kind;           /**< SCOUT_SYMBOL_HEADING for a node, SCOUT_SYMBOL_MACRO for a macro */
    GuideRange range;   /**< from \@node to its end, or the \@macro line */
    GuideRange selection; /**< the name in the defining line */
} GuideSymbol;

/** A problem found in the file. */
typedef struct GuideDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    GuideRange range;   /**< where it is */
} GuideDiagnostic;

/** The result of checking a file. */
typedef struct GuideResult {
    GuideSymbol *symbols;           /**< the nodes and macros, in the order of the file */
    long symbol_count;              /**< number of symbols */
    GuideDiagnostic *diagnostics;   /**< the diagnostics, in the order of the file */
    long diagnostic_count;          /**< number of diagnostics */
    long *tokens;                   /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind */
    long token_count;               /**< number of tokens (values / 4) */
    int out_of_memory;              /**< memory ran out; the result is incomplete */
    int stopped;                    /**< guide_parser_should_stop() asked to stop; the result is incomplete */
} GuideResult;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (GuideResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*guide_parser_should_stop)(void);

/**
 * @brief Check an AmigaGuide file.
 *
 * The language is that of amigaguide.datatype up to version 47 (AmigaOS
 * 3.2), with \@{IMG path}. Names of commands and nodes are compared without
 * regard to case. There is a first pass for the nodes and macros and a
 * second one for the checks, so a link may lead to a node further down.
 *
 * Errors: the first line is not \@database; a text command not closed on
 * its line (AmigaGuide cannot break them) or holding a '{'; a quote not
 * closed; a label without a command; a missing argument that the command
 * cannot do without. Warnings: an unknown command; a command in the wrong
 * place (inside or outside a node); \@node and \@endnode that do not pair
 * up; a node defined twice; a link or \@next, \@prev, \@toc, \@help, \@index
 * to a node this file does not have (targets holding '/' or ':' are in
 * other files and not looked at); an argument that should be a number or
 * a colour. An empty file (only blanks) gets no diagnostic.
 *
 * Symbols: every \@node with a name (kind heading, as the sections of a
 * document), the title being the second argument of \@node or else the
 * node's \@title. A node ends at its \@endnode, or before the next \@node or
 * at the end of the file if it has none. Every \@macro with a name (kind
 * macro), the detail being the text it stands for.
 *
 * Tokens: the name of a line command and of a text command as
 * SCOUT_TOKEN_FUNCTION, "\@{" and "}" as SCOUT_TOKEN_MACRO, a link's label
 * as SCOUT_TOKEN_CONSTANT, a node name as SCOUT_TOKEN_LABEL, other
 * arguments as SCOUT_TOKEN_PARAMETER. The lexical rules only know \@rem.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @return The result (free it with guide_result_free()), or NULL if out of
 *         memory.
 */
GuideResult *guide_parser_analyze(const char *source, size_t length);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void guide_result_free(GuideResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void guide_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (guide_parser_should_stop()) and nothing
 *         was written.
 */
int guide_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_GUIDE_PARSER_H */
