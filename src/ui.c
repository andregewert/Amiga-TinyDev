#include "editor.h"

#include <gadgets/clicktab.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/texteditor.h>
#include <images/bitmap.h>
#include <images/glyph.h>
#include <libraries/gadtools.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/icclass.h>
#include <proto/clicktab.h>
#include <proto/button.h>
#include <proto/bitmap.h>
#include <proto/glyph.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/texteditor.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <clib/alib_protos.h>
#include <reaction/reaction.h>

#include <stdio.h>
#include <string.h>

#define UD(id) ((APTR)(ULONG)(id))
#define TOOLBAR_ICON_SPACING 4

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
        struct Node *node =
            (struct Node *)GetTagData(CLICKTAB_NodeClosed, 0, tags);
        if (node != NULL) app->pending_close = node;
    }
    return (ULONG)msg;
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
    static const ULONG rgb[EDITOR_COLOR_COUNT][3] = {
        {0xffffffffUL, 0xffffffffUL, 0xffffffffUL},
        {0x00000000UL, 0x00000000UL, 0x00000000UL},
        {0x10101010UL, 0x30303030UL, 0xa0a0a0a0UL},
        {0x10101010UL, 0x70707070UL, 0x10101010UL},
        {0x50505050UL, 0x60606060UL, 0x50505050UL},
        {0x80808080UL, 0x20202020UL, 0x70707070UL}
    };
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
        app->editor_pens[i] = ObtainBestPenA(color_map, rgb[i][0], rgb[i][1],
                                             rgb[i][2], i == EDITOR_COLOR_BACKGROUND
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

typedef struct ToolbarSpec {
    ULONG id;
    const char *image;
    const char *label;
} ToolbarSpec;

static const ToolbarSpec toolbar_specs[] = {
    {GID_NEW,   "TBImages:New",   "New"},
    {GID_OPEN,  "TBImages:Open",  "Open"},
    {GID_SAVE,  "TBImages:Save",  "Save"},
    {GID_UNDO,  "TBImages:Undo",  "Undo"},
    {GID_REDO,  "TBImages:Redo",  "Redo"},
    {GID_CUT,   "TBImages:Cut",   "Cut"},
    {GID_COPY,  "TBImages:Copy",  "Copy"},
    {GID_PASTE, "TBImages:Paste", "Paste"}
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
    EasyRequestArgs(app != NULL ? app->window : NULL, &es, NULL, NULL);
}

/**
 * @brief Update the window title to reflect the active document.
 *
 * Sets the title to "AmiEditor" when no document is active, otherwise to
 * "AmiEditor - <title>" with a trailing '*' when the active document is dirty.
 *
 * @param app The application whose window title is refreshed.
 */
void ui_refresh(EditorApp *app)
{
    static char window_title[EDITOR_TITLE_MAX + 24];
    if (app->active == NULL) strcpy(window_title, "AmiEditor");
    else snprintf(window_title, sizeof(window_title), "AmiEditor - %s%s",
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
    static char status_text[64];
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
 * @brief Create a single toolbar button object from a toolbar spec.
 *
 * Attempts to load the spec's image via bitmap.image (masked/transparent) and
 * build an image button; if the image is unavailable it falls back to a
 * text-labeled button.  If the image loads but the button fails to build, the
 * image is disposed.  The created image (if any) is stored in
 * app->toolbar_images[index].
 *
 * @param app The application whose screen and toolbar-image array are used.
 * @param index Index into the toolbar spec/image arrays.
 * @return The created button object, or NULL on failure.
 */
static Object *toolbar_button(EditorApp *app, size_t index)
{
    const ToolbarSpec *spec = &toolbar_specs[index];
    Object *button;
    app->toolbar_images[index] = NewObject(BITMAP_GetClass(), NULL,
        BITMAP_SourceFile, (ULONG)spec->image,
        BITMAP_Screen, (ULONG)app->screen,
        BITMAP_Masking, TRUE,
        BITMAP_Transparent, TRUE,
        TAG_END);
    if (app->toolbar_images[index] != NULL)
        button = NewObject(BUTTON_GetClass(), NULL,
            GA_ID, spec->id, GA_RelVerify, TRUE,
            BUTTON_RenderImage, (ULONG)app->toolbar_images[index],
            TAG_END);
    else
        button = NewObject(BUTTON_GetClass(), NULL,
            GA_ID, spec->id, GA_RelVerify, TRUE,
            GA_Text, (ULONG)spec->label,
            TAG_END);
    if (button == NULL && app->toolbar_images[index] != NULL) {
        DisposeObject(app->toolbar_images[index]);
        app->toolbar_images[index] = NULL;
    }
    return button;
}

/**
 * @brief Build the horizontal toolbar layout and its buttons.
 *
 * Creates a shrink-wrapped horizontal layout and adds one button per toolbar
 * spec, each with zero weighted width/height.  Stores the layout in
 * app->toolbar.
 *
 * @param app The application whose toolbar is created.
 * @return 1 on success, 0 if the layout or any button could not be created.
 */
static int create_toolbar(EditorApp *app)
{
    size_t i;
    app->toolbar = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_ShrinkWrap, TRUE,
        LAYOUT_HorizAlignment, LAYOUT_ALIGN_LEFT,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_InnerSpacing, TOOLBAR_ICON_SPACING,
        TAG_END);
    if (app->toolbar == NULL) return 0;
    for (i = 0; i < sizeof(toolbar_specs) / sizeof(toolbar_specs[0]); ++i) {
        Object *button = toolbar_button(app, i);
        if (button == NULL) return 0;
        SetAttrs(app->toolbar,
            LAYOUT_AddChild, (ULONG)button,
            CHILD_WeightedWidth, 0,
            CHILD_WeightedHeight, 0,
            TAG_END);
    }
    return 1;
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
    app->screen = LockPubScreen(NULL);
    if (app->screen == NULL) return 0;
    if (!open_editor_colors(app)) {
        ui_error(app, "AmiEditor", "Could not reserve editor colors on the public screen.");
        return 0;
    }
    app->pages = NewObject(PAGE_GetClass(), NULL, PAGE_NoDispose, TRUE, TAG_END);
    if (app->pages == NULL) return 0;
    /* The tab close gadgets use the external TBImages:list_remove image,
     * loaded through the bitmap.image class with a transparent (masked)
     * background so it blends into the tab. */
    app->tab_close_image = NewObject(BITMAP_GetClass(), NULL,
        BITMAP_SourceFile, (ULONG)"TBImages:list_remove",
        BITMAP_Screen, (ULONG)app->screen,
        BITMAP_Masking, TRUE,
        BITMAP_Transparent, TRUE,
        TAG_END);
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
    app->tree_show_image = NewObject(GLYPH_GetClass(), NULL,
        GLYPH_Glyph, GLYPH_RIGHTARROW, TAG_END);
    app->tree_hide_image = NewObject(GLYPH_GetClass(), NULL,
        GLYPH_Glyph, GLYPH_DOWNARROW, TAG_END);
    if (app->tree_show_image == NULL || app->tree_hide_image == NULL) return 0;
    app->tree = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, GID_TREE,
        GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&app->tree_nodes,
        LISTBROWSER_Hierarchical, TRUE,
        LISTBROWSER_ShowImage, (ULONG)app->tree_show_image,
        LISTBROWSER_HideImage, (ULONG)app->tree_hide_image,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_HorizontalProp, TRUE,
        LISTBROWSER_MinVisible, 4,
        LISTBROWSER_AutoWheel, TRUE,
        TAG_END);
    if (app->tree == NULL) return 0;
    if (minimap_create_gadget(app) == NULL) return 0;
    if (!create_toolbar(app)) return 0;
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
    app->window_object = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"AmiEditor", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_Activate, TRUE,
        /* The window.class only enables the IDCMP flags its gadgets need, so
         * request IDCMP_NEWSIZE explicitly; without it WMHI_NEWSIZE is never
         * delivered and the minimap is not re-rendered after a resize (the
         * space.gadget is only cleared to its background pen).  IDCMP_MOUSEBUTTONS
         * is requested so a press/release over the passive minimap space.gadget
         * (which does not consume the button) is delivered to the window, and
         * IDCMP_MOUSEMOVE so the drag can be tracked once ReportMouse() is
         * enabled for the duration of the drag. */
        WA_IDCMP, IDCMP_NEWSIZE | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE,
        WA_PubScreen, (ULONG)app->screen,
        WA_Width, 640, WA_Height, 400,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_NewMenu, (ULONG)menus,
        WINDOW_MenuUserData, WGUD_IGNORE,
        WINDOW_ParentGroup, (ULONG)app->layout,
        /* Enable the IDCMPUPDATE class so the clicktab close notification
         * (routed to the window IDCMP via ICA_TARGET) actually reaches us
         * through this hook. */
        WINDOW_IDCMPHook, (ULONG)&app->tab_idcmp_hook,
        WINDOW_IDCMPHookBits, (ULONG)IDCMP_IDCMPUPDATE,
        TAG_END);
    if (app->window_object == NULL) return 0;
    if (IsListEmpty(&app->documents) && document_new(app) == NULL) return 0;
    app->window = (struct Window *)DoMethod(app->window_object, WM_OPEN, NULL);
    if (app->window == NULL) return 0;
    ui_relayout(app);
    document_activate(app, app->active);
    ui_update_status(app);
    minimap_start(app);
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
 * closing documents, quitting (after confirming all closes), editor commands,
 * toggling the folder tree and minimap, and toggling line numbers (updating
 * every document's editor gadget and relaying out).
 *
 * @param app The application acting on the command.
 * @param id The menu command identifier (MID_*).
 */
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
 * @brief Map a toolbar button id to the equivalent menu action.
 *
 * @param app The application acting on the command.
 * @param id The toolbar gadget identifier (GID_*).
 */
static void toolbar_action(EditorApp *app, ULONG id)
{
    switch (id) {
        case GID_NEW: menu_action(app, MID_NEW); break;
        case GID_OPEN: menu_action(app, MID_OPEN); break;
        case GID_SAVE: menu_action(app, MID_SAVE); break;
        case GID_UNDO: menu_action(app, MID_UNDO); break;
        case GID_REDO: menu_action(app, MID_REDO); break;
        case GID_CUT: menu_action(app, MID_CUT); break;
        case GID_COPY: menu_action(app, MID_COPY); break;
        case GID_PASTE: menu_action(app, MID_PASTE); break;
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
     * always zero and the minimap never saw a real SELECTDOWN/SELECTUP. */
    WORD code = 0;
    ULONG scroll_mask = 1UL << (ULONG)app->scroll_signal;
    ULONG minimap_mask = minimap_signal_mask(app);
    GetAttr(WINDOW_SigMask, app->window_object, &mask);
    app->running = 1;
    while (app->running) {
        signals = Wait(mask | scroll_mask | minimap_mask | SIGBREAKF_CTRL_C);
        if (minimap_mask != 0 && (signals & minimap_mask) != 0)
            minimap_handle_reply(app);
        if ((signals & scroll_mask) != 0) document_scroll_live(app);
        if (signals & SIGBREAKF_CTRL_C) { if (close_all(app)) break; }
        while ((result = DoMethod(app->window_object, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG kind = result & WMHI_CLASSMASK;
            if (kind == WMHI_CLOSEWINDOW) { if (close_all(app)) app->running = 0; }
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TABS) tab_event(app);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TREE) tree_handle_event(app);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_VSCROLL) document_scroll_finish(app, 0);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_HSCROLL) document_scroll_finish(app, 1);
            else if (kind == WMHI_MOUSEBUTTONS) minimap_handle_buttons(app, (UWORD)code);
            else if (kind == WMHI_MOUSEMOVE) {
                minimap_handle_mouse(app);
                /* A WeightBar drag repeatedly relayouts the content group and
                 * clears the minimap's space.gadget to grey without ever
                 * changing the minimap box geometry (dragging the left
                 * WeightBar shifts only the tree/editor split, leaving the
                 * right-hand minimap column at the same Left/Top/Width/Height).
                 * Geometry watching in minimap_poll() therefore cannot notice
                 * it.  The layout does, however, request mouse reports while a
                 * bar is being dragged, so those moves reach us here; force a
                 * re-render on every one so the freshly cleared area is redrawn
                 * (coalesced to a single outstanding job). */
                if (app->minimap_visible) minimap_request(app);
            }
            else if (kind == WMHI_NEWSIZE) minimap_request(app);
            else if (kind == WMHI_GADGETUP) toolbar_action(app, result & WMHI_GADGETMASK);
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
        document_sync_scrollers(app, app->active);
        ui_update_status(app);
        minimap_poll(app);
    }
    return 1;
}

/**
 * @brief Tear down the window, gadgets, and screen resources.
 *
 * Stops and joins the minimap task before disposing the window (and its
 * space.gadget), clears the folder tree, and frees all documents.  Disposes
 * the window/layout/gadget hierarchy following the established ownership and
 * cleanup ordering, disposes a detached minimap gadget if the column was
 * hidden, frees toolbar and tree images and the tab close image, releases the
 * editor colors, unlocks the public screen, and clears all cached pointers.
 *
 * @param app The application to tear down.
 */
void ui_destroy(EditorApp *app)
{
    size_t i;
    minimap_stop(app);
    tree_clear(app);
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
    /* When the minimap column is hidden its space.gadget has been removed from
     * the content layout, so the layout/window disposal above did not free it.
     * Dispose the still-detached gadget here to avoid leaking it. */
    if (!app->minimap_attached && app->minimap != NULL) {
        DisposeObject(app->minimap);
        app->minimap = NULL;
    }
    for (i = 0; i < sizeof(app->toolbar_images) / sizeof(app->toolbar_images[0]); ++i)
        if (app->toolbar_images[i] != NULL) DisposeObject(app->toolbar_images[i]);
    if (app->tree_show_image != NULL) DisposeObject(app->tree_show_image);
    if (app->tree_hide_image != NULL) DisposeObject(app->tree_hide_image);
    if (app->tab_close_image != NULL) { DisposeObject(app->tab_close_image); app->tab_close_image = NULL; }
    close_editor_colors(app);
    if (app->screen != NULL) UnlockPubScreen(NULL, app->screen);
    app->window_object = app->layout = app->toolbar = app->content_layout = NULL;
    app->statusbar = NULL;
    app->minimap = NULL;
    app->tree = app->tabs = app->pages = NULL;
    app->tree_show_image = app->tree_hide_image = NULL;
    app->window = NULL; app->screen = NULL;
}
