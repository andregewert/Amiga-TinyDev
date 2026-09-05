#include "editor.h"

#include <gadgets/clicktab.h>
#include <gadgets/layout.h>
#include <gadgets/texteditor.h>
#include <libraries/gadtools.h>
#include <proto/clicktab.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/texteditor.h>
#include <proto/window.h>
#include <clib/alib_protos.h>
#include <reaction/reaction.h>

#include <stdio.h>
#include <string.h>

#define UD(id) ((APTR)(ULONG)(id))
static struct NewMenu menus[] = {
    {NM_TITLE, "Project", NULL, 0, 0, NULL},
    {NM_ITEM, "New", "N", 0, 0, UD(MID_NEW)},
    {NM_ITEM, "Open...", "O", 0, 0, UD(MID_OPEN)},
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
    {NM_ITEM, "Line Numbers", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, UD(MID_LINE_NUMBERS)},
    {NM_TITLE, "Settings", NULL, 0, 0, NULL},
    {NM_ITEM, "Font...", NULL, 0, 0, UD(MID_FONT)},
    {NM_END, NULL, NULL, 0, 0, NULL}
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

int ui_create(EditorApp *app)
{
    app->pages = NewObject(PAGE_GetClass(), NULL, PAGE_NoDispose, TRUE, TAG_END);
    if (app->pages == NULL) return 0;
    app->tabs = NewObject(CLICKTAB_GetClass(), NULL,
        GA_ID, GID_TABS, GA_RelVerify, TRUE,
        CLICKTAB_Labels, (ULONG)&app->tab_nodes,
        CLICKTAB_PageGroup, (ULONG)app->pages,
        CLICKTAB_PageGroupBorder, TRUE,
        CLICKTAB_AutoFit, TRUE,
        TAG_END);
    if (app->tabs == NULL) { DisposeObject(app->pages); app->pages = NULL; return 0; }
    app->layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild, (ULONG)app->tabs,
        TAG_END);
    if (app->layout == NULL) { DisposeObject(app->tabs); app->tabs = app->pages = NULL; return 0; }
    app->window_object = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"AmiEditor", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_Activate, TRUE,
        WA_Width, 640, WA_Height, 400,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_NewMenu, (ULONG)menus,
        WINDOW_MenuUserData, WGUD_IGNORE,
        WINDOW_ParentGroup, (ULONG)app->layout,
        TAG_END);
    if (app->window_object == NULL) { DisposeObject(app->layout); app->layout = app->tabs = app->pages = NULL; return 0; }
    app->window = (struct Window *)DoMethod(app->window_object, WM_OPEN, NULL);
    if (app->window == NULL) { DisposeObject(app->window_object); app->window_object = app->layout = app->tabs = app->pages = NULL; return 0; }
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
        case MID_LINE_NUMBERS:
            app->line_numbers = !app->line_numbers;
            for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ; doc = (Document *)doc->node.ln_Succ)
                SetAttrs(doc->editor, GA_TEXTEDITOR_ShowLineNumbers, (ULONG)app->line_numbers, TAG_END);
            break;
        case MID_FONT: font_request(app); break;
    }
}

static void tab_event(EditorApp *app)
{
    ULONG value = 0; struct Node *node = NULL; Document *doc;
    GetAttr(CLICKTAB_NodeClosed, app->tabs, &value);
    if (value != 0) { node = (struct Node *)value; SetAttrs(app->tabs, CLICKTAB_NodeClosed, 0, TAG_END); GetClickTabNodeAttrs(node, TNA_UserData, (ULONG)&doc, TAG_END); document_close(app, doc, 1); return; }
    GetAttr(CLICKTAB_CurrentNode, app->tabs, &value);
    node = (struct Node *)value;
    if (node != NULL) { doc = NULL; GetClickTabNodeAttrs(node, TNA_UserData, (ULONG)&doc, TAG_END); if (doc != NULL) document_activate(app, doc); }
}

int ui_run(EditorApp *app)
{
    ULONG signals = 0, result, code, mask;
    GetAttr(WINDOW_SigMask, app->window_object, &mask);
    app->running = 1;
    while (app->running) {
        signals = Wait(mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) { if (close_all(app)) break; }
        while ((result = DoMethod(app->window_object, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG kind = result & WMHI_CLASSMASK;
            if (kind == WMHI_CLOSEWINDOW) { if (close_all(app)) app->running = 0; }
            else if (kind == WMHI_GADGETUP && (result & WMHI_GADGETMASK) == GID_TABS) tab_event(app);
            else if (kind == WMHI_MENUPICK) {
                struct Menu *strip = NULL; struct MenuItem *item;
                GetAttr(WINDOW_MenuStrip, app->window_object, (ULONG *)&strip);
                item = strip != NULL ? ItemAddress(strip, (UWORD)(result & WMHI_MENUMASK)) : NULL;
                if (item != NULL) menu_action(app, (ULONG)GTMENUITEM_USERDATA(item));
            }
            if (app->active != NULL) { ULONG changed = 0; GetAttr(GA_TEXTEDITOR_HasChanged, app->active->editor, &changed); if (changed) document_set_dirty(app, app->active, 1); }
        }
    }
    return 1;
}

void ui_destroy(EditorApp *app)
{
    document_free_all(app);
    if (app->window_object != NULL) { DoMethod(app->window_object, WM_CLOSE, NULL); DisposeObject(app->window_object); }
    app->window_object = app->layout = app->tabs = app->pages = NULL; app->window = NULL;
}
