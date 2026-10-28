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
    SYNTAX_PREPROCESSOR,/**< a preprocessor line */
    SYNTAX_TYPE,        /**< a type (type keyword, or a type of the project) */
    SYNTAX_FUNCTION,    /**< a function (builtin, or a function of the project) */
    SYNTAX_MACRO,       /**< a macro of the project */
    SYNTAX_CONSTANT,    /**< a constant or a number */
    SYNTAX_STYLE_COUNT  /**< number of styles */
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

struct SyntaxRules;
struct TokenMap;

/**
 * @brief Scan a line for highlighting: with the lexical rules of
 *        TinyDev-Scout's parser if there are some, otherwise with the
 *        built-in scanner of the language; then the tokens of the line
 *        (types, functions, macros, constants of the project) where the
 *        line is normal text.
 *
 * Only reads @p rules and @p tokens and allocates nothing: it runs in the
 * highlighter hook and in the minimap's render process.
 *
 * @param language The built-in language (used without rules).
 * @param rules The lexical rules (rules.h), NULL for the built-in scanner.
 * @param tokens The document's tokens (tokenmap.h), NULL for none.
 * @param text The line (terminated, without its line end).
 * @param previous The state the previous line ended in (0 at the start).
 * @param emit Called for each span (may be NULL).
 * @param context Passed to @p emit.
 * @return The state the line ends in.
 */
unsigned long syntax_scan(SyntaxLanguage language, const struct SyntaxRules *rules,
                          const struct TokenMap *tokens, const char *text,
                          unsigned long previous, SyntaxEmit emit, void *context);

#ifndef SYNTAX_HOST_TEST
#include <utility/hooks.h>
/** The texteditor.gadget highlighter hook of a document, with its pens. */
typedef struct SyntaxHookContext {
    struct Hook hook;               /**< GA_TEXTEDITOR_HighlighterHook */
    SyntaxLanguage language;        /**< built-in language highlighted (without rules) */
    void *object;                   /**< the editor while the hook runs */
    unsigned short pens[SYNTAX_STYLE_COUNT];    /**< pen of each style (SyntaxStyle) */
    const struct SyntaxRules *rules;    /**< Scout's lexical rules of the language, NULL */
    const struct TokenMap *tokens;  /**< Scout's tokens of the document, NULL */
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
 * @param pens The pen of each style, SyntaxStyle order (SYNTAX_STYLE_COUNT).
 */
void syntax_set_pens(SyntaxHookContext *hook, const unsigned short *pens);
#endif

#endif
