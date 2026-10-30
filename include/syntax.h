/**
 * @file syntax.h
 * @brief Portable highlighting interface (styles, spans, scanning a line
 *        with Scout's rules and tokens).
 */
#ifndef TINYDEV_SYNTAX_H
#define TINYDEV_SYNTAX_H

#include <stddef.h>

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
    SYNTAX_VARIABLE,    /**< a parameter or a script variable */
    SYNTAX_STYLE_COUNT  /**< number of styles */
} SyntaxStyle;

/** A highlighted span of a line (columns from 0, end exclusive). */
typedef struct SyntaxSpan {
    size_t start;       /**< first column */
    size_t end;         /**< column after the span */
    SyntaxStyle style;  /**< its style */
} SyntaxSpan;

/**
 * @brief Callback of syntax_scan() for each span.
 * @param context The context given to syntax_scan().
 * @param span The span.
 */
typedef void (*SyntaxEmit)(void *context, const SyntaxSpan *span);

struct SyntaxRules;
struct TokenMap;

/**
 * @brief Scan a line for highlighting: with the lexical rules of
 *        TinyDev-Scout's parser, then the tokens of the line (types,
 *        functions, macros, constants, parameters of the project) where
 *        the line is normal text.
 *
 * Parameters and variables (SYNTAX_VARIABLE) are also shown in strings and
 * preprocessor lines (AmigaDOS scripts substitute them there), only
 * comments hide them.
 *
 * There are no built-in scanners: without Scout (no rules, no tokens)
 * nothing is highlighted.
 *
 * Only reads @p rules and @p tokens and allocates nothing: it runs in the
 * highlighter hook and in the minimap's render process.
 *
 * @param rules The lexical rules (rules.h), NULL for none.
 * @param tokens The document's tokens (tokenmap.h), NULL for none.
 * @param text The line (terminated, without its line end).
 * @param previous The state the previous line ended in (0 at the start).
 * @param emit Called for each span (may be NULL).
 * @param context Passed to @p emit.
 * @return The state the line ends in.
 */
unsigned long syntax_scan(const struct SyntaxRules *rules, const struct TokenMap *tokens, const char *text,
                          unsigned long previous, SyntaxEmit emit, void *context);

#ifndef SYNTAX_HOST_TEST
#include <utility/hooks.h>
/** The texteditor.gadget highlighter hook of a document, with its pens. */
typedef struct SyntaxHookContext {
    struct Hook hook;               /**< GA_TEXTEDITOR_HighlighterHook */
    void *object;                   /**< the editor while the hook runs */
    unsigned short pens[SYNTAX_STYLE_COUNT];    /**< pen of each style (SyntaxStyle) */
    const struct SyntaxRules *rules;    /**< Scout's lexical rules of the language, NULL */
    const struct TokenMap *tokens;  /**< Scout's tokens of the document, NULL */
} SyntaxHookContext;
/**
 * @brief Initialise a syntax highlighting hook.
 *
 * Clears the context and wires up the standard HookEntry dispatcher and
 * the highlight_entry() sub-entry. Pens must still be configured
 * separately via syntax_set_pens(), the rules and tokens by the caller.
 *
 * @param hook The hook context to initialise.
 */
void syntax_init_hook(SyntaxHookContext *hook);

/**
 * @brief Configure the rendering pens used by a syntax highlighting hook.
 *
 * @param hook The hook context to update.
 * @param pens The pen of each style, SyntaxStyle order (SYNTAX_STYLE_COUNT).
 */
void syntax_set_pens(SyntaxHookContext *hook, const unsigned short *pens);
#endif

#endif
