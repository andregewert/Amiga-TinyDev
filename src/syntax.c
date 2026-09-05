#include "syntax.h"

#include <string.h>

static const char *const c_keywords[] = {
    "_Bool", "_Complex", "_Imaginary", "asm", "auto", "break", "case",
    "char", "const", "continue", "default", "do", "double", "else",
    "enum", "extern", "float", "for", "goto", "if", "inline", "int",
    "long", "register", "restrict", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while"
};

static const char *const dos_keywords[] = {
    "alias", "ask", "assign", "break", "cd", "copy", "date", "delete",
    "dir", "echo", "else", "endif", "endcli", "endshell", "execute",
    "failat", "fault", "filenote", "if", "info", "join", "lab", "list",
    "makedir", "mount", "path", "prompt", "protect", "quit", "rename",
    "resident", "run", "set", "setenv", "skip", "stack", "status",
    "type", "unset", "unsetenv", "version", "wait", "which", "why"
};

static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int suffix_equal(const char *value, const char *suffix)
{
    size_t vl = strlen(value), sl = strlen(suffix), i;
    if (vl < sl) return 0;
    value += vl - sl;
    for (i = 0; i < sl; ++i)
        if (ascii_lower((unsigned char)value[i]) != (unsigned char)suffix[i]) return 0;
    return 1;
}

SyntaxLanguage syntax_language_for_path(const char *path)
{
    if (path == NULL) return SYNTAX_PLAIN;
    if (suffix_equal(path, ".c") || suffix_equal(path, ".h")) return SYNTAX_C;
    if (suffix_equal(path, ".script") || suffix_equal(path, ".dos")) return SYNTAX_AMIGADOS;
    return SYNTAX_PLAIN;
}

static int ident_start(unsigned char c)
{
    return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int ident_char(unsigned char c)
{
    return ident_start(c) || (c >= '0' && c <= '9');
}

static int word_in(const char *word, size_t length, const char *const *words,
                   size_t count, int fold)
{
    size_t i, j;
    for (i = 0; i < count; ++i) {
        if (strlen(words[i]) != length) continue;
        for (j = 0; j < length; ++j) {
            int a = (unsigned char)word[j];
            int b = (unsigned char)words[i][j];
            if (fold) a = ascii_lower(a);
            if (a != b) break;
        }
        if (j == length) return 1;
    }
    return 0;
}

static void output(SyntaxEmit emit, void *context, size_t start, size_t end,
                   SyntaxStyle style)
{
    SyntaxSpan span;
    if (emit == NULL || start >= end) return;
    span.start = start;
    span.end = end;
    span.style = style;
    emit(context, &span);
}

static size_t quoted(const char *text, size_t i, unsigned char quote)
{
    ++i;
    while (text[i] != '\0') {
        if (text[i] == '\\' && text[i + 1] != '\0') i += 2;
        else if ((unsigned char)text[i++] == quote) break;
    }
    return i;
}

SyntaxState syntax_scan_line(SyntaxLanguage language, const char *text,
                             SyntaxState previous, SyntaxEmit emit,
                             void *context)
{
    size_t i = 0, start;
    if (text == NULL || language == SYNTAX_PLAIN) return SYNTAX_STATE_NORMAL;
    if (language == SYNTAX_C && previous == SYNTAX_STATE_C_COMMENT) {
        start = 0;
        while (text[i] != '\0' && !(text[i] == '*' && text[i + 1] == '/')) ++i;
        if (text[i] == '\0') {
            output(emit, context, start, i, SYNTAX_COMMENT);
            return SYNTAX_STATE_C_COMMENT;
        }
        i += 2;
        output(emit, context, start, i, SYNTAX_COMMENT);
    }
    if (language == SYNTAX_C) {
        size_t p = i;
        while (text[p] == ' ' || text[p] == '\t') ++p;
        if (text[p] == '#') {
            output(emit, context, p, strlen(text), SYNTAX_PREPROCESSOR);
            return SYNTAX_STATE_NORMAL;
        }
    }
    while (text[i] != '\0') {
        if (language == SYNTAX_C && text[i] == '/' && text[i + 1] == '/') {
            output(emit, context, i, strlen(text), SYNTAX_COMMENT);
            break;
        }
        if (language == SYNTAX_C && text[i] == '/' && text[i + 1] == '*') {
            start = i; i += 2;
            while (text[i] != '\0' && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            if (text[i] == '\0') {
                output(emit, context, start, i, SYNTAX_COMMENT);
                return SYNTAX_STATE_C_COMMENT;
            }
            i += 2;
            output(emit, context, start, i, SYNTAX_COMMENT);
            continue;
        }
        if (language == SYNTAX_AMIGADOS && text[i] == ';') {
            output(emit, context, i, strlen(text), SYNTAX_COMMENT);
            break;
        }
        if (text[i] == '"' || (language == SYNTAX_C && text[i] == '\'')) {
            start = i;
            i = quoted(text, i, (unsigned char)text[i]);
            output(emit, context, start, i, SYNTAX_STRING);
            continue;
        }
        if (ident_start((unsigned char)text[i])) {
            const char *const *words = language == SYNTAX_C ? c_keywords : dos_keywords;
            size_t count = language == SYNTAX_C
                ? sizeof(c_keywords) / sizeof(c_keywords[0])
                : sizeof(dos_keywords) / sizeof(dos_keywords[0]);
            start = i++;
            while (ident_char((unsigned char)text[i])) ++i;
            if (word_in(text + start, i - start, words, count,
                        language == SYNTAX_AMIGADOS))
                output(emit, context, start, i, SYNTAX_KEYWORD);
            continue;
        }
        ++i;
    }
    return SYNTAX_STATE_NORMAL;
}

#ifndef SYNTAX_HOST_TEST
#include <utility/hooks.h>
#include <gadgets/texteditor.h>
#include <proto/texteditor.h>
#include <clib/alib_protos.h>

static void format_span(void *context, const SyntaxSpan *span)
{
    SyntaxHookContext *hc = (SyntaxHookContext *)context;
    UWORD style = 0;
    switch (span->style) {
        case SYNTAX_KEYWORD: style = TBSTYLE_BOLD | TBSTYLE_SETCOLOR | (2U << 8); break;
        case SYNTAX_STRING: style = TBSTYLE_SETCOLOR | (3U << 8); break;
        case SYNTAX_COMMENT: style = TBSTYLE_ITALIC | TBSTYLE_SETCOLOR | (4U << 8); break;
        case SYNTAX_PREPROCESSOR: style = TBSTYLE_BOLD | TBSTYLE_SETCOLOR | (5U << 8); break;
        default: style = 0; break;
    }
    HighlightSetFormat(hc->object, (ULONG)span->start, (ULONG)span->end, style);
}

static ULONG highlight_entry(struct Hook *hook, APTR object, APTR message)
{
    SyntaxHookContext *hc = (SyntaxHookContext *)hook;
    struct HighlightMessage *hm = (struct HighlightMessage *)message;
    hc->object = object;
    HighlightSetFormat(object, 0, (ULONG)strlen(hm->Text), 0);
    return (ULONG)syntax_scan_line(hc->language, hm->Text,
        (SyntaxState)hm->StatusOfPrevBlock, format_span, hc);
}

void syntax_init_hook(SyntaxHookContext *hc, SyntaxLanguage language)
{
    memset(hc, 0, sizeof(*hc));
    hc->hook.h_Entry = (ULONG (*)())HookEntry;
    hc->hook.h_SubEntry = (ULONG (*)())highlight_entry;
    hc->language = language;
}
#endif
