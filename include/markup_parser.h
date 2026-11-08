/**
 * @file markup_parser.h
 * @brief Interface of the markup parser of Scout: the check of one HTML or
 *        XML file with its links, its headings as symbols, its tags and
 *        attributes as highlighting tokens, and the
 *        handling of the requests of the Scout protocol.
 *
 * Portable C without Amiga headers, so the host tests can use it.
 */
#ifndef TINYDEV_MARKUP_PARSER_H
#define TINYDEV_MARKUP_PARSER_H

#include <stddef.h>
#include "json.h"
#include "scout_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The language a file is checked as. */
typedef enum MarkupLanguage {
    MARKUP_HTML,    /**< HTML: tolerant, names without regard to case */
    MARKUP_XML      /**< XML: must be well-formed, names with case */
} MarkupLanguage;

/** A range in the file (1-based lines and byte columns, end exclusive). */
typedef struct MarkupRange {
    long start_line;    /**< first line */
    long start_col;     /**< first column */
    long end_line;      /**< line of the end */
    long end_col;       /**< column after the last character */
} MarkupRange;

/** A heading of an HTML file. */
typedef struct MarkupSymbol {
    char *name;         /**< its text without tags, entities decoded, blanks collapsed (allocated) */
    char *detail;       /**< its level and text as for Markdown, such as "## Options" (allocated) */
    int kind;           /**< SCOUT_SYMBOL_HEADING */
    long parent;        /**< index of the heading it lies below, or -1 */
    MarkupRange range;      /**< its tags and text */
    MarkupRange selection;  /**< its text, as far as it is on the first line */
} MarkupSymbol;

/** A problem found in the file (always within one line). */
typedef struct MarkupDiagnostic {
    char *message;      /**< one line of text (allocated) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    long line;          /**< its line (from 1) */
    long col;           /**< its first byte column (from 1) */
    long end_col;       /**< the column after it */
} MarkupDiagnostic;

/** A link of an HTML file to another file. */
typedef struct MarkupReference {
    char *target;       /**< the file's path, resolved against the file holding the link (allocated) */
    MarkupRange range;  /**< the link's target as written */
} MarkupReference;

/** The result of checking a file. */
typedef struct MarkupResult {
    MarkupSymbol *symbols;              /**< the headings, in the order of the file (HTML only) */
    long symbol_count;                  /**< number of headings */
    MarkupDiagnostic *diagnostics;      /**< the diagnostics, sorted by place */
    long diagnostic_count;              /**< number of diagnostics */
    MarkupReference *references;        /**< the links to other files, in the order of the file (HTML only) */
    long reference_count;               /**< number of references */
    long *tokens;                       /**< highlighting tokens, four values each: line, column, length, SCOUT_TOKEN_ kind; sorted, no overlaps */
    long token_count;                   /**< number of tokens (values / 4) */
    int out_of_memory;                  /**< memory ran out; the result is incomplete */
    int stopped;                        /**< markup_parser_should_stop() asked to stop; the result is incomplete */
} MarkupResult;

/**
 * @brief The access to other files the analysis needs for checking links.
 *
 * The program gives it the file system; without it, only what lies within
 * the file itself is checked.
 */
typedef struct MarkupFiles {
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
} MarkupFiles;

/**
 * @brief Optional function the analysis calls now and then; when it
 *        returns non-zero, the analysis stops (MarkupResult::stopped).
 *
 * The program sets it to a Ctrl-C check; NULL (the default) never stops.
 */
extern int (*markup_parser_should_stop)(void);

/**
 * @brief Check an HTML or XML file.
 *
 * One pass over the bytes with a stack of the open elements; bytes from
 * 0x80 on count as name characters, nothing is decoded. A mistake does
 * not end the check, at most 200 diagnostics are reported.
 *
 * XML has to be well-formed: elements nested properly and closed, one root
 * element, attributes with quoted values and only once per element, no
 * bare '&' or '<', only known entities (any DOCTYPE may declare others, so
 * then they are not checked; a DTD is not read), the XML declaration at
 * the very start. A file of blanks only gets no diagnostic.
 *
 * HTML is checked tolerantly, as much old HTML is valid or at least works:
 * end tags that are optional may be left out (an element is closed by the
 * start tag that may follow it, or the end of its parent), void elements
 * have no end tag, a short tag (\<br/\>) is accepted for them and inside
 * svg and math; on other elements it is a warning, as HTML ignores the
 * '/' there. A \</p\> whose paragraph a block element has already ended
 * (\<p\>\<ul\>...\</ul\>\</p\>) is a warning, as this is so common.
 * Attribute values without quotes are legal and only a hint.
 * The contents of script, style, textarea, title and xmp are not looked
 * at.
 *
 * Symbols (HTML only): the elements h1 to h6 that are closed and hold
 * text, as an outline of the document (SCOUT_SYMBOL_HEADING).
 *
 * Links (HTML only): href of a, area and link, src of images, scripts,
 * frames and media, data of object, background of body and table cells.
 * A warning for a file that does not exist and for an anchor (#name) that
 * no id, and no name of an a element, makes, also in another HTML file.
 * Left alone: links with a scheme (http:, mailto:, Work:), paths from the
 * root (/page), values with '{', '$' or '\<' (templates), and a whole page
 * with a base element, whose links may point anywhere. References: the
 * links to other files, their targets resolved to paths without "..",
 * which AmigaDOS reads as well; also those to files that do not exist.
 *
 * Both: a closing quote that is missing is an error, found by a '>' and
 * then a '<' inside the supposed value (not for the event attributes of
 * HTML, which hold scripts).
 *
 * Tokens: \<name, the closing \> and \</name\> as SCOUT_TOKEN_TYPE,
 * attribute names as SCOUT_TOKEN_PARAMETER, attribute values and CDATA
 * sections as SCOUT_TOKEN_CONSTANT, entities in the text as
 * SCOUT_TOKEN_MACRO, \<!DOCTYPE and \<?name as SCOUT_TOKEN_FUNCTION.
 *
 * @param source The text (any bytes; need not be terminated).
 * @param length Its length.
 * @param language What to check it as.
 * @param path The file's real path, the base of relative links (may be
 *             NULL: then links to other files are not checked).
 * @param files The access to other files (may be NULL: the same).
 * @return The result (free it with markup_result_free()), or NULL if out
 *         of memory.
 */
MarkupResult *markup_parser_analyze(const char *source, size_t length, MarkupLanguage language, const char *path,
                                    const MarkupFiles *files);

/**
 * @brief Free a result.
 * @param res The result (may be NULL).
 */
void markup_result_free(MarkupResult *res);

/**
 * @brief Write the result of scout/describe.
 * @param w The writer.
 */
void markup_parser_describe(JsonWriter *w);

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
 *         analysis was stopped (markup_parser_should_stop()) and nothing
 *         was written.
 */
int markup_parser_handle_request(const char *request, JsonWriter *response);

#ifdef __cplusplus
}
#endif

#endif /* TINYDEV_MARKUP_PARSER_H */
