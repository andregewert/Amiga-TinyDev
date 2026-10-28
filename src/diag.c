/**
 * @file diag.c
 * @brief The diagnostics of the documents: Scout's list per document and
 *        the marks in the editor's left bar.
 *
 * After an analysis that found problems (or found none where there were
 * some), scoutlink.c asks Scout for the document's diagnostics; the answer
 * becomes the document's list (for the status line and the problem list)
 * and its marks: one per line with the colour of its worst problem
 * (error red, warning orange, others blue), drawn by the left bar hook of
 * bookmarks.c. The hook may run during input handling, so the marks are a
 * fixed array in the document that is changed under Forbid() and never
 * freed while the document lives.
 *
 * The marks lag behind lines that move while typing until the next
 * analysis (a second after typing stops); a document is redrawn when its
 * marks change.
 */
#include "editor.h"
#include "json.h"
#include "scout_protocol.h"

#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <stdlib.h>
#include <string.h>

/** Colours of the marks (0xRRGGBB): error, warning, information/hint. */
static const unsigned long mark_colours[3] = { 0xD02818, 0xE08800, 0x0038C0 };

/** The pens of the marks, -1 if none could be obtained. */
static LONG mark_pens[3] = { -1, -1, -1 };

/** Where the pens are from. */
static struct ColorMap *mark_cm;

/** Size of a mark (pixels). */
#define MARK_SIZE 6

void diag_open(EditorApp *app)
{
    int i;
    if (mark_cm != NULL || app->screen == NULL) return;
    mark_cm = app->screen->ViewPort.ColorMap;
    for (i = 0; i < 3; ++i) {
        unsigned long c = mark_colours[i];
        mark_pens[i] = ObtainBestPen(mark_cm, ((c >> 16) & 0xFF) * 0x01010101UL,
                                     ((c >> 8) & 0xFF) * 0x01010101UL, (c & 0xFF) * 0x01010101UL,
                                     OBP_Precision, PRECISION_GUI, OBP_FailIfBad, FALSE, TAG_END);
    }
}

void diag_close(EditorApp *app)
{
    int i;
    (void)app;
    if (mark_cm == NULL) return;
    for (i = 0; i < 3; ++i) {
        if (mark_pens[i] >= 0) ReleasePen(mark_cm, (ULONG)mark_pens[i]);
        mark_pens[i] = -1;
    }
    mark_cm = NULL;
}

UWORD diag_pen(int severity, UWORD fallback)
{
    int i = severity <= SCOUT_SEVERITY_ERROR ? 0 : severity == SCOUT_SEVERITY_WARNING ? 1 : 2;
    return mark_pens[i] >= 0 ? (UWORD)mark_pens[i] : fallback;
}

/**
 * @brief Free a list of diagnostics.
 * @param items The list (may be NULL).
 * @param count Its length.
 */
static void free_items(DiagItem *items, long count)
{
    long i;
    for (i = 0; i < count; ++i) free(items[i].message);
    free(items);
}

void diag_free_document(Document *doc)
{
    Forbid();
    doc->diag_mark_count = 0;
    Permit();
    free_items(doc->diag_items, doc->diag_count);
    doc->diag_items = NULL;
    doc->diag_count = 0;
}

/**
 * @brief qsort() order of diagnostics: by line, then column.
 * @param a A diagnostic.
 * @param b Another diagnostic.
 * @return Less than, equal to or greater than 0.
 */
static int by_line(const void *a, const void *b)
{
    const DiagItem *x = (const DiagItem *)a, *y = (const DiagItem *)b;
    if (x->line != y->line) return x->line < y->line ? -1 : 1;
    return x->col < y->col ? -1 : x->col > y->col;
}

void diag_set_document(EditorApp *app, Document *doc, const char *json)
{
    DiagMark marks[DIAG_MARKS_MAX];
    LONG count = 0;
    JsonValue *root = json != NULL ? json_parse(json, NULL) : NULL;
    const JsonValue *list = root != NULL ? json_obj_get_array(root, "diagnostics") : NULL;
    size_t i, n = list != NULL ? json_array_length(list) : 0;
    DiagItem *items = n > 0 ? calloc(n, sizeof(DiagItem)) : NULL;
    long got = 0;
    int changed;

    for (i = 0; items != NULL && i < n; ++i) {
        const JsonValue *v = json_array_get(list, i);
        const char *msg = json_obj_get_string(v, "message", NULL);
        const JsonValue *range = json_obj_get_obj(v, "range");
        const JsonValue *start = range != NULL ? json_obj_get_array(range, "start") : NULL;
        if (msg == NULL || (items[got].message = malloc(strlen(msg) + 1)) == NULL) continue;
        strcpy(items[got].message, msg);
        items[got].severity = (int)json_obj_get_int(v, "severity", SCOUT_SEVERITY_ERROR);
        items[got].line = start != NULL && json_array_length(start) > 0 ? json_array_get(start, 0)->u.num_val.int_val : 1;
        items[got].col = start != NULL && json_array_length(start) > 1 ? json_array_get(start, 1)->u.num_val.int_val : 1;
        ++got;
    }
    json_value_free(root);
    if (got > 1) qsort(items, (size_t)got, sizeof(DiagItem), by_line);

    /* one mark per line, the worst severity (the lowest number) */
    for (i = 0; i < (size_t)got && count < DIAG_MARKS_MAX; ++i) {
        LONG line = (LONG)(items[i].line - 1);
        if (count > 0 && marks[count - 1].line == line) {
            if (items[i].severity < marks[count - 1].severity) marks[count - 1].severity = (UWORD)items[i].severity;
            continue;
        }
        marks[count].line = line;
        marks[count++].severity = (UWORD)items[i].severity;
    }
    changed = count != doc->diag_mark_count ||
              memcmp(marks, doc->diag_marks, (size_t)count * sizeof(DiagMark)) != 0;
    if (changed) {
        Forbid();
        memcpy(doc->diag_marks, marks, (size_t)count * sizeof(DiagMark));
        doc->diag_mark_count = count;
        Permit();
    }
    free_items(doc->diag_items, doc->diag_count);
    doc->diag_items = items;
    doc->diag_count = got;
    ++doc->diag_generation;
    if (changed && doc == app->active && app->window != NULL)
        RefreshGList((struct Gadget *)doc->editor, app->window, NULL, 1);
}

const DiagItem *diag_at_line(const Document *doc, long line)
{
    const DiagItem *best = NULL;
    long i;
    for (i = 0; i < doc->diag_count && doc->diag_items[i].line <= line; ++i)
        if (doc->diag_items[i].line == line && (best == NULL || doc->diag_items[i].severity < best->severity))
            best = &doc->diag_items[i];
    return best;
}

void diag_draw_mark(const Document *doc, struct RastPort *rp, LONG line, WORD width, WORD height)
{
    LONG lo = 0, hi = doc->diag_mark_count - 1;
    while (lo <= hi) {
        LONG mid = (lo + hi) / 2;
        LONG l = doc->diag_marks[mid].line;
        if (l == line) {
            WORD size = height - 2 < MARK_SIZE ? (WORD)(height - 2) : MARK_SIZE;
            WORD x = (WORD)(rp->cp_x + (width - size) / 2), y = (WORD)(rp->cp_y + (height - size) / 2);
            if (size < 2) return;
            SetAPen(rp, (UBYTE)diag_pen(doc->diag_marks[mid].severity, 1));
            /* a square with cut corners */
            RectFill(rp, (WORD)(x + 1), y, (WORD)(x + size - 2), (WORD)(y + size - 1));
            RectFill(rp, x, (WORD)(y + 1), (WORD)(x + size - 1), (WORD)(y + size - 2));
            return;
        }
        if (l < line) lo = mid + 1;
        else hi = mid - 1;
    }
}
