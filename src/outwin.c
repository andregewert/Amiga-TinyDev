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
 * The mouse wheel scrolls the list: its rawkey codes are handled here, as
 * listbrowser.gadget only gets them while it is the active gadget.
 *
 * Below the list a status line tells what runs or how it ended, beside it
 * Stop ends the build (build_stop()). Two buttons put the selected line
 * or all lines into the clipboard: as FTXT, written straight to
 * clipboard.device (unit 0), so no further library is needed. The selection
 * is kept here as a row number, as the list is detached and attached again
 * for every piece of output and the oldest nodes get freed; copying is
 * silent, only a failure is reported. The window opens below the main
 * window, as wide as it (toolwin.c); its box is kept for the session and
 * written into TinyDev.json by View/Snapshot Tool Windows.
 */
#include "editor.h"

#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <images/bevel.h>
#include <libraries/keymap.h>
#include <devices/clipboard.h>
#include <exec/memory.h>

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

enum { OG_LIST = 500, OG_STATUS, OG_STOP, OG_CLEAR, OG_COPYLINE, OG_COPYALL };

#define MIN_WIDTH 200               /**< smallest width of the window */
#define MIN_HEIGHT 80               /**< smallest height of the window */
#define WHEEL_ROWS 3                /**< rows the list moves per step of the mouse wheel */
#define OUT_MAX_LINES 2000          /**< lines kept */
#define OUT_LINE_MAX 512            /**< longest line kept (longer ones are cut) */

/** State of the output window and its lines (there is one). */
static struct {
    Object *winobj, *root, *list, *status, *stop, *copy_line, *copy_all;
    struct Window *win;
    struct List lines;              /* listbrowser nodes */
    long count;                     /* number of lines */
    long selected;                  /* row of the selected line, -1: none */
    int line_enabled;               /* Copy line is not ghosted */
    int all_enabled;                /* Copy all is not ghosted */
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
        if (ow.lines.lh_Head == NULL) {
            NewList(&ow.lines);
            ow.selected = -1;
        }
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
 * @brief Ghost the copy buttons that would do nothing (only on a change).
 */
static void update_copy_buttons(void)
{
    int line = ow.selected >= 0;
    int all = ow.count > 0;
    if (ow.win == NULL) return;
    if (line != ow.line_enabled) {
        ow.line_enabled = line;
        SetGadgetAttrs((struct Gadget *)ow.copy_line, ow.win, NULL, GA_Disabled, (ULONG)!line, TAG_END);
    }
    if (all != ow.all_enabled) {
        ow.all_enabled = all;
        SetGadgetAttrs((struct Gadget *)ow.copy_all, ow.win, NULL, GA_Disabled, (ULONG)!all, TAG_END);
    }
}

/**
 * @brief Show the lines anew (after lines were added or removed), keep the
 *        selection and follow the end.
 */
static void show_lines(void)
{
    if (ow.win == NULL) return;
    SetGadgetAttrs((struct Gadget *)ow.list, ow.win, NULL,
                   LISTBROWSER_Labels, (ULONG)&ow.lines,
                   LISTBROWSER_Selected, (ULONG)ow.selected,
                   ow.count > 0 ? LISTBROWSER_MakeVisible : TAG_IGNORE, (ULONG)(ow.count - 1),
                   TAG_END);
    update_copy_buttons();
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
        if (ow.selected >= 0) --ow.selected;    /* the rows moved up; row 0 is gone */
    }
}

/**
 * @brief The text of a line.
 * @param n The line's node.
 * @return Its text, owned by the node; never NULL.
 */
static const char *line_text(struct Node *n)
{
    STRPTR text = NULL;
    GetListBrowserNodeAttrs(n, LBNA_Column, 0, LBNCA_Text, (ULONG)&text, TAG_END);
    return text != NULL ? (const char *)text : "";
}

/**
 * @brief Write data as the new contents of the clipboard (unit 0).
 * @param data A complete IFF file.
 * @param length Its length.
 * @return Non-zero if it was written.
 */
static int clip_write(APTR data, ULONG length)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOClipReq *io = NULL;
    int ok = 0;
    if (port != NULL) io = (struct IOClipReq *)CreateIORequest(port, sizeof(struct IOClipReq));
    if (io != NULL && OpenDevice((STRPTR)"clipboard.device", PRIMARY_CLIP, (struct IORequest *)io, 0) == 0) {
        io->io_Command = CMD_WRITE;
        io->io_Data = (STRPTR)data;
        io->io_Length = length;
        io->io_Offset = 0;
        io->io_ClipID = 0;
        ok = DoIO((struct IORequest *)io) == 0 && io->io_Actual == length;
        /* always, even after a failed write: it ends the write */
        io->io_Command = CMD_UPDATE;
        if (DoIO((struct IORequest *)io) != 0) ok = 0;
        CloseDevice((struct IORequest *)io);
    }
    if (io != NULL) DeleteIORequest((struct IORequest *)io);
    if (port != NULL) DeleteMsgPort(port);
    return ok;
}

/**
 * @brief Put lines into the clipboard as FTXT.
 *
 * Each line ends with a line feed, except a single line, which can then be
 * pasted within a line.
 *
 * @param app The application state (for the requester).
 * @param first The first line.
 * @param rows The number of lines, at least 1.
 */
static void copy_lines(EditorApp *app, struct Node *first, long rows)
{
    struct Node *n = first;
    ULONG size = 0, total;
    ULONG *buffer;
    char *p;
    long i;
    for (i = 0; i < rows; ++i, n = n->ln_Succ) size += strlen(line_text(n)) + 1;
    if (rows == 1) --size;
    total = 20 + size + (size & 1);     /* IFF chunks have an even length */
    buffer = (ULONG *)AllocVec(total, MEMF_ANY);
    if (buffer != NULL) {
        buffer[0] = 0x464F524DUL;       /* FORM */
        buffer[1] = total - 8;
        buffer[2] = 0x46545854UL;       /* FTXT */
        buffer[3] = 0x43485253UL;       /* CHRS */
        buffer[4] = size;
        p = (char *)(buffer + 5);
        for (i = 0, n = first; i < rows; ++i, n = n->ln_Succ) {
            size_t length = strlen(line_text(n));
            memcpy(p, line_text(n), length);
            p += length;
            if (rows > 1) *p++ = '\n';
        }
        if (size & 1) *p = '\0';
    }
    if (buffer == NULL || !clip_write(buffer, total)) ui_error(app, APP_NAME, S(MSG_OUT_COPYFAILED));
    if (buffer != NULL) FreeVec(buffer);
}

/**
 * @brief Put the selected line into the clipboard, if there is one.
 * @param app The application state.
 */
static void copy_selected_line(EditorApp *app)
{
    struct Node *n = ow.lines.lh_Head;
    long i;
    if (ow.selected < 0 || ow.selected >= ow.count) return;
    for (i = 0; i < ow.selected; ++i) n = n->ln_Succ;
    copy_lines(app, n, 1);
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
    ow.selected = -1;
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
    ow.copy_line = button(OG_COPYLINE, S(MSG_OUT_COPYLINE));
    ow.copy_all = button(OG_COPYALL, S(MSG_OUT_COPYALL));
    ow.line_enabled = ow.selected >= 0;
    ow.all_enabled = ow.count > 0;
    if (ow.copy_line != NULL) SetAttrs(ow.copy_line, GA_Disabled, (ULONG)!ow.line_enabled, TAG_END);
    if (ow.copy_all != NULL) SetAttrs(ow.copy_all, GA_Disabled, (ULONG)!ow.all_enabled, TAG_END);
    ow.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(MSG_OUT_TITLE),
        WA_PubScreen, (ULONG)app->screen,
        WA_SmartRefresh, TRUE,     /* scrolled lists: see "Scrolling..." in TECHNICAL.md */
        WA_Left, (ULONG)box[0], WA_Top, (ULONG)box[1],
        WA_Width, (ULONG)box[2], WA_Height, (ULONG)box[3],
        WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, FALSE,
        WA_IDCMP, IDCMP_RAWKEY,
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
                LAYOUT_AddChild, (ULONG)ow.copy_line,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)ow.copy_all,
                CHILD_WeightedWidth, 0,
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
        ow.list = ow.status = ow.stop = ow.copy_line = ow.copy_all = ow.root = NULL;
        return;
    }
    /* a remembered box may be too small for the buttons */
    if (box[2] < ow.win->MinWidth) box[2] = ow.win->MinWidth;
    if (box[3] < ow.win->MinHeight) box[3] = ow.win->MinHeight;
    if (box[0] + box[2] > ow.win->WScreen->Width) box[0] = (WORD)(ow.win->WScreen->Width - box[2]);
    if (box[1] + box[3] > ow.win->WScreen->Height) box[1] = (WORD)(ow.win->WScreen->Height - box[3]);
    if (box[0] < 0) box[0] = 0;
    if (box[1] < 0) box[1] = 0;
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
    ow.list = ow.status = ow.stop = ow.copy_line = ow.copy_all = ow.root = NULL;
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
        case WMHI_RAWKEY:
            if ((result & WMHI_KEYMASK) == RAWKEY_WHEEL_UP || (result & WMHI_KEYMASK) == RAWKEY_WHEEL_DOWN) {
                ULONG top = 0;
                LONG t;
                GetAttr(LISTBROWSER_Top, ow.list, &top);
                t = (LONG)top + ((result & WMHI_KEYMASK) == RAWKEY_WHEEL_UP ? -WHEEL_ROWS : WHEEL_ROWS);
                if (t < 0) t = 0;
                SetGadgetAttrs((struct Gadget *)ow.list, ow.win, NULL, LISTBROWSER_Top, (ULONG)t, TAG_END);
            }
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case OG_LIST: {
                ULONG row = ~0UL;
                GetAttr(LISTBROWSER_Selected, ow.list, &row);
                ow.selected = (long)row;
                update_copy_buttons();
                break;
            }
            case OG_COPYLINE: copy_selected_line(app); break;
            case OG_COPYALL:
                if (ow.count > 0) copy_lines(app, ow.lines.lh_Head, ow.count);
                break;
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
