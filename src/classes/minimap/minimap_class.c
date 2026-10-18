/*
 * minimap_class.c - minimap.gadget, a scaled overview of a texteditor.gadget
 *
 * Subclass of gadgetclass. The text of the connected editor is exported on
 * the application's process (OM_SET with MINIMAP_Update) and rendered by a
 * process of the gadget into an off-screen bitmap, one pixel per non-blank
 * character, coloured by an optional hook. Only one job is outstanding at a
 * time. Two bitmaps are used: the front one is shown by GM_RENDER and only
 * touched by the application side, the spare one is handed to the render
 * process with the next job, so a refresh can never draw a bitmap that is
 * being freed or reallocated.
 *
 * Mouse input never touches the editor: the gadget only computes the line
 * the editor should scroll to and sends it with OM_NOTIFY (MINIMAP_Top,
 * MINIMAP_Dragging); the application scrolls on its own process.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>
#include <intuition/icclass.h>
#include <intuition/screens.h>
#include <devices/inputevent.h>
#include <gadgets/texteditor.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <string.h>

#include <gadgets/minimap.h>
#include "classlib.h"

#define MM_DEFAULT_COLUMNS  128
#define MM_MAX_COLUMNS      1024
#define MM_STACK_SIZE       16384

/* Commands of a render job. */
#define MM_CMD_RENDER       0
#define MM_CMD_QUIT         1

/** A render job; the single message reused for every job and the quit. */
typedef struct RenderJob {
    struct Message msg;
    ULONG command;              /**< MM_CMD_# */
    char *text;                 /**< exported text, FreeVec()'d by the task */
    struct Hook *hook;          /**< colour hook or NULL */
    Object *obj;                /**< the minimap, passed to the hook */
    ULONG columns;              /**< text columns mapped onto the width */
    UWORD text_pen;
    UWORD background_pen;
    UWORD view_fill_pen;
    UWORD view_border_pen;
    ULONG view_first;           /**< editor's first visible line */
    ULONG view_visible;         /**< editor's visible lines */
    ULONG view_total;           /**< editor's total lines */
    LONG width;                 /**< size of the picture */
    LONG height;
    ULONG depth;
    struct BitMap *friend_bm;
    struct BitMap *bitmap;      /**< in: buffer to reuse; out: the picture */
    LONG bm_width;              /**< size of bitmap (in and out) */
    LONG bm_height;
    int ok;                     /**< out: bitmap holds a finished picture */
} RenderJob;

/** Startup handshake: the task creates its request port and replies. */
typedef struct RenderStartup {
    struct Message msg;
    struct MsgPort *port;
} RenderStartup;

/** Buffers that belong to the render process. */
typedef struct RenderBuffers {
    char *line;
    ULONG line_size;
    UBYTE *pens;
    ULONG pens_size;
    UWORD *xs;
    ULONG xs_size;
} RenderBuffers;

/** Instance data of a minimap.gadget object. */
typedef struct MinimapData {
    Object *editor;             /**< connected texteditor.gadget or NULL */
    struct Hook *hook;          /**< colour hook or NULL */
    ULONG columns;
    LONG text_pen;              /**< pens, -1 = screen default */
    LONG background_pen;
    LONG view_fill_pen;
    LONG view_border_pen;
    struct Process *proc;       /**< render process or NULL */
    struct MsgPort *reply_port; /**< owned by the application's process */
    struct MsgPort *request_port; /**< owned by the render process */
    RenderJob job;
    int busy;                   /**< a job is outstanding */
    int dirty;                  /**< a new picture is needed */
    struct BitMap *front;       /**< picture shown by GM_RENDER */
    LONG front_width;
    LONG front_height;
    struct BitMap *spare;       /**< buffer for the next job */
    LONG spare_width;
    LONG spare_height;
    LONG last_width;            /**< size of the last job */
    LONG last_height;
    ULONG last_first;           /**< editor state of the last job */
    ULONG last_visible;
    ULONG last_total;
    ULONG top;                  /**< last MINIMAP_Top */
    int dragging;               /**< mouse drag in progress */
} MinimapData;

/* -- libraries ------------------------------------------------------------ */

/**
 * @brief Nothing beyond the frame's libraries is needed.
 * @return TRUE.
 */
BOOL class_open_libraries(void)
{
    return TRUE;
}

/** @brief Nothing to close. */
void class_close_libraries(void)
{
}

/* -- render process ------------------------------------------------------- */

/**
 * @brief Make sure a buffer of the render process has @p need bytes.
 * @param buffer In/out buffer.
 * @param size In/out capacity.
 * @param need Required capacity.
 * @return Non-zero if the buffer is available.
 */
static int grow(void **buffer, ULONG *size, ULONG need)
{
    if (*buffer != NULL && *size >= need) return 1;
    if (*buffer != NULL) FreeVec(*buffer);
    *buffer = AllocVec(need, MEMF_ANY);
    *size = *buffer != NULL ? need : 0;
    return *buffer != NULL;
}

/**
 * @brief Measure one line (LF, CR or CRLF terminated).
 * @param p Start of the line.
 * @param next Out: start of the next line (or the terminating NUL).
 * @return Length of the line without its line end.
 */
static ULONG line_length(const char *p, const char **next)
{
    const char *start = p;
    ULONG length;
    while (*p != '\0' && *p != '\n' && *p != '\r') ++p;
    length = (ULONG)(p - start);
    if (*p == '\r' && p[1] == '\n') p += 2;
    else if (*p != '\0') p += 1;
    *next = p;
    return length;
}

/**
 * @brief Count the lines of the text (at least one).
 * @param text The text, may be NULL.
 * @return The number of lines.
 */
static ULONG count_lines(const char *text)
{
    ULONG lines = 0;
    const char *p = text;
    if (text == NULL || *text == '\0') return 1;
    while (*p != '\0') { line_length(p, &p); ++lines; }
    return lines;
}

/**
 * @brief Render a job into its bitmap (render process, no Intuition).
 *
 * Reuses or allocates the bitmap, clears it, tints the editor's visible
 * range, plots one pixel per non-blank character in the pen chosen by the
 * colour hook, and frames the visible range.
 *
 * @param job The job; bitmap, size and ok are updated.
 * @param rb The render process's buffers.
 */
static void render_job(RenderJob *job, RenderBuffers *rb)
{
    struct RastPort rp;
    const char *p;
    ULONG total_lines, acc = 0, col;
    LONG width = job->width, height = job->height, y = 0;
    LONG view_top = 0, view_bottom = 0;
    int have_view = 0;
    ULONG state = 0;

    job->ok = 0;
    if (width <= 0 || height <= 0) return;
    if (job->bitmap == NULL || job->bm_width != width || job->bm_height != height) {
        if (job->bitmap != NULL) FreeBitMap(job->bitmap);
        job->bitmap = AllocBitMap((ULONG)width, (ULONG)height, job->depth,
                                  BMF_CLEAR, job->friend_bm);
        job->bm_width = job->bitmap != NULL ? width : 0;
        job->bm_height = job->bitmap != NULL ? height : 0;
    }
    if (job->bitmap == NULL) return;
    if (!grow((void **)&rb->xs, &rb->xs_size, job->columns * (ULONG)sizeof(UWORD)))
        return;
    /* Pixel column of every text column, so the loop needs no division. */
    for (col = 0; col < job->columns; ++col)
        rb->xs[col] = (UWORD)((col * (ULONG)width) / job->columns);

    InitRastPort(&rp);
    rp.BitMap = job->bitmap;
    SetRast(&rp, (UBYTE)job->background_pen);

    if (job->view_total > 0 && job->view_visible > 0) {
        view_top = (LONG)((unsigned long long)job->view_first * (ULONG)height /
                          job->view_total);
        view_bottom = (LONG)((unsigned long long)(job->view_first + job->view_visible) *
                             (ULONG)height / job->view_total);
        if (view_bottom > height) view_bottom = height;
        if (view_bottom <= view_top) view_bottom = view_top + 1;
        if (view_bottom > height) view_bottom = height;
        if (view_top >= height) view_top = height - 1;
        have_view = 1;
        SetAPen(&rp, (UBYTE)job->view_fill_pen);
        RectFill(&rp, 0, (WORD)view_top, (WORD)(width - 1), (WORD)(view_bottom - 1));
    }

    total_lines = count_lines(job->text);
    p = job->text != NULL ? job->text : "";
    while (*p != '\0') {
        const char *next;
        ULONG length = line_length(p, &next);
        ULONG limit = length < job->columns ? length : job->columns;
        if (grow((void **)&rb->line, &rb->line_size, length + 1) &&
            grow((void **)&rb->pens, &rb->pens_size, length + 1)) {
            memcpy(rb->line, p, length);
            rb->line[length] = '\0';
            memset(rb->pens, (int)job->text_pen, length + 1);
            if (job->hook != NULL) {
                struct MinimapLineMsg lm;
                lm.mlm_Text = (STRPTR)rb->line;
                lm.mlm_Length = length;
                lm.mlm_Pens = rb->pens;
                lm.mlm_State = state;
                state = CallHookPkt(job->hook, job->obj, &lm);
            }
            for (col = 0; col < limit; ++col) {
                unsigned char c = (unsigned char)rb->line[col];
                if (c == ' ' || c == '\t') continue;
                SetAPen(&rp, rb->pens[col]);
                WritePixel(&rp, (WORD)rb->xs[col], (WORD)(y < height ? y : height - 1));
            }
        }
        p = next;
        /* y = line * height / total_lines, kept as a running fraction. */
        acc += (ULONG)height;
        while (acc >= total_lines) { acc -= total_lines; ++y; }
    }

    if (have_view) {
        SetAPen(&rp, (UBYTE)job->view_border_pen);
        Move(&rp, 0, (WORD)view_top);
        Draw(&rp, (WORD)(width - 1), (WORD)view_top);
        Draw(&rp, (WORD)(width - 1), (WORD)(view_bottom - 1));
        Draw(&rp, 0, (WORD)(view_bottom - 1));
        Draw(&rp, 0, (WORD)view_top);
    }
    job->ok = 1;
}

/**
 * @brief Entry of the render process.
 *
 * Creates its request port during the startup handshake, then renders jobs
 * until MM_CMD_QUIT. Before replying to the quit it frees everything it
 * owns, including the bitmap of the quit message, so the application side
 * can go on as soon as it sees the reply.
 */
static void render_task(void)
{
    struct Process *self = (struct Process *)FindTask(NULL);
    RenderStartup *startup;
    struct MsgPort *port;
    RenderBuffers rb;

    WaitPort(&self->pr_MsgPort);
    startup = (RenderStartup *)GetMsg(&self->pr_MsgPort);
    if (startup == NULL) return;
    port = CreateMsgPort();
    startup->port = port;
    ReplyMsg(&startup->msg);
    if (port == NULL) return;
    memset(&rb, 0, sizeof(rb));

    for (;;) {
        RenderJob *job;
        WaitPort(port);
        while ((job = (RenderJob *)GetMsg(port)) != NULL) {
            if (job->command == MM_CMD_QUIT) {
                if (rb.line != NULL) FreeVec(rb.line);
                if (rb.pens != NULL) FreeVec(rb.pens);
                if (rb.xs != NULL) FreeVec(rb.xs);
                DeleteMsgPort(port);
                /* Forbid() keeps the application side from unloading the
                 * library code before this process has left it. */
                Forbid();
                ReplyMsg(&job->msg);
                return;
            }
            render_job(job, &rb);
            if (job->text != NULL) { FreeVec(job->text); job->text = NULL; }
            ReplyMsg(&job->msg);
        }
    }
}

/* -- application side ----------------------------------------------------- */

/**
 * @brief Take over the result of a finished job.
 * @param d The instance data.
 * @return Non-zero if a new picture is to be shown.
 */
static int collect_job(MinimapData *d)
{
    RenderJob *job = &d->job;
    d->busy = 0;
    if (job->ok) {
        d->spare = d->front;
        d->spare_width = d->front_width;
        d->spare_height = d->front_height;
        d->front = job->bitmap;
        d->front_width = job->bm_width;
        d->front_height = job->bm_height;
    } else {
        d->spare = job->bitmap;
        d->spare_width = job->bm_width;
        d->spare_height = job->bm_height;
    }
    job->bitmap = NULL;
    return job->ok;
}

/**
 * @brief Collect finished jobs without waiting.
 * @param d The instance data.
 * @return Non-zero if a new picture is to be shown.
 */
static int poll_replies(MinimapData *d)
{
    int shown = 0;
    while (GetMsg(d->reply_port) != NULL)
        if (collect_job(d)) shown = 1;
    return shown;
}

/**
 * @brief Wait for an outstanding job and collect it.
 * @param d The instance data.
 */
static void drain(MinimapData *d)
{
    while (d->busy) {
        WaitPort(d->reply_port);
        poll_replies(d);
    }
}

/**
 * @brief Start the render process (lazily, on the first job).
 * @param d The instance data.
 * @return Non-zero if the process runs.
 */
static int start_task(MinimapData *d)
{
    RenderStartup startup;
    if (d->proc != NULL) return 1;
    d->proc = CreateNewProcTags(
        NP_Entry, (ULONG)render_task,
        NP_Name, (ULONG)"minimap.gadget",
        NP_StackSize, MM_STACK_SIZE,
        NP_Priority, (ULONG)-1,
        TAG_END);
    if (d->proc == NULL) return 0;
    memset(&startup, 0, sizeof(startup));
    startup.msg.mn_Node.ln_Type = NT_MESSAGE;
    startup.msg.mn_Length = sizeof(startup);
    startup.msg.mn_ReplyPort = d->reply_port;
    PutMsg(&d->proc->pr_MsgPort, &startup.msg);
    while (GetMsg(d->reply_port) != &startup.msg) WaitPort(d->reply_port);
    d->request_port = startup.port;
    if (d->request_port == NULL) {
        d->proc = NULL;             /* it has already ended */
        return 0;
    }
    return 1;
}

/**
 * @brief Stop the render process and wait until it has ended.
 * @param d The instance data.
 */
static void stop_task(MinimapData *d)
{
    if (d->proc == NULL) return;
    drain(d);
    d->job.command = MM_CMD_QUIT;
    d->job.text = NULL;
    d->job.bitmap = NULL;
    d->job.msg.mn_Node.ln_Type = NT_MESSAGE;
    d->job.msg.mn_Length = sizeof(d->job);
    d->job.msg.mn_ReplyPort = d->reply_port;
    PutMsg(d->request_port, &d->job.msg);
    while (GetMsg(d->reply_port) != &d->job.msg) WaitPort(d->reply_port);
    d->proc = NULL;
    d->request_port = NULL;
}

/**
 * @brief Resolve a pen attribute (-1 = the screen's pen).
 * @param pen The attribute value.
 * @param dri The screen's DrawInfo, may be NULL.
 * @param which The DrawInfo pen index used for -1.
 * @return The pen number.
 */
static UWORD resolve_pen(LONG pen, struct DrawInfo *dri, UWORD which)
{
    if (pen >= 0) return (UWORD)pen;
    if (dri != NULL && which < dri->dri_NumPens) return dri->dri_Pens[which];
    return which == BACKGROUNDPEN ? 0 : 1;
}

/**
 * @brief Read the editor's scroll state.
 * @param d The instance data.
 * @param first Out: first visible line.
 * @param visible Out: visible lines (at least 1).
 * @param total Out: total lines (at least 1).
 */
static void editor_view(MinimapData *d, ULONG *first, ULONG *visible, ULONG *total)
{
    *first = 0; *visible = 1; *total = 1;
    if (d->editor != NULL) {
        GetAttr(GA_TEXTEDITOR_Prop_First, d->editor, first);
        GetAttr(GA_TEXTEDITOR_Prop_Visible, d->editor, visible);
        GetAttr(GA_TEXTEDITOR_Prop_Entries, d->editor, total);
    }
    if (*visible == 0) *visible = 1;
    if (*total == 0) *total = 1;
}

/**
 * @brief Export the editor's text and send a render job.
 * @param o The object.
 * @param d The instance data.
 * @param gi GadgetInfo (screen, DrawInfo).
 */
static void post_job(Object *o, MinimapData *d, struct GadgetInfo *gi)
{
    struct Gadget *g = (struct Gadget *)o;
    struct DrawInfo *dri = gi->gi_DrInfo;
    RenderJob *job = &d->job;
    UWORD background;

    if (!start_task(d)) return;
    memset(job, 0, sizeof(*job));
    if (d->editor != NULL) {
        struct GP_TEXTEDITOR_ExportText msg;
        msg.MethodID = GM_TEXTEDITOR_ExportText;
        msg.GInfo = gi;
        job->text = (char *)class_dm(d->editor, (Msg)&msg);
    }
    editor_view(d, &job->view_first, &job->view_visible, &job->view_total);
    d->last_first = job->view_first;
    d->last_visible = job->view_visible;
    d->last_total = job->view_total;
    d->last_width = g->Width;
    d->last_height = g->Height;

    background = resolve_pen(d->background_pen, dri, BACKGROUNDPEN);
    job->command = MM_CMD_RENDER;
    job->hook = d->hook;
    job->obj = o;
    job->columns = d->columns;
    job->text_pen = resolve_pen(d->text_pen, dri, TEXTPEN);
    job->background_pen = background;
    job->view_fill_pen = d->view_fill_pen >= 0 ? (UWORD)d->view_fill_pen : background;
    job->view_border_pen = resolve_pen(d->view_border_pen, dri, SHADOWPEN);
    job->width = g->Width;
    job->height = g->Height;
    job->friend_bm = gi->gi_Screen->RastPort.BitMap;
    job->depth = GetBitMapAttr(job->friend_bm, BMA_DEPTH);
    job->bitmap = d->spare;
    job->bm_width = d->spare_width;
    job->bm_height = d->spare_height;
    d->spare = NULL;
    d->spare_width = d->spare_height = 0;
    job->msg.mn_Node.ln_Type = NT_MESSAGE;
    job->msg.mn_Length = sizeof(*job);
    job->msg.mn_ReplyPort = d->reply_port;
    d->dirty = 0;
    d->busy = 1;
    PutMsg(d->request_port, &job->msg);
}

/**
 * @brief Redraw the gadget from OM_SET (only for objects of this class;
 *        a subclass renders itself, as with any BOOPSI class).
 * @param cl The class.
 * @param o The object.
 * @param gi GadgetInfo, NULL when not in a window.
 */
static void render_self(Class *cl, Object *o, struct GadgetInfo *gi)
{
    struct RastPort *rp;
    struct gpRender render;
    if (gi == NULL || OCLASS(o) != cl) return;
    rp = ObtainGIRPort(gi);
    if (rp == NULL) return;
    render.MethodID = GM_RENDER;
    render.gpr_GInfo = gi;
    render.gpr_RPort = rp;
    render.gpr_Redraw = GREDRAW_UPDATE;
    class_dm(o, (Msg)&render);
    ReleaseGIRPort(rp);
}

/**
 * @brief MINIMAP_Update: show a finished picture, start a job if needed.
 * @param cl The class.
 * @param o The object.
 * @param d The instance data.
 * @param gi GadgetInfo of the OM_SET.
 */
static void update(Class *cl, Object *o, MinimapData *d, struct GadgetInfo *gi)
{
    struct Gadget *g = (struct Gadget *)o;
    ULONG first, visible, total;
    if (poll_replies(d)) render_self(cl, o, gi);
    if (gi == NULL || g->Width <= 0 || g->Height <= 0) return;
    if (g->Width != d->last_width || g->Height != d->last_height) d->dirty = 1;
    editor_view(d, &first, &visible, &total);
    if (first != d->last_first || visible != d->last_visible || total != d->last_total)
        d->dirty = 1;
    if (d->dirty && !d->busy) post_job(o, d, gi);
}

/* -- input ---------------------------------------------------------------- */

/**
 * @brief Line the editor should show at its top for a pointer position:
 *        the visible part is centred on the pointer.
 * @param o The object.
 * @param d The instance data.
 * @param mouse_y Pointer position relative to the gadget.
 * @return The first line.
 */
static ULONG top_for_mouse(Object *o, MinimapData *d, LONG mouse_y)
{
    struct Gadget *g = (struct Gadget *)o;
    ULONG first, visible, total;
    LONG top;
    editor_view(d, &first, &visible, &total);
    if (g->Height <= 0) return first;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= g->Height) mouse_y = g->Height - 1;
    top = (LONG)(((unsigned long long)(ULONG)mouse_y * total) / (ULONG)(UWORD)g->Height);
    top -= (LONG)visible / 2;
    if ((ULONG)top + visible > total) top = (LONG)total - (LONG)visible;
    if (top < 0) top = 0;
    return (ULONG)top;
}

/**
 * @brief Send MINIMAP_Top and MINIMAP_Dragging to the ICA_TARGET.
 * @param o The object.
 * @param d The instance data.
 * @param gi GadgetInfo of the input method.
 * @param flags OPUF_INTERIM during the drag, 0 at its end.
 */
static void notify(Object *o, MinimapData *d, struct GadgetInfo *gi, ULONG flags)
{
    struct TagItem tags[4];
    struct opUpdate msg;
    tags[0].ti_Tag = MINIMAP_Top;      tags[0].ti_Data = d->top;
    tags[1].ti_Tag = MINIMAP_Dragging; tags[1].ti_Data = (ULONG)d->dragging;
    tags[2].ti_Tag = GA_ID;            tags[2].ti_Data = ((struct Gadget *)o)->GadgetID;
    tags[3].ti_Tag = TAG_END;          tags[3].ti_Data = 0;
    msg.MethodID = OM_NOTIFY;
    msg.opu_AttrList = tags;
    msg.opu_GInfo = gi;
    msg.opu_Flags = flags;
    class_dm(o, (Msg)&msg);
}

/**
 * @brief GM_GOACTIVE: a click starts a drag.
 * @param o The object.
 * @param d The instance data.
 * @param gpi The message.
 * @return GMR_MEACTIVE, or GMR_NOREUSE without a mouse click or editor.
 */
static ULONG go_active(Object *o, MinimapData *d, struct gpInput *gpi)
{
    if (gpi->gpi_IEvent == NULL || d->editor == NULL) return GMR_NOREUSE;
    d->dragging = 1;
    d->top = top_for_mouse(o, d, gpi->gpi_Mouse.Y);
    notify(o, d, gpi->gpi_GInfo, OPUF_INTERIM);
    return GMR_MEACTIVE;
}

/**
 * @brief GM_HANDLEINPUT: follow the pointer, end on button release.
 * @param o The object.
 * @param d The instance data.
 * @param gpi The message.
 * @return GMR_MEACTIVE while dragging, GMR_NOREUSE|GMR_VERIFY at the end.
 */
static ULONG handle_input(Object *o, MinimapData *d, struct gpInput *gpi)
{
    struct InputEvent *ie = gpi->gpi_IEvent;
    if (ie == NULL || ie->ie_Class != IECLASS_RAWMOUSE) return GMR_MEACTIVE;
    if (ie->ie_Code == SELECTUP) {
        d->dragging = 0;
        notify(o, d, gpi->gpi_GInfo, 0);
        *gpi->gpi_Termination = (LONG)d->top;
        return GMR_NOREUSE | GMR_VERIFY;
    }
    if (ie->ie_Code == MENUDOWN) {
        d->dragging = 0;
        notify(o, d, gpi->gpi_GInfo, 0);
        return GMR_NOREUSE;
    }
    {
        ULONG top = top_for_mouse(o, d, gpi->gpi_Mouse.Y);
        if (top != d->top) {
            d->top = top;
            notify(o, d, gpi->gpi_GInfo, OPUF_INTERIM);
        }
    }
    return GMR_MEACTIVE;
}

/* -- methods -------------------------------------------------------------- */

/**
 * @brief GM_RENDER: blit the current picture and draw a raised bevel.
 * @param o The object.
 * @param d The instance data.
 * @param gpr The message.
 * @return 0.
 */
static ULONG render(Object *o, MinimapData *d, struct gpRender *gpr)
{
    struct Gadget *g = (struct Gadget *)o;
    struct RastPort *rp = gpr->gpr_RPort;
    struct DrawInfo *dri = gpr->gpr_GInfo != NULL ? gpr->gpr_GInfo->gi_DrInfo : NULL;
    WORD left = g->LeftEdge, top = g->TopEdge, w = g->Width, h = g->Height;
    WORD right = (WORD)(left + w - 1), bottom = (WORD)(top + h - 1);
    WORD bw = 0, bh = 0;
    if (rp == NULL || w <= 0 || h <= 0) return 0;
    if (d->front != NULL) {
        bw = (WORD)(d->front_width < w ? d->front_width : w);
        bh = (WORD)(d->front_height < h ? d->front_height : h);
        BltBitMapRastPort(d->front, 0, 0, rp, left, top, bw, bh, 0xC0);
    }
    SetAPen(rp, (UBYTE)resolve_pen(d->background_pen, dri, BACKGROUNDPEN));
    if (bw < w) RectFill(rp, (WORD)(left + bw), top, right, bottom);
    if (bh < h && bw > 0) RectFill(rp, left, (WORD)(top + bh), (WORD)(left + bw - 1), bottom);
    if (w >= 2 && h >= 2) {
        SetAPen(rp, (UBYTE)resolve_pen(-1, dri, SHINEPEN));
        Move(rp, left, bottom);
        Draw(rp, left, top);
        Draw(rp, right, top);
        SetAPen(rp, (UBYTE)resolve_pen(-1, dri, SHADOWPEN));
        Draw(rp, right, bottom);
        Draw(rp, left, bottom);
    }
    return 0;
}

/**
 * @brief GM_DOMAIN: minimum, nominal and maximum size.
 * @param gpd The message.
 * @return TRUE.
 */
static ULONG domain(struct gpDomain *gpd)
{
    gpd->gpd_Domain.Left = gpd->gpd_Domain.Top = 0;
    switch (gpd->gpd_Which) {
        case GDOMAIN_MINIMUM: gpd->gpd_Domain.Width = 8; gpd->gpd_Domain.Height = 8; break;
        case GDOMAIN_NOMINAL: gpd->gpd_Domain.Width = 96; gpd->gpd_Domain.Height = 96; break;
        default:              gpd->gpd_Domain.Width = 16384; gpd->gpd_Domain.Height = 16384; break;
    }
    return TRUE;
}

/**
 * @brief OM_DISPOSE: stop the render process and free everything.
 * @param cl The class.
 * @param o The object.
 * @param msg The message.
 * @return The superclass result.
 */
static ULONG om_dispose(Class *cl, Object *o, Msg msg)
{
    MinimapData *d = INST_DATA(cl, o);
    stop_task(d);
    if (d->front != NULL) FreeBitMap(d->front);
    if (d->spare != NULL) FreeBitMap(d->spare);
    d->front = d->spare = NULL;
    if (d->reply_port != NULL) DeleteMsgPort(d->reply_port);
    d->reply_port = NULL;
    return class_dsm(cl, o, msg);
}

/**
 * @brief Apply the class's attributes from a tag list (OM_NEW and OM_SET).
 * @param cl The class.
 * @param o The object.
 * @param d The instance data.
 * @param tags The tag list.
 * @param gi GadgetInfo, NULL for OM_NEW.
 */
static void set_attrs(Class *cl, Object *o, MinimapData *d, struct TagItem *tags,
                      struct GadgetInfo *gi)
{
    struct TagItem *state = tags, *tag;
    int do_update = 0;
    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag) {
            case MINIMAP_TextEditor:
                if ((Object *)tag->ti_Data != d->editor) {
                    drain(d);
                    d->editor = (Object *)tag->ti_Data;
                    d->dirty = 1;
                }
                break;
            case MINIMAP_ColorHook:
                if ((struct Hook *)tag->ti_Data != d->hook) {
                    drain(d);
                    d->hook = (struct Hook *)tag->ti_Data;
                    d->dirty = 1;
                }
                break;
            case MINIMAP_TextPen:       d->text_pen = (LONG)tag->ti_Data; d->dirty = 1; break;
            case MINIMAP_BackgroundPen: d->background_pen = (LONG)tag->ti_Data; d->dirty = 1; break;
            case MINIMAP_ViewFillPen:   d->view_fill_pen = (LONG)tag->ti_Data; d->dirty = 1; break;
            case MINIMAP_ViewBorderPen: d->view_border_pen = (LONG)tag->ti_Data; d->dirty = 1; break;
            case MINIMAP_TextChanged:   if (tag->ti_Data) d->dirty = 1; break;
            case MINIMAP_Update:        if (tag->ti_Data) do_update = 1; break;
            default: break;
        }
    }
    if (do_update) update(cl, o, d, gi);
}

/**
 * @brief OM_NEW: create the gadget and its reply port.
 * @param cl The class.
 * @param o The class object.
 * @param ops The message.
 * @return The new object, or NULL.
 */
static ULONG om_new(Class *cl, Object *o, struct opSet *ops)
{
    Object *obj = (Object *)class_dsm(cl, o, (Msg)ops);
    MinimapData *d;
    if (obj == NULL) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof(*d));
    d->columns = GetTagData(MINIMAP_Columns, MM_DEFAULT_COLUMNS, ops->ops_AttrList);
    if (d->columns == 0) d->columns = 1;
    if (d->columns > MM_MAX_COLUMNS) d->columns = MM_MAX_COLUMNS;
    d->text_pen = d->background_pen = d->view_fill_pen = d->view_border_pen = -1;
    d->dirty = 1;
    d->reply_port = CreateMsgPort();
    if (d->reply_port == NULL) {
        struct opSet dispose;
        dispose.MethodID = OM_DISPOSE;
        om_dispose(cl, obj, (Msg)&dispose);
        return 0;
    }
    set_attrs(cl, obj, d, ops->ops_AttrList, NULL);
    return (ULONG)obj;
}

/**
 * @brief OM_GET.
 * @param cl The class.
 * @param o The object.
 * @param opg The message.
 * @return TRUE if the attribute is known.
 */
static ULONG om_get(Class *cl, Object *o, struct opGet *opg)
{
    MinimapData *d = INST_DATA(cl, o);
    switch (opg->opg_AttrID) {
        case MINIMAP_TextEditor: *opg->opg_Storage = (ULONG)d->editor; return TRUE;
        case MINIMAP_Columns:    *opg->opg_Storage = d->columns; return TRUE;
        case MINIMAP_SignalMask: *opg->opg_Storage = 1UL << d->reply_port->mp_SigBit; return TRUE;
        case MINIMAP_Top:        *opg->opg_Storage = d->top; return TRUE;
        case MINIMAP_Dragging:   *opg->opg_Storage = (ULONG)d->dragging; return TRUE;
        default:                 return class_dsm(cl, o, (Msg)opg);
    }
}

/**
 * @brief Class dispatcher.
 * @param cl The class.
 * @param o The object (the class for OM_NEW).
 * @param msg The message.
 * @return The method's result.
 */
static ULONG minimap_dispatcher(Class *cl __asm("a0"), Object *o __asm("a2"),
                                Msg msg __asm("a1"))
{
    switch (msg->MethodID) {
        case OM_NEW:
            return om_new(cl, o, (struct opSet *)msg);
        case OM_SET: {
            /* Only OM_SET: OM_UPDATE may come from input handling, where
             * the editor must not be read and no process started. */
            ULONG result = class_dsm(cl, o, msg);
            set_attrs(cl, o, INST_DATA(cl, o), ((struct opSet *)msg)->ops_AttrList,
                      ((struct opSet *)msg)->ops_GInfo);
            return result;
        }
        case OM_GET:
            return om_get(cl, o, (struct opGet *)msg);
        case OM_DISPOSE:
            return om_dispose(cl, o, msg);
        case GM_DOMAIN:
            return domain((struct gpDomain *)msg);
        case GM_RENDER:
            return render(o, INST_DATA(cl, o), (struct gpRender *)msg);
        case GM_GOACTIVE:
            return go_active(o, INST_DATA(cl, o), (struct gpInput *)msg);
        case GM_HANDLEINPUT:
            return handle_input(o, INST_DATA(cl, o), (struct gpInput *)msg);
        case GM_GOINACTIVE: {
            MinimapData *d = INST_DATA(cl, o);
            if (d->dragging) {
                d->dragging = 0;
                notify(o, d, ((struct gpGoInactive *)msg)->gpgi_GInfo, 0);
            }
            return class_dsm(cl, o, msg);
        }
        default:
            return class_dsm(cl, o, msg);
    }
}

/**
 * @brief Create the minimap.gadget class (subclass of gadgetclass).
 * @param name The public class name.
 * @return The class, or NULL.
 */
Class *class_create(const char *name)
{
    Class *cl = MakeClass((STRPTR)name, (STRPTR)"gadgetclass", NULL,
                          sizeof(MinimapData), 0);
    if (cl != NULL) cl->cl_Dispatcher.h_Entry = (ULONG (*)())minimap_dispatcher;
    return cl;
}
