/**
 * @file bmlist.c
 * @brief The bookmark list, a tool window.
 *
 * Shows the bookmarks of the active file, of all open files or of the
 * project ("Show" chooser), grouped by file: a row with a document symbol
 * and the file's name, below it a row per bookmark with a ribbon symbol,
 * the line number and the start of the line. A double-click (or Go to)
 * goes to a bookmark, opening its file if needed; Remove removes the
 * selected bookmark.
 *
 * The window stays open next to the main window (toolwin.c places it and
 * remembers its box in the settings). The list is built again whenever
 * bookmarks_generation() changes or, for the active file, another tab is
 * shown; the selection stays.
 */
#include "editor.h"

#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>
#include <graphics/rastport.h>
#include <libraries/gadtools.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

extern struct Library *ListBrowserBase;     /* shared with the settings window */

enum { BG_SCOPE = 400, BG_LIST, BG_GOTO, BG_REMOVE };

/**
 * Smallest width of the window.
 */
#define MIN_WIDTH 160
/**
 * Smallest height of the window.
 */
#define MIN_HEIGHT 100
#define ROW_PADDING 1               /**< above and below a row */
#define LEFT_PADDING 3              /**< left of a symbol */
#define INDENT 14                   /**< of the bookmark rows */
#define TEXT_GAP 5                  /**< between symbol and text */

/** The symbols, 9 x 11 pixels: the ribbon of a bookmark ('#' red, 'o'
 * dark red) and the document of a file ('#' white, 'o' black, '-' blue
 * lines); '.' is transparent. */
#define SYMBOL_WIDTH 9
/**
 * Height of the symbols.
 */
#define SYMBOL_HEIGHT 11
/**
 * The ribbon of a bookmark row.
 */
static const char *const ribbon[SYMBOL_HEIGHT] = {
    "ooooooooo", "o#######o", "o#######o", "o#######o", "o#######o", "o#######o",
    "o#######o", "o###o###o", "o##o.o##o", "o#o...o#o", "oo.....oo",
};
/**
 * The document of a file row.
 */
static const char *const document[SYMBOL_HEIGHT] = {
    "oooooo...", "o####oo..", "o####o#o.", "o####oooo", "o#######o", "o#-----#o",
    "o#######o", "o#-----#o", "o#######o", "o#-----#o", "ooooooooo",
};

/**
 * Texts of the "Show" choices (MSG_), BMSCOPE_ order.
 */
static const LONG scope_texts[BMSCOPE_COUNT] = {
    MSG_BML_SCOPE_CURRENT, MSG_BML_SCOPE_OPEN, MSG_BML_SCOPE_PROJECT
};

/** State of the bookmark list (there is one). */
static struct {
    Object *winobj, *root, *scope, *list;
    struct Window *win;
    struct List nodes;              /* LBNA_UserData: item index << 1 | file row */
    BookmarkItem *items;
    long count;
    int scope_on;
    ULONG generation;               /* bookmarks_generation() of the list */
    Document *active;               /* active document of the list */
    struct Hook render_hook;
    WORD row_height;
    struct ColorMap *cm;
    LONG red, dark;                 /* pens of the ribbon, -1 if none */
    STRPTR scope_labels[BMSCOPE_COUNT + 1];
} bl = { .red = -1, .dark = -1 };

/**
 * @brief Draw a symbol.
 * @param rp The RastPort.
 * @param x Left edge.
 * @param y Top edge.
 * @param rows The symbol's rows.
 * @param pens Pens for '#', 'o' and '-'.
 */
static void draw_symbol(struct RastPort *rp, WORD x, WORD y, const char *const *rows,
                        const UWORD pens[3])
{
    WORD row, col;
    for (row = 0; row < SYMBOL_HEIGHT; ++row)
        for (col = 0; col < SYMBOL_WIDTH; ++col) {
            char c = rows[row][col];
            if (c == '.') continue;
            SetAPen(rp, (UBYTE)pens[c == '#' ? 0 : c == 'o' ? 1 : 2]);
            WritePixel(rp, (WORD)(x + col), (WORD)(y + row));
        }
}

/**
 * @brief Render hook of the rows: background, symbol and text (cut to the
 *        row's width). Graphics only, it may run during input handling.
 * @param hook The hook.
 * @param node The row's node.
 * @param msg The draw message.
 * @return LBCB_OK, or LBCB_UNKNOWN for other methods.
 */
static ULONG render_row(struct Hook *hook, struct Node *node, struct LBDrawMsg *msg)
{
    struct RastPort *rp = msg->lbdm_RastPort;
    struct DrawInfo *dri = msg->lbdm_DrawInfo;
    struct Rectangle *b = &msg->lbdm_Bounds;
    int selected = msg->lbdm_State == LBR_SELECTED;
    ULONG data = 0, length, fit;
    const BookmarkItem *it;
    char text[BOOKMARK_TEXT_MAX + 16];
    const char *t;
    WORD x, height = (WORD)(b->MaxY - b->MinY + 1), top;
    UWORD pens[3];
    struct TextExtent te;
    int file_row;

    (void)hook;
    if (msg->lbdm_MethodID != LV_DRAW) return LBCB_UNKNOWN;
    if (dri == NULL) return LBCB_OK;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&data, TAG_END);
    if ((long)(data >> 1) >= bl.count) return LBCB_OK;
    it = &bl.items[data >> 1];
    file_row = (int)(data & 1);
    SetAPen(rp, (UBYTE)dri->dri_Pens[selected ? FILLPEN : BACKGROUNDPEN]);
    RectFill(rp, b->MinX, b->MinY, b->MaxX, b->MaxY);

    x = (WORD)(b->MinX + LEFT_PADDING + (file_row ? 0 : INDENT));
    top = (WORD)(b->MinY + (height - SYMBOL_HEIGHT) / 2);
    if (file_row) {
        pens[0] = dri->dri_Pens[SHINEPEN];
        pens[1] = dri->dri_Pens[TEXTPEN];
        pens[2] = dri->dri_Pens[FILLPEN];
        draw_symbol(rp, x, top, document, pens);
        t = it->name;
    } else {
        pens[0] = (UWORD)(bl.red >= 0 ? bl.red : dri->dri_Pens[FILLPEN]);
        pens[1] = (UWORD)(bl.dark >= 0 ? bl.dark : dri->dri_Pens[SHADOWPEN]);
        pens[2] = pens[1];
        draw_symbol(rp, x, top, ribbon, pens);
        /* line number and the line without its indentation */
        for (t = it->text; *t == ' ' || *t == '\t'; ++t) ;
        snprintf(text, sizeof(text), "%ld: %s", it->line + 1, t);
        {
            char *p;
            for (p = text; *p != '\0'; ++p)
                if (*p == '\t') *p = ' ';
        }
        t = text;
    }
    x = (WORD)(x + SYMBOL_WIDTH + TEXT_GAP);
    if (x > b->MaxX) return LBCB_OK;
    length = strlen(t);
    fit = TextFit(rp, (STRPTR)t, (UWORD)length, &te, NULL, 1, (UWORD)(b->MaxX - x + 1),
                  (UWORD)height);
    SetAPen(rp, (UBYTE)dri->dri_Pens[selected ? FILLTEXTPEN : TEXTPEN]);
    SetDrMd(rp, JAM1);
    Move(rp, x, (WORD)(b->MinY + (height - rp->TxHeight) / 2 + rp->TxBaseline));
    Text(rp, (STRPTR)t, (UWORD)fit);
    return LBCB_OK;
}

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
 * @brief Give the list its nodes (~0 detaches them for changes).
 * @param list The list.
 */
static void set_labels(struct List *list)
{
    if (bl.list == NULL) return;
    if (bl.win != NULL)
        SetGadgetAttrs((struct Gadget *)bl.list, bl.win, NULL, LISTBROWSER_Labels, (ULONG)list, TAG_END);
    else SetAttrs(bl.list, LISTBROWSER_Labels, (ULONG)list, TAG_END);
}

/** @brief Free the rows and the bookmarks they show (the list is detached). */
static void free_rows(void)
{
    struct Node *n;
    if (bl.nodes.lh_Head == NULL) NewList(&bl.nodes);
    while ((n = RemHead(&bl.nodes)) != NULL) FreeListBrowserNode(n);
    bookmarks_free_items(bl.items, bl.count);
    bl.items = NULL;
    bl.count = 0;
}

/**
 * @brief Return the bookmark of a row.
 * @param node The row, may be NULL.
 * @param file_row Set to non-zero for the row of a file.
 * @return The bookmark (the file's first one for a file row), or NULL.
 */
static const BookmarkItem *row_item(struct Node *node, int *file_row)
{
    ULONG data = 0;
    if (node == NULL) return NULL;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&data, TAG_END);
    if ((long)(data >> 1) >= bl.count) return NULL;
    if (file_row != NULL) *file_row = (int)(data & 1);
    return &bl.items[data >> 1];
}

/**
 * @brief Build the list again from the bookmarks; the selected bookmark
 *        stays selected (found by file and line).
 * @param app The application state.
 */
static void rebuild(EditorApp *app)
{
    char *sel_name = NULL;
    long sel_line = -1, i, index = 0, sel_index = -1;
    int sel_file = 0;
    const BookmarkItem *it = row_item((struct Node *)get(bl.list, LISTBROWSER_SelectedNode), &sel_file);
    struct Node *n;

    if (it != NULL && (sel_name = AllocVec((ULONG)strlen(it->name) + 1, MEMF_ANY)) != NULL) {
        strcpy(sel_name, it->name);
        sel_line = it->line;
    }
    set_labels((struct List *)~0);
    free_rows();
    bl.count = bookmarks_collect(app, bl.scope_on, &bl.items);
    bl.generation = bookmarks_generation();
    bl.active = app->active;
    for (i = 0; i < bl.count; ++i) {
        int k;
        for (k = (i == 0 || strcmp(bl.items[i].name, bl.items[i - 1].name) != 0) ? 1 : 0; k >= 0; --k) {
            n = AllocListBrowserNode(1, LBNA_UserData, (ULONG)(i << 1 | k),
                LBNA_Column, 0, LBNCA_RenderHook, (ULONG)&bl.render_hook,
                LBNCA_HookHeight, (ULONG)bl.row_height, TAG_END);
            if (n == NULL) continue;
            AddTail(&bl.nodes, n);
            if (sel_name != NULL && strcmp(bl.items[i].name, sel_name) == 0 &&
                (k ? sel_file : !sel_file && bl.items[i].line == sel_line))
                sel_index = index;
            ++index;
        }
    }
    if (sel_name != NULL) FreeVec(sel_name);
    set_labels(&bl.nodes);
    if (sel_index >= 0 && bl.win != NULL)
        SetGadgetAttrs((struct Gadget *)bl.list, bl.win, NULL, LISTBROWSER_Selected, (ULONG)sel_index,
                       LISTBROWSER_MakeVisible, (ULONG)sel_index, TAG_END);
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

/**
 * @brief A button as wide as its text.
 * @param id Gadget ID.
 * @param text Its text.
 * @return The gadget.
 */
static Object *button(ULONG id, const char *text)
{
    return NewObject(BUTTON_GetClass(), NULL, GA_ID, id, GA_RelVerify, TRUE,
                     GA_Text, (ULONG)text, BUTTON_TextPadding, TRUE, TAG_END);
}

/**
 * @brief Obtain the pens of the ribbon (once per screen).
 * @param app The application state.
 */
static void obtain_pens(EditorApp *app)
{
    if (bl.cm != NULL || app->screen == NULL) return;
    bl.cm = app->screen->ViewPort.ColorMap;
    bl.red = ObtainBestPen(bl.cm, 0xC8C8C8C8UL, 0x28282828UL, 0x1E1E1E1EUL,
                           OBP_Precision, PRECISION_GUI, OBP_FailIfBad, FALSE, TAG_END);
    bl.dark = ObtainBestPen(bl.cm, 0x6E6E6E6EUL, 0x10101010UL, 0x08080808UL,
                            OBP_Precision, PRECISION_GUI, OBP_FailIfBad, FALSE, TAG_END);
}

int bmlist_is_open(void)
{
    return bl.win != NULL;
}

void bmlist_remember(EditorApp *app)
{
    if (bl.win != NULL) toolwin_remember(bl.win, app->bm_box);
}

void bmlist_open(EditorApp *app)
{
    WORD box[4], h;
    int i;
    if (bl.win != NULL) {
        WindowToFront(bl.win);
        return;
    }
    if (app->window == NULL || !open_classes(app)) return;
    if (bl.nodes.lh_Head == NULL) NewList(&bl.nodes);
    obtain_pens(app);
    bl.render_hook.h_Entry = (ULONG (*)())HookEntry;
    bl.render_hook.h_SubEntry = (ULONG (*)())render_row;
    h = app->screen != NULL && app->screen->Font != NULL ? (WORD)app->screen->Font->ta_YSize : 8;
    bl.row_height = (WORD)((h > SYMBOL_HEIGHT ? h : SYMBOL_HEIGHT) + 2 * ROW_PADDING);
    for (i = 0; i < BMSCOPE_COUNT; ++i) bl.scope_labels[i] = (STRPTR)S(scope_texts[i]);
    bl.scope_labels[BMSCOPE_COUNT] = NULL;
    toolwin_box(app, app->bm_box, MIN_WIDTH, MIN_HEIGHT, box);

    bl.scope = NewObject(CHOOSER_GetClass(), NULL, GA_ID, BG_SCOPE, GA_RelVerify, TRUE,
        CHOOSER_PopUp, TRUE, CHOOSER_LabelArray, (ULONG)bl.scope_labels,
        CHOOSER_Selected, (ULONG)bl.scope_on, TAG_END);
    bl.list = NewObject(LISTBROWSER_GetClass(), NULL, GA_ID, BG_LIST, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&bl.nodes, LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_AutoWheel, TRUE, TAG_END);
    bl.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(MSG_BML_TITLE),
        WA_PubScreen, (ULONG)app->screen,
        WA_Left, (ULONG)box[0], WA_Top, (ULONG)box[1],
        WA_Width, (ULONG)box[2], WA_Height, (ULONG)box[3],
        WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, FALSE,
        WINDOW_ParentGroup, (ULONG)(bl.root = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_LeftSpacing, 6, LAYOUT_RightSpacing, 6,
            LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6,
            LAYOUT_InnerSpacing, 4,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)bl.scope,
            CHILD_Label, (ULONG)NewObject(LABEL_GetClass(), NULL,
                LABEL_Text, (ULONG)S(MSG_BML_SHOW), TAG_END),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)bl.list,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)button(BG_GOTO, S(MSG_BML_GOTO)),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)button(BG_REMOVE, S(MSG_BML_REMOVE)),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                TAG_END),
            CHILD_WeightedHeight, 0,
            TAG_END)),
        TAG_END);
    if (bl.winobj == NULL || (bl.win = (struct Window *)DoMethod(bl.winobj, WM_OPEN, NULL)) == NULL) {
        if (bl.winobj != NULL) DisposeObject(bl.winobj);
        bl.winobj = NULL;
        bl.list = bl.scope = bl.root = NULL;
        return;
    }
    /* window.class may change the box it was given (the window came out
     * higher than the main window): set it again */
    if (bl.win->LeftEdge != box[0] || bl.win->TopEdge != box[1] ||
        bl.win->Width != box[2] || bl.win->Height != box[3])
        ChangeWindowBox(bl.win, box[0], box[1], box[2], box[3]);
    rebuild(app);
}

void bmlist_close(EditorApp *app)
{
    if (bl.winobj == NULL) return;
    bmlist_remember(app);
    set_labels((struct List *)~0);
    DoMethod(bl.winobj, WM_CLOSE, NULL);
    DisposeObject(bl.winobj);
    bl.winobj = NULL;
    bl.win = NULL;
    bl.list = bl.scope = bl.root = NULL;
    free_rows();
}

ULONG bmlist_sigmask(void)
{
    ULONG sig = 0;
    if (bl.win != NULL) GetAttr(WINDOW_SigMask, bl.winobj, &sig);
    return sig;
}

/**
 * @brief Go to the selected bookmark (the first of a selected file) and
 *        bring the main window to the front, with the input focus.
 * @param app The application state.
 */
static void go_to_selected(EditorApp *app)
{
    const BookmarkItem *it = row_item((struct Node *)get(bl.list, LISTBROWSER_SelectedNode), NULL);
    if (it == NULL) return;
    bookmarks_go_to(app, it);
    if (app->window != NULL) {
        ActivateWindow(app->window);
        app->focus_editor = 1;          /* when the window is active */
    }
}

void bmlist_handle(EditorApp *app)
{
    ULONG result;
    WORD code;
    if (bl.win == NULL) return;
    while ((result = DoMethod(bl.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            bmlist_close(app);
            ui_set_menu_check(app, MID_BM_LIST, 0);
            return;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case BG_SCOPE:
                bl.scope_on = (int)get(bl.scope, CHOOSER_Selected);
                rebuild(app);
                break;
            case BG_LIST:
                if (get(bl.list, LISTBROWSER_RelEvent) & LBRE_DOUBLECLICK) go_to_selected(app);
                break;
            case BG_GOTO:
                go_to_selected(app);
                break;
            case BG_REMOVE: {
                int file_row = 0;
                const BookmarkItem *it = row_item((struct Node *)get(bl.list, LISTBROWSER_SelectedNode),
                                                  &file_row);
                if (it != NULL && !file_row) bookmarks_remove(app, it);
                break;
            }
            }
            break;
        }
    }
}

void bmlist_update(EditorApp *app)
{
    if (bl.win == NULL) return;
    if (bl.generation != bookmarks_generation() ||
        (bl.scope_on == BMSCOPE_CURRENT && bl.active != app->active))
        rebuild(app);
}

void bmlist_cleanup(EditorApp *app)
{
    bmlist_close(app);
    if (bl.cm != NULL) {
        if (bl.red >= 0) ReleasePen(bl.cm, (ULONG)bl.red);
        if (bl.dark >= 0) ReleasePen(bl.cm, (ULONG)bl.dark);
        bl.red = bl.dark = -1;
        bl.cm = NULL;
    }
}
