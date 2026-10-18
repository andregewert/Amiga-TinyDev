#include "editor.h"

#include <gadgets/clicktab.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/dirtree.h>
#include <gadgets/minimap.h>
#include <gadgets/listbrowser.h>
#include <gadgets/texteditor.h>
#include <images/bitmap.h>
#include <libraries/gadtools.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/icclass.h>
#include <proto/clicktab.h>
#include <proto/button.h>
#include <proto/bitmap.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/texteditor.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <clib/alib_protos.h>
#include <reaction/reaction.h>

#include <stdio.h>
#include <string.h>

#define UD(id) ((APTR)(ULONG)(id))

typedef struct EditorBackFillMessage {
    struct Layer *layer;
    struct Rectangle bounds;
    LONG offset_x;
    LONG offset_y;
} EditorBackFillMessage;

/**
 * @brief Backfill hook that paints layout backgrounds with the editor color.
 *
 * Invoked by the layout/window classes to fill exposed areas.  It fills the
 * requested rectangle with the editor background pen and preserves the
 * RastPort's previous foreground pen so subsequent rendering is unaffected.
 *
 * @param hook The hook whose h_Data points to the owning EditorApp.
 * @param rast_port The RastPort to render into.
 * @param message The backfill message describing the rectangle to fill.
 * @return Always 0.
 */
static ULONG editor_backfill_entry(struct Hook *hook, struct RastPort *rast_port,
                                   EditorBackFillMessage *message)
{
    EditorApp *app = (EditorApp *)hook->h_Data;
    UBYTE old_pen;
    if (app == NULL || rast_port == NULL || message == NULL) return 0;
    old_pen = (UBYTE)GetAPen(rast_port);
    SetAPen(rast_port,
            (UBYTE)app->editor_pens[EDITOR_COLOR_BACKGROUND]);
    RectFill(rast_port, message->bounds.MinX, message->bounds.MinY,
             message->bounds.MaxX, message->bounds.MaxY);
    SetAPen(rast_port, old_pen);
    return 0;
}

/* window.class IDCMP hook.  The clicktab.gadget delivers a tab close only as
 * an IDCMP_IDCMPUPDATE notification (see the ICA_TARGET tag on the clicktab
 * object); enabling the IDCMPUPDATE class through WINDOW_IDCMPHookBits lets us
 * receive it here and extract the closed node directly from the message tag
 * list.  Reading CLICKTAB_NodeClosed via GetAttr is not reliable, hence the
 * hook.  We only remember the node and defer the actual document_close() to
 * the event loop, because disposing gadgets from within input handling is
 * unsafe. */
/**
 * @brief window.class IDCMP hook capturing clicktab tab-close notifications.
 *
 * The clicktab.gadget delivers a tab close only as an IDCMP_IDCMPUPDATE
 * notification carrying the closed node in its tag list.  This hook extracts
 * that node and stores it in app->pending_close, deferring the actual
 * document_close() to the event loop because disposing gadgets from within
 * input handling is unsafe.
 *
 * @param hook The hook whose h_Data points to the owning EditorApp.
 * @param win The window object receiving the message (unused).
 * @param msg The IntuiMessage; only IDCMP_IDCMPUPDATE messages are handled.
 * @return The original message pointer cast to ULONG.
 */
static ULONG tab_idcmp_entry(struct Hook *hook, Object *win,
                             struct IntuiMessage *msg)
{
    EditorApp *app = (EditorApp *)hook->h_Data;
    (void)win;
    if (app != NULL && msg != NULL && msg->Class == IDCMP_IDCMPUPDATE) {
        struct TagItem *tags = (struct TagItem *)msg->IAddress;
        struct Node *node;
        /* minimap.gadget reports clicks and drags the same way. */
        if (minimap_handle_notify(app, tags)) return (ULONG)msg;
        node = (struct Node *)GetTagData(CLICKTAB_NodeClosed, 0, tags);
        if (node != NULL) app->pending_close = node;
    }
    return (ULONG)msg;
}

/**
 * @brief One component of an 0xRRGGBB colour as 32 bit value for
 *        ObtainBestPen().
 * @param c The colour.
 * @param shift 16 for red, 8 for green, 0 for blue.
 * @return The component, repeated into all 4 bytes.
 */
static ULONG rgb32(unsigned long c, int shift)
{
    return (ULONG)((c >> shift) & 0xffUL) * 0x01010101UL;
}

/**
 * @brief Reserve and configure the pens used for editor rendering.
 *
 * Obtains the fixed set of editor colors (background, text, and syntax pens)
 * from the public screen's ColorMap, validates that the screen provides the
 * required number of DrawInfo pens, and builds a private DrawInfo whose
 * background and text pens are overridden.  Also obtains an optional minimap
 * viewport-tint pen (non-fatal on failure) and initializes the backfill hook.
 * On any fatal failure all pens obtained so far are released.
 *
 * @param app The application whose screen and pen state are set up.
 * @return 1 on success, 0 on failure.
 */
static int open_editor_colors(EditorApp *app)
{
    struct TagItem tags[] = {
        {OBP_Precision, PRECISION_GUI},
        {OBP_FailIfBad, FALSE},
        {TAG_END, 0}
    };
    struct TagItem white_tags[] = {
        {OBP_Precision, (ULONG)PRECISION_EXACT},
        {OBP_FailIfBad, TRUE},
        {TAG_END, 0}
    };
    int i;
    struct ColorMap *color_map = app->screen->ViewPort.ColorMap;

    app->minimap_view_pen = -1;
    for (i = 0; i < EDITOR_COLOR_COUNT; ++i) app->editor_pens[i] = -1;
    for (i = 0; i < EDITOR_COLOR_COUNT; ++i) {
        /* white background, the other colours from the settings */
        unsigned long c = i == EDITOR_COLOR_BACKGROUND
            ? 0xffffffUL : app->settings.colours[i - EDITOR_COLOR_TEXT];
        app->editor_pens[i] = ObtainBestPenA(color_map, rgb32(c, 16), rgb32(c, 8),
                                             rgb32(c, 0), i == EDITOR_COLOR_BACKGROUND
                                             ? white_tags : tags);
        if (app->editor_pens[i] < 0) {
            while (--i >= 0) ReleasePen(color_map, (ULONG)app->editor_pens[i]);
            return 0;
        }
    }
    app->screen_draw_info = GetScreenDrawInfo(app->screen);
    if (app->screen_draw_info == NULL ||
        app->screen_draw_info->dri_NumPens < NUMDRIPENS) {
        if (app->screen_draw_info != NULL) {
            FreeScreenDrawInfo(app->screen, app->screen_draw_info);
            app->screen_draw_info = NULL;
        }
        for (i = EDITOR_COLOR_COUNT - 1; i >= 0; --i)
            ReleasePen(color_map, (ULONG)app->editor_pens[i]);
        return 0;
    }
    app->editor_draw_info = *app->screen_draw_info;
    memcpy(app->editor_draw_pens, app->screen_draw_info->dri_Pens,
           sizeof(app->editor_draw_pens));
    app->editor_draw_pens[BACKGROUNDPEN] =
        (UWORD)app->editor_pens[EDITOR_COLOR_BACKGROUND];
    app->editor_draw_pens[TEXTPEN] =
        (UWORD)app->editor_pens[EDITOR_COLOR_TEXT];
    app->editor_draw_info.dri_Pens = app->editor_draw_pens;
    app->editor_draw_info.dri_NumPens = NUMDRIPENS;
    /* A grey slightly darker than the editor's BACKGROUNDPEN, used to tint the
     * minimap's visible-area viewport rectangle.  Obtaining it is non-fatal:
     * if it fails the minimap falls back to its plain background. */
    app->minimap_view_pen = ObtainBestPenA(color_map, 0x88888888UL,
                                           0x88888888UL, 0x88888888UL, tags);
    memset(&app->editor_backfill_hook, 0, sizeof(app->editor_backfill_hook));
    app->editor_backfill_hook.h_Entry = (ULONG (*)())HookEntry;
    app->editor_backfill_hook.h_SubEntry = (ULONG (*)())editor_backfill_entry;
    app->editor_backfill_hook.h_Data = app;
    app->editor_colors_open = 1;
    return 1;
}

/**
 * @brief Release the pens and DrawInfo reserved by open_editor_colors().
 *
 * Frees the screen DrawInfo, releases the optional minimap view pen and all
 * editor pens back to the screen ColorMap, and clears the colors-open flag.
 * Does nothing if colors were never opened or the screen is gone.
 *
 * @param app The application whose editor colors are released.
 */
static void close_editor_colors(EditorApp *app)
{
    int i;
    if (!app->editor_colors_open || app->screen == NULL) return;
    if (app->screen_draw_info != NULL) {
        FreeScreenDrawInfo(app->screen, app->screen_draw_info);
        app->screen_draw_info = NULL;
    }
    if (app->minimap_view_pen >= 0) {
        ReleasePen(app->screen->ViewPort.ColorMap, (ULONG)app->minimap_view_pen);
        app->minimap_view_pen = -1;
    }
    for (i = EDITOR_COLOR_COUNT - 1; i >= 0; --i)
        ReleasePen(app->screen->ViewPort.ColorMap, (ULONG)app->editor_pens[i]);
    app->editor_colors_open = 0;
}

static struct NewMenu menus[] = {
    {NM_TITLE, "Project", NULL, 0, 0, NULL},
    {NM_ITEM, "New", "N", 0, 0, UD(MID_NEW)},
    {NM_ITEM, "Open...", "O", 0, 0, UD(MID_OPEN)},
    {NM_ITEM, "Open Directory...", NULL, 0, 0, UD(MID_OPEN_DIRECTORY)},
    {NM_ITEM, "Save", "S", 0, 0, UD(MID_SAVE)},
    {NM_ITEM, "Save As...", NULL, 0, 0, UD(MID_SAVE_AS)},
    {NM_ITEM, "Close", "W", 0, 0, UD(MID_CLOSE)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, "Settings...", ",", 0, 0, UD(MID_SETTINGS)},
    {NM_ITEM, "About...", "?", 0, 0, UD(MID_ABOUT)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, "Quit", "Q", 0, 0, UD(MID_QUIT)},
    {NM_TITLE, "Edit", NULL, 0, 0, NULL},
    {NM_ITEM, "Undo", "Z", 0, 0, UD(MID_UNDO)},
    {NM_ITEM, "Redo", "Y", 0, 0, UD(MID_REDO)},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, "Cut", "X", 0, 0, UD(MID_CUT)},
    {NM_ITEM, "Copy", "C", 0, 0, UD(MID_COPY)},
    {NM_ITEM, "Paste", "V", 0, 0, UD(MID_PASTE)},
    {NM_ITEM, "Select All", "A", 0, 0, UD(MID_SELECT_ALL)},
    {NM_TITLE, "View", NULL, 0, 0, NULL},
    {NM_ITEM, "Folder Tree", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, UD(MID_FOLDER_TREE)},
    {NM_ITEM, "Line Numbers", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, UD(MID_LINE_NUMBERS)},
    {NM_ITEM, "Minimap", NULL, CHECKIT | MENUTOGGLE, 0, UD(MID_MINIMAP)},
    {NM_END, NULL, NULL, 0, 0, NULL}
};

/**
 * @brief Display a simple modal error/notification requester with an OK button.
 *
 * @param app The application; its window is used as the requester parent (may
 *            be NULL).
 * @param title The requester title text.
 * @param message The requester body text.
 */
void ui_error(EditorApp *app, const char *title, const char *message)
{
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)title, (STRPTR)message, "OK"};
    splash_close();                     /* not over the requester */
    EasyRequestArgs(app != NULL ? app->window : NULL, &es, NULL, NULL);
}

/**
 * @brief Show the About window (as in Kickdown), with a requester as fallback.
 *
 * The details name the website, mail address and licence and the versions
 * of TinyDev's own gadget classes in use.
 *
 * @param app The application owning the main window.
 */
void ui_about(EditorApp *app)
{
    char details[200];
    struct EasyStruct es = {sizeof(es), 0, "About " APP_NAME, "%s %s\n\n%s", "OK"};
    ULONG args[3];
    snprintf(details, sizeof(details),
             "www.ubergeek.de - agewert@ubergeek.de\n"
             "Released under the MIT License\n\n"
             "dirtree.gadget %ld.%ld, minimap.gadget %ld.%ld",
             DirTreeBase != NULL ? (long)DirTreeBase->lib_Version : 0L,
             DirTreeBase != NULL ? (long)DirTreeBase->lib_Revision : 0L,
             MinimapBase != NULL ? (long)MinimapBase->lib_Version : 0L,
             MinimapBase != NULL ? (long)MinimapBase->lib_Revision : 0L);
    if (about_window(app, details)) return;
    /* without the window: a requester */
    args[0] = (ULONG)APP_NAME;
    args[1] = (ULONG)APP_VERSION;
    args[2] = (ULONG)details;
    EasyRequestArgs(app != NULL ? app->window : NULL, &es, NULL, args);
}

/**
 * @brief Update the window title to reflect the active document.
 *
 * Sets the title to "TinyDev" when no document is active, otherwise to
 * "TinyDev - <title>" with a trailing '*' when the active document is dirty.
 *
 * @param app The application whose window title is refreshed.
 */
void ui_refresh(EditorApp *app)
{
    static char window_title[EDITOR_TITLE_MAX + 24];
    if (app->active == NULL) strcpy(window_title, "TinyDev");
    else snprintf(window_title, sizeof(window_title), "TinyDev - %s%s",
                  app->active->title, app->active->dirty ? "*" : "");
    if (app->window_object != NULL) SetAttrs(app->window_object, WA_Title, (ULONG)window_title, TAG_END);
}

/**
 * @brief Refresh the bottom status line with document and line counts.
 *
 * Counts the open documents and reads the line count of the active document
 * from its texteditor gadget, then updates the status bar text.  Uses
 * SetGadgetAttrs when a window is open so the gadget redraws, otherwise
 * SetAttrs.  Does nothing if the status bar does not exist.
 *
 * @param app The application whose status line is updated.
 */
void ui_update_status(EditorApp *app)
{
    static char status_text[64], shown_text[64];
    unsigned long doc_count = 0;
    ULONG lines = 0;
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        ++doc_count;
    if (app->active != NULL)
        GetAttr(GA_TEXTEDITOR_Prop_Entries, app->active->editor, &lines);
    snprintf(status_text, sizeof(status_text),
             "Documents: %lu  Lines: %lu", doc_count, (unsigned long)lines);
    if (app->statusbar == NULL) return;
    /* Called on every tick: only redraw the status line when it changed. */
    if (strcmp(status_text, shown_text) == 0) return;
    strcpy(shown_text, status_text);
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->statusbar, app->window, NULL,
            GA_Text, (ULONG)status_text, TAG_END);
    else SetAttrs(app->statusbar, GA_Text, (ULONG)status_text, TAG_END);
}

/**
 * @brief Recompute the window's layout after a structural change.
 *
 * Calls RethinkLayout on the root layout when both the window and layout
 * exist; otherwise does nothing.
 *
 * @param app The application whose layout is recomputed.
 */
void ui_relayout(EditorApp *app)
{
    if (app->window != NULL && app->layout != NULL)
        RethinkLayout((struct Gadget *)app->layout, app->window, NULL, TRUE);
}

/**
 * @brief Confirm closing a document, prompting to save unsaved changes.
 *
 * Returns immediately allowing the close for a document that is not dirty.
 * Otherwise presents a Save/Discard/Cancel requester: Discard allows the
 * close, Cancel blocks it, and Save saves to the document's existing path or
 * prompts for one before allowing the close.
 *
 * @param app The application owning the window used for the requester.
 * @param doc The document being closed.
 * @return Nonzero if the document may be closed, 0 to cancel the close.
 */
int ui_confirm_close(EditorApp *app, Document *doc)
{
    char text[EDITOR_TITLE_MAX + 80]; int answer;
    if (!doc->dirty) return 1;
    snprintf(text, sizeof(text), "Save changes to %s?", doc->title);
    { struct EasyStruct es = {sizeof(es), 0, "Unsaved changes", text, "Save|Discard|Cancel"};
      answer = EasyRequestArgs(app->window, &es, NULL, NULL); }
    if (answer == 2) return 1;
    if (answer != 1) return 0;
    return doc->path != NULL ? file_save(app, doc, doc->path) : file_request_save(app, doc);
}



/**
 * @brief Build the application's screen resources, gadgets, and main window.
 *
 * Locks the public screen, reserves editor colors, and creates the page group,
 * click tabs (with a masked close image and IDCMP-targeted close notification),
 * folder tree, minimap gadget, toolbar, content and root layouts, and the
 * status bar.  Sets up the tab IDCMP hook, creates the window (requesting the
 * IDCMP flags needed for resizing and minimap interaction), ensures at least
 * one document exists, opens the window, performs the initial layout and
 * activation, starts the minimap task, and hides the minimap by default.
 *
 * @param app The application to initialize.
 * @return 1 on success, 0 on any failure.
 */
int ui_create(EditorApp *app)
{
    long win_width, win_height, win_left, win_top;
    app->screen = LockPubScreen(NULL);
    if (app->screen == NULL) return 0;
    if (!open_editor_colors(app)) {
        ui_error(app, "TinyDev", "Could not reserve editor colors on the public screen.");
        return 0;
    }
    app->pages = NewObject(PAGE_GetClass(), NULL, PAGE_NoDispose, TRUE, TAG_END);
    if (app->pages == NULL) return 0;
    /* The tab close gadgets show a simple built-in black cross. */
    app->tab_close_image = close_image_new();
    if (app->tab_close_image == NULL) return 0;
    app->tabs = NewObject(CLICKTAB_GetClass(), NULL,
        GA_ID, GID_TABS, GA_RelVerify, TRUE,
        CLICKTAB_Labels, (ULONG)&app->tab_nodes,
        CLICKTAB_PageGroup, (ULONG)app->pages,
        CLICKTAB_PageGroupBorder, TRUE,
        CLICKTAB_AutoFit, TRUE,
        CLICKTAB_CloseImage, (ULONG)app->tab_close_image,
        CLICKTAB_ClosePlacement, PLACECLOSE_LEFT,
        /* The clicktab.gadget does NOT emit a GADGETUP when a tab close
         * gadget is used (the autodocs are wrong on this point).  The
         * close event is only delivered through the interconnection
         * (icclass) notification chain, so target the window IDCMP to
         * receive it as a WMHI_GADGETUP that tab_event() can react to. */
        ICA_TARGET, (ULONG)ICTARGET_IDCMP,
        TAG_END);
    if (app->tabs == NULL) return 0;
    /* dirtree.gadget owns the hierarchical node list and its expand/collapse
     * arrows; only the listbrowser look is configured here. */
    app->tree = NewObject(NULL, DIRTREE_CLASSNAME,
        GA_ID, GID_TREE,
        GA_RelVerify, TRUE,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_HorizontalProp, TRUE,
        LISTBROWSER_MinVisible, 4,
        LISTBROWSER_AutoWheel, TRUE,
        TAG_END);
    if (app->tree == NULL) return 0;
    if (minimap_create_gadget(app) == NULL) return 0;
    if (!toolbar_create(app)) return 0;
    app->content_layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild, (ULONG)app->tree,
        CHILD_MinWidth, 140,
        CHILD_WeightedWidth, 25,
        CHILD_WeightBar, TRUE,
        LAYOUT_AddChild, (ULONG)app->tabs,
        CHILD_WeightedWidth, 75,
        /* A WeightBar between the editor and the minimap lets the user resize
         * the minimap column the same way the folder tree is resized.  It is
         * declared statically and never toggled at runtime (layout.gadget does
         * not remove a toggled weight bar, so toggling would accumulate stray
         * bars).  minimap_set_visible() only collapses the minimap child to
         * zero width when hidden, exactly like the folder tree. */
        CHILD_WeightBar, TRUE,
        LAYOUT_AddChild, (ULONG)app->minimap,
        CHILD_MinWidth, 0,
        CHILD_MaxWidth, 0,
        CHILD_WeightedWidth, 0,
        TAG_END);
    if (app->content_layout == NULL) return 0;
    /* The minimap is a permanent child of the content layout; it is only
     * collapsed to zero width when hidden (like the folder tree), never removed
     * at runtime, so it stays attached for the whole lifetime of the layout. */
    app->minimap_attached = 1;
    app->statusbar = NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly, TRUE,
        GA_Text, (ULONG)"Documents: 0  Lines: 0",
        BUTTON_BevelStyle, BVS_DISPLAY,
        BUTTON_Justification, BCJ_LEFT,
        TAG_END);
    if (app->statusbar == NULL) return 0;
    app->layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild, (ULONG)app->toolbar,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)app->content_layout,
        CHILD_WeightedHeight, 100,
        LAYOUT_AddChild, (ULONG)app->statusbar,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (app->layout == NULL) return 0;
    memset(&app->tab_idcmp_hook, 0, sizeof(app->tab_idcmp_hook));
    app->tab_idcmp_hook.h_Entry = (ULONG (*)())HookEntry;
    app->tab_idcmp_hook.h_SubEntry = (ULONG (*)())tab_idcmp_entry;
    app->tab_idcmp_hook.h_Data = app;
    {
        /* size and position from the settings: size 0 = from the screen,
         * position -1 = centred; a window that would stick out is moved in */
        long sw = app->screen->Width, sh = app->screen->Height;
        win_width = app->settings.winwidth > 0 ? app->settings.winwidth : sw * 9 / 10;
        win_height = app->settings.winheight > 0 ? app->settings.winheight : sh * 4 / 5;
        if (win_width > sw) win_width = sw;
        if (win_height > sh) win_height = sh;
        win_left = app->settings.winleft;
        win_top = app->settings.wintop;
        if (win_left > sw - win_width) win_left = sw - win_width;
        if (win_top > sh - win_height) win_top = sh - win_height;
    }
    app->window_object = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"TinyDev", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_Activate, TRUE,
        /* The window.class only enables the IDCMP flags its gadgets need.
         * IDCMP_NEWSIZE and IDCMP_MOUSEMOVE (reported while a WeightBar is
         * dragged) wake the event loop after the minimap's size changed, so
         * minimap_poll() can have it re-rendered at the new size.
         * IDCMP_INTUITICKS wakes it regularly while the window is active,
         * so the toolbar follows the selection and undo state. */
        WA_IDCMP, IDCMP_NEWSIZE | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS,
        WA_PubScreen, (ULONG)app->screen,
        WA_Width, (ULONG)win_width, WA_Height, (ULONG)win_height,
        app->settings.winleft >= 0 ? WA_Left : TAG_IGNORE, (ULONG)win_left,
        app->settings.wintop >= 0 ? WA_Top : TAG_IGNORE, (ULONG)win_top,
        app->settings.winleft < 0 || app->settings.wintop < 0 ? WINDOW_Position : TAG_IGNORE,
            WPOS_CENTERSCREEN,
        WINDOW_NewMenu, (ULONG)menus,
        WINDOW_MenuUserData, WGUD_IGNORE,
        WINDOW_ParentGroup, (ULONG)app->layout,
        /* Enable the IDCMPUPDATE class so the clicktab close notification
         * (routed to the window IDCMP via ICA_TARGET) actually reaches us
         * through this hook. */
        WINDOW_HintInfo, (ULONG)toolbar_hints(),
        WINDOW_GadgetHelp, TRUE,
        WINDOW_IDCMPHook, (ULONG)&app->tab_idcmp_hook,
        WINDOW_IDCMPHookBits, (ULONG)IDCMP_IDCMPUPDATE,
        TAG_END);
    if (app->window_object == NULL) return 0;
    if (IsListEmpty(&app->documents) && document_new(app) == NULL) return 0;
    splash_status("Opening the window...");
    app->window = (struct Window *)DoMethod(app->window_object, WM_OPEN, NULL);
    if (app->window == NULL) return 0;
    splash_step();
    splash_close();
    ui_relayout(app);
    toolbar_window_opened(app);
    document_activate(app, app->active);
    ui_update_status(app);
    minimap_set_visible(app, 0);
    return 1;
}

/**
 * @brief Send an ARexx-style command to the active document's editor gadget.
 *
 * Issues the command (e.g. UNDO/REDO/CUT/COPY/PASTE/SELECTALL) via the
 * texteditor gadget.  Because toolbar/menu picks steal input focus, it forces
 * an immediate refresh of the editor gadget and reactivates it so the updated
 * contents and caret appear at once, then resyncs the scrollbars and requests
 * a minimap update.  Does nothing if no document is active.
 *
 * @param app The application whose active document receives the command.
 * @param command The ARexx command string to execute.
 */
static void editor_command(EditorApp *app, const char *command)
{
    if (app->active == NULL) return;
    DoMethod(app->active->editor, GM_TEXTEDITOR_ARexxCmd, NULL, (STRPTR)command);
    /* Toolbar buttons and menu picks steal the input focus from the editor
     * gadget, so a command issued through them (Undo/Redo/Cut/Copy/Paste)
     * changes the buffer without the gadget redrawing right away.  Force an
     * immediate refresh and hand the focus back to the editor so the new
     * contents and caret appear at once. */
    if (app->window != NULL) {
        RefreshGList((struct Gadget *)app->active->editor, app->window, NULL, 1);
        if (app->layout != NULL)
            ActivateLayoutGadget((struct Gadget *)app->layout, app->window,
                                 NULL, (ULONG)(100 + app->active->number));
    }
    document_sync_scrollers(app, app->active);
    minimap_request(app);
}

/**
 * @brief Save the active document, optionally forcing a Save As prompt.
 *
 * Saves directly to the existing path when save_as is false and a path is
 * known; otherwise prompts the user for a destination path.
 *
 * @param app The application whose active document is saved.
 * @param save_as Nonzero to force a Save As file requester.
 * @return Nonzero on a successful save, 0 on failure or if no document is
 *         active.
 */
static int save_active(EditorApp *app, int save_as)
{
    Document *doc = app->active;
    if (doc == NULL) return 0;
    if (!save_as && doc->path != NULL) return file_save(app, doc, doc->path);
    return file_request_save(app, doc);
}

/**
 * @brief Confirm closing every open document.
 *
 * Iterates over all documents and runs ui_confirm_close() on each; aborts as
 * soon as the user cancels one.
 *
 * @param app The application whose documents are checked.
 * @return 1 if all documents may be closed, 0 if the user cancelled.
 */
static int close_all(EditorApp *app)
{
    Document *doc, *next;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ; doc = next) {
        next = (Document *)doc->node.ln_Succ;
        if (!ui_confirm_close(app, doc)) return 0;
    }
    return 1;
}

/**
 * @brief Dispatch a menu item selection to the corresponding action.
 *
 * Handles the Project, Edit, and View menu commands: creating/opening/saving/
 * closing documents, showing the About requester, quitting (after confirming
 * all closes), editor commands, toggling the folder tree and minimap, and
 * toggling line numbers (updating every document's editor gadget and relaying
 * out).
 *
 * @param app The application acting on the command.
 * @param id The menu command identifier (MID_*).
 */
static void menu_action(EditorApp *app, ULONG id);

/**
 * @brief Show the settings window and take over its result; "Save" also
 *        writes the settings into the program icon.
 * @param app The application state.
 */
static void edit_settings(EditorApp *app)
{
    Settings n = app->settings;
    int r = prefs_dialog(app, &n);
    if (r == PREFS_CANCEL) return;
    ui_apply_settings(app, &n);
    if (r == PREFS_SAVE && !settings_save(&app->settings))
        ui_error(app, APP_NAME, "The settings could not be saved in the program icon.");
}

/**
 * @brief Take over edited settings.
 *
 * New highlighting colours take effect at once: the new pens are obtained,
 * every document's highlighter and the minimap use them, the visible editor
 * is highlighted and drawn again, then the old pens are released. If a pen
 * cannot be obtained the old colours stay. Window, splash and toolbar
 * settings take effect at the next start.
 *
 * @param app The application state.
 * @param n The new settings.
 */
void ui_apply_settings(EditorApp *app, const Settings *n)
{
    struct ColorMap *cm;
    long pens[SCOL_COUNT];
    int i, changed;
    Document *doc;

    changed = memcmp(app->settings.colours, n->colours, sizeof(n->colours)) != 0;
    app->settings = *n;
    if (!changed || !app->editor_colors_open || app->screen == NULL) return;
    cm = app->screen->ViewPort.ColorMap;
    for (i = 0; i < SCOL_COUNT; ++i) {
        unsigned long c = n->colours[i];
        pens[i] = ObtainBestPen(cm, rgb32(c, 16), rgb32(c, 8), rgb32(c, 0),
                                OBP_Precision, PRECISION_GUI, OBP_FailIfBad, FALSE, TAG_END);
        if (pens[i] < 0) {
            while (--i >= 0) ReleasePen(cm, (ULONG)pens[i]);
            return;
        }
    }
    for (i = 0; i < SCOL_COUNT; ++i) {
        long old = app->editor_pens[EDITOR_COLOR_TEXT + i];
        app->editor_pens[EDITOR_COLOR_TEXT + i] = pens[i];
        pens[i] = old;                          /* released below */
    }
    app->editor_draw_pens[TEXTPEN] = (UWORD)app->editor_pens[EDITOR_COLOR_TEXT];
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL;
         doc = (Document *)doc->node.ln_Succ) {
        syntax_set_pens(&doc->highlight,
            (UWORD)app->editor_pens[EDITOR_COLOR_TEXT],
            (UWORD)app->editor_pens[EDITOR_COLOR_KEYWORD],
            (UWORD)app->editor_pens[EDITOR_COLOR_STRING],
            (UWORD)app->editor_pens[EDITOR_COLOR_COMMENT],
            (UWORD)app->editor_pens[EDITOR_COLOR_PREPROCESSOR]);
    }
    if (app->active != NULL && app->window != NULL) {
        /* setting the hook again makes the editor highlight anew */
        SetGadgetAttrs((struct Gadget *)app->active->editor, app->window, NULL,
            GA_TEXTEDITOR_HighlighterHook, 0, TAG_END);
        SetGadgetAttrs((struct Gadget *)app->active->editor, app->window, NULL,
            GA_TEXTEDITOR_HighlighterHook, (ULONG)&app->active->highlight.hook, TAG_END);
        RefreshPageGadget((struct Gadget *)app->active->page, app->pages, app->window, NULL);
    }
    if (app->minimap != NULL) {
        if (app->window != NULL)
            SetGadgetAttrs((struct Gadget *)app->minimap, app->window, NULL,
                MINIMAP_TextPen, (ULONG)app->editor_pens[EDITOR_COLOR_TEXT],
                MINIMAP_TextChanged, TRUE, TAG_END);
        else SetAttrs(app->minimap, MINIMAP_TextPen, (ULONG)app->editor_pens[EDITOR_COLOR_TEXT],
                      MINIMAP_TextChanged, TRUE, TAG_END);
    }
    for (i = 0; i < SCOL_COUNT; ++i) ReleasePen(cm, (ULONG)pens[i]);
}

static void menu_action(EditorApp *app, ULONG id)
{
    Document *doc;
    switch (id) {
        case MID_NEW: document_new(app); break;
        case MID_OPEN: file_request_open(app); break;
        case MID_OPEN_DIRECTORY: tree_request_directory(app); break;
        case MID_SAVE: save_active(app, 0); break;
        case MID_SAVE_AS: save_active(app, 1); break;
        case MID_CLOSE: document_close(app, app->active, 1); break;
        case MID_ABOUT: ui_about(app); break;
        case MID_SETTINGS: edit_settings(app); break;
        case MID_QUIT: if (close_all(app)) app->running = 0; break;
        case MID_UNDO: editor_command(app, "UNDO"); break;
        case MID_REDO: editor_command(app, "REDO"); break;
        case MID_CUT: editor_command(app, "CUT"); break;
        case MID_COPY: editor_command(app, "COPY"); break;
        case MID_PASTE: editor_command(app, "PASTE"); break;
        case MID_SELECT_ALL: editor_command(app, "SELECTALL"); break;
        case MID_FOLDER_TREE: tree_set_visible(app, !app->tree_visible); break;
        case MID_MINIMAP:
            minimap_set_visible(app, !app->minimap_visible);
            if (app->minimap_visible) minimap_request(app);
            break;
        case MID_LINE_NUMBERS:
            app->line_numbers = !app->line_numbers;
            for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
                 doc = (Document *)doc->node.ln_Succ) {
                SetAttrs(doc->editor, GA_TEXTEDITOR_ShowLineNumbers,
                         (ULONG)app->line_numbers, TAG_END);
            }
            ui_relayout(app);
            if (app->window != NULL && app->active != NULL)
                RefreshPageGadget((struct Gadget *)app->active->page,
                                  app->pages, app->window, NULL);
            break;
    }
}


/**
 * @brief Close the document whose tab close gadget was used.
 *
 * The closed node is normally captured by tab_idcmp_entry() into
 * app->pending_close; if none is pending it falls back to querying
 * CLICKTAB_NodeClosed.  It resolves the associated Document from the node's
 * user data, resets the gadget's close-node (to avoid closing the same
 * document twice and dereferencing a freed node), and closes the document.
 * Called on every event-loop wake-up and before a tab switch so gadget
 * disposal happens outside raw input handling.
 *
 * @param app The application whose pending tab close is processed.
 */
static void tab_check_closed(EditorApp *app)
{
    ULONG value = 0; struct Node *node; Document *doc = NULL;
    if (app->tabs == NULL) return;
    /* The IDCMP hook records a closed node from the clicktab's IDCMPUPDATE
     * notification; fall back to querying the attribute directly in case a
     * value was set without a delivered notification. */
    node = app->pending_close;
    app->pending_close = NULL;
    if (node == NULL) {
        GetAttr(CLICKTAB_NodeClosed, app->tabs, &value);
        node = (struct Node *)value;
    }
    if (node == NULL) return;
    GetClickTabNodeAttrs(node, TNA_UserData, (ULONG)&doc, TAG_END);
    /* Clear the close-node right away: the gadget keeps returning the last
     * closed node until it is reset, which would otherwise make us close the
     * same document again and dereference a node that document_close() has
     * already freed. */
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->tabs, app->window, NULL,
            CLICKTAB_NodeClosed, (ULONG)NULL, TAG_END);
    else SetAttrs(app->tabs, CLICKTAB_NodeClosed, (ULONG)NULL, TAG_END);
    if (doc != NULL) document_close(app, doc, 1);
}

/**
 * @brief Handle a click on the tab bar (switch and/or close).
 *
 * First processes any pending tab close so a tab about to be removed is never
 * switched to, then reads the current tab node and, if it maps to a document,
 * makes it active: refreshes the title, redraws its page, resyncs the
 * scrollbars, and requests a minimap re-render from the new contents.
 *
 * @param app The application whose active tab may change.
 */
static void tab_event(EditorApp *app)
{
    ULONG value = 0; struct Node *node = NULL; Document *doc;
    /* A close gadget click may arrive together with a tab switch; handle a
     * pending close first so we never switch to a tab that is about to go. */
    tab_check_closed(app);
    GetAttr(CLICKTAB_CurrentNode, app->tabs, &value);
    node = (struct Node *)value;
    if (node != NULL) {
        doc = NULL;
        GetClickTabNodeAttrs(node, TNA_UserData, (ULONG)&doc, TAG_END);
        if (doc != NULL) {
            app->active = doc;
            ui_refresh(app);
            if (app->window != NULL)
                RefreshPageGadget((struct Gadget *)doc->page, app->pages,
                                  app->window, NULL);
            document_sync_scrollers(app, doc);
            /* Switching tabs changes the active document, so the minimap must
             * be re-rendered from the newly selected document's contents. */
            minimap_request(app);
        }
    }
}

/**
 * @brief Run the main event loop until the application should quit.
 *
 * Waits on the window, live-scroll, and minimap signals plus CTRL-C, then
 * services minimap replies, live scrolling, and break requests.  Drains
 * WM_HANDLEINPUT, dispatching close, tab/tree/scrollbar/toolbar/menu gadget
 * events and minimap mouse/resize handling.  After each input batch it mirrors
 * keyboard/mouse-driven editor scrolling onto the scrollbars, marks the active
 * document dirty when it changed, processes pending tab closes, and refreshes
 * the status line and minimap.
 *
 * @param app The application to run.
 * @return Always 1 when the loop exits.
 */
int ui_run(EditorApp *app)
{
    ULONG signals = 0, result, mask;
    /* WM_HANDLEINPUT stores the gadget/IDCMP code through a WORD* (wmh_Code),
     * so this must be a 16-bit WORD.  Using a ULONG here made the returned
     * value land in the high word on the big-endian 68000, so (UWORD)code was
     * always zero. */
    WORD code = 0;
    ULONG scroll_mask = 1UL << (ULONG)app->scroll_signal;
    ULONG minimap_mask = minimap_signal_mask(app);
    GetAttr(WINDOW_SigMask, app->window_object, &mask);
    app->running = 1;
    while (app->running) {
        /* A finished minimap render job only needs to wake the loop: the
         * minimap_poll() at its end shows the new picture. */
        signals = Wait(mask | scroll_mask | minimap_mask | SIGBREAKF_CTRL_C);
        if ((signals & scroll_mask) != 0) document_scroll_live(app);
        if (signals & SIGBREAKF_CTRL_C) { if (close_all(app)) break; }
        while ((result = DoMethod(app->window_object, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG kind = result & WMHI_CLASSMASK;
            if (kind == WMHI_CLOSEWINDOW) { if (close_all(app)) app->running = 0; }
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TABS) tab_event(app);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TREE) tree_handle_event(app);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_VSCROLL) document_scroll_finish(app, 0);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_HSCROLL) document_scroll_finish(app, 1);
            /* Clicks and drags in the minimap arrive as IDCMPUPDATE
             * notifications (window IDCMP hook); its GADGETUP needs nothing. */
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_MINIMAP) ;
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TOOLBAR)
                menu_action(app, (ULONG)(UWORD)code);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_OVERFLOW) {
                ULONG command = toolbar_overflow_command((ULONG)(UWORD)code);
                if (command != 0) menu_action(app, command);
            }
            else if (kind == WMHI_MENUPICK) {
                struct Menu *strip = NULL; struct MenuItem *item;
                GetAttr(WINDOW_MenuStrip, app->window_object, (ULONG *)&strip);
                item = strip != NULL ? ItemAddress(strip, (UWORD)(result & WMHI_MENUMASK)) : NULL;
                if (item != NULL) menu_action(app, (ULONG)GTMENUITEM_USERDATA(item));
            }
        }
        /* The texteditor gadget does not notify listeners when the user
         * scrolls it from the keyboard or mouse, so re-read its scroll
         * position after every batch of input events (the window wakes us
         * for each key/mouse event) and mirror it onto the scrollbars.  The
         * sync has to happen here instead of inside the WM_HANDLEINPUT loop
         * because that loop body is skipped once WM_HANDLEINPUT returns
         * WMHI_LASTMSG. */
        if (app->active != NULL) {
            ULONG changed = 0;
            GetAttr(GA_TEXTEDITOR_HasChanged, app->active->editor, &changed);
            if (changed) { document_set_dirty(app, app->active, 1); minimap_request(app); }
        }
        /* The close gadgets on the tabs do not generate a dedicated event, so
         * check for a pending tab close on every wake-up as well. */
        tab_check_closed(app);
        minimap_handle_scroll(app);
        document_sync_scrollers(app, app->active);
        ui_update_status(app);
        toolbar_update(app);
        minimap_poll(app);
    }
    return 1;
}

/**
 * @brief Tear down the window, gadgets, and screen resources.
 *
 * Disconnects the minimap from the editors (it waits for a running render
 * job, whose colour hook reads the document) and frees all documents; the
 * minimap.gadget stops its render process itself when it is disposed with the
 * window.  Disposes
 * the window/layout/gadget hierarchy following the established ownership and
 * cleanup ordering, disposes a detached minimap gadget if the column was
 * hidden, frees toolbar and tree images and the tab close image, releases the
 * editor colors, unlocks the public screen, and clears all cached pointers.
 *
 * @param app The application to tear down.
 */
void ui_destroy(EditorApp *app)
{
    minimap_forget_document(app, NULL);
    document_free_all(app);
    if (app->window_object != NULL) {
        if (app->window != NULL) DoMethod(app->window_object, WM_CLOSE, NULL);
        DisposeObject(app->window_object);
    }
    else if (app->layout != NULL) DisposeObject(app->layout);
    else {
        if (app->statusbar != NULL) DisposeObject(app->statusbar);
        if (app->toolbar != NULL) DisposeObject(app->toolbar);
        if (app->content_layout != NULL) DisposeObject(app->content_layout);
        else {
            if (app->minimap != NULL) { DisposeObject(app->minimap); app->minimap = NULL; }
            if (app->tree != NULL) DisposeObject(app->tree);
            if (app->tabs != NULL) DisposeObject(app->tabs);
            else if (app->pages != NULL) DisposeObject(app->pages);
        }
    }
    /* When the minimap column is hidden its gadget has been removed from
     * the content layout, so the layout/window disposal above did not free it.
     * Dispose the still-detached gadget here to avoid leaking it. */
    if (!app->minimap_attached && app->minimap != NULL) {
        DisposeObject(app->minimap);
        app->minimap = NULL;
    }
    toolbar_dispose(app);
    if (app->tab_close_image != NULL) { DisposeObject(app->tab_close_image); app->tab_close_image = NULL; }
    close_image_free_class();
    close_editor_colors(app);
    if (app->screen != NULL) UnlockPubScreen(NULL, app->screen);
    app->window_object = app->layout = app->toolbar = app->content_layout = NULL;
    app->statusbar = NULL;
    app->minimap = NULL;
    app->tree = app->tabs = app->pages = NULL;
    app->window = NULL; app->screen = NULL;
}
