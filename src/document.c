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
    if (app->tabs == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->tabs, app->window, NULL,
            CLICKTAB_Labels, ~0UL, TAG_END);
    else SetAttrs(app->tabs, CLICKTAB_Labels, ~0UL, TAG_END);
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
    if (app->tabs == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->tabs, app->window, NULL,
            CLICKTAB_Labels, (ULONG)&app->tab_nodes, TAG_END);
    else SetAttrs(app->tabs, CLICKTAB_Labels,
                  (ULONG)&app->tab_nodes, TAG_END);
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
Document *document_new(EditorApp *app)
{
    Document *doc = AllocVec(sizeof(*doc), MEMF_ANY | MEMF_CLEAR);
    if (doc == NULL) { ui_error(app, "AmiEditor", "Not enough memory for a document."); return NULL; }
    doc->number = ++app->next_document;
    snprintf(doc->title, sizeof(doc->title), "Untitled %lu", doc->number);
    strcpy(doc->tab_title, doc->title);
    doc->eol = EOL_LF;
    doc->language = SYNTAX_PLAIN;
    syntax_init_hook(&doc->highlight, doc->language);
    syntax_set_pens(&doc->highlight,
        (UWORD)app->editor_pens[EDITOR_COLOR_TEXT],
        (UWORD)app->editor_pens[EDITOR_COLOR_KEYWORD],
        (UWORD)app->editor_pens[EDITOR_COLOR_STRING],
        (UWORD)app->editor_pens[EDITOR_COLOR_COMMENT],
        (UWORD)app->editor_pens[EDITOR_COLOR_PREPROCESSOR]);
    doc->editor = create_editor(app, doc, "");
    if (doc->editor == NULL) { FreeVec(doc); ui_error(app, "AmiEditor", "Could not create TextEditor gadget."); return NULL; }
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
        ui_error(app, "AmiEditor", "Could not create the editor scrollbars.");
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
        ui_error(app, "AmiEditor", "Could not create the editor layout.");
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
        ui_error(app, "AmiEditor", "Could not create the document page.");
        return NULL;
    }
    doc->tab = AllocClickTabNode(TNA_Text, (ULONG)doc->tab_title, TNA_Number,
        0, TNA_UserData, (ULONG)doc,
        TNA_CloseGadget, TRUE, TAG_END);
    if (doc->tab == NULL) { DisposeObject(doc->page); FreeVec(doc); ui_error(app, "AmiEditor", "Could not create tab."); return NULL; }
    detach_tabs(app);
    AddTail(&app->documents, &doc->node);
    AddTail(&app->tab_nodes, doc->tab);
    renumber_tabs(app);
    if (app->pages != NULL) {
        if (app->window != NULL)
            SetGadgetAttrs((struct Gadget *)app->pages, app->window, NULL,
                PAGE_Add, (ULONG)doc->page, TAG_END);
        else SetAttrs(app->pages, PAGE_Add, (ULONG)doc->page, TAG_END);
    }
    attach_tabs(app);
    document_activate(app, doc);
    ui_relayout(app);
    document_sync_scrollers(app, doc);
    return doc;
}

/**
 * @brief Find an open document by its file path.
 *
 * Performs a case-insensitive comparison (Stricmp) of the given path against
 * the stored path of every open document.
 *
 * @param app The application whose documents are searched.
 * @param path The file path to look for.
 * @return The matching document, or NULL if no open document uses @p path.
 */
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
Document *document_open(EditorApp *app, const char *path)
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

/**
 * @brief Make the given document the active one and update the UI.
 *
 * Sets app->active, selects the document's tab, brings its page gadget to the
 * front, refreshes the UI, synchronizes the scrollers, and requests a minimap
 * update. Does nothing when @p doc is NULL.
 *
 * @param app The owning application.
 * @param doc The document to activate.
 */
void document_activate(EditorApp *app, Document *doc)
{
    if (doc == NULL) return;
    app->active = doc;
    if (app->tabs != NULL) {
        if (app->window != NULL)
            SetGadgetAttrs((struct Gadget *)app->tabs, app->window, NULL,
                CLICKTAB_CurrentNode, (ULONG)doc->tab, TAG_END);
        else SetAttrs(app->tabs, CLICKTAB_CurrentNode,
                      (ULONG)doc->tab, TAG_END);
    }
    if (app->window != NULL)
        RefreshPageGadget((struct Gadget *)doc->page, app->pages,
                          app->window, NULL);
    ui_refresh(app);
    document_sync_scrollers(app, doc);
    minimap_request(app);
}

/**
 * @brief Set a document's modified (dirty) state and update its tab title.
 *
 * When the state actually changes, updates the tab title to append a '*' marker
 * for dirty documents, pushes the new label to the click-tab gadget as a minor
 * label change, and relays out and refreshes the UI. Does nothing when @p doc
 * is NULL or already in the requested state.
 *
 * @param app The owning application.
 * @param doc The document whose dirty state changes.
 * @param dirty Non-zero to mark the document modified, zero to mark it clean.
 */
void document_set_dirty(EditorApp *app, Document *doc, int dirty)
{
    if (doc == NULL || doc->dirty == dirty) return;
    doc->dirty = dirty;
    snprintf(doc->tab_title, sizeof(doc->tab_title), "%s%s", doc->title, dirty ? "*" : "");
    SetClickTabNodeAttrs(doc->tab, TNA_Text, (ULONG)doc->tab_title, TAG_END);
    if (app->tabs != NULL) {
        if (app->window != NULL)
            SetGadgetAttrs((struct Gadget *)app->tabs, app->window, NULL,
                CLICKTAB_MinorLabelChange, TRUE,
                CLICKTAB_Labels, (ULONG)&app->tab_nodes, TAG_END);
        else SetAttrs(app->tabs, CLICKTAB_MinorLabelChange, TRUE,
                      CLICKTAB_Labels, (ULONG)&app->tab_nodes, TAG_END);
    }
    ui_relayout(app);
    ui_refresh(app);
}

/**
 * @brief Close a document, optionally confirming unsaved changes.
 *
 * When @p ask is set the user is prompted and the close is aborted if declined.
 * Otherwise the document's page is removed from the pager, its tab and list
 * node are unlinked, tabs are renumbered, and all its resources (tab node,
 * page, path buffer, and the document itself) are freed. The next remaining
 * document is activated, or a fresh empty document is created when none remain,
 * followed by a relayout.
 *
 * @param app The owning application.
 * @param doc The document to close.
 * @param ask Non-zero to ask the user for confirmation before closing.
 * @return 1 if the document was closed, 0 if the close was cancelled or
 *         @p doc was NULL.
 */
int document_close(EditorApp *app, Document *doc, int ask)
{
    Document *next;
    if (doc == NULL || (ask && !ui_confirm_close(app, doc))) return 0;
    next = doc->node.ln_Succ->ln_Succ ? (Document *)doc->node.ln_Succ :
           (doc->node.ln_Pred->ln_Pred ? (Document *)doc->node.ln_Pred : NULL);
    detach_tabs(app);
    if (app->pages != NULL) {
        if (app->window != NULL)
            SetGadgetAttrs((struct Gadget *)app->pages, app->window, NULL,
                PAGE_Remove, (ULONG)doc->page, TAG_END);
        else SetAttrs(app->pages, PAGE_Remove, (ULONG)doc->page, TAG_END);
    }
    Remove(doc->tab); Remove(&doc->node);
    renumber_tabs(app);
    FreeClickTabNode(doc->tab);
    DisposeObject(doc->page);
    if (doc->path != NULL) FreeVec(doc->path);
    FreeVec(doc);
    attach_tabs(app);
    if (next != NULL) document_activate(app, next);
    else document_new(app);
    ui_relayout(app);
    return 1;
}

/**
 * @brief Free every document without touching UI activation logic.
 *
 * Removes each document from the list in turn and releases its tab node, page
 * object, path buffer, and the document structure itself. Intended for teardown
 * where no replacement document should be created.
 *
 * @param app The application whose documents are all released.
 */
void document_free_all(EditorApp *app)
{
    Document *doc;
    while ((doc = (Document *)RemHead(&app->documents)) != NULL) {
        if (doc->tab != NULL) { Remove(doc->tab); FreeClickTabNode(doc->tab); }
        if (app->pages != NULL && doc->page != NULL)
            SetAttrs(app->pages, PAGE_Remove, (ULONG)doc->page, TAG_END);
        if (doc->page != NULL) DisposeObject(doc->page);
        if (doc->path != NULL) FreeVec(doc->path);
        FreeVec(doc);
    }
}

/**
 * @brief Assign a file path to a document and derive its title and language.
 *
 * Duplicates the path, replaces any previous path, sets the document title from
 * the path's base name, and selects the syntax language from the path (updating
 * both the document and its highlighter). Toggling the dirty flag on then off
 * refreshes the tab title while leaving the document marked clean. Returns
 * without changes if the path copy could not be allocated.
 *
 * @param app The owning application.
 * @param doc The document to update.
 * @param path The new file path to associate with the document.
 */
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
    document_set_dirty(app, doc, 1);
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
}

/**
 * @brief Synchronize the scrollers with the editor's current view.
 *
 * Queries the TextEditor's vertical and horizontal prop attributes (total
 * entries, visible amount, first line/column) and pushes them to the vertical
 * and horizontal scrollers, clamping zero totals/visibles to one. Does nothing
 * unless @p doc is the active document.
 *
 * @param app The owning application.
 * @param doc The document whose scrollers are synchronized.
 */
void document_sync_scrollers(EditorApp *app, Document *doc)
{
    ULONG total = 1, visible = 1, top = 0;
    if (doc == NULL || doc != app->active) return;
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

/**
 * @brief Apply the current scroller position to the active editor on one axis.
 *
 * Reads the top position from the chosen (horizontal or vertical) scroller and,
 * when it differs from the editor's current first line/column, applies it to
 * the editor, refreshes the page gadget, and re-synchronizes the scrollers.
 * Does nothing without an active document and window.
 *
 * @param app The owning application.
 * @param horizontal Non-zero to scroll the horizontal axis, zero for vertical.
 */
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
    RefreshPageGadget((struct Gadget *)doc->page, app->pages,
                      app->window, NULL);
    document_sync_scrollers(app, doc);
}

/**
 * @brief Handle an intermediate scrollbar drag update.
 *
 * On the first live update the highlighter hook is switched off (setting the
 * shared scrolling flag) so the rapid redraws that follow the mouse stay fast;
 * the position of both the vertical and horizontal axes is then mirrored onto
 * the editor.
 *
 * @param app The owning application.
 */
void document_scroll_live(EditorApp *app)
{
    if (app->active != NULL && !app->scrolling) {
        set_highlight_enabled(app, app->active, 0);
        app->scrolling = 1;
    }
    document_scroll(app, 0);
    document_scroll(app, 1);
}

/**
 * @brief Handle the end of a scrollbar interaction.
 *
 * Covers button release, arrow, and page-click events on the given axis. After
 * applying the final position, re-enables highlighting if it was disabled
 * during a drag (clearing the scrolling flag) and forces a full page redraw so
 * the now-visible lines are highlighted again.
 *
 * @param app The owning application.
 * @param horizontal Non-zero for the horizontal axis, zero for vertical.
 */
void document_scroll_finish(EditorApp *app, int horizontal)
{
    document_scroll(app, horizontal);
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

/**
 * @brief Suspend the syntax highlighter during an interactive minimap drag.
 *
 * Behaves exactly like a scrollbar drag, disabling the highlighter on the
 * active document so the rapid re-scrolls that follow the mouse stay fast. The
 * shared app->scrolling flag also tells minimap_poll() not to re-render the
 * viewport overlay until the drag has finished.
 *
 * @param app The owning application.
 */
void document_suspend_highlight(EditorApp *app)
{
    if (app->active != NULL && !app->scrolling) {
        set_highlight_enabled(app, app->active, 0);
        app->scrolling = 1;
    }
}

/**
 * @brief Restore the syntax highlighter after an interactive minimap drag.
 *
 * Clears the shared scrolling flag, re-enables highlighting on the active
 * document, and forces a full page redraw so the now-visible lines are
 * highlighted again. Does nothing if no drag was in progress.
 *
 * @param app The owning application.
 */
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
