/**
 * @file bookmarks.c
 * @brief Bookmarks of the documents.
 *
 * The bookmarks are those of texteditor.gadget itself (ARexx commands
 * SETBOOKMARK TOGGLE, CLEARBOOKMARKS, LISTBOOKMARKS): the editor keeps them
 * on their lines while text is inserted or deleted above and draws a small
 * mark for them. TinyDev reads them with LISTBOOKMARKS into a per-document
 * cache; the result lists the lines counted from 0, each in quotes
 * ("98""130").
 *
 * The editor's left bar (GA_TEXTEDITOR_LeftBarHook, 20 pixels, a render
 * and a mouse message per line) is the place to click: a click toggles the
 * bookmark of that line (the editor itself does nothing on a click). It
 * shows no bookmark symbol of our own: that would be drawn from the cache,
 * which lags behind when lines move (the editor redraws them before
 * TinyDev can read the new positions), while the editor's own marks are
 * always right. It shows the marks of Scout's diagnostics (diag.c).
 *
 * The commands are sent with the window (DoGadgetMethod), so the editor
 * draws the change itself. A menu pick takes the input focus from the
 * editor; ActivateLayoutGadget() gives it back (with the editor object,
 * not its ID), otherwise its cursor is not shown.
 *
 * Next/Previous bookmark are done here and not with GOTOBOOKMARK, which
 * scrolls to the bookmark but leaves the cursor where it was: the cursor
 * is set to the line instead.
 *
 * The bookmarks of the project (the directory of the folder tree) are kept
 * in \<project\>/.tinydev/bookmarks.json (bmstore.c). While a file is open
 * its bookmarks live in the editor; the store holds those of the files
 * that are not open. The file is read when a directory is opened, and
 * written (open documents and store together) when a bookmark is set or
 * removed, a document is saved or closed, another directory is opened and
 * TinyDev quits. A file that cannot be read is never overwritten.
 */
#include "editor.h"
#include "bmstore.h"

#include <dos/dos.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <devices/inputevent.h>
#include <graphics/rastport.h>
#include <gadgets/layout.h>
#include <gadgets/texteditor.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <clib/alib_protos.h>

#define LEFTBAR_WIDTH 20        /**< width of the left bar (texteditor.gadget) */

/** Changes of the bookmarks, for the bookmark list. */
static ULONG generation = 0;

ULONG bookmarks_generation(void)
{
    return generation;
}

/** Background pen of the left bar (the editor's background). */
static UWORD leftbar_background = 0;

/**
 * @brief Left bar hook: clear the bar of a line, or note a click (handled
 *        by bookmarks_poll(), as the hook is called during input handling).
 * @param hook The document's leftbar_hook (h_Data = document).
 * @param obj Not used (NULL).
 * @param msg A LeftBarRenderMessage or LeftBarMouseMessage.
 * @return 0.
 */
static ULONG leftbar_entry(struct Hook *hook, Object *obj, APTR msg)
{
    Document *doc = (Document *)hook->h_Data;
    UWORD command = *(UWORD *)msg;
    (void)obj;
    if (command == LEFTBAR_RENDERCOMMAND) {
        struct LeftBarRenderMessage *r = (struct LeftBarRenderMessage *)msg;
        struct RastPort *rp = r->RastPort;
        WORD h = rp->Font != NULL ? (WORD)rp->Font->tf_YSize : 8;
        SetAPen(rp, (UBYTE)leftbar_background);
        RectFill(rp, rp->cp_x, rp->cp_y, (WORD)(rp->cp_x + LEFTBAR_WIDTH - 1), (WORD)(rp->cp_y + h - 1));
        /* a wrapped line's mark only on its first row */
        if (r->LineSubNum == 0) diag_draw_mark(doc, rp, (LONG)r->BlockNum, LEFTBAR_WIDTH, h);
    } else if (command == LEFTBAR_MOUSECOMMAND) {
        struct LeftBarMouseMessage *m = (struct LeftBarMouseMessage *)msg;
        if (m->Code == IECODE_LBUTTON) doc->bm_click_line = (LONG)m->BlockNum;
    }
    return 0;
}

void bookmarks_set_background(UWORD pen)
{
    leftbar_background = pen;
}

void bookmarks_init_document(Document *doc)
{
    doc->leftbar_hook.h_Entry = (ULONG (*)())HookEntry;
    doc->leftbar_hook.h_SubEntry = (ULONG (*)())leftbar_entry;
    doc->leftbar_hook.h_Data = doc;
    doc->bm_count = 0;
    doc->bm_click_line = -1;
    doc->bm_goto = -1;
}

/**
 * @brief Read the document's bookmarks from the editor into its cache.
 * @param doc The document.
 * @return Non-zero if the cache changed.
 */
static int read_bookmarks(Document *doc)
{
    ULONG lines[BOOKMARKS_MAX];
    long n = 0, i;
    ULONG r = DoMethod(doc->editor, GM_TEXTEDITOR_ARexxCmd, NULL, (ULONG)"LISTBOOKMARKS");
    const char *p;
    /* FALSE: not used, TRUE: no result, otherwise an allocated string */
    if (r > 1) {
        for (p = (const char *)r; *p != '\0' && n < BOOKMARKS_MAX; ) {
            if (*p >= '0' && *p <= '9') {
                ULONG v = 0;
                while (*p >= '0' && *p <= '9') v = v * 10 + (ULONG)(*p++ - '0');
                lines[n++] = v;
            } else ++p;
        }
        FreeVec((APTR)r);
    }
    if (n == doc->bm_count) {
        for (i = 0; i < n && lines[i] == doc->bm_lines[i]; ++i) ;
        if (i == n) return 0;
    }
    for (i = 0; i < n; ++i) doc->bm_lines[i] = lines[i];
    doc->bm_count = n;
    ++generation;
    return 1;
}

/**
 * @brief Send an ARexx command to the active document's editor with the
 *        window, so it draws the change itself.
 * @param app The application state.
 * @param doc The active document.
 * @param command The command.
 */
static void editor_command(EditorApp *app, Document *doc, const char *command)
{
    DoGadgetMethod((struct Gadget *)doc->editor, app->window, NULL, GM_TEXTEDITOR_ARexxCmd, 0,
                   (ULONG)command);
}

/**
 * @brief Give the active document's editor the input focus again (menu
 *        picks take it away) and update the scrollers.
 * @param app The application state.
 * @param doc The active document.
 */
static void focus_editor(EditorApp *app, Document *doc)
{
    if (app->layout != NULL)
        ActivateLayoutGadget((struct Gadget *)app->layout, app->window, NULL, (ULONG)doc->editor);
    document_sync_scrollers(app, doc);
}

/**
 * @brief Move the cursor to the next (or previous) bookmark, round to the
 *        first (last) one at the end.
 * @param app The application state.
 * @param doc The active document.
 * @param previous Non-zero for the previous bookmark.
 */
static void go_to_bookmark(EditorApp *app, Document *doc, int previous)
{
    ULONG y = 0, best = 0, wrap = 0, line;
    int found = 0, wrapped = 0;
    long i;
    read_bookmarks(doc);
    if (doc->bm_count == 0) {
        if (app->screen != NULL) DisplayBeep(app->screen);
        return;
    }
    GetAttr(GA_TEXTEDITOR_CursorY, doc->editor, &y);
    for (i = 0; i < doc->bm_count; ++i) {
        line = doc->bm_lines[i];
        if (previous ? line < y && (!found || line > best) : line > y && (!found || line < best)) {
            best = line;
            found = 1;
        }
        /* round: the first (last) bookmark of the document */
        if (!wrapped || (previous ? line > wrap : line < wrap)) {
            wrap = line;
            wrapped = 1;
        }
    }
    if (!found) best = wrap;
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
                   GA_TEXTEDITOR_CursorY, best, GA_TEXTEDITOR_CursorX, 0, TAG_END);
}

void bookmarks_refresh(EditorApp *app, Document *doc)
{
    (void)app;
    if (doc != NULL) read_bookmarks(doc);
}

void bookmarks_command(EditorApp *app, ULONG id)
{
    Document *doc = app->active;
    if (doc == NULL || app->window == NULL) return;
    switch (id) {
        case MID_BM_TOGGLE: editor_command(app, doc, "SETBOOKMARK TOGGLE"); break;
        case MID_BM_CLEAR: editor_command(app, doc, "CLEARBOOKMARKS"); break;
        case MID_BM_NEXT:
        case MID_BM_PREV:
            go_to_bookmark(app, doc, id == MID_BM_PREV);
            break;
    }
    read_bookmarks(doc);
    focus_editor(app, doc);
    if (id == MID_BM_TOGGLE || id == MID_BM_CLEAR) bookmarks_save(app);
}

static void apply_project(EditorApp *app, Document *doc);
static void go_to_pending(EditorApp *app, Document *doc);

void bookmarks_poll(EditorApp *app)
{
    Document *doc;
    LONG line;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL;
         doc = (Document *)doc->node.ln_Succ) {
        if (doc->bm_apply) apply_project(app, doc);
        if (doc->bm_goto >= 0) go_to_pending(app, doc);
    }
    doc = app->active;
    if (doc == NULL || doc->bm_click_line < 0 || app->window == NULL) return;
    line = doc->bm_click_line;
    doc->bm_click_line = -1;
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
                   GA_TEXTEDITOR_CursorY, (ULONG)line, GA_TEXTEDITOR_CursorX, 0, TAG_END);
    bookmarks_command(app, MID_BM_TOGGLE);
}

/* ---- the project's bookmarks file ------------------------------------ */

/**
 * The bookmarks file in it.
 */
#define BOOKMARKS_FILE "bookmarks.json"

/** The bookmarks of the project. */
static struct {
    BmStore store;              /* of the project's files that are not open */
    char root[EDITOR_PATH_MAX]; /* the project directory, "" if none */
    int keep_file;              /* the file could not be read: never write it */
    int reported;               /* a write error was reported */
} pj;

/**
 * @brief Return a document's file relative to the project.
 * @param doc The document.
 * @return The name relative to the project, or NULL if the file is not
 *         in the project (or there is none).
 */
static const char *relative(const Document *doc)
{
    size_t n = strlen(pj.root), i;
    const char *path = doc->path;
    if (n == 0 || path == NULL || strlen(path) <= n) return NULL;
    for (i = 0; i < n; ++i) {
        char a = path[i], b = pj.root[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return NULL;
    }
    if (pj.root[n - 1] == ':') return path + n;
    return path[n] == '/' ? path + n + 1 : NULL;
}

/**
 * @brief Return the text of a line of a document's text.
 * @param text The text.
 * @param size Its size.
 * @param line The line, counted from 0.
 * @param length Set to the length of the line.
 * @return The start of the line (not terminated), or NULL.
 */
static const char *line_of(const char *text, long size, ULONG line, long *length)
{
    long offset = 0, end;
    ULONG n = 0;
    for (;;) {
        end = offset;
        while (end < size && text[end] != '\n' && text[end] != '\r') ++end;
        if (n == line) {
            *length = end - offset;
            return text + offset;
        }
        if (end >= size) return NULL;
        offset = text[end] == '\r' && end + 1 < size && text[end + 1] == '\n' ? end + 2 : end + 1;
        ++n;
    }
}

/**
 * @brief Put the bookmarks of an open document into a store (instead of
 *        those the store had for its file).
 * @param s The store.
 * @param doc The document (in the project).
 * @return Non-zero on success.
 */
static int capture(BmStore *s, Document *doc)
{
    const char *file = relative(doc), *line;
    char *text;
    long size, length, i;
    int ok = 1;
    if (file == NULL || doc->bm_apply) return 1;    /* not yet set: the store has them */
    read_bookmarks(doc);
    bmstore_remove_file(s, file);
    if (doc->bm_count == 0) return 1;
    text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    size = text != NULL ? (long)strlen(text) : 0;
    for (i = 0; i < doc->bm_count && ok; ++i) {
        line = text != NULL ? line_of(text, size, doc->bm_lines[i], &length) : NULL;
        ok = bmstore_add(s, file, (long)doc->bm_lines[i], line, line != NULL ? length : 0);
    }
    if (text != NULL) FreeVec(text);
    return ok;
}

/**
 * @brief Set the project's bookmarks of a document's file in its editor,
 *        each on its line found again in the (maybe changed) text; they
 *        then leave the store.
 * @param app The application state.
 * @param doc The document.
 */
static void apply_project(EditorApp *app, Document *doc)
{
    const char *file;
    char *text;
    long size, i, line;
    ULONG x = 0, y = 0;
    int set = 0;
    doc->bm_apply = 0;
    if ((file = relative(doc)) == NULL) return;
    text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    if (text == NULL) return;
    size = (long)strlen(text);
    GetAttr(GA_TEXTEDITOR_CursorX, doc->editor, &x);
    GetAttr(GA_TEXTEDITOR_CursorY, doc->editor, &y);
    for (i = 0; i < pj.store.count; ++i) {
        BmEntry *e = &pj.store.entries[i];
        if (!bmstore_same_file(e->file, file)) continue;
        line = bmstore_relocate(text, size, e->line, e->text);
        SetAttrs(doc->editor, GA_TEXTEDITOR_CursorY, (ULONG)line, GA_TEXTEDITOR_CursorX, 0, TAG_END);
        DoMethod(doc->editor, GM_TEXTEDITOR_ARexxCmd, NULL, (ULONG)"SETBOOKMARK");
        set = 1;
    }
    FreeVec(text);
    if (!set) return;
    bmstore_remove_file(&pj.store, file);
    ++generation;
    SetAttrs(doc->editor, GA_TEXTEDITOR_CursorY, y, GA_TEXTEDITOR_CursorX, x, TAG_END);
    read_bookmarks(doc);
    if (doc == app->active && app->window != NULL)
        RefreshGList((struct Gadget *)doc->editor, app->window, NULL, 1);
}

/**
 * @brief Report a problem with the bookmarks file once per project.
 * @param app The application state.
 * @param id The message (MSG_ERR_BM_*, with %s for the file).
 * @param name The file.
 */
static void report(EditorApp *app, LONG id, const char *name)
{
    char text[EDITOR_PATH_MAX + 160];
    if (pj.reported) return;
    pj.reported = 1;
    snprintf(text, sizeof(text), S(id), name);
    ui_error(app, S(MSG_MENU_BOOKMARKS), text);
}

/**
 * @brief Read the project's bookmarks file into the store.
 * @param app The application state.
 */
static void load_project(EditorApp *app)
{
    char name[EDITOR_PATH_MAX];
    char *data;
    int r = projdata_read(pj.root, BOOKMARKS_FILE, &data);
    if (r == 0) return;                 /* none yet */
    if (r < 0 || !bmstore_parse(&pj.store, data)) pj.keep_file = 1;
    free(data);
    if (pj.keep_file && projdata_path(pj.root, BOOKMARKS_FILE, name, sizeof(name)))
        report(app, MSG_ERR_BM_READ, name);
}

void bookmarks_save(EditorApp *app)
{
    BmStore all;
    Document *doc;
    char name[EDITOR_PATH_MAX], *json;
    long i;
    int ok = 1;

    if (pj.root[0] == '\0' || pj.keep_file) return;
    /* the store and the open documents of the project together */
    bmstore_init(&all);
    for (i = 0; i < pj.store.count && ok; ++i) {
        BmEntry *e = &pj.store.entries[i];
        ok = bmstore_add(&all, e->file, e->line, e->text, (long)strlen(e->text));
    }
    for (doc = (Document *)app->documents.lh_Head; ok && doc->node.ln_Succ != NULL;
         doc = (Document *)doc->node.ln_Succ)
        ok = capture(&all, doc);
    /* no bookmarks and no file yet: nothing to write, no directory made */
    if (!ok || !projdata_path(pj.root, BOOKMARKS_FILE, name, sizeof(name)) ||
        (all.count == 0 && !projdata_exists(pj.root, BOOKMARKS_FILE))) {
        bmstore_clear(&all);
        return;
    }
    json = bmstore_format(&all);
    bmstore_clear(&all);
    if (json == NULL || !projdata_write(pj.root, BOOKMARKS_FILE, json))
        report(app, MSG_ERR_BM_WRITE, name);
    free(json);
}

void bookmarks_project_opened(EditorApp *app)
{
    Document *doc;
    char root[EDITOR_PATH_MAX];
    if (!tree_project_root(app, root, sizeof(root))) return;
    if (strcmp(root, pj.root) == 0) return;
    bookmarks_save(app);                /* the previous project */
    bmstore_clear(&pj.store);
    strcpy(pj.root, root);
    pj.keep_file = pj.reported = 0;
    load_project(app);
    ++generation;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL;
         doc = (Document *)doc->node.ln_Succ)
        if (relative(doc) != NULL) doc->bm_apply = 1;
}

void bookmarks_document_loaded(EditorApp *app, Document *doc)
{
    (void)app;
    if (relative(doc) != NULL) doc->bm_apply = 1;
}

void bookmarks_document_closing(EditorApp *app, Document *doc)
{
    ++generation;                       /* it leaves the bookmark list */
    if (relative(doc) == NULL) return;
    capture(&pj.store, doc);
    bookmarks_save(app);
}

void bookmarks_cleanup(EditorApp *app)
{
    bookmarks_save(app);
    bmstore_clear(&pj.store);
    pj.root[0] = '\0';
}

/* ---- the bookmark list's view ------------------------------------------ */

/**
 * @brief Copy a string with malloc().
 * @param text The string (need not be terminated).
 * @param length Its length.
 * @return The copy, or NULL.
 */
static char *copy_text(const char *text, long length)
{
    char *c = malloc((size_t)length + 1);
    if (c == NULL) return NULL;
    memcpy(c, text, (size_t)length);
    c[length] = '\0';
    return c;
}

/**
 * @brief Add a bookmark to the array of bookmarks_collect().
 * @param items The array.
 * @param count The number in it.
 * @param capacity Its size.
 * @param item The bookmark (its strings are taken over, or freed on error).
 * @return Non-zero on success.
 */
static int add_item(BookmarkItem **items, long *count, long *capacity, BookmarkItem *item)
{
    if (item->name == NULL || item->text == NULL) {
        free(item->path); free(item->name); free(item->file); free(item->text);
        return 0;
    }
    if (*count == *capacity) {
        long n = *capacity ? *capacity * 2 : 32;
        BookmarkItem *grown = realloc(*items, (size_t)n * sizeof(*grown));
        if (grown == NULL) {
            free(item->path); free(item->name); free(item->file); free(item->text);
            return 0;
        }
        *items = grown;
        *capacity = n;
    }
    (*items)[(*count)++] = *item;
    return 1;
}

/**
 * @brief qsort() order of the list: by shown name, then by line.
 * @param a A bookmark.
 * @param b Another bookmark.
 * @return <0, 0 or >0.
 */
static int item_order(const void *a, const void *b)
{
    const BookmarkItem *x = a, *y = b;
    int c = bmstore_compare_files(x->name, y->name);
    if (c != 0) return c;
    return x->line < y->line ? -1 : x->line > y->line;
}

long bookmarks_collect(EditorApp *app, int scope, BookmarkItem **items)
{
    long count = 0, capacity = 0, i, size, length;
    Document *doc;
    *items = NULL;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL;
         doc = (Document *)doc->node.ln_Succ) {
        const char *file = relative(doc), *line;
        char *text;
        if (scope == BMSCOPE_CURRENT && doc != app->active) continue;
        if (scope == BMSCOPE_PROJECT && file == NULL) continue;
        if (doc->bm_apply) continue;            /* not yet set: the store has them */
        read_bookmarks(doc);
        if (doc->bm_count == 0) continue;
        text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
        size = text != NULL ? (long)strlen(text) : 0;
        for (i = 0; i < doc->bm_count; ++i) {
            BookmarkItem it;
            const char *name = file != NULL ? file : doc->title;
            line = text != NULL ? line_of(text, size, doc->bm_lines[i], &length) : NULL;
            if (line == NULL) length = 0;
            if (length > BOOKMARK_TEXT_MAX) length = BOOKMARK_TEXT_MAX;
            it.doc = doc;
            it.path = doc->path != NULL ? copy_text(doc->path, (long)strlen(doc->path)) : NULL;
            it.name = copy_text(name, (long)strlen(name));
            it.file = file != NULL ? copy_text(file, (long)strlen(file)) : NULL;
            it.line = (long)doc->bm_lines[i];
            it.text = copy_text(line != NULL ? line : "", length);
            add_item(items, &count, &capacity, &it);
        }
        if (text != NULL) FreeVec(text);
    }
    /* the project's files that are not open (or not yet set) */
    if (scope == BMSCOPE_PROJECT && pj.root[0] != '\0') {
        char path[EDITOR_PATH_MAX];
        for (i = 0; i < pj.store.count; ++i) {
            BmEntry *e = &pj.store.entries[i];
            BookmarkItem it;
            strncpy(path, pj.root, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            if (!AddPart((STRPTR)path, (STRPTR)e->file, sizeof(path))) continue;
            it.doc = NULL;
            it.path = copy_text(path, (long)strlen(path));
            it.name = copy_text(e->file, (long)strlen(e->file));
            it.file = copy_text(e->file, (long)strlen(e->file));
            it.line = e->line;
            it.text = copy_text(e->text, (long)strlen(e->text));
            add_item(items, &count, &capacity, &it);
        }
    }
    if (count > 1) qsort(*items, (size_t)count, sizeof(**items), item_order);
    return count;
}

void bookmarks_free_items(BookmarkItem *items, long count)
{
    long i;
    for (i = 0; i < count; ++i) {
        free(items[i].path);
        free(items[i].name);
        free(items[i].file);
        free(items[i].text);
    }
    free(items);
}

/**
 * @brief Tell whether a document is (still) open.
 * @param app The application state.
 * @param doc The document.
 * @return Non-zero if it is in the list of documents.
 */
static int is_open(EditorApp *app, Document *doc)
{
    Document *d;
    for (d = (Document *)app->documents.lh_Head; d->node.ln_Succ != NULL;
         d = (Document *)d->node.ln_Succ)
        if (d == doc) return 1;
    return 0;
}

/**
 * @brief Put the cursor of the active document on a line and give the
 *        editor the input focus.
 * @param app The application state.
 * @param doc The active document.
 * @param line The line.
 */
static void put_cursor(EditorApp *app, Document *doc, long line)
{
    if (app->window == NULL || doc != app->active) return;
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
                   GA_TEXTEDITOR_CursorY, (ULONG)line, GA_TEXTEDITOR_CursorX, 0, TAG_END);
    focus_editor(app, doc);
}

/**
 * @brief Go to the line noted for a document that was opened from the
 *        bookmark list, once its bookmarks are set.
 * @param app The application state.
 * @param doc The document.
 */
static void go_to_pending(EditorApp *app, Document *doc)
{
    char *text;
    long line = doc->bm_goto;
    if (doc->bm_apply) return;
    doc->bm_goto = -1;
    if (doc != app->active) return;
    text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    if (text != NULL) {
        line = bmstore_relocate(text, (long)strlen(text), line, doc->bm_goto_text);
        FreeVec(text);
    }
    put_cursor(app, doc, line);
}

void bookmarks_go_to(EditorApp *app, const BookmarkItem *item)
{
    Document *doc = item->doc;
    if (doc != NULL && is_open(app, doc)) {
        if (doc != app->active) document_activate(app, doc);
        put_cursor(app, doc, item->line);
        return;
    }
    if (item->path == NULL || (doc = document_open(app, item->path)) == NULL) return;
    /* its bookmarks are set one turn later; then go to the line */
    doc->bm_goto = item->line;
    strncpy(doc->bm_goto_text, item->text, BOOKMARK_TEXT_MAX);
    doc->bm_goto_text[BOOKMARK_TEXT_MAX] = '\0';
}

void bookmarks_remove(EditorApp *app, const BookmarkItem *item)
{
    Document *doc = item->doc;
    if (doc != NULL && is_open(app, doc)) {
        ULONG x = 0, y = 0;
        int shown = doc == app->active && app->window != NULL;
        GetAttr(GA_TEXTEDITOR_CursorX, doc->editor, &x);
        GetAttr(GA_TEXTEDITOR_CursorY, doc->editor, &y);
        SetAttrs(doc->editor, GA_TEXTEDITOR_CursorY, (ULONG)item->line,
                 GA_TEXTEDITOR_CursorX, 0, TAG_END);
        if (shown) editor_command(app, doc, "SETBOOKMARK OFF");
        else DoMethod(doc->editor, GM_TEXTEDITOR_ARexxCmd, NULL, (ULONG)"SETBOOKMARK OFF");
        if (shown)
            SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
                           GA_TEXTEDITOR_CursorY, y, GA_TEXTEDITOR_CursorX, x, TAG_END);
        else SetAttrs(doc->editor, GA_TEXTEDITOR_CursorY, y, GA_TEXTEDITOR_CursorX, x, TAG_END);
        read_bookmarks(doc);
    } else if (item->file != NULL && bmstore_remove(&pj.store, item->file, item->line)) {
        ++generation;
    }
    bookmarks_save(app);
}
