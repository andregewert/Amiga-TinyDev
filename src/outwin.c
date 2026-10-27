/**
 * @file outwin.c
 * @brief The output window, a tool window: what the Build command writes,
 *        line by line, and its state.
 *
 * The lines are kept here, also while the window is closed (or the main
 * window is iconified), as listbrowser nodes; the window shows them and
 * follows the end while new lines come. At most OUT_MAX_LINES lines are
 * kept, the oldest go first. Text arrives in pieces (outwin_add_text()):
 * an unfinished line waits for its end; CRs and the escape sequences of
 * console output (colours) are dropped.
 *
 * Below the list a status line tells what runs or how it ended, beside it
 * Stop ends the build (build_stop()). The window opens below the main
 * window, as wide as it (toolwin.c); its box is kept for the session and
 * written into the program icon by View/Snapshot Tool Windows.
 */
#include "editor.h"

#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <images/bevel.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/listbrowser.h>
#include <clib/alib_protos.h>

#include <stdlib.h>
#include <string.h>

extern struct Library *ListBrowserBase;     /* shared with the settings window */

enum { OG_LIST = 500, OG_STATUS, OG_STOP, OG_CLEAR };

#define MIN_WIDTH 200               /**< smallest width of the window */
#define MIN_HEIGHT 80               /**< smallest height of the window */
#define OUT_MAX_LINES 2000          /**< lines kept */
#define OUT_LINE_MAX 512            /**< longest line kept (longer ones are cut) */

/** State of the output window and its lines (there is one). */
static struct {
    Object *winobj, *root, *list, *status, *stop;
    struct Window *win;
    struct List lines;              /* listbrowser nodes */
    long count;                     /* number of lines */
    char partial[OUT_LINE_MAX];     /* the unfinished last line */
    size_t partial_length;
    int escape;                     /* inside an escape sequence: 1 after ESC, 2 in CSI */
    char status_text[160];
    int running;                    /* Stop is not ghosted */
} ow;

/**
 * @brief Open the class the window needs (once).
 * @param app The application state (for the requester).
 * @return Non-zero if it is available.
 */
static int open_classes(EditorApp *app)
{
    if (ListBrowserBase == NULL) ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44);
    if (ListBrowserBase != NULL) {
        if (ow.lines.lh_Head == NULL) NewList(&ow.lines);
        return 1;
    }
    ui_error(app, APP_NAME, S(MSG_OUT_NEEDCLASSES));
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
 * @brief Show the lines anew (after lines were added or removed) and
 *        follow the end.
 */
static void show_lines(void)
{
    if (ow.win == NULL) return;
    SetGadgetAttrs((struct Gadget *)ow.list, ow.win, NULL,
                   LISTBROWSER_Labels, (ULONG)&ow.lines,
                   ow.count > 0 ? LISTBROWSER_MakeVisible : TAG_IGNORE, (ULONG)(ow.count - 1),
                   TAG_END);
}

/**
 * @brief Detach the lines from the list before they change.
 */
static void detach(void)
{
    if (ow.win != NULL)
        SetGadgetAttrs((struct Gadget *)ow.list, ow.win, NULL, LISTBROWSER_Labels, ~0UL, TAG_END);
}

/**
 * @brief Add a finished line (detached list).
 * @param text The line (need not be terminated).
 * @param length Its length.
 */
static void add_line(const char *text, size_t length)
{
    char line[OUT_LINE_MAX + 1];
    struct Node *n;
    if (length > OUT_LINE_MAX) length = OUT_LINE_MAX;
    memcpy(line, text, length);
    line[length] = '\0';
    n = AllocListBrowserNode(1, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)line, TAG_END);
    if (n == NULL) return;
    AddTail(&ow.lines, n);
    if (++ow.count > OUT_MAX_LINES) {
        struct Node *first = RemHead(&ow.lines);
        FreeListBrowserNode(first);
        --ow.count;
    }
}

void outwin_add_text(EditorApp *app, const char *text, long length)
{
    long i;
    if (!open_classes(app)) return;
    detach();
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        /* escape sequences: ESC [ ... letter, or CSI (0x9B) ... letter */
        if (ow.escape == 1) {
            ow.escape = c == '[' ? 2 : 0;
            continue;
        }
        if (ow.escape == 2) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') ow.escape = 0;
            continue;
        }
        if (c == 0x1B) { ow.escape = 1; continue; }
        if (c == 0x9B) { ow.escape = 2; continue; }
        if (c == '\n') {
            add_line(ow.partial, ow.partial_length);
            ow.partial_length = 0;
        } else if (c != '\r' && ow.partial_length < OUT_LINE_MAX) {
            ow.partial[ow.partial_length++] = (char)(c == '\t' ? ' ' : c);
        }
    }
    show_lines();
}

void outwin_flush(EditorApp *app)
{
    if (ow.partial_length == 0 || !open_classes(app)) return;
    detach();
    add_line(ow.partial, ow.partial_length);
    ow.partial_length = 0;
    show_lines();
}

void outwin_clear(EditorApp *app)
{
    if (!open_classes(app)) return;
    detach();
    FreeListBrowserList(&ow.lines);
    NewList(&ow.lines);
    ow.count = 0;
    ow.partial_length = 0;
    ow.escape = 0;
    show_lines();
}

void outwin_set_status(const char *text, int running)
{
    strncpy(ow.status_text, text, sizeof(ow.status_text) - 1);
    ow.status_text[sizeof(ow.status_text) - 1] = '\0';
    ow.running = running;
    if (ow.win == NULL) return;
    SetGadgetAttrs((struct Gadget *)ow.status, ow.win, NULL, GA_Text, (ULONG)ow.status_text, TAG_END);
    SetGadgetAttrs((struct Gadget *)ow.stop, ow.win, NULL, GA_Disabled, (ULONG)!running, TAG_END);
}

int outwin_is_open(void)
{
    return ow.win != NULL;
}

void outwin_open(EditorApp *app)
{
    WORD box[4];
    if (ow.win != NULL) {
        WindowToFront(ow.win);
        return;
    }
    if (app->window == NULL || !open_classes(app)) return;
    toolwin_box(app, app->out_box, MIN_WIDTH, MIN_HEIGHT, 1, box);

    ow.list = NewObject(LISTBROWSER_GetClass(), NULL, GA_ID, OG_LIST, GA_RelVerify, TRUE,
        GA_ReadOnly, FALSE,
        LISTBROWSER_Labels, (ULONG)&ow.lines,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_AutoWheel, TRUE,
        LISTBROWSER_HorizontalProp, TRUE,
        TAG_END);
    /* a read-only button as status line: the window's pattern shows through */
    ow.status = NewObject(BUTTON_GetClass(), NULL, GA_ID, OG_STATUS, GA_ReadOnly, TRUE,
        GA_Underscore, 0, GA_Text, (ULONG)ow.status_text,
        BUTTON_BevelStyle, BVS_NONE, BUTTON_Transparent, TRUE, BUTTON_Justification, BCJ_LEFT,
        TAG_END);
    ow.stop = button(OG_STOP, S(MSG_OUT_STOP));
    if (ow.stop != NULL) SetAttrs(ow.stop, GA_Disabled, (ULONG)!ow.running, TAG_END);
    ow.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(MSG_OUT_TITLE),
        WA_PubScreen, (ULONG)app->screen,
        WA_Left, (ULONG)box[0], WA_Top, (ULONG)box[1],
        WA_Width, (ULONG)box[2], WA_Height, (ULONG)box[3],
        WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, FALSE,
        WINDOW_ParentGroup, (ULONG)(ow.root = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_LeftSpacing, 6, LAYOUT_RightSpacing, 6,
            LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6,
            LAYOUT_InnerSpacing, 4,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)ow.list,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_InnerSpacing, 4,
                LAYOUT_AddChild, (ULONG)ow.status,
                LAYOUT_AddChild, (ULONG)ow.stop,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)button(OG_CLEAR, S(MSG_OUT_CLEAR)),
                CHILD_WeightedWidth, 0,
                TAG_END),
            CHILD_WeightedHeight, 0,
            TAG_END)),
        TAG_END);
    if (ow.winobj == NULL || (ow.win = (struct Window *)DoMethod(ow.winobj, WM_OPEN, NULL)) == NULL) {
        if (ow.winobj != NULL) DisposeObject(ow.winobj);
        ow.winobj = NULL;
        ow.list = ow.status = ow.stop = ow.root = NULL;
        return;
    }
    /* window.class may change the box it was given: set it again */
    if (ow.win->LeftEdge != box[0] || ow.win->TopEdge != box[1] ||
        ow.win->Width != box[2] || ow.win->Height != box[3])
        ChangeWindowBox(ow.win, box[0], box[1], box[2], box[3]);
    show_lines();
}

void outwin_remember(EditorApp *app)
{
    if (ow.win != NULL) toolwin_remember(ow.win, app->out_box);
}

void outwin_close(EditorApp *app)
{
    if (ow.winobj == NULL) return;
    toolwin_remember(ow.win, app->out_box);
    detach();
    DoMethod(ow.winobj, WM_CLOSE, NULL);
    DisposeObject(ow.winobj);
    ow.winobj = NULL;
    ow.win = NULL;
    ow.list = ow.status = ow.stop = ow.root = NULL;
}

ULONG outwin_sigmask(void)
{
    ULONG sig = 0;
    if (ow.win != NULL) GetAttr(WINDOW_SigMask, ow.winobj, &sig);
    return sig;
}

void outwin_handle(EditorApp *app)
{
    ULONG result;
    WORD code;
    if (ow.win == NULL) return;
    while ((result = DoMethod(ow.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            outwin_close(app);
            ui_set_menu_check(app, MID_OUTPUT, 0);
            return;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case OG_STOP: build_stop(app); break;
            case OG_CLEAR: outwin_clear(app); break;
            }
            break;
        }
    }
}

void outwin_cleanup(EditorApp *app)
{
    outwin_close(app);
    if (ow.lines.lh_Head != NULL) FreeListBrowserList(&ow.lines);
    memset(&ow, 0, sizeof(ow));
}
