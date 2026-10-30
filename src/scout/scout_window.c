/**
 * @file scout_window.c
 * @brief Scout's window: its state, sessions and parsers, opened with
 *        Exchange's Show, the hotkey CX_POPKEY or at the start (CX_POPUP).
 *
 * The window shows what Scout does (the parser running, jobs waiting,
 * free memory, counts since the start) and lists the sessions and the
 * parsers. A session can be closed (e.g. one of an editor that crashed),
 * the parser drawer read again and a parser that hung used again. Closing
 * the window only hides it; "Quit Scout" ends Scout. While it is open, a
 * timer brings it up to date every second; a list is rebuilt only when
 * its rows changed, keeping the selection.
 *
 * The texts come from TinyDev-Scout.catalog (src/scout/catalogs/), so
 * Scout stays independent of TinyDev; the ReAction classes are opened the
 * first time the window is shown. Scout's ARexx results and its messages
 * in the Shell stay English.
 */
#include "scout_server.h"

/** Defines msg_builtin[] (the English texts) in this file. */
#define SCOUT_STRINGS
#include "scout_strings.h"

#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>
#include <libraries/locale.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/locale.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/listbrowser.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

/** intuition.library (opened with the window's classes). */
struct IntuitionBase *IntuitionBase = NULL;
/** locale.library, NULL without it. */
struct LocaleBase *LocaleBase = NULL;
/** window.class. */
struct Library *WindowBase = NULL;
/** layout.gadget. */
struct Library *LayoutBase = NULL;
/** button.gadget. */
struct Library *ButtonBase = NULL;
/** listbrowser.gadget. */
struct Library *ListBrowserBase = NULL;
/** label.image. */
struct Library *LabelBase = NULL;

/** Gadget IDs. */
enum {
    GID_STATE = 1, GID_RUNNING, GID_QUEUE, GID_MEMORY, GID_COMMANDS, GID_ANALYSES, GID_TIMEOUTS, GID_CACHED,
    GID_SESSIONS, GID_CLOSE_SESSION, GID_PARSERS, GID_RESCAN, GID_ENABLE,
    GID_QUIT, GID_HIDE
};

/** Lines of the state and counts groups (in the order of their gadget IDs). */
enum {
    LINE_STATE = 0, LINE_RUNNING, LINE_QUEUE, LINE_MEMORY,
    LINE_COMMANDS, LINE_ANALYSES, LINE_TIMEOUTS, LINE_CACHED, LINE_COUNT
};

/** Columns of the session list. */
enum { SC_NUMBER = 0, SC_PROJECT, SC_FILES, SC_DRAWERS, SC_IDLE, SC_COUNT };

/** Columns of the parser list. */
enum { PC_NAME = 0, PC_VERSION, PC_LANGUAGES, PC_RUNS, PC_FAILURES, PC_STATE, PC_COUNT };

/** Longest text of a line of the state group. */
#define LINE_MAX 160

/** State of the window. */
static struct {
    struct Catalog *catalog;            /**< TinyDev-Scout.catalog, NULL for English */
    int classes;                        /**< 1 opened, -1 missing */
    Object *winobj;                     /**< the window object, NULL while hidden */
    struct Window *win;                 /**< its window */
    Object *line[LINE_COUNT];           /**< the values of the state group */
    char text[LINE_COUNT][LINE_MAX];    /**< their texts */
    Object *sessions, *parsers;         /**< the lists */
    struct List session_nodes;          /**< rows (LBNA_UserData: session number) */
    struct List parser_nodes;           /**< rows (LBNA_UserData: parser index) */
    struct ColumnInfo *session_columns; /**< columns of the session list */
    struct ColumnInfo *parser_columns;  /**< columns of the parser list */
    unsigned long session_hash;         /**< checksum of the session rows shown */
    unsigned long parser_hash;          /**< checksum of the parser rows shown */
    struct HintInfo hints[4];           /**< help bubbles */
    WORD box[4];                        /**< where the window was, box[2] 0 before */
    struct MsgPort *timer_port;         /**< port of the refresh timer */
    struct timerequest *timer;          /**< the refresh timer */
    int timer_open;                     /**< its timer.device is open */
    int timer_pending;                  /**< it runs */
} sw;

/**
 * @brief Return a text in the user's language.
 * @param id MSG_ number.
 * @return The text.
 */
static const char *S(LONG id)
{
    const char *builtin = id >= 0 && id < MSG_COUNT ? msg_builtin[id] : "";
    if (sw.catalog == NULL) return builtin;
    return (const char *)GetCatalogStr(sw.catalog, id, (STRPTR)builtin);
}

/**
 * @brief Show a message in a requester (the window cannot be shown).
 * @param text The message.
 */
static void message(const char *text)
{
    struct EasyStruct es;
    if (!IntuitionBase) return;
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)SCOUT_BROKER_TITLE;
    es.es_TextFormat = (STRPTR)"%s";
    es.es_GadgetFormat = (STRPTR)S(MSG_OK);
    EasyRequest(NULL, &es, NULL, (ULONG)text);
}

/**
 * @brief Open the classes of the window (once).
 * @return Non-zero if all are there.
 */
static int open_classes(void)
{
    const char *missing = NULL;
    char text[96];
    if (sw.classes) return sw.classes > 0;
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    if (!IntuitionBase) {
        sw.classes = -1;
        return 0;
    }
    if ((WindowBase = OpenLibrary((STRPTR)"window.class", 44)) == NULL) missing = "window.class";
    else if ((LayoutBase = OpenLibrary((STRPTR)"gadgets/layout.gadget", 44)) == NULL) missing = "layout.gadget";
    else if ((ButtonBase = OpenLibrary((STRPTR)"gadgets/button.gadget", 44)) == NULL) missing = "button.gadget";
    else if ((ListBrowserBase = OpenLibrary((STRPTR)"gadgets/listbrowser.gadget", 44)) == NULL) missing = "listbrowser.gadget";
    else if ((LabelBase = OpenLibrary((STRPTR)"images/label.image", 44)) == NULL) missing = "label.image";
    if (missing) {
        snprintf(text, sizeof(text), S(MSG_ERR_CLASS), missing);
        message(text);
        sw.classes = -1;
        return 0;
    }
    sw.session_columns = AllocLBColumnInfo(SC_COUNT,
        LBCIA_Column, SC_NUMBER, LBCIA_Title, (ULONG)S(MSG_COL_NUMBER), LBCIA_Weight, 8,
        LBCIA_Column, SC_PROJECT, LBCIA_Title, (ULONG)S(MSG_COL_PROJECT), LBCIA_Weight, 56,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, SC_FILES, LBCIA_Title, (ULONG)S(MSG_COL_FILES), LBCIA_Weight, 12,
        LBCIA_Column, SC_DRAWERS, LBCIA_Title, (ULONG)S(MSG_COL_DRAWERS), LBCIA_Weight, 12,
        LBCIA_Column, SC_IDLE, LBCIA_Title, (ULONG)S(MSG_COL_IDLE), LBCIA_Weight, 12, TAG_END);
    sw.parser_columns = AllocLBColumnInfo(PC_COUNT,
        LBCIA_Column, PC_NAME, LBCIA_Title, (ULONG)S(MSG_COL_PARSER), LBCIA_Weight, 24,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, PC_VERSION, LBCIA_Title, (ULONG)S(MSG_COL_VERSION), LBCIA_Weight, 12,
        LBCIA_Column, PC_LANGUAGES, LBCIA_Title, (ULONG)S(MSG_COL_LANGUAGES), LBCIA_Weight, 28,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, PC_RUNS, LBCIA_Title, (ULONG)S(MSG_COL_RUNS), LBCIA_Weight, 10,
        LBCIA_Column, PC_FAILURES, LBCIA_Title, (ULONG)S(MSG_COL_FAILURES), LBCIA_Weight, 10,
        LBCIA_Column, PC_STATE, LBCIA_Title, (ULONG)S(MSG_COL_PSTATE), LBCIA_Weight, 16, TAG_END);
    if (!sw.session_columns || !sw.parser_columns) {
        sw.classes = -1;
        return 0;
    }
    sw.classes = 1;
    return 1;
}

/**
 * @brief Start the refresh timer (one second).
 */
static void start_timer(void)
{
    if (!sw.timer_open || sw.timer_pending) return;
    sw.timer->tr_node.io_Command = TR_ADDREQUEST;
    sw.timer->tr_time.tv_secs = 1;
    sw.timer->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)sw.timer);
    sw.timer_pending = 1;
}

/**
 * @brief Stop the refresh timer.
 */
static void stop_timer(void)
{
    if (!sw.timer_pending) return;
    AbortIO((struct IORequest *)sw.timer);
    WaitIO((struct IORequest *)sw.timer);
    SetSignal(0, 1UL << sw.timer_port->mp_SigBit);
    sw.timer_pending = 0;
}

/**
 * @brief Checksum of a text, added to a running one.
 * @param h The checksum so far.
 * @param text The text.
 * @return The new checksum.
 */
static unsigned long hash_text(unsigned long h, const char *text)
{
    while (*text) h = ((h << 5) + h) ^ (unsigned char)*text++;
    return ((h << 5) + h) ^ 0xff;
}

/**
 * @brief Show a text in a line of the state group (only if it changed).
 * @param line LINE_ value.
 * @param text The text.
 */
static void set_line(int line, const char *text)
{
    if (!sw.line[line] || strcmp(sw.text[line], text) == 0) return;
    strncpy(sw.text[line], text, LINE_MAX - 1);
    sw.text[line][LINE_MAX - 1] = '\0';
    SetGadgetAttrs((struct Gadget *)sw.line[line], sw.win, NULL, GA_Text, (ULONG)sw.text[line], TAG_END);
}

/**
 * @brief Return the row selected in a list (its LBNA_UserData).
 * @param list The list.
 * @return The value, or -1 if none is selected.
 */
static long selected(Object *list)
{
    struct Node *node = NULL;
    ULONG data = 0;
    GetAttr(LISTBROWSER_SelectedNode, list, (ULONG *)&node);
    if (!node) return -1;
    GetListBrowserNodeAttrs(node, LBNA_UserData, (ULONG)&data, TAG_END);
    return (long)data;
}

/**
 * @brief Free the rows of a list (it is detached).
 * @param nodes The rows.
 */
static void free_nodes(struct List *nodes)
{
    struct Node *n;
    while ((n = RemHead(nodes)) != NULL) FreeListBrowserNode(n);
}

/**
 * @brief Give a list new rows, keeping the selected row (by its
 *        LBNA_UserData).
 * @param list The list.
 * @param nodes Its rows (filled by @p fill while the list is detached).
 * @param fill Builds the rows.
 * @param srv The server, for @p fill.
 */
static void replace_rows(Object *list, struct List *nodes, void (*fill)(ScoutServer *, struct List *),
                         ScoutServer *srv)
{
    long keep = selected(list);
    struct Node *n, *pick = NULL;
    SetGadgetAttrs((struct Gadget *)list, sw.win, NULL, LISTBROWSER_Labels, ~0UL, TAG_END);
    free_nodes(nodes);
    fill(srv, nodes);
    for (n = nodes->lh_Head; keep >= 0 && n->ln_Succ; n = n->ln_Succ) {
        ULONG data = 0;
        GetListBrowserNodeAttrs(n, LBNA_UserData, (ULONG)&data, TAG_END);
        if ((long)data == keep) pick = n;
    }
    SetGadgetAttrs((struct Gadget *)list, sw.win, NULL, LISTBROWSER_Labels, (ULONG)nodes,
                   LISTBROWSER_SelectedNode, (ULONG)pick, TAG_END);
}

/**
 * @brief Count the watched drawers of a session.
 * @param srv The server.
 * @param session The session.
 * @return The number.
 */
static long watch_count(const ScoutServer *srv, long session)
{
    const ScoutWatch *w;
    long n = 0;
    for (w = srv->watches; w; w = w->next) n += w->session == session;
    return n;
}

/**
 * @brief Write the texts of a session's row.
 * @param srv The server.
 * @param s The session.
 * @param now The time (scout_now()).
 * @param[out] cols The texts (SC_COUNT of 32 characters; the project is
 *                  not copied).
 */
static void session_texts(const ScoutServer *srv, const ScoutSession *s, unsigned long now, char cols[][32])
{
    unsigned long idle = now > s->last_used ? now - s->last_used : 0;
    snprintf(cols[SC_NUMBER], 32, "%ld", s->id);
    snprintf(cols[SC_FILES], 32, "%ld", s->file_count);
    snprintf(cols[SC_DRAWERS], 32, "%ld", watch_count(srv, s->id));
    if (idle < 60) snprintf(cols[SC_IDLE], 32, S(MSG_IDLE_SECONDS), idle);
    else snprintf(cols[SC_IDLE], 32, S(MSG_IDLE_MINUTES), idle / 60);
}

/**
 * @brief Build the rows of the session list (session 0 is left out).
 * @param srv The server.
 * @param nodes The rows.
 */
static void fill_sessions(ScoutServer *srv, struct List *nodes)
{
    const ScoutSession *s;
    unsigned long now = scout_now();
    for (s = srv->store.sessions; s; s = s->next) {
        char cols[SC_COUNT][32];
        struct Node *n;
        if (s->id == 0) continue;
        session_texts(srv, s, now, cols);
        n = AllocListBrowserNode(SC_COUNT, LBNA_UserData, (ULONG)s->id,
            LBNA_Column, SC_NUMBER, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[SC_NUMBER],
            LBNA_Column, SC_PROJECT, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)s->project,
            LBNA_Column, SC_FILES, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[SC_FILES],
            LBNCA_Justification, LCJ_RIGHT,
            LBNA_Column, SC_DRAWERS, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[SC_DRAWERS],
            LBNCA_Justification, LCJ_RIGHT,
            LBNA_Column, SC_IDLE, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[SC_IDLE],
            LBNCA_Justification, LCJ_RIGHT, TAG_END);
        if (n) AddTail(nodes, n);
    }
}

/**
 * @brief Checksum of the session rows as fill_sessions() would build them.
 * @param srv The server.
 * @return The checksum.
 */
static unsigned long sessions_hash(const ScoutServer *srv)
{
    const ScoutSession *s;
    unsigned long h = 5381, now = scout_now();
    int i;
    for (s = srv->store.sessions; s; s = s->next) {
        char cols[SC_COUNT][32];
        if (s->id == 0) continue;
        cols[SC_PROJECT][0] = '\0';
        session_texts(srv, s, now, cols);
        h = hash_text(h, s->project);
        for (i = 0; i < SC_COUNT; i++) h = hash_text(h, cols[i]);
    }
    return h;
}

/**
 * @brief Write the languages of a parser, separated by commas.
 * @param srv The server.
 * @param parser Its index.
 * @param[out] out The text.
 * @param size Size of @p out.
 */
static void parser_languages(const ScoutServer *srv, long parser, char *out, size_t size)
{
    long i;
    size_t len = 0;
    out[0] = '\0';
    for (i = 0; i < srv->store.language_count; i++) {
        const ScoutLanguage *l = &srv->store.languages[i];
        if (l->parser != parser) continue;
        snprintf(out + len, size - len, "%s%s", len ? ", " : "", l->name ? l->name : l->id);
        len = strlen(out);
    }
}

/**
 * @brief Write the texts of a parser's row.
 * @param srv The server.
 * @param i Its index.
 * @param[out] cols The texts (PC_COUNT of 96 characters).
 */
static void parser_texts(const ScoutServer *srv, long i, char cols[][96])
{
    const ScoutParser *p = &srv->store.parsers[i];
    snprintf(cols[PC_NAME], 96, "%s", p->name ? p->name : (const char *)FilePart((STRPTR)p->path));
    snprintf(cols[PC_VERSION], 96, "%s", p->version ? p->version : "");
    parser_languages(srv, i, cols[PC_LANGUAGES], 96);
    snprintf(cols[PC_RUNS], 96, "%ld", p->runs);
    snprintf(cols[PC_FAILURES], 96, "%ld", p->failures);
    snprintf(cols[PC_STATE], 96, "%s", S(p->disabled ? MSG_PARSER_DISABLED : MSG_PARSER_ACTIVE));
}

/**
 * @brief Build the rows of the parser list.
 * @param srv The server.
 * @param nodes The rows.
 */
static void fill_parsers(ScoutServer *srv, struct List *nodes)
{
    long i;
    for (i = 0; i < srv->store.parser_count; i++) {
        char cols[PC_COUNT][96];
        struct Node *n;
        parser_texts(srv, i, cols);
        n = AllocListBrowserNode(PC_COUNT, LBNA_UserData, (ULONG)i,
            LBNA_Column, PC_NAME, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[PC_NAME],
            LBNA_Column, PC_VERSION, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[PC_VERSION],
            LBNA_Column, PC_LANGUAGES, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[PC_LANGUAGES],
            LBNA_Column, PC_RUNS, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[PC_RUNS],
            LBNCA_Justification, LCJ_RIGHT,
            LBNA_Column, PC_FAILURES, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[PC_FAILURES],
            LBNCA_Justification, LCJ_RIGHT,
            LBNA_Column, PC_STATE, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)cols[PC_STATE], TAG_END);
        if (n) AddTail(nodes, n);
    }
}

/**
 * @brief Checksum of the parser rows as fill_parsers() would build them.
 * @param srv The server.
 * @return The checksum.
 */
static unsigned long parsers_hash(const ScoutServer *srv)
{
    unsigned long h = 5381;
    long i;
    int c;
    for (i = 0; i < srv->store.parser_count; i++) {
        char cols[PC_COUNT][96];
        parser_texts(srv, i, cols);
        for (c = 0; c < PC_COUNT; c++) h = hash_text(h, cols[c]);
    }
    return h;
}

/**
 * @brief Bring the window up to date.
 * @param srv The server.
 * @param force Rebuild the lists even if nothing changed.
 */
static void refresh(ScoutServer *srv, int force)
{
    char text[LINE_MAX], state[64];
    unsigned long h, free_kb = AvailMem(MEMF_ANY) / 1024UL;
    const ScoutWalk *wk;
    long walks = 0;
    if (!sw.win) return;

    snprintf(state, sizeof(state), "%s", S(srv->enabled ? MSG_STATE_ACTIVE : MSG_STATE_DISABLED));
    if (srv->scanning) snprintf(text, sizeof(text), S(MSG_STATE_SCANNING), state);
    else snprintf(text, sizeof(text), "%s", state);
    set_line(LINE_STATE, text);

    if (srv->job) {
        const ScoutJob *job = srv->job;
        unsigned long now = scout_now(), secs = now > job->started ? now - job->started : 0;
        const char *parser = (const char *)FilePart((STRPTR)job->program);
        if (job->type == SCOUT_JOB_DESCRIBE || job->count == 0)
            snprintf(text, sizeof(text), S(MSG_RUNNING_DESCRIBE), parser, secs);
        else if (job->count == 1)
            snprintf(text, sizeof(text), S(MSG_RUNNING_ANALYZE), parser,
                     (const char *)FilePart((STRPTR)job->items[0].path), secs);
        else
            snprintf(text, sizeof(text), S(MSG_RUNNING_MORE), parser,
                     (const char *)FilePart((STRPTR)job->items[0].path), (long)(job->count - 1), secs);
    } else {
        snprintf(text, sizeof(text), "%s", S(MSG_RUNNING_IDLE));
    }
    set_line(LINE_RUNNING, text);

    for (wk = srv->walks; wk; wk = wk->next) walks++;
    snprintf(text, sizeof(text), S(MSG_QUEUE), scout_jobs_count(srv) - (srv->job != NULL), walks);
    set_line(LINE_QUEUE, text);

    if (srv->minfree <= 0) snprintf(text, sizeof(text), S(MSG_MEMORY_NOLIMIT), free_kb);
    else snprintf(text, sizeof(text), S(scout_memory_low(srv) ? MSG_MEMORY_LOW : MSG_MEMORY), free_kb, srv->minfree);
    set_line(LINE_MEMORY, text);

    snprintf(text, sizeof(text), "%lu", srv->stats.commands);
    set_line(LINE_COMMANDS, text);
    snprintf(text, sizeof(text), S(MSG_ANALYSES), srv->stats.analyses, srv->stats.failed);
    set_line(LINE_ANALYSES, text);
    snprintf(text, sizeof(text), "%lu", srv->stats.timeouts);
    set_line(LINE_TIMEOUTS, text);
    snprintf(text, sizeof(text), "%lu", srv->stats.cached);
    set_line(LINE_CACHED, text);

    h = sessions_hash(srv);
    if (force || h != sw.session_hash) {
        sw.session_hash = h;
        replace_rows(sw.sessions, &sw.session_nodes, fill_sessions, srv);
    }
    h = parsers_hash(srv);
    if (force || h != sw.parser_hash) {
        sw.parser_hash = h;
        replace_rows(sw.parsers, &sw.parser_nodes, fill_parsers, srv);
    }
}

/**
 * @brief Make a line of the state group: a label and a read-only button
 *        showing the value.
 * @param line LINE_ value.
 * @return The button.
 */
static Object *make_line(int line)
{
    sw.text[line][0] = '\0';
    sw.line[line] = NewObject(BUTTON_GetClass(), NULL, GA_ID, (ULONG)(GID_STATE + line), GA_ReadOnly, TRUE,
        GA_Underscore, 0, GA_Text, (ULONG)sw.text[line], BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE, BUTTON_Justification, BCJ_LEFT, TAG_END);
    return sw.line[line];
}

/**
 * @brief Make a label.
 * @param id MSG_ number of its text.
 * @return The label.
 */
static Object *make_label(LONG id)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)S(id), TAG_END);
}

/**
 * @brief Make a button as wide as its text.
 * @param id Its gadget ID.
 * @param text MSG_ number of its text.
 * @return The button.
 */
static Object *make_button(ULONG id, LONG text)
{
    return NewObject(BUTTON_GetClass(), NULL, GA_ID, id, GA_RelVerify, TRUE,
                     GA_Text, (ULONG)S(text), BUTTON_TextPadding, TRUE, TAG_END);
}

/** Tags of a group with a frame and a title. */
#define GROUP(title) \
    LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_BevelStyle, BVS_GROUP, \
    LAYOUT_Label, (ULONG)S(title), LAYOUT_SpaceOuter, TRUE, \
    LAYOUT_LeftSpacing, 8, LAYOUT_RightSpacing, 8, \
    LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6, LAYOUT_InnerSpacing, 4

void scout_window_show(ScoutServer *srv)
{
    struct Screen *screen;
    if (sw.win) {
        WindowToFront(sw.win);
        ActivateWindow(sw.win);
        return;
    }
    if (!open_classes()) return;
    if ((screen = LockPubScreen(NULL)) == NULL) return;
    NewList(&sw.session_nodes);
    NewList(&sw.parser_nodes);
    sw.hints[0].hi_GadgetID = GID_CLOSE_SESSION;
    sw.hints[0].hi_Code = -1;
    sw.hints[0].hi_Text = (STRPTR)S(MSG_HINT_CLOSE_SESSION);
    sw.hints[1].hi_GadgetID = GID_RESCAN;
    sw.hints[1].hi_Code = -1;
    sw.hints[1].hi_Text = (STRPTR)S(MSG_HINT_RESCAN);
    sw.hints[2].hi_GadgetID = GID_ENABLE;
    sw.hints[2].hi_Code = -1;
    sw.hints[2].hi_Text = (STRPTR)S(MSG_HINT_ENABLE);
    sw.hints[3].hi_GadgetID = -1;
    sw.sessions = NewObject(LISTBROWSER_GetClass(), NULL, GA_ID, GID_SESSIONS, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&sw.session_nodes, LISTBROWSER_ColumnInfo, (ULONG)sw.session_columns,
        LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_AutoWheel, TRUE,
        LISTBROWSER_MinVisible, 3, TAG_END);
    sw.parsers = NewObject(LISTBROWSER_GetClass(), NULL, GA_ID, GID_PARSERS, GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&sw.parser_nodes, LISTBROWSER_ColumnInfo, (ULONG)sw.parser_columns,
        LISTBROWSER_ColumnTitles, TRUE, LISTBROWSER_ShowSelected, TRUE, LISTBROWSER_AutoWheel, TRUE,
        LISTBROWSER_MinVisible, 2, TAG_END);
    sw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)S(MSG_WIN_TITLE),
        WA_PubScreen, (ULONG)screen,
        WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
        WA_Activate, TRUE,
        sw.box[2] ? WA_Left : TAG_IGNORE, (ULONG)sw.box[0],
        sw.box[2] ? WA_Top : TAG_IGNORE, (ULONG)sw.box[1],
        sw.box[2] ? WA_Width : TAG_IGNORE, (ULONG)sw.box[2],
        sw.box[2] ? WA_Height : TAG_IGNORE, (ULONG)sw.box[3],
        sw.box[2] ? TAG_IGNORE : WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_HintInfo, (ULONG)sw.hints, WINDOW_GadgetHelp, TRUE,
        WINDOW_ParentGroup, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_LeftSpacing, 6, LAYOUT_RightSpacing, 6,
            LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6,
            LAYOUT_InnerSpacing, 6,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, GROUP(MSG_GROUP_STATE),
                LAYOUT_AddChild, (ULONG)make_line(LINE_STATE),
                CHILD_Label, (ULONG)make_label(MSG_LABEL_STATE),
                LAYOUT_AddChild, (ULONG)make_line(LINE_RUNNING),
                CHILD_Label, (ULONG)make_label(MSG_LABEL_RUNNING),
                LAYOUT_AddChild, (ULONG)make_line(LINE_QUEUE),
                CHILD_Label, (ULONG)make_label(MSG_LABEL_QUEUE),
                LAYOUT_AddChild, (ULONG)make_line(LINE_MEMORY),
                CHILD_Label, (ULONG)make_label(MSG_LABEL_MEMORY),
                TAG_END),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, GROUP(MSG_GROUP_COUNTS),
                LAYOUT_AddChild, (ULONG)make_line(LINE_COMMANDS),
                CHILD_Label, (ULONG)make_label(MSG_LABEL_COMMANDS),
                LAYOUT_AddChild, (ULONG)make_line(LINE_ANALYSES),
                CHILD_Label, (ULONG)make_label(MSG_LABEL_ANALYSES),
                LAYOUT_AddChild, (ULONG)make_line(LINE_TIMEOUTS),
                CHILD_Label, (ULONG)make_label(MSG_LABEL_TIMEOUTS),
                LAYOUT_AddChild, (ULONG)make_line(LINE_CACHED),
                CHILD_Label, (ULONG)make_label(MSG_LABEL_CACHED),
                TAG_END),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, GROUP(MSG_GROUP_SESSIONS),
                LAYOUT_AddChild, (ULONG)sw.sessions,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_AddChild, (ULONG)make_button(GID_CLOSE_SESSION, MSG_BTN_CLOSE_SESSION),
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                    TAG_END),
                CHILD_WeightedHeight, 0,
                TAG_END),
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, GROUP(MSG_GROUP_PARSERS),
                LAYOUT_AddChild, (ULONG)sw.parsers,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_InnerSpacing, 4,
                    LAYOUT_AddChild, (ULONG)make_button(GID_RESCAN, MSG_BTN_RESCAN),
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, (ULONG)make_button(GID_ENABLE, MSG_BTN_ENABLE),
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                    TAG_END),
                CHILD_WeightedHeight, 0,
                TAG_END),
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, (ULONG)make_button(GID_QUIT, MSG_BTN_QUIT),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL, TAG_END),
                LAYOUT_AddChild, (ULONG)make_button(GID_HIDE, MSG_BTN_HIDE),
                CHILD_WeightedWidth, 0,
                TAG_END),
            CHILD_WeightedHeight, 0,
            TAG_END),
        TAG_END);
    if (sw.winobj) sw.win = (struct Window *)DoMethod(sw.winobj, WM_OPEN, NULL);
    UnlockPubScreen(NULL, screen);
    if (!sw.win) {
        if (sw.winobj) DisposeObject(sw.winobj);
        sw.winobj = NULL;
        sw.sessions = sw.parsers = NULL;
        memset(sw.line, 0, sizeof(sw.line));
        message(S(MSG_ERR_WINDOW));
        return;
    }
    refresh(srv, 1);
    start_timer();
}

void scout_window_hide(ScoutServer *srv)
{
    (void)srv;
    if (!sw.winobj) return;
    stop_timer();
    if (sw.win) {
        sw.box[0] = sw.win->LeftEdge;
        sw.box[1] = sw.win->TopEdge;
        sw.box[2] = sw.win->Width;
        sw.box[3] = sw.win->Height;
    }
    SetAttrs(sw.sessions, LISTBROWSER_Labels, ~0UL, TAG_END);
    SetAttrs(sw.parsers, LISTBROWSER_Labels, ~0UL, TAG_END);
    DoMethod(sw.winobj, WM_CLOSE, NULL);
    DisposeObject(sw.winobj);
    sw.winobj = NULL;
    sw.win = NULL;
    sw.sessions = sw.parsers = NULL;
    memset(sw.line, 0, sizeof(sw.line));
    free_nodes(&sw.session_nodes);
    free_nodes(&sw.parser_nodes);
}

int scout_window_init(ScoutServer *srv)
{
    (void)srv;
    if ((LocaleBase = (struct LocaleBase *)OpenLibrary((STRPTR)"locale.library", 38)) != NULL)
        sw.catalog = OpenCatalog(NULL, (STRPTR)"TinyDev-Scout.catalog",
                                 OC_BuiltInLanguage, (ULONG)"english", TAG_END);
    if ((sw.timer_port = CreateMsgPort()) == NULL) return 0;
    sw.timer = (struct timerequest *)CreateIORequest(sw.timer_port, sizeof(struct timerequest));
    if (!sw.timer) return 0;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)sw.timer, 0) != 0) return 0;
    sw.timer_open = 1;
    return 1;
}

ULONG scout_window_sigmask(const ScoutServer *srv)
{
    ULONG sig = 0;
    (void)srv;
    if (sw.winobj) GetAttr(WINDOW_SigMask, sw.winobj, &sig);
    if (sw.timer_port) sig |= 1UL << sw.timer_port->mp_SigBit;
    return sig;
}

void scout_window_handle(ScoutServer *srv, ULONG signals)
{
    ULONG result;
    WORD code;
    long id;
    if (sw.timer_port && (signals & (1UL << sw.timer_port->mp_SigBit)) &&
        sw.timer_pending && CheckIO((struct IORequest *)sw.timer)) {
        WaitIO((struct IORequest *)sw.timer);
        sw.timer_pending = 0;
        refresh(srv, 0);
        if (sw.win) start_timer();
    }
    while (sw.winobj && (result = DoMethod(sw.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            scout_window_hide(srv);
            return;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case GID_CLOSE_SESSION:
                if ((id = selected(sw.sessions)) > 0) scout_session_end(srv, id);
                refresh(srv, 0);
                break;
            case GID_RESCAN:
                if (!srv->scanning) {
                    scout_store_clear_parsers(&srv->store);
                    scout_jobs_scan(srv);
                }
                refresh(srv, 0);
                break;
            case GID_ENABLE:
                if ((id = selected(sw.parsers)) >= 0 && id < srv->store.parser_count)
                    srv->store.parsers[id].disabled = 0;
                refresh(srv, 0);
                break;
            case GID_QUIT:
                srv->running = 0;
                break;
            case GID_HIDE:
                scout_window_hide(srv);
                return;
            }
            break;
        }
    }
}

void scout_window_cleanup(ScoutServer *srv)
{
    scout_window_hide(srv);
    stop_timer();
    if (sw.timer_open) CloseDevice((struct IORequest *)sw.timer);
    if (sw.timer) DeleteIORequest((struct IORequest *)sw.timer);
    if (sw.timer_port) DeleteMsgPort(sw.timer_port);
    sw.timer = NULL;
    sw.timer_port = NULL;
    sw.timer_open = 0;
    if (sw.session_columns) FreeLBColumnInfo(sw.session_columns);
    if (sw.parser_columns) FreeLBColumnInfo(sw.parser_columns);
    sw.session_columns = sw.parser_columns = NULL;
    if (LabelBase) CloseLibrary(LabelBase);
    if (ListBrowserBase) CloseLibrary(ListBrowserBase);
    if (ButtonBase) CloseLibrary(ButtonBase);
    if (LayoutBase) CloseLibrary(LayoutBase);
    if (WindowBase) CloseLibrary(WindowBase);
    LabelBase = ListBrowserBase = ButtonBase = LayoutBase = WindowBase = NULL;
    if (LocaleBase) {
        CloseCatalog(sw.catalog);
        CloseLibrary((struct Library *)LocaleBase);
    }
    sw.catalog = NULL;
    LocaleBase = NULL;
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    IntuitionBase = NULL;
    sw.classes = 0;
}
