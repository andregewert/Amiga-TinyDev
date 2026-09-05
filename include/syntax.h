#ifndef AMIEDITOR_SYNTAX_H
#define AMIEDITOR_SYNTAX_H

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

SyntaxLanguage syntax_language_for_path(const char *path);
SyntaxState syntax_scan_line(SyntaxLanguage language, const char *text,
                             SyntaxState previous, SyntaxEmit emit,
                             void *context);

#ifndef SYNTAX_HOST_TEST
#include <utility/hooks.h>
typedef struct SyntaxHookContext {
    struct Hook hook;
    SyntaxLanguage language;
    void *object;
} SyntaxHookContext;
void syntax_init_hook(SyntaxHookContext *hook, SyntaxLanguage language);
#endif

#endif
