#ifndef TINYDEV_SYNTAX_H
#define TINYDEV_SYNTAX_H

#include <stddef.h>

typedef enum SyntaxLanguage {
    SYNTAX_PLAIN = 0,
    SYNTAX_C,
    SYNTAX_AMIGADOS
} SyntaxLanguage;

typedef enum SyntaxStyle {
    SYNTAX_NORMAL = 0,
    SYNTAX_KEYWORD,
    SYNTAX_STRING,
    SYNTAX_COMMENT,
    SYNTAX_PREPROCESSOR
} SyntaxStyle;

typedef enum SyntaxState {
    SYNTAX_STATE_NORMAL = 0,
    SYNTAX_STATE_C_COMMENT = 1
} SyntaxState;

typedef struct SyntaxSpan {
    size_t start;
    size_t end;
    SyntaxStyle style;
} SyntaxSpan;

typedef void (*SyntaxEmit)(void *context, const SyntaxSpan *span);

/**
 * @brief Determine the syntax language implied by a file path.
 *
 * @param path The file path to classify, may be NULL.
 * @return The detected ::SyntaxLanguage (::SYNTAX_PLAIN for unknown paths).
 */
SyntaxLanguage syntax_language_for_path(const char *path);

/**
 * @brief Scan a single line of text and emit its highlighted spans.
 *
 * Emits keyword, string, comment and preprocessor spans for @p language
 * through @p emit. Carries multi-line C block-comment state across calls via
 * @p previous and the return value.
 *
 * @param language The ::SyntaxLanguage to scan for.
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
typedef struct SyntaxHookContext {
    struct Hook hook;
    SyntaxLanguage language;
    void *object;
    unsigned short normal_pen;
    unsigned short keyword_pen;
    unsigned short string_pen;
    unsigned short comment_pen;
    unsigned short preprocessor_pen;
} SyntaxHookContext;
/**
 * @brief Initialise a TextEditor syntax highlighting hook for a language.
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
