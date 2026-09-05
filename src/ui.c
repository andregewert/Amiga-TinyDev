#include "editor.h"

#include <gadgets/clicktab.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/texteditor.h>
#include <images/bitmap.h>
#include <images/glyph.h>
#include <libraries/gadtools.h>
#include <proto/clicktab.h>
#include <proto/button.h>
#include <proto/bitmap.h>
#include <proto/glyph.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/texteditor.h>
#include <proto/window.h>
#include <clib/alib_protos.h>
#include <reaction/reaction.h>

#include <stdio.h>
#include <string.h>

#define UD(id) ((APTR)(ULONG)(id))
#define TOOLBAR_ICON_SPACING 4
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
    {NM_TITLE, "Settings", NULL, 0, 0, NULL},
    {NM_ITEM, "Font...", NULL, 0, 0, UD(MID_FONT)},
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
    app->pages = NewObject(PAGE_GetClass(), NULL, PAGE_NoDispose, TRUE, TAG_END);
    if (app->pages == NULL) return 0;
    app->tab_close_image = NewObject(BITMAP_GetClass(), NULL,
        BITMAP_SourceFile, (ULONG)"TBImages:Close",
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
        TAG_END);
    if (app->tabs == NULL) return 0;
    app->tree_show_image = NewObject(GLYPH_GetClass(), NULL,
        GLYPH_Glyph, GLYPH_DOWNARROW, TAG_END);
    app->tree_hide_image = NewObject(GLYPH_GetClass(), NULL,
        GLYPH_Glyph, GLYPH_RIGHTARROW, TAG_END);
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
    app->layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild, (ULONG)app->toolbar,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)app->content_layout,
        CHILD_WeightedHeight, 100,
        TAG_END);
    if (app->layout == NULL) return 0;
    app->window_object = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"AmiEditor", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_Activate, TRUE,
        WA_PubScreen, (ULONG)app->screen,
        WA_Width, 640, WA_Height, 400,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_NewMenu, (ULONG)menus,
        WINDOW_MenuUserData, WGUD_IGNORE,
        WINDOW_ParentGroup, (ULONG)app->layout,
        TAG_END);
    if (app->window_object == NULL) return 0;
    if (IsListEmpty(&app->documents) && document_new(app) == NULL) return 0;
    app->window = (struct Window *)DoMethod(app->window_object, WM_OPEN, NULL);
    if (app->window == NULL) return 0;
    ui_relayout(app);
    document_activate(app, app->active);
    return 1;
}

static void editor_command(EditorApp *app, const char *command)
{
    if (app->active != NULL) DoMethod(app->active->editor, GM_TEXTEDITOR_ARexxCmd, NULL, (STRPTR)command);
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
        case MID_FONT: font_request(app); break;
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

static void tab_event(EditorApp *app)
{
    ULONG value = 0; struct Node *node = NULL; Document *doc;
    GetAttr(CLICKTAB_NodeClosed, app->tabs, &value);
    node = (struct Node *)value;
    if (node != NULL) {
        doc = NULL;
        GetClickTabNodeAttrs(node, TNA_UserData, (ULONG)&doc, TAG_END);
        if (doc != NULL) document_close(app, doc, 1);
        return;
    }
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
            if (app->active != NULL) { ULONG changed = 0; GetAttr(GA_TEXTEDITOR_HasChanged, app->active->editor, &changed); if (changed) document_set_dirty(app, app->active, 1); }
            document_sync_scrollers(app, app->active);
        }
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
    if (app->screen != NULL) UnlockPubScreen(NULL, app->screen);
    app->window_object = app->layout = app->toolbar = app->content_layout = NULL;
    app->tree = app->tabs = app->pages = NULL;
    app->tree_show_image = app->tree_hide_image = NULL;
    app->tab_close_image = NULL;
    app->window = NULL; app->screen = NULL;
}
