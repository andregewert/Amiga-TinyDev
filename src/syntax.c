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

/**
 * @brief Fold a single ASCII character to lower case.
 *
 * Only the plain ASCII range 'A'..'Z' is affected; every other value
 * (including non-ASCII bytes) is returned unchanged.
 *
 * @param c The character value to fold.
 * @return The lower-cased character, or @p c unchanged if it is not 'A'..'Z'.
 */
static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/**
 * @brief Test whether a string ends with a given suffix, case-insensitively.
 *
 * The comparison folds ASCII case on the @p value bytes so that, for example,
 * ".C" matches ".c". The @p suffix is compared as-is.
 *
 * @param value The full string to inspect.
 * @param suffix The suffix to look for at the end of @p value.
 * @return Non-zero if @p value ends with @p suffix, zero otherwise.
 */
static int suffix_equal(const char *value, const char *suffix)
{
    size_t vl = strlen(value), sl = strlen(suffix), i;
    if (vl < sl) return 0;
    value += vl - sl;
    for (i = 0; i < sl; ++i)
        if (ascii_lower((unsigned char)value[i]) != (unsigned char)suffix[i]) return 0;
    return 1;
}

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
SyntaxLanguage syntax_language_for_path(const char *path)
{
    if (path == NULL) return SYNTAX_PLAIN;
    if (suffix_equal(path, ".c") || suffix_equal(path, ".h")) return SYNTAX_C;
    if (suffix_equal(path, ".script") || suffix_equal(path, ".dos")) return SYNTAX_AMIGADOS;
    return SYNTAX_PLAIN;
}

/**
 * @brief Test whether a byte may begin an identifier.
 *
 * @param c The byte to classify.
 * @return Non-zero if @p c is an underscore or an ASCII letter, zero otherwise.
 */
static int ident_start(unsigned char c)
{
    return (c == '_') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * @brief Test whether a byte may appear inside an identifier.
 *
 * @param c The byte to classify.
 * @return Non-zero if @p c is an identifier-start character or an ASCII digit,
 *         zero otherwise.
 */
static int ident_char(unsigned char c)
{
    return ident_start(c) || (c >= '0' && c <= '9');
}

/**
 * @brief Check whether a word is present in a keyword table.
 *
 * Compares the @p length bytes at @p word against each entry of @p words,
 * optionally folding ASCII case on the input word so that case-insensitive
 * languages (such as AmigaDOS) match regardless of capitalisation.
 *
 * @param word Pointer to the first byte of the word (not necessarily terminated).
 * @param length Number of bytes making up the word.
 * @param words Array of NUL-terminated keyword strings to search.
 * @param count Number of entries in @p words.
 * @param fold Non-zero to fold ASCII case on @p word before comparing.
 * @return Non-zero if the word matches a keyword, zero otherwise.
 */
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

/**
 * @brief Emit a single styled span through the caller-supplied callback.
 *
 * Empty spans (@p start >= @p end) and a NULL @p emit callback are silently
 * ignored, which keeps the scanner call sites free of guard checks.
 *
 * @param emit The callback that receives the span, may be NULL.
 * @param context Opaque context pointer forwarded to @p emit.
 * @param start Byte offset of the first character of the span.
 * @param end Byte offset one past the last character of the span.
 * @param style The ::SyntaxStyle to attach to the span.
 */
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

/**
 * @brief Scan past a quoted string or character literal.
 *
 * Starting at the opening quote at index @p i, the scan consumes bytes until
 * the matching @p quote is found or the string terminates, honouring
 * backslash escapes so that an escaped quote does not end the literal.
 *
 * @param text The NUL-terminated line being scanned.
 * @param i Index of the opening quote character.
 * @param quote The quote byte that terminates the literal.
 * @return The index one past the closing quote, or the end of @p text if the
 *         literal is unterminated.
 */
static size_t quoted(const char *text, size_t i, unsigned char quote)
{
    ++i;
    while (text[i] != '\0') {
        if (text[i] == '\\' && text[i + 1] != '\0') i += 2;
        else if ((unsigned char)text[i++] == quote) break;
    }
    return i;
}

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

/**
 * @brief Apply TextEditor formatting for one scanned span.
 *
 * Used as the ::SyntaxEmit callback while highlighting inside the ReAction
 * texteditor.gadget. Maps the span's ::SyntaxStyle to the appropriate
 * TBSTYLE flags and configured pen, then calls HighlightSetFormat() on the
 * target object.
 *
 * @param context The ::SyntaxHookContext (as a void pointer) driving the run.
 * @param span The styled span to format.
 */
static void format_span(void *context, const SyntaxSpan *span)
{
    SyntaxHookContext *hc = (SyntaxHookContext *)context;
    UWORD style = 0;
    switch (span->style) {
        case SYNTAX_KEYWORD:
            style = (UWORD)(TBSTYLE_BOLD | TBSTYLE_SETCOLOR |
                            ((hc->keyword_pen & 0xffU) << 8));
            break;
        case SYNTAX_STRING:
            style = (UWORD)(TBSTYLE_SETCOLOR |
                            ((hc->string_pen & 0xffU) << 8));
            break;
        case SYNTAX_COMMENT:
            style = (UWORD)(TBSTYLE_ITALIC | TBSTYLE_SETCOLOR |
                            ((hc->comment_pen & 0xffU) << 8));
            break;
        case SYNTAX_PREPROCESSOR:
            style = (UWORD)(TBSTYLE_BOLD | TBSTYLE_SETCOLOR |
                            ((hc->preprocessor_pen & 0xffU) << 8));
            break;
        default: style = 0; break;
    }
    HighlightSetFormat(hc->object, (ULONG)span->start, (ULONG)span->end, style);
}

/**
 * @brief TextEditor highlighting hook sub-entry.
 *
 * Invoked by the texteditor.gadget for every line that needs highlighting.
 * It first resets the whole line to the normal pen, then runs
 * syntax_scan_line() to overlay the language-specific styles.
 *
 * @param hook The hook, which is really a ::SyntaxHookContext.
 * @param object The texteditor gadget object being highlighted.
 * @param message A struct HighlightMessage describing the line.
 * @return The carry-out ::SyntaxState for the next line, as a ULONG.
 */
static ULONG highlight_entry(struct Hook *hook, APTR object, APTR message)
{
    SyntaxHookContext *hc = (SyntaxHookContext *)hook;
    struct HighlightMessage *hm = (struct HighlightMessage *)message;
    UWORD normal_style;
    hc->object = object;
    normal_style = (UWORD)(TBSTYLE_SETCOLOR |
                           ((hc->normal_pen & 0xffU) << 8));
    HighlightSetFormat(object, 0, (ULONG)strlen(hm->Text), normal_style);
    return (ULONG)syntax_scan_line(hc->language, hm->Text,
        (SyntaxState)hm->StatusOfPrevBlock, format_span, hc);
}

/**
 * @brief Initialise a syntax highlighting hook for a given language.
 *
 * Clears the context, wires up the standard HookEntry dispatcher and the
 * highlight_entry() sub-entry, and records the language. Pens must still be
 * configured separately via syntax_set_pens().
 *
 * @param hc The hook context to initialise.
 * @param language The ::SyntaxLanguage the hook will highlight.
 */
void syntax_init_hook(SyntaxHookContext *hc, SyntaxLanguage language)
{
    memset(hc, 0, sizeof(*hc));
    hc->hook.h_Entry = (ULONG (*)())HookEntry;
    hc->hook.h_SubEntry = (ULONG (*)())highlight_entry;
    hc->language = language;
}

/**
 * @brief Configure the rendering pens used by a syntax highlighting hook.
 *
 * @param hc The hook context to update.
 * @param normal_pen Pen used for ordinary, unclassified text.
 * @param keyword_pen Pen used for language keywords.
 * @param string_pen Pen used for string and character literals.
 * @param comment_pen Pen used for comments.
 * @param preprocessor_pen Pen used for preprocessor directives.
 */
void syntax_set_pens(SyntaxHookContext *hc, unsigned short normal_pen,
                     unsigned short keyword_pen, unsigned short string_pen,
                     unsigned short comment_pen,
                     unsigned short preprocessor_pen)
{
    hc->normal_pen = normal_pen;
    hc->keyword_pen = keyword_pen;
    hc->string_pen = string_pen;
    hc->comment_pen = comment_pen;
    hc->preprocessor_pen = preprocessor_pen;
}
#endif
