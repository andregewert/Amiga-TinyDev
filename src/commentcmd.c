/**
 * @file commentcmd.c
 * @brief Edit/Comment Lines: comments or uncomments the marked lines (or
 *        the cursor's line) with the comment form of the document's
 *        language.
 *
 * The form comes from Scout's lexical rules (comment.c chooses it), so a
 * document without a language, or one whose language has no comments,
 * gets a beep. The lines are taken from the exported text, replaced as
 * one block (GM_TEXTEDITOR_MarkText and GM_TEXTEDITOR_Replace) and marked
 * again, so the command can be given once more to take it back. A block
 * that ends in the first column of a line leaves that line out, as when
 * whole lines were marked with the cursor keys.
 */
#include "editor.h"
#include "comment.h"

#include <gadgets/texteditor.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

#include <stdlib.h>
#include <string.h>

/**
 * @brief Find where a line starts in a text.
 * @param text The text.
 * @param line The line (from 0).
 * @return Its start, NULL if the text has fewer lines.
 */
static const char *line_start(const char *text, ULONG line)
{
    while (line > 0) {
        const char *nl = strchr(text, '\n');
        if (nl == NULL) return NULL;
        text = nl + 1;
        --line;
    }
    return text;
}

/**
 * @brief Mark a range of the active document's editor.
 * @param app The application state.
 * @param doc The document.
 * @param x0 First column.
 * @param y0 First line.
 * @param x1 Column after the end.
 * @param y1 Last line.
 */
static void mark(EditorApp *app, Document *doc, ULONG x0, ULONG y0, ULONG x1, ULONG y1)
{
    DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_MarkText, 0,
                   x0, y0, x1, y1);
}

void comment_lines(EditorApp *app)
{
    Document *doc = app->active;
    CommentStyle style;
    ULONG x0 = 0, y0 = 0, x1 = 0, y1 = 0, marked = 0;
    const char *first, *last, *end, *nl;
    char *text, *out;
    int commented;

    if (doc == NULL || doc->editor == NULL || app->window == NULL) return;
    if (!comment_style(doc->highlight.rules, &style)) {
        DisplayBeep(app->screen);
        ui_status_message(app, S(MSG_COMMENT_NONE));
        return;
    }
    GetAttr(GA_TEXTEDITOR_AreaMarked, doc->editor, &marked);
    if (marked) {
        DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_BlockInfo, 0,
                       (ULONG)&x0, (ULONG)&y0, (ULONG)&x1, (ULONG)&y1);
        if (y1 > y0 && x1 == 0) --y1;
    } else {
        GetAttr(GA_TEXTEDITOR_CursorY, doc->editor, &y0);
        y1 = y0;
    }
    if ((text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL)) == NULL) return;
    first = line_start(text, y0);
    last = first != NULL ? line_start(first, y1 - y0) : NULL;
    if (last == NULL) {
        FreeVec(text);
        return;
    }
    end = (nl = strchr(last, '\n')) != NULL ? nl : last + strlen(last);
    if ((out = comment_toggle(first, (size_t)(end - first), &style, &commented)) == NULL) {
        FreeVec(text);
        ui_error(app, APP_NAME, S(MSG_PJ_NOMEM));
        return;
    }
    if (strlen(out) != (size_t)(end - first) || memcmp(out, first, (size_t)(end - first)) != 0) {
        const char *tail = strrchr(out, '\n');
        mark(app, doc, 0, y0, (ULONG)(end - last), y1);
        DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_Replace, 0,
                       (ULONG)out, 0);
        /* marked again, to take it back with the same command */
        mark(app, doc, 0, y0, (ULONG)strlen(tail != NULL ? tail + 1 : out), y1);
    }
    free(out);
    FreeVec(text);
}
