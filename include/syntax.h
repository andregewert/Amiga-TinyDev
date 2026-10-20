/**
 * @file syntax.h
 * @brief Portable syntax-scanner interface (languages, styles, line scanning).
 */
#ifndef TINYDEV_SYNTAX_H
#define TINYDEV_SYNTAX_H

#include <stddef.h>

/** Languages the scanners know. */
typedef enum SyntaxLanguage {
    SYNTAX_PLAIN = 0,   /**< plain text, nothing is highlighted */
    SYNTAX_C,           /**< C sources and headers */
    SYNTAX_AMIGADOS     /**< AmigaDOS scripts */
} SyntaxLanguage;

/** Styles of highlighted spans. */
typedef enum SyntaxStyle {
    SYNTAX_NORMAL = 0,  /**< normal text */
    SYNTAX_KEYWORD,     /**< a keyword */
    SYNTAX_STRING,      /**< a string or character constant */
    SYNTAX_COMMENT,     /**< a comment */
    SYNTAX_PREPROCESSOR /**< a preprocessor line */
} SyntaxStyle;

/** State carried from one line to the next. */
typedef enum SyntaxState {
    SYNTAX_STATE_NORMAL = 0,    /**< nothing open */
    SYNTAX_STATE_C_COMMENT = 1  /**< inside a C block comment */
} SyntaxState;

/** A highlighted span of a line (columns from 0, end exclusive). */
typedef struct SyntaxSpan {
    size_t start;       /**< first column */
    size_t end;         /**< column after the span */
    SyntaxStyle style;  /**< its style */
} SyntaxSpan;

/**
 * @brief Callback of syntax_scan_line() for each span.
 * @param context The context given to syntax_scan_line().
 * @param span The span.
 */
typedef void (*SyntaxEmit)(void *context, const SyntaxSpan *span);

/**
 * @brief Determine the syntax language implied by a file path.
 *
 * Detection is purely extension based: ".c"/".h" map to C and
 * ".script"/".dos" map to AmigaDOS scripts. Any other path (or a NULL path)
 * is treated as plain text.
 *
 * @param path The file path to classify, may be NULL.
 * @return The detected ::SyntaxLanguage.
 */
SyntaxLanguage syntax_language_for_path(const char *path);

/**
 * @brief Scan a single line of text and emit its highlighted spans.
 *
 * Walks @p text once, emitting keyword, string, comment and preprocessor
 * spans appropriate for @p language via @p emit. Multi-line C block comments
 * are supported through the @p previous / return state: a line that ends
 * inside an unterminated block comment returns ::SYNTAX_STATE_C_COMMENT so the
 * caller can feed it back in for the following line.
 *
 * @param language The ::SyntaxLanguage to scan for; ::SYNTAX_PLAIN emits nothing.
 * @param text The NUL-terminated line to scan, may be NULL.
 * @param previous The carry-in ::SyntaxState from the preceding line.
 * @param emit Callback invoked for each styled span, may be NULL.
 * @param context Opaque context pointer forwarded to @p emit.
 * @return The carry-out ::SyntaxState to pass to the next line.
 */
SyntaxState syntax_scan_line(SyntaxLanguage language, const char *text,
                             SyntaxState previous, SyntaxEmit emit,
                             void *context);

#ifndef SYNTAX_HOST_TEST
#include <utility/hooks.h>
/** The texteditor.gadget highlighter hook of a document, with its pens. */
typedef struct SyntaxHookContext {
    struct Hook hook;               /**< GA_TEXTEDITOR_HighlighterHook */
    SyntaxLanguage language;        /**< language highlighted */
    void *object;                   /**< the editor while the hook runs */
    unsigned short normal_pen;      /**< pen of normal text */
    unsigned short keyword_pen;     /**< pen of keywords */
    unsigned short string_pen;      /**< pen of strings */
    unsigned short comment_pen;     /**< pen of comments */
    unsigned short preprocessor_pen;    /**< pen of preprocessor lines */
} SyntaxHookContext;
/**
 * @brief Initialise a syntax highlighting hook for a given language.
 *
 * Clears the context, wires up the standard HookEntry dispatcher and the
 * highlight_entry() sub-entry, and records the language. Pens must still be
 * configured separately via syntax_set_pens().
 *
 * @param hook The hook context to initialise.
 * @param language The ::SyntaxLanguage the hook will highlight.
 */
void syntax_init_hook(SyntaxHookContext *hook, SyntaxLanguage language);

/**
 * @brief Configure the rendering pens used by a syntax highlighting hook.
 *
 * @param hook The hook context to update.
 * @param normal_pen Pen used for ordinary, unclassified text.
 * @param keyword_pen Pen used for language keywords.
 * @param string_pen Pen used for string and character literals.
 * @param comment_pen Pen used for comments.
 * @param preprocessor_pen Pen used for preprocessor directives.
 */
void syntax_set_pens(SyntaxHookContext *hook, unsigned short normal_pen,
                     unsigned short keyword_pen, unsigned short string_pen,
                     unsigned short comment_pen,
                     unsigned short preprocessor_pen);
#endif

#endif
