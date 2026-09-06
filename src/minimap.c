#include "editor.h"

#include <dos/dostags.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <gadgets/layout.h>
#include <gadgets/space.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/space.h>
#include <proto/texteditor.h>
#include <gadgets/texteditor.h>
#include <reaction/reaction.h>
#include <clib/alib_protos.h>

#include <string.h>

/* Default on-screen width of the minimap column and the horizontal source range
 * (in characters) that is mapped across that width.  The width is only a
 * starting point; the user can resize the column with the WeightBar. */
#define MINIMAP_WIDTH   96
#define MINIMAP_COLUMNS 128
/* MINIMAP_MIN_WIDTH and MINIMAP_DEFAULT_WEIGHT (the smallest width the column
 * may be shrunk to and its initial relative layout weight) are shared with the
 * folder tree and declared in editor.h. */

/* Commands exchanged with the render task. */
enum { MINIMAP_RENDER = 0, MINIMAP_QUIT = 1 };

/* A single render job passed to the background task.  The main task fills in
 * the request, exports a private snapshot of the document text and posts it to
 * the render task; the task renders it into an off-screen bitmap and replies.
 * Because only one job is ever outstanding at a time the same message is
 * reused for every render and for the final quit request. */
typedef struct MinimapRequest {
    struct Message msg;
    int command;
    char *text;                     /* FreeVec()'d by the render task */
    SyntaxLanguage language;
    UWORD pens[EDITOR_COLOR_COUNT];
    UWORD background;               /* screen BACKGROUNDPEN, matches the editor */
    UWORD view_fill;                /* slightly darker tint of the viewport box */
    UWORD view_border;              /* frame colour around the viewport box */
    ULONG view_first;               /* first visible line (Prop units) */
    ULONG view_visible;             /* visible line span (Prop units) */
    ULONG view_total;               /* total document extent (Prop units) */
    LONG width;
    LONG height;
    ULONG depth;
    struct BitMap *friend;
    struct BitMap *result;          /* filled in by the render task */
    int ok;
} MinimapRequest;

/* Startup handshake message: the parent hands the render task a pointer to the
 * shared Minimap state, the task creates its request port and replies. */
typedef struct MinimapStartup {
    struct Message msg;
    struct Minimap *mm;
    int ok;
} MinimapStartup;

struct Minimap {
    struct Process *proc;
    struct MsgPort *reply_port;     /* owned by the main task */
    struct MsgPort *request_port;   /* owned by the render task */
    MinimapRequest request;
    int busy;                       /* a render job is in flight */
    int dirty;                      /* a fresh render is needed */
    int ready;                      /* the render task started up */
    /* The following members are only ever touched by the render task. */
    struct BitMap *bitmap;
    LONG bm_width;
    LONG bm_height;
    /* Last drawing-area size seen by the main task, used to notice layout
     * changes (e.g. WeightBar drags) that resize the space.gadget without a
     * WMHI_NEWSIZE event. */
    LONG last_width;
    LONG last_height;
    /* Last vertical scroll position seen by the main task, used to re-render the
     * viewport overlay after (not during) a scrolling operation. */
    LONG last_first;
    char *linebuf;
    size_t linebuf_size;
    unsigned char *stylebuf;
    size_t stylebuf_size;
};

/* -- render task side --------------------------------------------------- */

typedef struct FillContext {
    unsigned char *styles;
    size_t length;
} FillContext;

/* syntax emit callback: record the style of every character of the line. */
static void minimap_fill_styles(void *context, const SyntaxSpan *span)
{
    FillContext *fc = (FillContext *)context;
    size_t i, end = span->end;
    if (end > fc->length) end = fc->length;
    for (i = span->start; i < end; ++i)
        fc->styles[i] = (unsigned char)span->style;
}

/* Map a syntax style onto the matching reserved editor colour pen. */
static UWORD minimap_pen_for_style(const MinimapRequest *req, unsigned char style)
{
    switch ((SyntaxStyle)style) {
        case SYNTAX_KEYWORD:      return req->pens[EDITOR_COLOR_KEYWORD];
        case SYNTAX_STRING:       return req->pens[EDITOR_COLOR_STRING];
        case SYNTAX_COMMENT:      return req->pens[EDITOR_COLOR_COMMENT];
        case SYNTAX_PREPROCESSOR: return req->pens[EDITOR_COLOR_PREPROCESSOR];
        default:                  return req->pens[EDITOR_COLOR_TEXT];
    }
}

/* Advance over a single physical line, honouring LF, CR and CRLF endings.
 * Returns the length of the line and updates *next to the start of the line
 * that follows (or the terminating NUL). */
static size_t minimap_line_length(const char *p, const char **next)
{
    const char *start = p;
    while (*p != '\0' && *p != '\n' && *p != '\r') ++p;
    { size_t length = (size_t)(p - start);
      if (*p == '\r' && *(p + 1) == '\n') p += 2;
      else if (*p != '\0') p += 1;
      *next = p;
      return length; }
}

/* Count the physical lines in the snapshot so line numbers can be scaled onto
 * the available pixel height. */
static ULONG minimap_count_lines(const char *text)
{
    ULONG lines = 0;
    const char *p = text;
    if (text == NULL || *text == '\0') return 1;
    while (*p != '\0') { minimap_line_length(p, &p); ++lines; }
    return lines != 0 ? lines : 1;
}

/* Ensure a reusable buffer is at least "need" bytes; returns 0 on failure. */
static int minimap_grow(void **buffer, size_t *size, size_t need)
{
    if (*size >= need && *buffer != NULL) return 1;
    if (*buffer != NULL) FreeVec(*buffer);
    *buffer = AllocVec((ULONG)need, MEMF_ANY);
    *size = *buffer != NULL ? need : 0;
    return *buffer != NULL;
}

/* Draw the exported text into the off-screen bitmap using the syntax colours.
 * Runs entirely in the render task and never touches Intuition. */
static void minimap_render(struct Minimap *mm, MinimapRequest *req)
{
    struct RastPort rp;
    const char *p;
    ULONG total_lines;
    ULONG line = 0;
    LONG width = req->width, height = req->height;
    LONG view_top = 0, view_bottom = 0;
    int have_view = 0;
    SyntaxState state = SYNTAX_STATE_NORMAL;

    req->result = NULL;
    req->ok = 0;
    if (width <= 0 || height <= 0) return;

    if (mm->bitmap == NULL || mm->bm_width != width || mm->bm_height != height) {
        if (mm->bitmap != NULL) FreeBitMap(mm->bitmap);
        mm->bitmap = AllocBitMap((ULONG)width, (ULONG)height, req->depth,
                                 BMF_CLEAR, req->friend);
        mm->bm_width = width;
        mm->bm_height = height;
    }
    if (mm->bitmap == NULL) { mm->bm_width = mm->bm_height = 0; return; }

    InitRastPort(&rp);
    rp.BitMap = mm->bitmap;
    /* The AmigaOS 3.2 texteditor.gadget draws its text area with the screen's
     * BACKGROUNDPEN (ignoring GA_BackFill/GA_DrawInfo), so clear the minimap to
     * that same pen to match the editor's grey background rather than the
     * reserved white EDITOR_COLOR_BACKGROUND pen. */
    SetRast(&rp, (UBYTE)req->background);

    /* Compute the vertical extent of the editor's currently visible region and
     * tint it with a slightly darker background before the text is drawn, so the
     * viewport rectangle shows through behind the rendered lines.  Only the
     * vertical range matters; the box always spans the full width. */
    if (req->view_total > 0 && req->view_visible > 0) {
        view_top = (LONG)((unsigned long long)req->view_first * (ULONG)height /
                          req->view_total);
        view_bottom = (LONG)((unsigned long long)(req->view_first +
                             req->view_visible) * (ULONG)height / req->view_total);
        if (view_bottom > height) view_bottom = height;
        if (view_bottom <= view_top) view_bottom = view_top + 1;
        if (view_bottom > height) view_bottom = height;
        have_view = 1;
        SetAPen(&rp, (UBYTE)req->view_fill);
        RectFill(&rp, 0, (WORD)view_top, (WORD)(width - 1),
                 (WORD)(view_bottom - 1));
    }

    total_lines = minimap_count_lines(req->text);
    p = req->text != NULL ? req->text : "";
    while (*p != '\0') {
        const char *next;
        size_t length = minimap_line_length(p, &next);
        LONG y = (LONG)((unsigned long long)line * (ULONG)height / total_lines);
        size_t col, limit = length;
        if (y >= height) y = height - 1;
        if (limit > (size_t)MINIMAP_COLUMNS) limit = (size_t)MINIMAP_COLUMNS;
        if (minimap_grow((void **)&mm->linebuf, &mm->linebuf_size, length + 1) &&
            minimap_grow((void **)&mm->stylebuf, &mm->stylebuf_size,
                         length + 1)) {
            FillContext fc;
            memcpy(mm->linebuf, p, length);
            mm->linebuf[length] = '\0';
            memset(mm->stylebuf, SYNTAX_NORMAL, length);
            fc.styles = mm->stylebuf;
            fc.length = length;
            state = syntax_scan_line(req->language, mm->linebuf, state,
                                     minimap_fill_styles, &fc);
            for (col = 0; col < limit; ++col) {
                unsigned char c = (unsigned char)mm->linebuf[col];
                LONG x;
                if (c == ' ' || c == '\t') continue;
                x = (LONG)((col * (size_t)width) / (size_t)MINIMAP_COLUMNS);
                if (x >= width) continue;
                SetAPen(&rp, (UBYTE)minimap_pen_for_style(req, mm->stylebuf[col]));
                WritePixel(&rp, (WORD)x, (WORD)y);
            }
        }
        p = next;
        ++line;
    }
    /* Draw the simple frame around the visible-area viewport on top of the text
     * so it stays visible regardless of the rendered content. */
    if (have_view) {
        SetAPen(&rp, (UBYTE)req->view_border);
        Move(&rp, 0, (WORD)view_top);
        Draw(&rp, (WORD)(width - 1), (WORD)view_top);
        Draw(&rp, (WORD)(width - 1), (WORD)(view_bottom - 1));
        Draw(&rp, 0, (WORD)(view_bottom - 1));
        Draw(&rp, 0, (WORD)view_top);
    }
    req->result = mm->bitmap;
    req->ok = 1;
}

/* Entry point of the background render task. */
static void minimap_task(void)
{
    struct Process *self = (struct Process *)FindTask(NULL);
    struct MinimapStartup *startup;
    struct Minimap *mm;
    int running = 1;

    WaitPort(&self->pr_MsgPort);
    startup = (struct MinimapStartup *)GetMsg(&self->pr_MsgPort);
    if (startup == NULL) return;
    mm = startup->mm;
    mm->request_port = CreateMsgPort();
    startup->ok = mm->request_port != NULL;
    ReplyMsg(&startup->msg);
    if (mm->request_port == NULL) return;

    while (running) {
        struct Message *m;
        WaitPort(mm->request_port);
        while ((m = GetMsg(mm->request_port)) != NULL) {
            MinimapRequest *req = (MinimapRequest *)m;
            if (req->command == MINIMAP_QUIT) {
                running = 0;
            } else {
                minimap_render(mm, req);
            }
            if (req->text != NULL) { FreeVec(req->text); req->text = NULL; }
            if (!running) {
                /* Free everything we own before acknowledging the quit so the
                 * main task can safely tear the shared state down once it sees
                 * the reply. */
                if (mm->bitmap != NULL) { FreeBitMap(mm->bitmap); mm->bitmap = NULL; }
                if (mm->linebuf != NULL) { FreeVec(mm->linebuf); mm->linebuf = NULL; }
                if (mm->stylebuf != NULL) { FreeVec(mm->stylebuf); mm->stylebuf = NULL; }
                DeleteMsgPort(mm->request_port);
                mm->request_port = NULL;
                ReplyMsg(m);
                return;
            }
            ReplyMsg(m);
        }
    }
}

/* -- main task side ----------------------------------------------------- */

/* Create the space.gadget that reserves the drawing area for the minimap. */
Object *minimap_create_gadget(EditorApp *app)
{
    app->minimap = NewObject(SPACE_GetClass(), NULL,
        SPACE_MinWidth, MINIMAP_MIN_WIDTH,
        SPACE_MinHeight, 1,
        SPACE_Transparent, FALSE,
        TAG_END);
    return app->minimap;
}

/* Spawn the background render task and complete the startup handshake. */
int minimap_start(EditorApp *app)
{
    struct Minimap *mm;
    MinimapStartup startup;

    app->minimap_ctx = NULL;
    mm = AllocVec(sizeof(*mm), MEMF_ANY | MEMF_CLEAR);
    if (mm == NULL) return 0;
    mm->reply_port = CreateMsgPort();
    if (mm->reply_port == NULL) { FreeVec(mm); return 0; }
    mm->proc = CreateNewProcTags(
        NP_Entry, (ULONG)minimap_task,
        NP_Name, (ULONG)"AmiEditor Minimap",
        NP_StackSize, 16384,
        NP_Priority, (ULONG)-1,
        TAG_END);
    if (mm->proc == NULL) {
        DeleteMsgPort(mm->reply_port);
        FreeVec(mm);
        return 0;
    }
    memset(&startup, 0, sizeof(startup));
    startup.msg.mn_Node.ln_Type = NT_MESSAGE;
    startup.msg.mn_Length = sizeof(startup);
    startup.msg.mn_ReplyPort = mm->reply_port;
    startup.mm = mm;
    PutMsg(&mm->proc->pr_MsgPort, &startup.msg);
    WaitPort(mm->reply_port);
    GetMsg(mm->reply_port);
    if (!startup.ok) {
        /* The task failed to create its port and has already exited. */
        DeleteMsgPort(mm->reply_port);
        FreeVec(mm);
        return 0;
    }
    mm->ready = 1;
    app->minimap_ctx = mm;
    return 1;
}

/* Wait for any outstanding reply and drain the reply port. */
static void minimap_drain(struct Minimap *mm)
{
    if (!mm->busy) return;
    WaitPort(mm->reply_port);
    while (GetMsg(mm->reply_port) != NULL) ;
    mm->busy = 0;
}

/* Stop the render task and release all shared resources.  Must run before the
 * window (and therefore the space.gadget) is disposed. */
void minimap_stop(EditorApp *app)
{
    struct Minimap *mm = app->minimap_ctx;
    if (mm == NULL) return;
    if (mm->ready && mm->request_port != NULL) {
        minimap_drain(mm);
        if (mm->request.text != NULL) { FreeVec(mm->request.text); mm->request.text = NULL; }
        mm->request.command = MINIMAP_QUIT;
        mm->request.text = NULL;
        mm->request.msg.mn_Node.ln_Type = NT_MESSAGE;
        mm->request.msg.mn_Length = sizeof(mm->request);
        mm->request.msg.mn_ReplyPort = mm->reply_port;
        PutMsg(mm->request_port, &mm->request.msg);
        WaitPort(mm->reply_port);
        while (GetMsg(mm->reply_port) != NULL) ;
    }
    if (mm->reply_port != NULL) DeleteMsgPort(mm->reply_port);
    FreeVec(mm);
    app->minimap_ctx = NULL;
}

/* Toggle the visibility of the minimap column in the content layout.
 *
 * Show/hide works exactly like the folder tree: the minimap child always stays
 * in the layout and is only collapsed to zero width when hidden (never added or
 * removed at runtime, which used to corrupt the cached layout domains and
 * crash, and never by toggling the WeightBar, which left a growing stack of
 * stray bars).  Showing the minimap always uses the default column width; the
 * previous on-screen width is intentionally not remembered or restored. */
void minimap_set_visible(EditorApp *app, int visible)
{
    app->minimap_visible = visible;
    if (app->content_layout == NULL || app->minimap == NULL) return;
    if (app->window != NULL)
        SetGadgetAttrs((struct Gadget *)app->content_layout, app->window, NULL,
            LAYOUT_ModifyChild, (ULONG)app->minimap,
            CHILD_MinWidth, visible ? MINIMAP_MIN_WIDTH : 0,
            CHILD_MaxWidth, visible ? ~0UL : 0,
            CHILD_WeightedWidth, visible ? MINIMAP_DEFAULT_WEIGHT : 0,
            TAG_END);
    else SetAttrs(app->content_layout,
        LAYOUT_ModifyChild, (ULONG)app->minimap,
        CHILD_MinWidth, visible ? MINIMAP_MIN_WIDTH : 0,
        CHILD_MaxWidth, visible ? ~0UL : 0,
        CHILD_WeightedWidth, visible ? MINIMAP_DEFAULT_WEIGHT : 0,
        TAG_END);
    app->minimap_attached = 1;
    ui_relayout(app);
}

/* Flag that the minimap contents are stale and should be re-rendered. */
void minimap_request(EditorApp *app)
{
    struct Minimap *mm = app->minimap_ctx;
    if (mm == NULL || !mm->ready) return;
    mm->dirty = 1;
}

/* Post a render job to the background task if one is warranted.  Rendering is
 * skipped entirely while the minimap is hidden, and only a single job is ever
 * outstanding so updates coalesce to the minimum. */
void minimap_poll(EditorApp *app)
{
    struct Minimap *mm = app->minimap_ctx;
    struct IBox *box = NULL;
    STRPTR text;
    ULONG view_first = 0, view_visible = 1, view_total = 1;
    int i;
    if (mm == NULL || !mm->ready || !app->minimap_visible) return;
    if (app->minimap == NULL || app->window == NULL) return;
    GetAttr(SPACE_AreaBox, app->minimap, (ULONG *)&box);
    if (box == NULL || box->Width <= 0 || box->Height <= 0) return;
    /* Dragging the WeightBar between the tree and the editor changes the layout
     * and resizes the space.gadget without generating a WMHI_NEWSIZE, so notice
     * a changed drawing area here and force a re-render. */
    if (box->Width != mm->last_width || box->Height != mm->last_height) {
        mm->last_width = box->Width;
        mm->last_height = box->Height;
        mm->dirty = 1;
    }
    /* Read the editor's vertical visible range.  A change while the user is not
     * actively dragging a scrollbar means a scrolling operation has finished
     * (keyboard, wheel, arrow or the end of a drag), so re-render the viewport
     * overlay; positions seen mid-drag are ignored so nothing updates during
     * scrolling. */
    if (app->active != NULL) {
        GetAttr(GA_TEXTEDITOR_Prop_First, app->active->editor, &view_first);
        GetAttr(GA_TEXTEDITOR_Prop_Visible, app->active->editor, &view_visible);
        GetAttr(GA_TEXTEDITOR_Prop_Entries, app->active->editor, &view_total);
    }
    if (view_total == 0) view_total = 1;
    if (view_visible == 0) view_visible = 1;
    if (!app->scrolling && (LONG)view_first != mm->last_first) {
        mm->last_first = (LONG)view_first;
        mm->dirty = 1;
    }
    if (!mm->dirty || mm->busy) return;

    text = NULL;
    if (app->active != NULL)
        text = (STRPTR)DoMethod(app->active->editor, GM_TEXTEDITOR_ExportText,
            NULL);

    mm->request.command = MINIMAP_RENDER;
    mm->request.text = (char *)text;
    mm->request.language = app->active != NULL ? app->active->language
                                               : SYNTAX_PLAIN;
    for (i = 0; i < EDITOR_COLOR_COUNT; ++i)
        mm->request.pens[i] = (UWORD)app->editor_pens[i];
    mm->request.background = app->screen_draw_info != NULL
        ? app->screen_draw_info->dri_Pens[BACKGROUNDPEN]
        : (UWORD)app->editor_pens[EDITOR_COLOR_BACKGROUND];
    mm->request.view_fill = app->minimap_view_pen >= 0
        ? (UWORD)app->minimap_view_pen
        : mm->request.background;
    mm->request.view_border = app->screen_draw_info != NULL
        ? app->screen_draw_info->dri_Pens[SHADOWPEN]
        : (UWORD)app->editor_pens[EDITOR_COLOR_TEXT];
    mm->request.view_first = view_first;
    mm->request.view_visible = view_visible;
    mm->request.view_total = view_total;
    mm->request.width = box->Width;
    mm->request.height = box->Height;
    mm->request.friend = app->window->RPort->BitMap;
    mm->request.depth = GetBitMapAttr(app->window->RPort->BitMap, BMA_DEPTH);
    mm->request.result = NULL;
    mm->request.ok = 0;
    mm->request.msg.mn_Node.ln_Type = NT_MESSAGE;
    mm->request.msg.mn_Length = sizeof(mm->request);
    mm->request.msg.mn_ReplyPort = mm->reply_port;
    mm->dirty = 0;
    mm->busy = 1;
    PutMsg(mm->request_port, &mm->request.msg);
}

/* Draw a simple raised bevel around the minimap's drawing area so the
 * space.gadget looks like a framed panel.  The top and left edges use the
 * screen's SHINEPEN and the bottom and right edges use its SHADOWPEN, which is
 * the standard AmigaOS look for a raised border.  It is drawn on the window's
 * RastPort after every blit (the blit fills the whole box first), so it always
 * sits on top of the freshly rendered minimap contents. */
static void minimap_draw_raised_border(EditorApp *app, struct IBox *box)
{
    struct RastPort *rp;
    UWORD shine, shadow;
    WORD left, top, right, bottom;
    if (app->window == NULL || box == NULL) return;
    if (box->Width < 2 || box->Height < 2) return;
    rp = app->window->RPort;
    shine = app->screen_draw_info != NULL
        ? app->screen_draw_info->dri_Pens[SHINEPEN]
        : (UWORD)app->editor_pens[EDITOR_COLOR_TEXT];
    shadow = app->screen_draw_info != NULL
        ? app->screen_draw_info->dri_Pens[SHADOWPEN]
        : (UWORD)app->editor_pens[EDITOR_COLOR_TEXT];
    left = box->Left;
    top = box->Top;
    right = (WORD)(box->Left + box->Width - 1);
    bottom = (WORD)(box->Top + box->Height - 1);
    /* Top and left highlighted edges. */
    SetAPen(rp, (UBYTE)shine);
    Move(rp, left, bottom);
    Draw(rp, left, top);
    Draw(rp, right, top);
    /* Bottom and right shadowed edges. */
    SetAPen(rp, (UBYTE)shadow);
    Draw(rp, right, bottom);
    Draw(rp, left, bottom);
}

/* Signal mask for the render task's replies, folded into the main Wait(). */
ULONG minimap_signal_mask(EditorApp *app)
{
    struct Minimap *mm = app->minimap_ctx;
    if (mm == NULL || !mm->ready || mm->reply_port == NULL) return 0;
    return 1UL << mm->reply_port->mp_SigBit;
}

/* Blit a finished render into the window and dispatch a follow-up job if the
 * contents changed again while this one was being produced. */
void minimap_handle_reply(EditorApp *app)
{
    struct Minimap *mm = app->minimap_ctx;
    struct Message *m;
    if (mm == NULL || mm->reply_port == NULL) return;
    while ((m = GetMsg(mm->reply_port)) != NULL) {
        MinimapRequest *req = (MinimapRequest *)m;
        mm->busy = 0;
        if (app->minimap_visible && req->ok && req->result != NULL &&
            app->window != NULL) {
            struct IBox *box = NULL;
            GetAttr(SPACE_AreaBox, app->minimap, (ULONG *)&box);
            if (box != NULL && box->Width > 0 && box->Height > 0) {
                BltBitMapRastPort(req->result, 0, 0, app->window->RPort,
                    box->Left, box->Top, box->Width, box->Height, 0xC0);
                /* The blit above fills the whole drawing area, so redraw the
                 * raised border on top so the space.gadget keeps its framed
                 * panel look. */
                minimap_draw_raised_border(app, box);
            }
        }
    }
    if (app->minimap_visible && mm->dirty && !mm->busy) minimap_poll(app);
}
