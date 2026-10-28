/**
 * @file diaglist.c
 * @brief The problem list, a tool window: the diagnostics TinyDev-Scout
 *        found in the active document or in the project.
 *
 * "Show" chooses the active document (its diagnostics, kept by diag.c, in
 * the order of their lines) or the project (asked from Scout through
 * scoutlink.c, by file and line). The first column shows the severity as
 * a coloured letter (E error, W warning, I information, H hint), the
 * others the message and the position. A double-click or Go to puts the
 * cursor there, opening the file if needed; the mouse wheel scrolls (its
 * rawkey codes are handled here, as the listbrowser only gets them while
 * it is active).
 *
 * The list of the document follows Document::diag_generation; that of the
 * project is asked again when scoutlink_project_generation() or the
 * diagnostics of an open document change (Scout's project list holds the
 * results of the editor's snapshots, so unsaved changes show too). The
 * window is placed and remembered like the other tool windows (toolwin.c).
 */
#include "editor.h"
#include "json.h"
#include "scout_protocol.h"

#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>
#include <libraries/gadtools.h>
#include <libraries/keymap.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/label.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct Library *ListBrowserBase;     /* shared with the settings window */

enum { DG_SCOPE = 600, DG_LIST, DG_GOTO, DG_STATUS };

/** Smallest width of the window. */
#define MIN_WIDTH 200
/** Smallest height of the window. */
#define MIN_HEIGHT 100
/** Rows the list moves per step of the mouse wheel. */
#define WHEEL_ROWS 3

/** What the list shows. */
enum { SCOPE_FILE = 0, SCOPE_PROJECT, SCOPE_COUNT };

/** Columns. */
enum { COL_SEVERITY = 0, COL_MESSAGE, COL_WHERE, COL_COUNT };

/** A problem of the list. */
typedef struct DLItem {
    char *file;         /**< its file (project, malloc()), NULL for the document */
    char *message;      /**< its text (malloc()) */
    int severity;       /**< SCOUT_SEVERITY_ value */
    long line;          /**< its line (1-based) */
    long col;           /**< its column (1-based) */
} DLItem;

/** Texts of the scopes (MSG_). */
static const LONG scope_texts[SCOPE_COUNT] = { MSG_DIAG_SCOPE_FILE, MSG_DIAG_SCOPE_PROJECT };

/** State of the window and its problems. */
static struct {
    Object *winobj, *root, *scope, *list, *status;
    struct Window *win;
    struct List nodes;              /* LBNA_UserData: index in items */
    struct ColumnInfo *columns;
    DLItem *items;
    long count;
    int scope_on;
    unsigned long doc;              /* document of the list (number), 0 for none */
    unsigned long generation;       /* its diag_generation / the project generation */
    int asked;                      /* the list is up to date with doc and generation */
    int waiting;                    /* the project's diagnostics are on their way */
    struct Hook render_hook;
    WORD row_height;
    char where_text[EDITOR_PATH_MAX];
    STRPTR scope_labels[SCOPE_COUNT + 1];
} dl;

/**
 * @brief Read an attribute.
 * @param o The object.
 * @param attr The attribute.
 * @return Its value.
 */
static ULONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return v;
}

/**
 * @brief Render hook of the first column: the severity's letter in white
 *        on its colour. Graphics only, it may run during input handling.
 * @param hook The hook.
 * @param node The row's node.
 * @param msg The draw message.
 * @return LBCB_OK, or LBCB_UNKNOWN for other methods.
 */
static ULONG render_severity(struct Hook *hook, struct Node *node, struct LBDrawMsg *msg)
{
    static const char letters[] = "EWIH";
    struct RastPort *rp = msg->lbdm_RastPort;
    struct DrawInfo *dri = msg->lbdm_DrawInfo;
    struct Rectangle *b = &msg->lbdm_Bounds;
    ULONG index = 0;
    WORD size, x, y, w;
    int sev;
    char letter;
    (void)hook;
    if (msg->lbdm_MethodID != LV_DRAW) return LBCB_UNKNOWN;
    if (dri == NULL) return LBCB_OK;
    SetAPen(rp, (UBYTE)dri->dri_Pens[msg->lbdm_State == LBR_SELECTED ? FILLPEN : BACKGROUNDPEN]);
    RectFill(rp, b->MinX, b->MinY, b->MaxX, b->MaxY);
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((long)index >= dl.count) return LBCB_OK;
    sev = dl.items[index].severity;
    letter = letters[sev >= 1 && sev <= 4 ? sev - 1 : 0];
    size = (WORD)(b->MaxY - b->MinY - 1);
    if (size > b->MaxX - b->MinX - 1) size = (WORD)(b->MaxX - b->MinX - 1);
    if (size < 4) return LBCB_OK;
    x = (WORD)(b->MinX + (b->MaxX - b->MinX + 1 - size) / 2);
    y = (WORD)(b->MinY + (b->MaxY - b->MinY + 1 - size) / 2);
    SetAPen(rp, (UBYTE)diag_pen(sev, dri->dri_Pens[FILLPEN]));
    RectFill(rp, x, y, (WORD)(x + size - 1), (WORD)(y + size - 1));
    w = (WORD)TextLength(rp, (STRPTR)&letter, 1);
    SetAPen(rp, (UBYTE)dri->dri_Pens[SHINEPEN]);
    SetDrMd(rp, JAM1);
    Move(rp, (WORD)(x + (size - w) / 2), (WORD)(y + (size - rp->TxHeight) / 2 + rp->TxBaseline));
    Text(rp, (STRPTR)&letter, 1);
    return LBCB_OK;
}

/**
 * @brief Give the list its nodes (~0 detaches them for changes).
 * @param list The list.
 */
static void set_labels(struct List *list)
{
    if (dl.list == NULL) return;
    if (dl.win != NULL)
        SetGadgetAttrs((struct Gadget *)dl.list, dl.win, NULL, LISTBROWSER_Labels, (ULONG)list, TAG_END);
    else SetAttrs(dl.list, LISTBROWSER_Labels, (ULONG)list, TAG_END);
}

/**
 * @brief Show a text in the line below the list (only when it changed).
 * @param text The text.
 */
static void set_status(const char *text)
{
    static char shown[200];
    if (dl.status == NULL || strncmp(shown, text, sizeof(shown) - 1) == 0) return;
    strncpy(shown, text, sizeof(shown) - 1);
    shown[sizeof(shown) - 1] = '\0';
    if (dl.win != NULL)
        SetGadgetAttrs((struct Gadget *)dl.status, dl.win, NULL, GA_Text, (ULONG)shown, TAG_END);
    else SetAttrs(dl.status, GA_Text, (ULONG)shown, TAG_END);
}

/** @brief Free the rows and the problems (the list is detached). */
static void free_all(void)
{
    struct Node *n;
    long i;
    if (dl.nodes.lh_Head == NULL) NewList(&dl.nodes);
    while ((n = RemHead(&dl.nodes)) != NULL) FreeListBrowserNode(n);
    for (i = 0; i < dl.count; ++i) {
        free(dl.items[i].file);
        free(dl.items[i].message);
    }
    free(dl.items);
    dl.items = NULL;
    dl.count = 0;
}

/** @brief Build the rows from the problems. */
static void rebuild(void)
{
    char text[80];
    long i;
    struct Node *n;
    set_labels((struct List *)~0);
    while ((n = RemHead(&dl.nodes)) != NULL) FreeListBrowserNode(n);
    for (i = 0; i < dl.count; ++i) {
        const DLItem *it = &dl.items[i];
        if (it->file != NULL)
            snprintf(dl.where_text, sizeof(dl.where_text), "%s:%ld", (const char *)FilePart((STRPTR)it->file),
                     it->line);
        else
            snprintf(dl.where_text, sizeof(dl.where_text), "%ld", it->line);
        n = AllocListBrowserNode(COL_COUNT, LBNA_UserData, (ULONG)i,
            LBNA_Column, COL_SEVERITY, LBNCA_RenderHook, (ULONG)&dl.render_hook,
            LBNCA_HookHeight, (ULONG)dl.row_height,
            LBNA_Column, COL_MESSAGE, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)it->message,
            LBNA_Column, COL_WHERE, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)dl.where_text,
            TAG_END);
        if (n != NULL) AddTail(&dl.nodes, n);
    }
    set_labels(&dl.nodes);
    if (dl.count == 0) {
        set_status(S(MSG_DIAG_NONE));
    } else {
        snprintf(text, sizeof(text), S(MSG_DIAG_COUNT), dl.count);
        set_status(text);
    }
}

/**
 * @brief Copy a string with malloc().
 * @param s The string.
 * @return The copy, or NULL.
 */
static char *dup_str(const char *s)
{
    char *copy = malloc(strlen(s) + 1);
    if (copy != NULL) strcpy(copy, s);
    return copy;
}

/**
 * @brief Take the diagnostics of the active document into the list.
 * @param doc The document (may be NULL).
 */
static void take_document(Document *doc)
{
    long i;
    set_labels((struct List *)~0);
    free_all();
    if (doc != NULL && doc->diag_count > 0 && (dl.items = calloc((size_t)doc->diag_count, sizeof(DLItem))) != NULL) {
        for (i = 0; i < doc->diag_count; ++i) {
            DLItem *it = &dl.items[dl.count];
            if ((it->message = dup_str(doc->diag_items[i].message)) == NULL) continue;
            it->severity = doc->diag_items[i].severity;
            it->line = doc->diag_items[i].line;
            it->col = doc->diag_items[i].col;
            ++dl.count;
        }
    }
    rebuild();
}

void diaglist_project(EditorApp *app, const char *json)
{
    JsonValue *root;
    const JsonValue *files;
    size_t f, i, total = 0;
    (void)app;
    dl.waiting = 0;
    if (dl.win == NULL || dl.scope_on != SCOPE_PROJECT) return;
    set_labels((struct List *)~0);
    free_all();
    root = json != NULL ? json_parse(json, NULL) : NULL;
    files = root != NULL ? json_obj_get_array(root, "files") : NULL;
    for (f = 0; files != NULL && f < json_array_length(files); ++f)
        total += json_array_length(json_obj_get_array(json_array_get(files, f), "diagnostics"));
    if (total > 0) dl.items = calloc(total, sizeof(DLItem));
    for (f = 0; dl.items != NULL && f < json_array_length(files); ++f) {
        const JsonValue *fv = json_array_get(files, f);
        const char *file = json_obj_get_string(fv, "file", "");
        const JsonValue *list = json_obj_get_array(fv, "diagnostics");
        for (i = 0; list != NULL && i < json_array_length(list); ++i) {
            const JsonValue *v = json_array_get(list, i);
            const char *msg = json_obj_get_string(v, "message", NULL);
            const JsonValue *range = json_obj_get_obj(v, "range");
            const JsonValue *start = range != NULL ? json_obj_get_array(range, "start") : NULL;
            DLItem *it = &dl.items[dl.count];
            if (msg == NULL) continue;
            it->message = dup_str(msg);
            it->file = dup_str(file);
            if (it->message == NULL || it->file == NULL) {
                free(it->message);
                free(it->file);
                continue;
            }
            it->severity = (int)json_obj_get_int(v, "severity", SCOUT_SEVERITY_ERROR);
            it->line = start != NULL && json_array_length(start) > 0 ? json_array_get(start, 0)->u.num_val.int_val : 1;
            it->col = start != NULL && json_array_length(start) > 1 ? json_array_get(start, 1)->u.num_val.int_val : 1;
            ++dl.count;
        }
    }
    json_value_free(root);
    if (json == NULL) {
        set_labels(&dl.nodes);
        set_status(S(MSG_DIAG_FAILED));
        dl.asked = 0;
        return;
    }
    rebuild();
}

/**
 * @brief Bring the list up to date: the active document's problems, or
 *        ask Scout for the project's.
 * @param app The application state.
 * @param force Even if nothing changed.
 */
static void refresh(EditorApp *app, int force)
{
    Document *doc = app->active;
    int why;
    if (dl.win == NULL) return;
    if (dl.scope_on == SCOPE_FILE) {
        unsigned long number = doc != NULL ? doc->number : 0;
        unsigned long gen = doc != NULL ? doc->diag_generation : 0;
        if (!force && dl.asked && number == dl.doc && gen == dl.generation) return;
        dl.doc = number;
        dl.generation = gen;
        dl.asked = 1;
        take_document(doc);
        if ((why = scoutlink_unavailable(app)) == 1) set_status(S(MSG_SYM_OFF));
        return;
    }
    if (dl.waiting) return;
    {
        unsigned long gen = scoutlink_project_generation();
        Document *d;
        for (d = (Document *)app->documents.lh_Head; d->node.ln_Succ; d = (Document *)d->node.ln_Succ)
            gen += d->diag_generation;
        if (!force && dl.asked && gen == dl.generation) return;
        dl.generation = gen;
    }
    if ((why = scoutlink_unavailable(app)) != 0) {
        set_status(S(why == 1 ? MSG_SYM_OFF : MSG_SYM_WAIT));
        dl.asked = 0;
        return;
    }
    if (!scoutlink_request_diagnostics(app, NULL)) {
        set_status(S(MSG_SYM_NOPROJECT));
        dl.asked = 0;
        return;
    }
    dl.waiting = 1;
    dl.asked = 1;
}

/**
 * @brief Open the classes the window needs (once).
 * @param app The application state (for the requester).
 * @return Non-zero if all are available.
 */
static int open_classes(EditorApp *app)
{
    if (ListBrowserBase == NULL) ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44);
    if (ChooserBase == NULL) ChooserBase = OpenLibrary((STRPTR)"gadgets/chooser.gadget", 44);
    if (ListBrowserBase != NULL && ChooserBase != NULL) return 1;
    ui_error(app, APP_NAME, S(MSG_BML_NEEDCLASSES));
    return 0;
}

int diaglist_is_open(void)
{
    return dl.win != NULL;
}

void diaglist_remember(EditorApp *app)
{
    if (dl.win != NULL) toolwin_remember(dl.win, app->diag_box);
}

void diaglist_open(EditorApp *app)
{
    WORD box[4];
    int i;
    if (dl.win != NULL) {
        WindowToFront(dl.win);
        return;
    }
    if (app->window == NULL || !open_classes(app)) return;
    if (dl.nodes.lh_Head == NULL) NewList(&dl.nodes);
    if (dl.columns == NULL &&
        (dl.columns = AllocLBColumnInfo(COL_COUNT,
                                        LBCIA_Column, COL_SEVERITY, LBCIA_Title, (ULONG)"",
                                        LBCIA_Weight, 6, LBCIA_DraggableSeparator, TRUE,
                                        LBCIA_Column, COL_MESSAGE, LBCIA_Title, (ULONG)S(MSG_DIAG_COL_MESSAGE),
                                        LBCIA_Weight, 70, LBCIA_DraggableSeparator, TRUE,
                                        LBCIA_Column, COL_WHERE, LBCIA_Title, (ULONG)S(MSG_SYM_COL_WHERE),
                                        LBCIA_Weight, 24, TAG_END)) == NULL)
        return;
    dl.render_hook.h_Entry = (ULONG (*)())HookEntry;
    dl.render_hook.h_SubEntry = (ULONG (*)())render_severity;
    dl.row_height = (WORD)((app->screen != NULL && app->screen->Font != NULL ?
                            app->screen->Font->ta_YSize : 8) + 2);
    for (i = 0; i < SCOPE_COUNT; ++i) dl.scope_labels[i] = (STRPTR)S(scope_texts[i]);
    dl.scope_labels[SCOPE_COUNT] = NULL;
    toolwin_box(app, app->diag_box, MIN_WIDTH, MIN_HEIGHT, 1, box);

    dl.scope = NewObject(CHOOSER_GetClass(), NULL, GA_ID, DG_SCOPE, GA_RelVerify, TRUE,
        CHOOSER_PopUp, TRUE, CHOOSER_LabelArray, (ULONG)dl.scope_labels,
        CHOOSER_Selected, (ULONG)dl.scope_on, TAG_END);
    dl.list = NewObject(LISTBROWSER_GetClass(), NULL, GA_ID, DG_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&dl.nodes, LISTBROWSER_ColumnInfo, (ULONG)dl.columns,
        LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_AutoWheel, TRUE, TAG_END);
    dl.status = NewObject(BUTTON_GetClass(), NULL, GA_ID, DG_STATUS, GA_ReadOnly, TRUE,
        GA_Underscore, 0, GA_Text, (ULONG)"", BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE,
        BUTTON_Justification, BCJ_LEFT, TAG_END);
    dl.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(MSG_DIAG_TITLE),
        WA_PubScreen, (ULONG)app->screen,
        WA_Left, (ULONG)box[0], WA_Top, (ULONG)box[1],
        WA_Width, (ULONG)box[2], WA_Height, (ULONG)box[3],
        WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, FALSE,
        WA_IDCMP, IDCMP_RAWKEY,
        WINDOW_ParentGroup, (ULONG)(dl.root = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_LeftSpacing, 6, LAYOUT_RightSpacing, 6,
            LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6,
            LAYOUT_InnerSpacing, 4,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)dl.scope,
                CHILD_Label, (ULONG)NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)S(MSG_SYM_SHOW), TAG_END),
                LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL, GA_ID, DG_GOTO,
                    GA_RelVerify, TRUE, GA_Text, (ULONG)S(MSG_SYM_GOTO), BUTTON_TextPadding, TRUE, TAG_END),
                CHILD_WeightedWidth, 0,
                TAG_END),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)dl.list,
            LAYOUT_AddChild, (ULONG)dl.status,
            CHILD_WeightedHeight, 0,
            TAG_END)),
        TAG_END);
    if (dl.winobj == NULL || (dl.win = (struct Window *)DoMethod(dl.winobj, WM_OPEN, NULL)) == NULL) {
        if (dl.winobj != NULL) DisposeObject(dl.winobj);
        dl.winobj = NULL;
        dl.list = dl.scope = dl.status = dl.root = NULL;
        return;
    }
    if (dl.win->LeftEdge != box[0] || dl.win->TopEdge != box[1] ||
        dl.win->Width != box[2] || dl.win->Height != box[3])
        ChangeWindowBox(dl.win, box[0], box[1], box[2], box[3]);
    dl.asked = 0;
    refresh(app, 1);
}

void diaglist_close(EditorApp *app)
{
    if (dl.winobj == NULL) return;
    diaglist_remember(app);
    set_labels((struct List *)~0);
    DoMethod(dl.winobj, WM_CLOSE, NULL);
    DisposeObject(dl.winobj);
    dl.winobj = NULL;
    dl.win = NULL;
    dl.list = dl.scope = dl.status = dl.root = NULL;
    free_all();
    dl.waiting = 0;
}

ULONG diaglist_sigmask(void)
{
    ULONG sig = 0;
    if (dl.win != NULL) GetAttr(WINDOW_SigMask, dl.winobj, &sig);
    return sig;
}

/**
 * @brief Go to the selected problem and bring the main window to the
 *        front, with the input focus.
 * @param app The application state.
 */
static void go_to_selected(EditorApp *app)
{
    struct Node *node = (struct Node *)get(dl.list, LISTBROWSER_SelectedNode);
    ULONG index = 0;
    const DLItem *it;
    Document *doc;
    if (node == NULL) return;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&index, TAG_END);
    if ((long)index >= dl.count) return;
    it = &dl.items[index];
    if (it->file != NULL) {
        document_go_to(app, it->file, it->line, it->col);
    } else {
        for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
             doc = (Document *)doc->node.ln_Succ)
            if (doc->number == dl.doc) break;
        if (doc->node.ln_Succ == NULL) return;
        if (doc != app->active) document_activate(app, doc);
        document_go_to(app, NULL, it->line, it->col);
    }
    if (app->window != NULL) {
        ActivateWindow(app->window);
        app->focus_editor = 1;          /* when the window is active */
    }
}

void diaglist_handle(EditorApp *app)
{
    ULONG result;
    WORD code;
    if (dl.win == NULL) return;
    while ((result = DoMethod(dl.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            diaglist_close(app);
            ui_set_menu_check(app, MID_DIAGLIST, 0);
            return;
        case WMHI_RAWKEY:
            if ((result & WMHI_KEYMASK) == RAWKEY_WHEEL_UP || (result & WMHI_KEYMASK) == RAWKEY_WHEEL_DOWN) {
                LONG top = (LONG)get(dl.list, LISTBROWSER_Top);
                top += (result & WMHI_KEYMASK) == RAWKEY_WHEEL_UP ? -WHEEL_ROWS : WHEEL_ROWS;
                if (top < 0) top = 0;
                SetGadgetAttrs((struct Gadget *)dl.list, dl.win, NULL, LISTBROWSER_Top, (ULONG)top, TAG_END);
            }
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case DG_SCOPE:
                dl.scope_on = (int)get(dl.scope, CHOOSER_Selected);
                set_labels((struct List *)~0);
                free_all();
                set_labels(&dl.nodes);
                dl.waiting = 0;
                refresh(app, 1);
                break;
            case DG_LIST:
                if (get(dl.list, LISTBROWSER_RelEvent) & LBRE_DOUBLECLICK) go_to_selected(app);
                break;
            case DG_GOTO:
                go_to_selected(app);
                break;
            }
            break;
        }
    }
}

void diaglist_update(EditorApp *app)
{
    refresh(app, 0);
}

void diaglist_cleanup(EditorApp *app)
{
    diaglist_close(app);
    if (dl.columns != NULL) {
        FreeLBColumnInfo(dl.columns);
        dl.columns = NULL;
    }
}
