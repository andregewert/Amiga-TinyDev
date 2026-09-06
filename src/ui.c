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
    memset(&app->editor_backfill_hook, 0, sizeof(app->editor_backfill_hook));
    app->editor_backfill_hook.h_Entry = (ULONG (*)())HookEntry;
    app->editor_backfill_hook.h_SubEntry = (ULONG (*)())editor_backfill_entry;
    app->editor_backfill_hook.h_Data = app;
    app->editor_colors_open = 1;
    return 1;
}

static void close_editor_colors(EditorApp *app)
{
    int i;
    if (!app->editor_colors_open || app->screen == NULL) return;
    if (app->screen_draw_info != NULL) {
        FreeScreenDrawInfo(app->screen, app->screen_draw_info);
        app->screen_draw_info = NULL;
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

void ui_error(EditorApp *app, const char *title, const char *message)
{
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)title, (STRPTR)message, "OK"};
    EasyRequestArgs(app != NULL ? app->window : NULL, &es, NULL, NULL);
}

void ui_refresh(EditorApp *app)
{
    static char window_title[EDITOR_TITLE_MAX + 24];
    if (app->active == NULL) strcpy(window_title, "AmiEditor");
    else snprintf(window_title, sizeof(window_title), "AmiEditor - %s%s",
                  app->active->title, app->active->dirty ? "*" : "");
    if (app->window_object != NULL) SetAttrs(app->window_object, WA_Title, (ULONG)window_title, TAG_END);
}

/* Refresh the bottom status line with the number of open documents and the
 * line count of the currently displayed document. */
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

void ui_relayout(EditorApp *app)
{
    if (app->window != NULL && app->layout != NULL)
        RethinkLayout((struct Gadget *)app->layout, app->window, NULL, TRUE);
}

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
    if (!create_toolbar(app)) return 0;
    app->content_layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild, (ULONG)app->tree,
        CHILD_MinWidth, 140,
        CHILD_WeightedWidth, 25,
        CHILD_WeightBar, TRUE,
        LAYOUT_AddChild, (ULONG)app->tabs,
        CHILD_WeightedWidth, 75,
        TAG_END);
    if (app->content_layout == NULL) return 0;
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
    return 1;
}

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
}

static int save_active(EditorApp *app, int save_as)
{
    Document *doc = app->active;
    if (doc == NULL) return 0;
    if (!save_as && doc->path != NULL) return file_save(app, doc, doc->path);
    return file_request_save(app, doc);
}

static int close_all(EditorApp *app)
{
    Document *doc, *next;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ; doc = next) {
        next = (Document *)doc->node.ln_Succ;
        if (!ui_confirm_close(app, doc)) return 0;
    }
    return 1;
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
        case MID_QUIT: if (close_all(app)) app->running = 0; break;
        case MID_UNDO: editor_command(app, "UNDO"); break;
        case MID_REDO: editor_command(app, "REDO"); break;
        case MID_CUT: editor_command(app, "CUT"); break;
        case MID_COPY: editor_command(app, "COPY"); break;
        case MID_PASTE: editor_command(app, "PASTE"); break;
        case MID_SELECT_ALL: editor_command(app, "SELECTALL"); break;
        case MID_FOLDER_TREE: tree_set_visible(app, !app->tree_visible); break;
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

/* Close the document whose tab close gadget was used.  The close node is
 * captured by tab_idcmp_entry() from the clicktab's IDCMPUPDATE notification;
 * this is called on every event-loop wake-up (and before a tab switch) to act
 * on a pending close outside of raw input handling. */
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
        }
    }
}

int ui_run(EditorApp *app)
{
    ULONG signals = 0, result, code, mask;
    ULONG scroll_mask = 1UL << (ULONG)app->scroll_signal;
    GetAttr(WINDOW_SigMask, app->window_object, &mask);
    app->running = 1;
    while (app->running) {
        signals = Wait(mask | scroll_mask | SIGBREAKF_CTRL_C);
        if ((signals & scroll_mask) != 0) {
            document_scroll(app, 0);
            document_scroll(app, 1);
        }
        if (signals & SIGBREAKF_CTRL_C) { if (close_all(app)) break; }
        while ((result = DoMethod(app->window_object, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG kind = result & WMHI_CLASSMASK;
            if (kind == WMHI_CLOSEWINDOW) { if (close_all(app)) app->running = 0; }
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TABS) tab_event(app);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TREE) tree_handle_event(app);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_VSCROLL) document_scroll(app, 0);
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_HSCROLL) document_scroll(app, 1);
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
            if (changed) document_set_dirty(app, app->active, 1);
        }
        /* The close gadgets on the tabs do not generate a dedicated event, so
         * check for a pending tab close on every wake-up as well. */
        tab_check_closed(app);
        document_sync_scrollers(app, app->active);
        ui_update_status(app);
    }
    return 1;
}

void ui_destroy(EditorApp *app)
{
    size_t i;
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
            if (app->tree != NULL) DisposeObject(app->tree);
            if (app->tabs != NULL) DisposeObject(app->tabs);
            else if (app->pages != NULL) DisposeObject(app->pages);
        }
    }
    for (i = 0; i < sizeof(app->toolbar_images) / sizeof(app->toolbar_images[0]); ++i)
        if (app->toolbar_images[i] != NULL) DisposeObject(app->toolbar_images[i]);
    if (app->tree_show_image != NULL) DisposeObject(app->tree_show_image);
    if (app->tree_hide_image != NULL) DisposeObject(app->tree_hide_image);
    if (app->tab_close_image != NULL) DisposeObject(app->tab_close_image);
    close_editor_colors(app);
    if (app->screen != NULL) UnlockPubScreen(NULL, app->screen);
    app->window_object = app->layout = app->toolbar = app->content_layout = NULL;
    app->statusbar = NULL;
    app->tree = app->tabs = app->pages = NULL;
    app->tree_show_image = app->tree_hide_image = NULL;
    app->tab_close_image = NULL;
    app->window = NULL; app->screen = NULL;
}
