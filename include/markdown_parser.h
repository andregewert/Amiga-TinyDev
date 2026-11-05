/**
 * @file markdown_parser.h
 * @brief Interface of the Markdown parser of Scout: the analysis of one
 *        Markdown file (headings as symbols, dead links and anchors, links
 *        to other files as references, highlighting tokens) and the handling of the requests of the
 *        Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_MARKDOWN_PARSER_H
#define TINYDEV_MARKDOWN_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A range in the file (1-based lines and byte columns, end exclusive). */
typedef struct MarkdownRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} MarkdownRange;

/** A heading of the file. */
typedef struct MarkdownSymbol {
    char *name;         /**< its text without inline markup (allocated) */
    char *detail;       /**< its level and text, such as "## Options" (allocated) */
    int kind;           /**< SCOUT_SYMBOL_HEADING */
    int level;          /**< its level, 1 to 6 */
    long parent;        /**< index of the heading it lies below, or -1 */
    MarkdownRange range;    /**< its lines (with the underline of a Setext heading) */
    MarkdownRange selection;/**< its text */
} MarkdownSymbol;

/** A problem found in the file. */
typedef struct MarkdownDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    MarkdownRange range;/**< where it is */
} MarkdownDiagnostic;

/** A link to another file. */
typedef struct MarkdownReference {
    char *target;       /**< the file's path, resolved against the file holding the link (allocated) */
    MarkdownRange range;/**< the link's target as written */
} MarkdownReference;

/** The result of analysing a file. */
typedef struct MarkdownParseResult {
    MarkdownSymbol *symbols;            /**< the headings, in the order of the file */
    long symbol_count;                  /**< number of headings */
    MarkdownDiagnostic *diagnostics;    /**< the diagnostics, in the order of their lines */
    long diagnostic_count;              /**< number of diagnostics */
    MarkdownReference *references;      /**< the links to other files, in the order of the file */
    long reference_count;               /**< number of references */
    long *tokens;                       /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;                   /**< number of tokens (values / 4) */
    int out_of_memory;                  /**< memory ran out; the result is incomplete */
    int stopped;                        /**< markdown_parser_should_stop() asked to stop; the result is incomplete */
} MarkdownParseResult;

/**
 * @brief The access to other files the analysis needs for checking links.
 *
 * The program gives it the file system; without it, only what lies within
 * the file itself is checked.
 */
typedef struct MarkdownFiles {
    /**
     * @brief Tell whether a file or drawer exists.
     * @param path Its path.
     * @return Non-zero if it does.
     */
    int (*exists)(const char *path);
    /**
     * @brief Read a whole file.
     * @param path Its path.
     * @param[out] length Its length.
     * @return Its contents (the analysis free()s them), or NULL.
     */
    char *(*read)(const char *path, size_t *length);
} MarkdownFiles;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (MarkdownParseResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*markdown_parser_should_stop)(void);

/**
 * @brief Analyse a Markdown file.
 *
 * The dialect is GitHub's, as read by mdtohtml and Kickdown. Symbols: ATX
 * and Setext headings. Diagnostics (warnings): a link or image whose file
 * does not exist, an anchor (#name) no heading of its file makes, with
 * the rule of mdtohtml for a heading's anchor; a reference ([text][name])
 * without a definition; a link without a target; a code fence not closed.
 * Links with a scheme (http:, mailto:, Work:) and paths from the root
 * (/page) are not looked at. A link to x.html counts as found if x.md or
 * x.markdown exists, as mdtohtml makes one from the other. References:
 * the links and images to other files (with a @p path), their targets
 * resolved to paths without "..", which AmigaDOS reads as well (the
 * Markdown file, when x.html stands for it); also links to files that do
 * not exist. Tokens:
 * headings as SCOUT_TOKEN_TYPE, code as SCOUT_TOKEN_CONSTANT, link targets
 * and autolinks as SCOUT_TOKEN_MACRO; markers (of lists, task boxes, block
 * quotes, tables, thematic breaks, emphasis) as SCOUT_TOKEN_FUNCTION; HTML
 * tags and entities as SCOUT_TOKEN_PARAMETER.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @param path The file's real path, the base of relative links (may be
 *             NULL: then links to other files are not checked).
 * @param files The access to other files (may be NULL: the same).
 * @return The result (free it with markdown_parse_result_free()), or NULL
 *         if out of memory.
 */
MarkdownParseResult *markdown_parser_analyze(const char *source, size_t length, const char *path,
                                             const MarkdownFiles *files);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void markdown_parse_result_free(MarkdownParseResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void markdown_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (markdown_parser_should_stop()) and nothing
 *         was written.
 */
int markdown_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_MARKDOWN_PARSER_H */
