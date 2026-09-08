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
    /* The render task allocates and fills these; the main task additionally
     * reads bitmap/bm_width/bm_height from the SPACE_RenderHook to repaint the
     * last render on a layout refresh.  Only one render is outstanding at a
     * time, so a hook firing mid-render can at worst show a harmless transient
     * that the completing render immediately supersedes. */
    struct BitMap *bitmap;
    LONG bm_width;
    LONG bm_height;
    /* Last drawing-area position and size seen by the main task, used to notice
     * layout changes (e.g. WeightBar drags) that move or resize the
     * space.gadget without a WMHI_NEWSIZE event.  The position must be tracked
     * as well as the size: dragging the left WeightBar (between the folder tree
     * and the editor) shifts the minimap column sideways without changing its
     * width or height, so watching the size alone would miss it and leave the
     * moved drawing area cleared to its grey background. */
    LONG last_left;
    LONG last_top;
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

/**
 * @brief Syntax emit callback that records the style of each character.
 *
 * Invoked by the syntax scanner for every styled span of a line.  It writes
 * the span's style value into the per-character style buffer supplied through
 * the FillContext, clamping the span end to the buffer length so it never
 * writes past the end of the buffer.
 *
 * @param context Opaque pointer to a FillContext holding the style buffer and
 *                its length.
 * @param span    Styled span describing the [start, end) character range and
 *                the style to apply.
 */
static void minimap_fill_styles(void *context, const SyntaxSpan *span)
{
    FillContext *fc = (FillContext *)context;
    size_t i, end = span->end;
    if (end > fc->length) end = fc->length;
    for (i = span->start; i < end; ++i)
        fc->styles[i] = (unsigned char)span->style;
}

/**
 * @brief Map a syntax style onto the matching reserved editor colour pen.
 *
 * Translates a SyntaxStyle value into the corresponding pen from the request's
 * pen table, falling back to the plain text pen for any unrecognised style.
 *
 * @param req   Render request carrying the resolved editor colour pens.
 * @param style Syntax style value to translate.
 * @return The pen number to use when drawing characters of that style.
 */
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

/**
 * @brief Advance over a single physical line, honouring LF, CR and CRLF.
 *
 * Scans from @p p up to the next line terminator or the terminating NUL,
 * treating LF, CR and CRLF sequences as line endings, and reports where the
 * following line begins.
 *
 * @param p    Pointer to the start of the current line.
 * @param next Output pointer set to the start of the next line, or to the
 *             terminating NUL when the last line has been reached.
 * @return The length of the line in characters, excluding its line ending.
 */
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

/**
 * @brief Count the physical lines in the text snapshot.
 *
 * Walks the whole snapshot line by line so line numbers can later be scaled
 * onto the available pixel height.  An empty or NULL snapshot is reported as a
 * single line, and the result is never zero.
 *
 * @param text The document text snapshot, which may be NULL.
 * @return The number of physical lines, always at least one.
 */
static ULONG minimap_count_lines(const char *text)
{
    ULONG lines = 0;
    const char *p = text;
    if (text == NULL || *text == '\0') return 1;
    while (*p != '\0') { minimap_line_length(p, &p); ++lines; }
    return lines != 0 ? lines : 1;
}

/**
 * @brief Ensure a reusable buffer is at least @p need bytes large.
 *
 * If the current buffer is already big enough it is kept; otherwise any
 * existing allocation is freed and a fresh one is made with AllocVec().  On
 * failure the size is reset to zero.
 *
 * @param buffer In/out pointer to the buffer allocation, updated on reallocation.
 * @param size   In/out current capacity of the buffer, updated to match.
 * @param need   Required minimum capacity in bytes.
 * @return 1 if the buffer is available at the requested size, 0 on allocation
 *         failure.
 */
static int minimap_grow(void **buffer, size_t *size, size_t need)
{
    if (*size >= need && *buffer != NULL) return 1;
    if (*buffer != NULL) FreeVec(*buffer);
    *buffer = AllocVec((ULONG)need, MEMF_ANY);
    *size = *buffer != NULL ? need : 0;
    return *buffer != NULL;
}

/**
 * @brief Render the exported document text into the off-screen bitmap.
 *
 * Runs entirely in the render task and never touches Intuition.  It allocates
 * or reuses the off-screen bitmap sized to the request, clears it to the
 * editor's background pen, tints and frames the viewport rectangle for the
 * currently visible region, then plots one scaled pixel per non-blank source
 * character coloured by its syntax style.  On success it stores the bitmap in
 * @p req->result and sets @p req->ok; on any failure result stays NULL and ok
 * stays 0.
 *
 * @param mm  Shared minimap state owning the reusable bitmap and line buffers.
 * @param req Render request describing the text, colours, dimensions and
 *            viewport, and receiving the result bitmap and ok flag.
 */
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

/**
 * @brief Entry point of the background render task.
 *
 * Waits for the startup handshake message, creates the task's request port and
 * replies with success or failure.  It then loops servicing render requests
 * until a MINIMAP_QUIT command arrives, freeing each request's exported text
 * after use.  On quit it frees everything the task owns (bitmap, line and style
 * buffers, request port) before replying to the quit message so the main task
 * can safely tear down the shared state.
 */
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

/* Draw a raised bevel around the minimap drawing area on the given RastPort. */
static void minimap_draw_raised_border(EditorApp *app, struct RastPort *rp,
                                       struct IBox *box);

/**
 * @brief SPACE_RenderHook callback for the minimap space.gadget.
 *
 * Invoked by the gadget whenever it refreshes, including the relayouts
 * triggered while a WeightBar is dragged, when the layout would otherwise clear
 * the space.gadget to its grey background.  It synchronously blits the last
 * rendered minimap bitmap (clamped to the drawing box) into the refresh
 * RastPort and redraws the raised border, keeping the minimap visible during
 * and after any layout change without racing the layout.
 *
 * @param hook The hook whose h_Data points to the EditorApp.
 * @param obj  The space.gadget object being refreshed (unused).
 * @param gpr  The gadget render message carrying the target RastPort.
 */
static void minimap_space_render(struct Hook *hook, Object *obj,
                                 struct gpRender *gpr)
{
    EditorApp *app = (EditorApp *)hook->h_Data;
    struct Minimap *mm;
    struct RastPort *rp;
    struct IBox *box = NULL;
    (void)obj;
    if (app == NULL || gpr == NULL || app->minimap == NULL) return;
    rp = gpr->gpr_RPort;
    if (rp == NULL) return;
    GetAttr(SPACE_AreaBox, app->minimap, (ULONG *)&box);
    if (box == NULL || box->Width <= 0 || box->Height <= 0) return;
    mm = app->minimap_ctx;
    if (mm != NULL && mm->bitmap != NULL && mm->bm_width > 0 &&
        mm->bm_height > 0) {
        LONG w = box->Width, h = box->Height;
        if (w > mm->bm_width) w = mm->bm_width;
        if (h > mm->bm_height) h = mm->bm_height;
        BltBitMapRastPort(mm->bitmap, 0, 0, rp, box->Left, box->Top,
            (WORD)w, (WORD)h, 0xC0);
    }
    minimap_draw_raised_border(app, rp, box);
}

/**
 * @brief Create the space.gadget that reserves the minimap drawing area.
 *
 * Initialises the app's render hook (pointing at minimap_space_render) and
 * creates a plain, passive space.gadget that renders through that hook.  The
 * gadget is left non-interactive so that clicks over the minimap fall through
 * to the window as IDCMP_MOUSEBUTTONS events, which the drag handling relies
 * on.
 *
 * @param app The editor application whose minimap gadget and render hook are
 *            set up.
 * @return The created space.gadget object, or NULL on failure.
 */
Object *minimap_create_gadget(EditorApp *app)
{
    /* The space.gadget is left as a plain, passive spacer (no GA_RelVerify /
     * GA_Immediate / GA_FollowMouse).  A bare space.gadget does not consume the
     * mouse button, so a click over the minimap area falls through to the
     * window as an IDCMP_MOUSEBUTTONS event, which is what the drag handling
     * relies on.  Making it interactive here proved unreliable: the spacer did
     * not deliver a dependable WMHI_GADGETUP for GID_MINIMAP, so the drag never
     * ended and later mouse moves from other active gadgets scrolled the editor
     * from unrelated places in the UI. */
    memset(&app->minimap_render_hook, 0, sizeof(app->minimap_render_hook));
    app->minimap_render_hook.h_Entry = (ULONG (*)())HookEntry;
    app->minimap_render_hook.h_SubEntry = (ULONG (*)())minimap_space_render;
    app->minimap_render_hook.h_Data = app;
    app->minimap = NewObject(SPACE_GetClass(), NULL,
        GA_ID, GID_MINIMAP,
        SPACE_MinWidth, MINIMAP_MIN_WIDTH,
        SPACE_MinHeight, 1,
        SPACE_Transparent, FALSE,
        SPACE_RenderHook, (ULONG)&app->minimap_render_hook,
        TAG_END);
    return app->minimap;
}

/**
 * @brief Scroll the active document to the minimap line under the cursor.
 *
 * Maps the window-relative mouse Y coordinate onto a document line and scrolls
 * the active editor so its visible region is centred on that line, clamping to
 * the valid range.  Only the vertical position changes; horizontal scroll is
 * untouched.  Runs on the main task and refreshes the page gadget and
 * scrollbars directly.
 *
 * @param app     The editor application owning the active document and window.
 * @param mouse_y Window-relative mouse Y coordinate to scroll to.
 */
static void minimap_scroll_to(EditorApp *app, LONG mouse_y)
{
    Document *doc = app->active;
    struct IBox *box = NULL;
    ULONG total = 1, visible = 1;
    LONG rel, first;
    if (doc == NULL || app->minimap == NULL || app->window == NULL) return;
    GetAttr(SPACE_AreaBox, app->minimap, (ULONG *)&box);
    if (box == NULL || box->Height <= 0) return;
    GetAttr(GA_TEXTEDITOR_Prop_Entries, doc->editor, &total);
    GetAttr(GA_TEXTEDITOR_Prop_Visible, doc->editor, &visible);
    if (total == 0) total = 1;
    if (visible == 0) visible = 1;
    rel = mouse_y - box->Top;
    if (rel < 0) rel = 0;
    if (rel >= box->Height) rel = box->Height - 1;
    first = (LONG)(((unsigned long long)(ULONG)rel * total) /
                   (unsigned long long)(ULONG)box->Height);
    first -= (LONG)visible / 2;      /* centre the viewport on the cursor */
    if ((ULONG)first + visible > total) first = (LONG)total - (LONG)visible;
    if (first < 0) first = 0;
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
        GA_TEXTEDITOR_Prop_First, (ULONG)first, TAG_END);
    RefreshPageGadget((struct Gadget *)doc->page, app->pages, app->window, NULL);
    document_sync_scrollers(app, doc);
}

/**
 * @brief Report whether a window-relative point lies inside the minimap.
 *
 * Queries the space.gadget's current drawing box and tests the point against
 * it.
 *
 * @param app The editor application owning the minimap gadget.
 * @param mx  Window-relative X coordinate of the point.
 * @param my  Window-relative Y coordinate of the point.
 * @return 1 if the point is inside the minimap drawing area, 0 otherwise.
 */
static int minimap_point_inside(EditorApp *app, WORD mx, WORD my)
{
    struct IBox *box = NULL;
    if (app->minimap == NULL) return 0;
    GetAttr(SPACE_AreaBox, app->minimap, (ULONG *)&box);
    if (box == NULL) return 0;
    return mx >= box->Left && mx < box->Left + box->Width &&
           my >= box->Top && my < box->Top + box->Height;
}

/**
 * @brief Handle an IDCMP_MOUSEBUTTONS event delivered to the window.
 *
 * A left-button press (SELECTDOWN) inside the minimap area starts a drag: the
 * highlighter is suspended for speed, ReportMouse() is enabled so the window
 * delivers IDCMP_MOUSEMOVE events for the drag, and the editor jumps to the
 * clicked line.  The matching release (SELECTUP) ends the drag, disables
 * ReportMouse(), restores the highlighter and requests a fresh render so the
 * viewport overlay is redrawn only after the drag.  Anchoring the drag to real
 * button events keeps unrelated clicks elsewhere in the UI from triggering a
 * phantom scroll.
 *
 * @param app  The editor application whose window and drag state are updated.
 * @param code The IDCMP_MOUSEBUTTONS code, either SELECTDOWN or SELECTUP.
 */
void minimap_handle_buttons(EditorApp *app, UWORD code)
{
    if (app->window == NULL) return;
    if (code == SELECTDOWN) {
        if (!app->minimap_visible || app->active == NULL) return;
        if (!minimap_point_inside(app, app->window->MouseX, app->window->MouseY))
            return;
        app->minimap_dragging = 1;
        document_suspend_highlight(app);
        ReportMouse(TRUE, app->window);
        minimap_scroll_to(app, app->window->MouseY);
    } else if (code == SELECTUP) {
        if (!app->minimap_dragging) return;
        app->minimap_dragging = 0;
        ReportMouse(FALSE, app->window);
        document_resume_highlight(app);
        minimap_request(app);
    }
}

/**
 * @brief Handle an IDCMP_MOUSEMOVE event for minimap dragging.
 *
 * While a minimap drag is in progress (started by minimap_handle_buttons) the
 * editor is scrolled to follow the cursor.  Moves at any other time are ignored
 * so the minimap only reacts to a genuine drag that began with a press inside
 * it.
 *
 * @param app The editor application whose active document may be scrolled.
 */
void minimap_handle_mouse(EditorApp *app)
{
    if (!app->minimap_dragging) return;
    if (app->window == NULL || app->active == NULL) return;
    minimap_scroll_to(app, app->window->MouseY);
}

/**
 * @brief Spawn the background render task and complete the startup handshake.
 *
 * Allocates the shared Minimap state and its reply port, launches the render
 * process, and exchanges the startup message so the task can create its request
 * port.  On any failure all partially acquired resources are released and the
 * app's minimap context is left NULL.
 *
 * @param app The editor application receiving the created minimap context.
 * @return 1 if the render task started successfully, 0 on failure.
 */
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

/**
 * @brief Wait for any outstanding reply and drain the reply port.
 *
 * If a render job is in flight, blocks until its reply arrives, removes all
 * pending messages from the reply port and clears the busy flag.  Does nothing
 * when no job is outstanding.
 *
 * @param mm The shared minimap state whose reply port is drained.
 */
static void minimap_drain(struct Minimap *mm)
{
    if (!mm->busy) return;
    WaitPort(mm->reply_port);
    while (GetMsg(mm->reply_port) != NULL) ;
    mm->busy = 0;
}

/**
 * @brief Stop the render task and release all shared resources.
 *
 * Drains any in-flight job, sends a MINIMAP_QUIT request and waits for the task
 * to acknowledge it (by which point the task has freed its own resources), then
 * deletes the reply port and frees the shared state.  Must run before the
 * window (and therefore the space.gadget) is disposed.
 *
 * @param app The editor application whose minimap context is torn down and set
 *            to NULL.
 */
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

/**
 * @brief Toggle the visibility of the minimap column in the content layout.
 *
 * Show/hide works like the folder tree: the minimap child always stays in the
 * layout and is only collapsed to zero width when hidden, never added or
 * removed at runtime and never via the WeightBar.  Showing the minimap always
 * uses the default column width; the previous on-screen width is intentionally
 * not remembered.  Applies the change live when a window exists, otherwise sets
 * it on the layout directly, and triggers a relayout.
 *
 * @param app     The editor application whose content layout is updated.
 * @param visible Non-zero to show the minimap column, zero to collapse it.
 */
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

/**
 * @brief Flag that the minimap contents are stale and need re-rendering.
 *
 * Marks the shared state dirty so the next poll posts a fresh render job.  Does
 * nothing if the minimap context is absent or the render task is not ready.
 *
 * @param app The editor application whose minimap is marked dirty.
 */
void minimap_request(EditorApp *app)
{
    struct Minimap *mm = app->minimap_ctx;
    if (mm == NULL || !mm->ready) return;
    mm->dirty = 1;
}

/**
 * @brief Post a render job to the background task when one is warranted.
 *
 * Skipped entirely while the minimap is hidden.  Detects layout moves/resizes
 * of the drawing area and finished (non-dragging) scroll changes, marking the
 * state dirty accordingly.  When dirty and no job is outstanding, it exports a
 * private snapshot of the active document, fills the shared request with the
 * current colours, viewport and dimensions, and posts it to the render task so
 * only a single job is ever in flight at a time.
 *
 * @param app The editor application whose active document is snapshotted and
 *            queued for rendering.
 */
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
    /* Dragging a WeightBar changes the layout and moves and/or resizes the
     * space.gadget without generating a WMHI_NEWSIZE, so notice a changed
     * drawing area here and force a re-render.  Both the origin and the size
     * are checked: the right WeightBar (between the editor and the minimap)
     * changes the width, while the left WeightBar (between the folder tree and
     * the editor) only shifts the column sideways, leaving the width and height
     * unchanged - watching the size alone would miss that and leave the moved
     * area cleared to grey. */
    if (box->Left != mm->last_left || box->Top != mm->last_top ||
        box->Width != mm->last_width || box->Height != mm->last_height) {
        mm->last_left = box->Left;
        mm->last_top = box->Top;
        mm->last_width = box->Width;
        mm->last_height = box->Height;
        mm->dirty = 1;
    }
    /* Read the editor's vertical visible range.  A change while the user is not
     * actively dragging a scrollbar means a scrolling operation has finished
     * (keyboard, wheel, arrow or the end of a drag), so re-render the viewport
     * overlay; positions seen mid-drag of a scrollbar are ignored so nothing
     * updates during scrolling.  A minimap drag is the exception: even though it
     * shares the app->scrolling flag (to suspend the editor highlighter), the
     * viewport overlay must follow the cursor and be re-rendered live while the
     * overlay itself is being dragged. */
    if (app->active != NULL) {
        GetAttr(GA_TEXTEDITOR_Prop_First, app->active->editor, &view_first);
        GetAttr(GA_TEXTEDITOR_Prop_Visible, app->active->editor, &view_visible);
        GetAttr(GA_TEXTEDITOR_Prop_Entries, app->active->editor, &view_total);
    }
    if (view_total == 0) view_total = 1;
    if (view_visible == 0) view_visible = 1;
    if ((!app->scrolling || app->minimap_dragging) &&
        (LONG)view_first != mm->last_first) {
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

/**
 * @brief Draw a raised bevel around the minimap's drawing area.
 *
 * Draws the standard AmigaOS raised border with the screen's SHINEPEN on the
 * top and left edges and SHADOWPEN on the bottom and right edges (falling back
 * to the editor text pen when no screen draw info is available).  Intended to
 * run after each blit so it sits on top of the freshly rendered contents; does
 * nothing for boxes smaller than 2x2.
 *
 * @param rp  The RastPort to draw the border on.
 * @param box The drawing area whose border is drawn.
 * @param app The editor application supplying the screen draw info and pens.
 */
static void minimap_draw_raised_border(EditorApp *app, struct RastPort *rp,
                                       struct IBox *box)
{
    UWORD shine, shadow;
    WORD left, top, right, bottom;
    if (rp == NULL || box == NULL) return;
    if (box->Width < 2 || box->Height < 2) return;
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

/**
 * @brief Signal mask for the render task's replies.
 *
 * Returns the reply port's signal bit as a mask so it can be folded into the
 * main event loop's Wait().  Returns 0 when the minimap context or its reply
 * port is unavailable.
 *
 * @param app The editor application owning the minimap reply port.
 * @return The signal mask for the reply port, or 0 if unavailable.
 */
ULONG minimap_signal_mask(EditorApp *app)
{
    struct Minimap *mm = app->minimap_ctx;
    if (mm == NULL || !mm->ready || mm->reply_port == NULL) return 0;
    return 1UL << mm->reply_port->mp_SigBit;
}

/**
 * @brief Blit a finished render into the window and chain any follow-up job.
 *
 * Drains completed render replies, clears the busy flag, and (while the minimap
 * is visible and the render succeeded) blits the result bitmap into the window
 * over the current drawing box and redraws the raised border.  If the contents
 * became dirty again while this job was rendering, it kicks off another poll.
 *
 * @param app The editor application whose window receives the finished render.
 */
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
                minimap_draw_raised_border(app, app->window->RPort, box);
            }
        }
    }
    if (app->minimap_visible && mm->dirty && !mm->busy) minimap_poll(app);
}
