/**
 * @file document.c
 * @brief Documents and their tabs: creating, opening, activating and closing, scrollbar synchronisation and highlighting suspension while scrolling.
 *
 * A document whose changes are all undone (or redone back to the saved
 * text) is marked unchanged again: after Undo and Redo and whenever editing
 * pauses (for texts up to 256 KB) the text is compared with a checksum of
 * the text as loaded or saved (text_hash(); the line-end style and one
 * line end at the end of the text do not count, as the editor exports the
 * text with or without one; empty lines added at the end do count).
 *
 * To keep dragging a scrollbar as smooth as the editor's own wheel
 * scrolling, syntax highlighting is suspended while a scrollbar is dragged
 * and restored with a full redraw when the drag ends: at the scroller's
 * GADGETUP or after two IntuiTicks without a scroll signal (the scroller
 * sends no GADGETUP when the button is released outside it).
 */
#include "editor.h"

#include <proto/clicktab.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/scroller.h>
#include <proto/texteditor.h>
#include <proto/utility.h>
#include <reaction/reaction.h>
#include <gadgets/clicktab.h>
#include <gadgets/layout.h>
#include <gadgets/scroller.h>
#include <gadgets/texteditor.h>
#include <intuition/icclass.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

/**
 * @brief Duplicate a NUL-terminated string into a freshly allocated buffer.
 *
 * Allocates memory with AllocVec (MEMF_ANY) sized to hold the string plus its
 * terminating NUL and copies the contents. The caller owns the returned buffer
 * and must release it with FreeVec.
 *
 * @param s The source NUL-terminated string to copy.
 * @return A newly allocated copy of @p s, or NULL if the allocation failed.
 */
static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = AllocVec((ULONG)n, MEMF_ANY);
    if (copy != NULL) memcpy(copy, s, n);
    return copy;
}

/**
 * @brief Return the file-name portion of a path.
 *
 * Scans the path and returns a pointer just past the last '/' or ':'
 * separator. If the path ends with a separator (empty base name) the original
 * path pointer is returned instead.
 *
 * @param path The path to examine.
 * @return A pointer into @p path at the start of the base name.
 */
static const char *base_name(const char *path)
{
    const char *p, *best = path;
    for (p = path; *p != '\0'; ++p) if (*p == '/' || *p == ':') best = p + 1;
    return *best != '\0' ? best : path;
}

/**
 * @brief Create the ReAction TextEditor gadget for a document.
 *
 * Instantiates a TextEditor object configured with the document's initial
 * contents, a fixed font, horizontal scrolling, the application's backfill and
 * draw-info hooks, the document's syntax-highlighting hook, and line-ending
 * export set to mirror the imported endings.
 *
 * @param app The owning application, providing shared hooks and settings.
 * @param doc The document the editor belongs to (supplies id and highlight hook).
 * @param contents The initial text to place in the editor.
 * @return The newly created TextEditor object, or NULL on failure.
 */
static Object *create_editor(EditorApp *app, Document *doc,
                             const char *contents)
{
    return NewObject(TEXTEDITOR_GetClass(), NULL,
        GA_ID, (ULONG)(100 + doc->number),
        GA_RelVerify, TRUE,
        ICA_TARGET, (ULONG)ICTARGET_IDCMP,
        GA_BackFill, (ULONG)&app->editor_backfill_hook,
        GA_DrawInfo, (ULONG)&app->editor_draw_info,
        GA_TEXTEDITOR_Contents, (ULONG)contents,
        GA_TEXTEDITOR_FixedFont, TRUE,
        GA_TEXTEDITOR_WrapBorder, 0,
        GA_TEXTEDITOR_HorizontalScroll, TRUE,
        GA_TEXTEDITOR_ShowLineNumbers, (ULONG)app->line_numbers,
        GA_TEXTEDITOR_HighlighterHook, (ULONG)&doc->highlight.hook,
        GA_TEXTEDITOR_LeftBarHook, (ULONG)&doc->leftbar_hook,
        GA_TEXTEDITOR_LineEndingExport, LINEENDING_ASIMPORT,
        TAG_END);
}

/**
 * @brief Temporarily detach the click-tab label list from the tabs gadget.
 *
 * Clears the CLICKTAB_Labels attribute (using the window-aware SetGadgetAttrs
 * when a window exists, otherwise SetAttrs) so the tab node list can be safely
 * modified. Does nothing when the tabs gadget is absent.
 *
 * @param app The application whose tabs gadget is detached.
 */
static void detach_tabs(EditorApp *app)
{
    ui_gadget_set(app, app->tabs, CLICKTAB_Labels, ~0UL, TAG_IGNORE, 0);
}

/**
 * @brief Re-attach the application's click-tab label list to the tabs gadget.
 *
 * Restores the CLICKTAB_Labels attribute to point at app->tab_nodes after the
 * list has been modified, using the window-aware SetGadgetAttrs when a window
 * exists. Does nothing when the tabs gadget is absent.
 *
 * @param app The application whose tabs gadget is re-attached.
 */
static void attach_tabs(EditorApp *app)
{
    ui_gadget_set(app, app->tabs, CLICKTAB_Labels, (ULONG)&app->tab_nodes, TAG_IGNORE, 0);
}

/**
 * @brief Renumber all click-tab nodes to match document order.
 *
 * Walks the document list and assigns each document's tab node a sequential
 * TNA_Number starting at zero, keeping tab numbering consistent after
 * insertion or removal.
 *
 * @param app The application whose document tabs are renumbered.
 */
static void renumber_tabs(EditorApp *app)
{
    Document *doc;
    ULONG index = 0;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ, ++index)
        SetClickTabNodeAttrs(doc->tab, TNA_Number,
                             (ULONG)(UWORD)index, TAG_END);
}

/**
 * @brief Create, register and activate a new empty document.
 *
 * Allocates and initializes a Document (default "Untitled" title, LF line
 * endings, plain-text syntax), sets up its highlighter pens, and creates the
 * TextEditor gadget, the vertical and horizontal scrollers, the editor-row and
 * page layouts, and the click-tab node. The document and its tab are added to
 * the application lists, tabs are renumbered, the page is added to the pager,
 * and the new document is activated, relaid out and its scrollers synced. On
 * any allocation or object-creation failure the partially built resources are
 * disposed in reverse order, an error requester is shown, and NULL is returned.
 *
 * @param app The owning application.
 * @return The newly created document, or NULL on failure.
 */
static Document *new_document(EditorApp *app);

Document *document_new(EditorApp *app)
{
    Document *doc;
    ui_begin_update(app);
    doc = new_document(app);
    ui_end_update(app);
    return doc;
}

/* The work of document_new(). */
static Document *new_document(EditorApp *app)
{
    Document *doc = AllocVec(sizeof(*doc), MEMF_ANY | MEMF_CLEAR);
    if (doc == NULL) { ui_error(app, APP_NAME, S(MSG_ERR_DOCMEM)); return NULL; }
    doc->number = ++app->next_document;
    snprintf(doc->title, sizeof(doc->title), S(MSG_UNTITLED), doc->number);
    strcpy(doc->tab_title, doc->title);
    doc->eol = EOL_LF;
    doc->final_eol = 1;
    document_mark_saved(doc, "", 0, 0);
    doc->language = SYNTAX_PLAIN;
    syntax_init_hook(&doc->highlight, doc->language);
    minimap_init_document(doc);
    bookmarks_init_document(doc);
    doc->scout_symbols = -1;
    document_set_pens(app, doc);
    doc->editor = create_editor(app, doc, "");
    if (doc->editor == NULL) { FreeVec(doc); ui_error(app, APP_NAME, S(MSG_ERR_EDITOR)); return NULL; }
    doc->vscroll = NewObject(SCROLLER_GetClass(), NULL,
        GA_ID, GID_VSCROLL, GA_RelVerify, TRUE, GA_Immediate, TRUE,
        GA_FollowMouse, TRUE,
        SCROLLER_Orientation, SCROLLER_VERTICAL,
        SCROLLER_Arrows, TRUE,
        SCROLLER_SignalTask, (ULONG)FindTask(NULL),
        SCROLLER_SignalTaskBit, 1UL << (ULONG)app->scroll_signal,
        SCROLLER_Total, 1,
        SCROLLER_Visible, 1,
        TAG_END);
    doc->hscroll = NewObject(SCROLLER_GetClass(), NULL,
        GA_ID, GID_HSCROLL, GA_RelVerify, TRUE, GA_Immediate, TRUE,
        GA_FollowMouse, TRUE,
        SCROLLER_Orientation, SCROLLER_HORIZONTAL,
        SCROLLER_Arrows, TRUE,
        SCROLLER_SignalTask, (ULONG)FindTask(NULL),
        SCROLLER_SignalTaskBit, 1UL << (ULONG)app->scroll_signal,
        SCROLLER_Total, 1,
        SCROLLER_Visible, 1,
        TAG_END);
    if (doc->vscroll == NULL || doc->hscroll == NULL) {
        if (doc->vscroll != NULL) DisposeObject(doc->vscroll);
        if (doc->hscroll != NULL) DisposeObject(doc->hscroll);
        DisposeObject(doc->editor); FreeVec(doc);
        ui_error(app, APP_NAME, S(MSG_ERR_SCROLLERS));
        return NULL;
    }
    doc->editor_row = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_AddChild, (ULONG)doc->editor,
        CHILD_WeightedWidth, 100,
        LAYOUT_AddChild, (ULONG)doc->vscroll,
        CHILD_WeightedWidth, 0,
        TAG_END);
    if (doc->editor_row == NULL) {
        DisposeObject(doc->hscroll);
        DisposeObject(doc->vscroll);
        DisposeObject(doc->editor); FreeVec(doc);
        ui_error(app, APP_NAME, S(MSG_ERR_DOCLAYOUT));
        return NULL;
    }
    doc->page = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_AddChild, (ULONG)doc->editor_row,
        CHILD_WeightedHeight, 100,
        LAYOUT_AddChild, (ULONG)doc->hscroll,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (doc->page == NULL) {
        DisposeObject(doc->editor_row);
        DisposeObject(doc->hscroll); FreeVec(doc);
        ui_error(app, APP_NAME, S(MSG_ERR_PAGE));
        return NULL;
    }
    doc->tab = AllocClickTabNode(TNA_Text, (ULONG)doc->tab_title, TNA_Number,
        0, TNA_UserData, (ULONG)doc,
        TNA_CloseGadget, TRUE, TAG_END);
    if (doc->tab == NULL) { DisposeObject(doc->page); FreeVec(doc); ui_error(app, APP_NAME, S(MSG_ERR_TAB)); return NULL; }
    detach_tabs(app);
    AddTail(&app->documents, &doc->node);
    AddTail(&app->tab_nodes, doc->tab);
    renumber_tabs(app);
    ui_gadget_set(app, app->pages, PAGE_Add, (ULONG)doc->page, TAG_IGNORE, 0);
    attach_tabs(app);
    document_activate(app, doc);
    ui_relayout(app);
    document_sync_scrollers(app, doc);
    return doc;
}

Document *document_find_path(EditorApp *app, const char *path)
{
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        if (doc->path != NULL && Stricmp(doc->path, path) == 0) return doc;
    return NULL;
}

/**
 * @brief Open a file, reusing or creating a document as appropriate.
 *
 * If the path is already open, its document is activated and returned. Otherwise
 * the active document is reused only when it is a pristine, untitled, sole
 * document; otherwise a new document is created. The file is then loaded. On a
 * load failure a newly created document is closed again (activating the
 * previously selected document if loading detected a duplicate), while a reused
 * document is simply re-activated.
 *
 * @param app The owning application.
 * @param path The file path to open.
 * @return The document showing the file, the pre-existing document for a
 *         duplicate, or NULL if opening failed.
 */
static Document *open_document(EditorApp *app, const char *path);

Document *document_open(EditorApp *app, const char *path)
{
    Document *doc;
    ui_begin_update(app);
    doc = open_document(app, path);
    ui_end_update(app);
    return doc;
}

/* The work of document_open(). */
static Document *open_document(EditorApp *app, const char *path)
{
    Document *doc = document_find_path(app, path), *selected;
    int duplicate, created = 0;
    if (doc != NULL) { document_activate(app, doc); return doc; }
    doc = app->active;
    if (doc == NULL || doc->path != NULL || doc->dirty ||
        doc->node.ln_Succ == NULL || doc->node.ln_Succ->ln_Succ != NULL) {
        doc = document_new(app);
        created = 1;
        if (doc == NULL) return NULL;
    }
    if (!file_load(app, doc, path)) {
        if (!created) { document_activate(app, doc); return NULL; }
        selected = app->active;
        duplicate = selected != doc;
        document_close(app, doc, 0);
        if (duplicate) { document_activate(app, selected); return selected; }
        return NULL;
    }
    return doc;
}

void document_activate(EditorApp *app, Document *doc)
{
    if (doc == NULL) return;
    app->active = doc;
    ui_gadget_set(app, app->tabs, CLICKTAB_CurrentNode, (ULONG)doc->tab, TAG_IGNORE, 0);
    ui_refresh_page(app, doc);
    ui_refresh(app);
    document_sync_scrollers(app, doc);
    minimap_request(app);
}

/** Larger texts are compared only after Undo and Redo, not after every
 *  pause in typing. */
#define CHECK_IDLE_MAX (256L * 1024L)

/**
 * @brief Checksum of a text (djb2 with xor, 32 bits, shifts and adds only)
 *        that ignores how lines end (LF, CR, CRLF) and line ends at the
 *        end of the text, as the editor's export may differ there from
 *        the file.
 * @param text The text.
 * @param length Its length.
 * @param counted Set to the number of characters checked.
 * @return The checksum.
 */
static unsigned long text_hash(const char *text, long length, long *counted)
{
    unsigned long h = 5381UL;
    long i, n = 0;
    unsigned char c;
    /* one line end at the end does not count (LF, CR or CR LF) */
    if (length > 0 && text[length - 1] == '\n') --length;
    if (length > 0 && text[length - 1] == '\r') --length;
    for (i = 0; i < length; ++i) {
        c = (unsigned char)text[i];
        if (c == '\r') {
            if (i + 1 < length && text[i + 1] == '\n') continue;
            c = '\n';
        }
        h = (((h << 5) + h) ^ c) & 0xffffffffUL;
        ++n;
    }
    *counted = n;
    return h;
}

void document_mark_saved(Document *doc, const char *text, long length, int from_file)
{
    doc->saved_hash = text_hash(text, length, &doc->saved_length);
    doc->saved_from_file = from_file;
}

void document_check_unchanged(EditorApp *app, Document *doc)
{
    char *text;
    long counted;
    unsigned long hash;
    if (doc == NULL) return;
    doc->check_pending = 0;
    if (!doc->dirty) return;
    text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    if (text == NULL) return;
    hash = text_hash(text, (long)strlen(text), &counted);
    FreeVec(text);
    if (counted == doc->saved_length && hash == doc->saved_hash) {
        SetAttrs(doc->editor, GA_TEXTEDITOR_HasChanged, FALSE, TAG_END);
        document_set_dirty(app, doc, 0);
    }
}

void document_changed(EditorApp *app)
{
    Document *doc = app->active;
    if (doc == NULL) return;
    /* HasChanged is cleared again, so its next change shows the next edit
     * (Undo with the keyboard is handled by the editor itself) */
    SetAttrs(doc->editor, GA_TEXTEDITOR_HasChanged, FALSE, TAG_END);
    doc->saved_from_file = 0;           /* too late to take it from the editor */
    doc->check_pending = 1;
    app->change_idle_ticks = 0;
    document_set_dirty(app, doc, 1);
    scoutlink_document_changed(doc);
}

void document_change_tick(EditorApp *app)
{
    Document *doc = app->active;
    if (doc == NULL || app->change_idle_ticks++ < 2) return;
    if (!doc->dirty && doc->saved_from_file) {
        /* compare later changes with the text as the editor exports it */
        char *text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
        if (text != NULL) {
            document_mark_saved(doc, text, (long)strlen(text), 0);
            FreeVec(text);
        }
        doc->saved_from_file = 0;
    }
    if (doc->check_pending && doc->saved_length <= CHECK_IDLE_MAX)
        document_check_unchanged(app, doc);
}

void document_set_dirty(EditorApp *app, Document *doc, int dirty)
{
    if (doc == NULL || doc->dirty == dirty) return;
    doc->dirty = dirty;
    snprintf(doc->tab_title, sizeof(doc->tab_title), "%s%s", doc->title, dirty ? "*" : "");
    SetClickTabNodeAttrs(doc->tab, TNA_Text, (ULONG)doc->tab_title, TAG_END);
    ui_gadget_set(app, app->tabs, CLICKTAB_MinorLabelChange, TRUE,
                  CLICKTAB_Labels, (ULONG)&app->tab_nodes);
    ui_refresh(app);
}

int document_close(EditorApp *app, Document *doc, int ask)
{
    Document *next;
    if (doc == NULL || (ask && !ui_confirm_close(app, doc))) return 0;
    /* Tabs, pages and the next document change together: lay the window
     * out and draw it once at the end. */
    ui_begin_update(app);
    /* The minimap must let go of the editor (and its colour hook, which reads
     * this document) before the page with the editor is disposed. */
    bookmarks_document_closing(app, doc);
    scoutlink_document_closing(app, doc);
    diag_free_document(doc);
    minimap_forget_document(app, doc);
    find_forget_document(doc);
    next = doc->node.ln_Succ->ln_Succ ? (Document *)doc->node.ln_Succ :
           (doc->node.ln_Pred->ln_Pred ? (Document *)doc->node.ln_Pred : NULL);
    detach_tabs(app);
    ui_gadget_set(app, app->pages, PAGE_Remove, (ULONG)doc->page, TAG_IGNORE, 0);
    Remove(doc->tab); Remove(&doc->node);
    renumber_tabs(app);
    FreeClickTabNode(doc->tab);
    DisposeObject(doc->page);
    scoutlink_document_free(doc);
    if (doc->path != NULL) FreeVec(doc->path);
    FreeVec(doc);
    attach_tabs(app);
    if (next != NULL) document_activate(app, next);
    else document_new(app);
    ui_relayout(app);
    ui_end_update(app);
    return 1;
}

int document_close_all(EditorApp *app)
{
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        if (!ui_confirm_close(app, doc)) return 0;
    for (;;) {
        doc = (Document *)app->documents.lh_Head;
        if (doc->node.ln_Succ == NULL) break;
        /* closing the last document makes an empty one: that one stays */
        if (doc->path == NULL && !doc->dirty && doc->node.ln_Succ->ln_Succ == NULL) break;
        document_close(app, doc, 0);
    }
    return 1;
}

void document_set_pens(EditorApp *app, Document *doc)
{
    UWORD pens[SYNTAX_STYLE_COUNT];
    int i;
    /* the colours of the settings are in the order of the styles */
    for (i = 0; i < SYNTAX_STYLE_COUNT; ++i) pens[i] = (UWORD)app->editor_pens[EDITOR_COLOR_TEXT + i];
    syntax_set_pens(&doc->highlight, pens);
}

void document_go_to(EditorApp *app, const char *path, long line, long col)
{
    Document *doc = path != NULL ? document_find_path(app, path) : app->active;
    if (doc == NULL) {
        if (path == NULL || (doc = document_open(app, path)) == NULL) return;
    } else if (doc != app->active) {
        document_activate(app, doc);
    }
    /* set once the document is shown and laid out (document_goto_poll()) */
    doc->goto_line = line > 0 ? line : 1;
    doc->goto_col = col > 0 ? col : 1;
}

void document_goto_poll(EditorApp *app)
{
    Document *doc = app->active;
    if (doc == NULL || doc->goto_line <= 0 || app->window == NULL) return;
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
                   GA_TEXTEDITOR_CursorY, (ULONG)(doc->goto_line - 1),
                   GA_TEXTEDITOR_CursorX, (ULONG)(doc->goto_col - 1), TAG_END);
    doc->goto_line = 0;
    document_sync_scrollers(app, doc);
    /* a menu pick or another window took the input focus: give it back,
     * or the cursor is not shown */
    if (app->layout != NULL && (app->window->Flags & WFLG_WINDOWACTIVE))
        ActivateLayoutGadget((struct Gadget *)app->layout, app->window, NULL, (ULONG)doc->editor);
}

void document_free_all(EditorApp *app)
{
    Document *doc;
    while ((doc = (Document *)RemHead(&app->documents)) != NULL) {
        if (doc->tab != NULL) { Remove(doc->tab); FreeClickTabNode(doc->tab); }
        if (app->pages != NULL && doc->page != NULL)
            SetAttrs(app->pages, PAGE_Remove, (ULONG)doc->page, TAG_END);
        if (doc->page != NULL) DisposeObject(doc->page);
        scoutlink_document_free(doc);
        if (doc->path != NULL) FreeVec(doc->path);
        diag_free_document(doc);
        FreeVec(doc);
    }
}

void document_set_path(EditorApp *app, Document *doc, const char *path)
{
    char *copy = copy_string(path);
    if (copy == NULL) return;
    if (doc->path != NULL) FreeVec(doc->path);
    doc->path = copy;
    strncpy(doc->title, base_name(path), sizeof(doc->title) - 1);
    doc->title[sizeof(doc->title) - 1] = '\0';
    doc->language = syntax_language_for_path(path);
    doc->highlight.language = doc->language;
    /* The callers mark the document clean right after this; show the new
     * title now (without toggling the dirty flag, which redrew twice). */
    doc->dirty = 1;
    document_set_dirty(app, doc, 0);
}

/**
 * @brief Update a scroller gadget's range only when its state changed.
 *
 * Reads the scroller's current total, visible and top attributes and, if any
 * differ from the requested values, applies them together (using the
 * window-aware SetGadgetAttrs when a window exists). Skipping unchanged updates
 * avoids needless redraws.
 *
 * @param app The owning application (provides the window, if any).
 * @param scroller The scroller object to update.
 * @param total The new total range.
 * @param visible The new visible amount.
 * @param top The new top position.
 */
static void set_scroller_state(EditorApp *app, Object *scroller,
                               ULONG total, ULONG visible, ULONG top)
{
    ULONG old_total = 0, old_visible = 0, old_top = 0;
    GetAttr(SCROLLER_Total, scroller, &old_total);
    GetAttr(SCROLLER_Visible, scroller, &old_visible);
    GetAttr(SCROLLER_Top, scroller, &old_top);
    if (total == old_total && visible == old_visible && top == old_top) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)scroller, app->window, NULL,
            SCROLLER_Total, total, SCROLLER_Visible, visible,
            SCROLLER_Top, top, TAG_END);
    else SetAttrs(scroller, SCROLLER_Total, total,
                  SCROLLER_Visible, visible, SCROLLER_Top, top, TAG_END);
    /* If the scroller signals changes made by the program as well, that
     * signal must not be taken for a drag by the user. */
    if (app->scroll_signal >= 0) SetSignal(0, 1UL << (ULONG)app->scroll_signal);
}

void document_sync_scrollers(EditorApp *app, Document *doc)
{
    ULONG total = 1, visible = 1, top = 0;
    if (doc == NULL || doc != app->active) return;
    /* Never write into a scroller the user is dragging: setting its top
     * while it is active makes the knob jump back and lose its pressed
     * look. The scrollers are synced again when the drag ends. */
    if (app->scrollbar_dragging) return;
    GetAttr(GA_TEXTEDITOR_Prop_Entries, doc->editor, &total);
    GetAttr(GA_TEXTEDITOR_Prop_Visible, doc->editor, &visible);
    GetAttr(GA_TEXTEDITOR_Prop_First, doc->editor, &top);
    if (total == 0) total = 1;
    if (visible == 0) visible = 1;
    set_scroller_state(app, doc->vscroll, total, visible, top);
    total = visible = 1; top = 0;
    GetAttr(GA_TEXTEDITOR_HProp_Entries, doc->editor, &total);
    GetAttr(GA_TEXTEDITOR_HProp_Visible, doc->editor, &visible);
    GetAttr(GA_TEXTEDITOR_HProp_First, doc->editor, &top);
    if (total == 0) total = 1;
    if (visible == 0) visible = 1;
    set_scroller_state(app, doc->hscroll, total, visible, top);
}

/**
 * @brief Enable or disable the TextEditor's syntax highlighting hook.
 *
 * Highlighting is switched off while the user is actively dragging a scrollbar
 * so that the per-line scanner does not run on every intermediate redraw, which
 * otherwise makes scrollbar scrolling much slower than the gadget's own
 * mouse-wheel scrolling. Passing zero for @p enabled installs a NULL hook.
 * Does nothing when @p doc is NULL.
 *
 * @param app The owning application (provides the window, if any).
 * @param doc The document whose editor hook is changed.
 * @param enabled Non-zero to install the highlighter hook, zero to remove it.
 */
static void set_highlight_enabled(EditorApp *app, Document *doc, int enabled)
{
    ULONG hook = enabled ? (ULONG)&doc->highlight.hook : 0;
    if (doc == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
            GA_TEXTEDITOR_HighlighterHook, hook, TAG_END);
    else SetAttrs(doc->editor, GA_TEXTEDITOR_HighlighterHook, hook, TAG_END);
}

void document_scroll(EditorApp *app, int horizontal)
{
    Document *doc = app->active;
    ULONG top = 0, old_top = 0;
    ULONG editor_attribute = horizontal ? GA_TEXTEDITOR_HProp_First :
                                          GA_TEXTEDITOR_Prop_First;
    if (doc == NULL || app->window == NULL) return;
    GetAttr(SCROLLER_Top, horizontal ? doc->hscroll : doc->vscroll, &top);
    GetAttr(editor_attribute, doc->editor, &old_top);
    if (top == old_top) return;
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
        editor_attribute, top, TAG_END);
    RefreshGList((struct Gadget *)doc->editor, app->window, NULL, 1);
}

void document_scroll_live(EditorApp *app)
{
    app->scrollbar_dragging = 1;
    app->scroll_idle_ticks = 0;
    if (app->active != NULL && !app->scrolling) {
        set_highlight_enabled(app, app->active, 0);
        app->scrolling = 1;
    }
    document_scroll(app, 0);
    document_scroll(app, 1);
}

void document_scroll_finish(EditorApp *app, int horizontal)
{
    document_scroll(app, horizontal);
    app->scrollbar_dragging = 0;
    app->scroll_idle_ticks = 0;
    document_sync_scrollers(app, app->active);
    if (app->scrolling) {
        app->scrolling = 0;
        if (app->active != NULL) {
            set_highlight_enabled(app, app->active, 1);
            if (app->window != NULL)
                RefreshPageGadget((struct Gadget *)app->active->page,
                                  app->pages, app->window, NULL);
        }
    }
}

void document_scroll_tick(EditorApp *app)
{
    if (!app->scrollbar_dragging) return;
    if (++app->scroll_idle_ticks < 2) return;
    document_scroll(app, 1);
    document_scroll_finish(app, 0);
}

void document_suspend_highlight(EditorApp *app)
{
    if (app->active != NULL && !app->scrolling) {
        set_highlight_enabled(app, app->active, 0);
        app->scrolling = 1;
    }
}

void document_resume_highlight(EditorApp *app)
{
    if (app->scrolling) {
        app->scrolling = 0;
        if (app->active != NULL) {
            set_highlight_enabled(app, app->active, 1);
            if (app->window != NULL)
                RefreshPageGadget((struct Gadget *)app->active->page,
                                  app->pages, app->window, NULL);
        }
    }
}
