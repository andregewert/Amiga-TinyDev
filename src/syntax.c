/**
 * @file syntax.c
 * @brief The combined scan of a line (Scout's lexical rules, then its
 *        tokens) and the texteditor.gadget highlighter hook.
 *
 * TinyDev has no scanners of its own: the language of a document and its
 * rules come from TinyDev-Scout (scoutlink.c), so without Scout nothing
 * is highlighted.
 */
#include "syntax.h"
#include "rules.h"
#include "tokenmap.h"

#include <string.h>

/** Most styled spans of a line remembered for the token layer. */
#define RECORD_MAX 48

/** The styled spans of a line, recorded while they are passed on. */
typedef struct Recorder {
    SyntaxEmit emit;            /**< where the spans go */
    void *context;              /**< its context */
    size_t start[RECORD_MAX];   /**< first column of each span */
    size_t end[RECORD_MAX];     /**< column after it */
    unsigned char style[RECORD_MAX];    /**< its style (SyntaxStyle) */
    int count;                  /**< number of spans */
    int full;                   /**< more spans than RECORD_MAX */
} Recorder;

/**
 * @brief SyntaxEmit callback that passes a span on and remembers it unless
 *        it is normal text.
 * @param context The Recorder.
 * @param span The span.
 */
static void record(void *context, const SyntaxSpan *span)
{
    Recorder *r = (Recorder *)context;
    if (r->emit != NULL) r->emit(r->context, span);
    if (span->style == SYNTAX_NORMAL) return;
    if (r->count == RECORD_MAX) { r->full = 1; return; }
    r->start[r->count] = span->start;
    r->end[r->count] = span->end;
    r->style[r->count++] = (unsigned char)span->style;
}

unsigned long syntax_scan(const struct SyntaxRules *rules, const struct TokenMap *tokens, const char *text,
                          unsigned long previous, SyntaxEmit emit, void *context)
{
    Recorder rec;
    unsigned long state;
    rec.emit = emit;
    rec.context = context;
    rec.count = 0;
    rec.full = 0;
    state = rules != NULL ? rules_scan_line(rules, text, previous, record, &rec) : 0;
    if (tokens != NULL && text != NULL && emit != NULL && !rec.full) {
        size_t len = strlen(text);
        long n = 0, i;
        const TokenSpan *spans = tokenmap_find(tokens, text, len, &n);
        for (i = 0; i < n; i++) {
            size_t s = spans[i].col, e = s + spans[i].len;
            int k, free_text = e <= len, variable = spans[i].style == SYNTAX_VARIABLE;
            /* only where the line is normal text (not a comment, a string...);
             * parameters and variables also in strings and preprocessor
             * lines, where scripts substitute them */
            for (k = 0; free_text && k < rec.count; k++)
                if (s < rec.end[k] && e > rec.start[k] && (!variable || rec.style[k] == SYNTAX_COMMENT))
                    free_text = 0;
            if (free_text) {
                SyntaxSpan span;
                span.start = s;
                span.end = e;
                span.style = (SyntaxStyle)spans[i].style;
                emit(context, &span);
            }
        }
    }
    return state;
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
    if (span->style >= SYNTAX_STYLE_COUNT) return;
    style = (UWORD)(TBSTYLE_SETCOLOR | ((hc->pens[span->style] & 0xffU) << 8));
    switch (span->style) {
        case SYNTAX_KEYWORD:
        case SYNTAX_PREPROCESSOR: style |= TBSTYLE_BOLD; break;
        /* no italics: the slanted bitmap fonts read poorly */
        default: break;
    }
    HighlightSetFormat(hc->object, (ULONG)span->start, (ULONG)span->end, style);
}

/**
 * @brief TextEditor highlighting hook sub-entry.
 *
 * Invoked by the texteditor.gadget for every line that needs highlighting.
 * It first resets the whole line to the normal pen, then runs
 * syntax_scan() to overlay the styles of Scout's rules and tokens.
 *
 * @param hook The hook, which is really a ::SyntaxHookContext.
 * @param object The texteditor gadget object being highlighted.
 * @param message A struct HighlightMessage describing the line.
 * @return The state for the next line.
 */
static ULONG highlight_entry(struct Hook *hook, APTR object, APTR message)
{
    SyntaxHookContext *hc = (SyntaxHookContext *)hook;
    struct HighlightMessage *hm = (struct HighlightMessage *)message;
    UWORD normal_style;
    hc->object = object;
    normal_style = (UWORD)(TBSTYLE_SETCOLOR |
                           ((hc->pens[SYNTAX_NORMAL] & 0xffU) << 8));
    HighlightSetFormat(object, 0, (ULONG)strlen(hm->Text), normal_style);
    return (ULONG)syntax_scan(hc->rules, hc->tokens, hm->Text,
        hm->StatusOfPrevBlock, format_span, hc);
}

void syntax_init_hook(SyntaxHookContext *hc)
{
    memset(hc, 0, sizeof(*hc));
    hc->hook.h_Entry = (ULONG (*)())HookEntry;
    hc->hook.h_SubEntry = (ULONG (*)())highlight_entry;
}

void syntax_set_pens(SyntaxHookContext *hc, const unsigned short *pens)
{
    memcpy(hc->pens, pens, sizeof(hc->pens));
}
#endif
