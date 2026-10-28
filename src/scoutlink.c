/**
 * @file scoutlink.c
 * @brief TinyDev as a client of TinyDev-Scout: starting it, a session per
 *        project, and the analysis of the documents.
 *
 * TinyDev talks to Scout only through its ARexx port TINYDEV_SCOUT
 * (src/scout/docs/Scout.md). Messages go out with PutMsg() and their
 * answers come back to a port of TinyDev that the event loop waits on,
 * so TinyDev never waits for Scout. Each message is kept with what it
 * was for (a request) until its answer arrives.
 *
 * Scout is started (PROGDIR:TinyDev-Scout, asynchronously) when it is
 * first needed and its port does not exist; TinyDev then looks for the
 * port for up to ten seconds. Without Scout, or with the setting off
 * (NOSCOUT), TinyDev works on as before. A timer of its own (four times a
 * second while something is to be done) drives the waiting and the pause
 * in typing, as IntuiTicks only come while the main window is active.
 *
 * The project (the folder tree's root) gets a session (OPEN, CLOSE when
 * the project changes or TinyDev quits). A document is analysed when it
 * is loaded or saved (the file) and after a pause in typing (a snapshot
 * of the editor's text, written to the drawer of the temporary files, with
 * a version raised for every snapshot). One analysis per document is on
 * its way at a time; a change meanwhile is sent after the answer. Files
 * whose extension no parser handles (LANGUAGES) are not sent.
 *
 * The answers' counts of symbols and diagnostics are shown in the status
 * line. The symbol list asks for the symbols of a document or of the
 * project (SYMBOLS with TO: the answer can exceed what ARexx takes); the
 * file is read, handed to symlist_symbols() and deleted. While Scout
 * indexes a new project, its STATUS is asked every two seconds; the
 * number of jobs waiting is shown by the symbol list, and when it drops to
 * nought the project's symbols count as changed. After an analysis that
 * found problems (or none where there were some), the document's
 * diagnostics are asked for (DIAGNOSTICS ... TO) and go to diag.c.
 */
#include "editor.h"
#include "json.h"

#include <devices/timer.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <gadgets/texteditor.h>
#include <rexx/errors.h>
#include <rexx/storage.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/rexxsyslib.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** rexxsyslib.library, opened with the first message (NULL: no Scout). */
struct RxsLib *RexxSysBase = NULL;

/** Name of Scout's ARexx port. */
#define SCOUT_PORT "TINYDEV_SCOUT"
/** Interval of the timer (microseconds). */
#define TICK_MICROS 250000
/** Timer ticks without typing before a changed document is analysed (1 s). */
#define PAUSE_TICKS 4
/** Timer ticks Scout may take to show its port after being started (10 s). */
#define START_TICKS 40
/** Most extensions remembered from LANGUAGES. */
#define MAX_EXTENSIONS 48
/** Timer ticks between two STATUS while the project is indexed (2 s). */
#define STATUS_TICKS 8
/** Scout's error number when no parser handles a file. */
#define SCOUT_E_NO_PARSER 4

/** States of the connection. */
enum {
    SL_OFF = 0,         /**< not connected (Scout not started by us) */
    SL_STARTING,        /**< started, waiting for its port */
    SL_READY,           /**< the port exists */
    SL_FAILED           /**< could not be started: not tried again */
};

/** What a message was for. */
enum {
    RQ_OPEN = 1,        /**< OPEN of the project */
    RQ_CLOSE,           /**< CLOSE of a session */
    RQ_LANGUAGES,       /**< LANGUAGES */
    RQ_ANALYZE,         /**< ANALYZE of a document */
    RQ_FORGET,          /**< FORGET of a document's snapshot */
    RQ_SYMBOLS,         /**< SYMBOLS of a document or the project */
    RQ_STATUS,          /**< STATUS, while the project is indexed */
    RQ_DIAGNOSTICS      /**< DIAGNOSTICS of a document or the project */
};

/** A message on its way to Scout. */
typedef struct ScoutRequest {
    struct ScoutRequest *next;  /**< the next one */
    struct RexxMsg *rx;         /**< the message */
    int kind;                   /**< RQ_ value */
    unsigned long doc;          /**< the document's number (RQ_ANALYZE) */
    long version;               /**< the snapshot's version, -1 for the file */
    long session;               /**< the session it was sent for */
    char *to;                   /**< the file of the result (TO; AllocVec()), NULL */
} ScoutRequest;

/** The state of the connection. */
static struct {
    struct MsgPort *port;       /**< where the answers come back */
    int state;                  /**< SL_ value */
    int ticks;                  /**< timer ticks waited for the port */
    int idle;                   /**< timer ticks since the last change of a text */
    struct MsgPort *timer_port; /**< port of the timer */
    struct timerequest *timer;  /**< the timer */
    int timer_open;             /**< timer.device is open */
    int timer_pending;          /**< the timer runs */
    ScoutRequest *requests;          /**< messages on their way */
    long session;               /**< the project's session, 0 for none */
    int opening;                /**< OPEN is on its way */
    int want_open;              /**< a session is to be opened for project */
    char project[EDITOR_PATH_MAX];  /**< the project of the session */
    int languages;              /**< LANGUAGES was answered */
    int asked_languages;        /**< LANGUAGES is on its way */
    char *extensions[MAX_EXTENSIONS];   /**< extensions with a parser (malloc()) */
    int extension_count;        /**< number of them */
    unsigned long project_generation;   /**< see scoutlink_project_generation() */
    int indexing;               /**< the project is being indexed (STATUS is asked) */
    int asked_status;           /**< STATUS is on its way */
    int status_ticks;           /**< timer ticks since the last STATUS */
    long queue;                 /**< Scout's jobs waiting, by the last STATUS */
    ScoutRequest *last;         /**< the request send_command() made last */
} sl;

/* ---- Messages ---- */

/**
 * @brief Send a command to Scout.
 * @param command The command line.
 * @param kind What it is for (RQ_).
 * @param doc The document's number (RQ_ANALYZE), 0.
 * @param version The snapshot's version, -1.
 * @return Non-zero if it was sent; 0 if Scout's port is gone (the
 *         connection is reset then) or memory ran out.
 */
static int send_command(const char *command, int kind, unsigned long doc, long version)
{
    struct MsgPort *scout;
    ScoutRequest *rq;
    if (RexxSysBase == NULL) return 0;
    if (sl.port == NULL && (sl.port = CreateMsgPort()) == NULL) return 0;
    rq = AllocVec(sizeof(ScoutRequest), MEMF_ANY | MEMF_CLEAR);
    if (rq == NULL) return 0;
    rq->rx = CreateRexxMsg(sl.port, NULL, NULL);
    if (rq->rx == NULL) { FreeVec(rq); return 0; }
    rq->rx->rm_Args[0] = (STRPTR)CreateArgstring((STRPTR)command, (ULONG)strlen(command));
    if (rq->rx->rm_Args[0] == NULL) { DeleteRexxMsg(rq->rx); FreeVec(rq); return 0; }
    rq->rx->rm_Action = RXCOMM | RXFF_RESULT;
    rq->kind = kind;
    rq->doc = doc;
    rq->version = version;
    rq->session = sl.session;
    Forbid();
    scout = FindPort((STRPTR)SCOUT_PORT);
    if (scout != NULL) PutMsg(scout, (struct Message *)rq->rx);
    Permit();
    if (scout == NULL) {
        DeleteArgstring((UBYTE *)rq->rx->rm_Args[0]);
        DeleteRexxMsg(rq->rx);
        FreeVec(rq);
        /* Scout has quit: start again when it is needed */
        if (sl.state == SL_READY) {
            sl.state = SL_OFF;
            sl.session = 0;
            sl.opening = 0;
            sl.want_open = sl.project[0] != '\0';
            sl.languages = sl.asked_languages = 0;
        }
        return 0;
    }
    rq->next = sl.requests;
    sl.requests = rq;
    sl.last = rq;
    return 1;
}

/**
 * @brief Free a request (and delete its result file).
 * @param rq The request.
 */
static void free_request(ScoutRequest *rq)
{
    if (rq->to != NULL) {
        DeleteFile((STRPTR)rq->to);
        FreeVec(rq->to);
    }
    FreeVec(rq);
}

/**
 * @brief Read a whole file.
 * @param path The file.
 * @return Its contents, terminated (FreeVec() it), or NULL.
 */
static char *read_file(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf = NULL;
    LONG size;
    if (fh == 0) return NULL;
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size >= 0 && (buf = AllocVec((ULONG)size + 1, MEMF_ANY)) != NULL) {
        LONG got = Read(fh, buf, size);
        buf[got > 0 ? got : 0] = '\0';
    }
    Close(fh);
    return buf;
}

/**
 * @brief Find a document by its number.
 * @param app The application state.
 * @param number The number.
 * @return The document, or NULL if it was closed.
 */
static Document *find_document(EditorApp *app, unsigned long number)
{
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        if (doc->number == number) return doc;
    return NULL;
}

/**
 * @brief Start Scout if its port does not exist, or mark it ready.
 * @param app The application state.
 */
static void connect(EditorApp *app)
{
    static char program[EDITOR_PATH_MAX], command[EDITOR_PATH_MAX + 4];
    struct MsgPort *port;
    BPTR lock, in;
    LONG rc = -1;
    if (sl.state == SL_READY || sl.state == SL_STARTING || sl.state == SL_FAILED) return;
    if (RexxSysBase == NULL &&
        (RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36)) == NULL) {
        sl.state = SL_FAILED;
        return;
    }
    Forbid();
    port = FindPort((STRPTR)SCOUT_PORT);
    Permit();
    if (port != NULL) {
        sl.state = SL_READY;
        return;
    }
    if (NameFromLock(GetProgramDir(), (STRPTR)program, sizeof(program)) &&
        AddPart((STRPTR)program, (STRPTR)"TinyDev-Scout", sizeof(program)) &&
        (lock = Lock((STRPTR)program, ACCESS_READ)) != 0) {
        UnLock(lock);
        snprintf(command, sizeof(command), "\"%s\"", program);
        if ((in = Open((STRPTR)"NIL:", MODE_OLDFILE)) != 0) {
            /* asynchronous: the system closes the input when Scout ends */
            rc = SystemTags((STRPTR)command, SYS_Input, (ULONG)in, SYS_Output, 0,
                            SYS_Asynch, TRUE, TAG_DONE);
            if (rc == -1) Close(in);
        }
    }
    sl.state = rc == -1 ? SL_FAILED : SL_STARTING;
    sl.ticks = 0;
    (void)app;
}

/**
 * @brief Do what is due now that Scout's port exists: LANGUAGES and the
 *        project's session.
 */
static void when_ready(void)
{
    char command[EDITOR_PATH_MAX + 16];
    if (sl.state != SL_READY) return;
    if (!sl.languages && !sl.asked_languages && send_command("LANGUAGES", RQ_LANGUAGES, 0, -1))
        sl.asked_languages = 1;
    if (sl.want_open && !sl.opening) {
        snprintf(command, sizeof(command), "OPEN \"%s\"", sl.project);
        if (send_command(command, RQ_OPEN, 0, -1)) {
            sl.opening = 1;
            sl.want_open = 0;
        }
    }
}

/**
 * @brief Tell whether a parser handles a file (by its extension).
 * @param path The file.
 * @return Non-zero if one does.
 */
static int has_parser(const char *path)
{
    const char *dot = strrchr(path, '.');
    int i;
    if (dot == NULL || strchr(dot, '/') != NULL || strchr(dot, ':') != NULL) return 0;
    for (i = 0; i < sl.extension_count; ++i)
        if (Stricmp((STRPTR)sl.extensions[i], (STRPTR)dot) == 0) return 1;
    return 0;
}

/**
 * @brief Write a document's text as its snapshot.
 * @param app The application state.
 * @param doc The document.
 * @return Non-zero on success.
 */
static int write_snapshot(EditorApp *app, Document *doc)
{
    char name[EDITOR_PATH_MAX];
    char *text;
    BPTR fh;
    LONG len;
    int ok;
    if (doc->scout_snapshot == NULL) {
        if (!temp_name(app, ".snap", name, sizeof(name))) return 0;
        doc->scout_snapshot = AllocVec(strlen(name) + 1, MEMF_ANY);
        if (doc->scout_snapshot == NULL) return 0;
        strcpy(doc->scout_snapshot, name);
    }
    text = (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL);
    if (text == NULL) return 0;
    len = (LONG)strlen(text);
    fh = Open((STRPTR)doc->scout_snapshot, MODE_NEWFILE);
    ok = fh != 0 && Write(fh, text, len) == len;
    if (fh != 0) Close(fh);
    FreeVec(text);
    return ok;
}

/**
 * @brief Send the analysis of a document: of its snapshot if it is
 *        changed, otherwise of its file.
 * @param app The application state.
 * @param doc The document.
 */
static void analyze(EditorApp *app, Document *doc)
{
    char command[3 * EDITOR_PATH_MAX];
    char session[24];
    long version = -1;
    session[0] = '\0';
    if (sl.session > 0) snprintf(session, sizeof(session), "SESSION %ld ", sl.session);
    if (doc->dirty) {
        if (!write_snapshot(app, doc)) { doc->scout_pending = 0; return; }
        version = ++doc->scout_version;
        snprintf(command, sizeof(command), "ANALYZE %sFILE \"%s\" TEXT \"%s\" VERSION %ld",
                 session, doc->path, doc->scout_snapshot, version);
    } else {
        snprintf(command, sizeof(command), "ANALYZE %sFILE \"%s\"", session, doc->path);
    }
    if (send_command(command, RQ_ANALYZE, doc->number, version)) {
        doc->scout_busy = 1;
        doc->scout_pending = 0;
        doc->scout_now = 0;
    }
}

/**
 * @brief Send the analyses that are due.
 * @param app The application state.
 */
static void send_due(EditorApp *app)
{
    Document *doc;
    if (sl.state != SL_READY || !sl.languages || sl.opening || sl.want_open) return;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ) {
        if (!doc->scout_pending || doc->scout_busy || doc->path == NULL) continue;
        if (doc->scout_none || !has_parser(doc->path)) { doc->scout_pending = 0; continue; }
        if (doc->scout_now || sl.idle >= PAUSE_TICKS) analyze(app, doc);
    }
}

/**
 * @brief Mark all documents for analysis (a new session or Scout again).
 * @param app The application state.
 */
static void all_pending(EditorApp *app)
{
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ) {
        doc->scout_pending = doc->scout_now = 1;
        doc->scout_none = 0;
    }
}

/**
 * @brief Read an integer member of a JSON result.
 * @param result The result.
 * @param key The member.
 * @param fallback The value if it is missing.
 * @return Its value.
 */
static long result_int(const char *result, const char *key, long fallback)
{
    JsonValue *v = json_parse(result, NULL);
    long n = v != NULL ? json_obj_get_int(v, key, fallback) : fallback;
    json_value_free(v);
    return n;
}

/**
 * @brief Remember the extensions of LANGUAGES' answer.
 * @param result The answer.
 */
static void read_languages(const char *result)
{
    JsonValue *v = json_parse(result, NULL);
    const JsonValue *langs = v != NULL ? json_obj_get_array(v, "languages") : NULL;
    size_t i, j;
    while (sl.extension_count > 0) free(sl.extensions[--sl.extension_count]);
    for (i = 0; langs != NULL && i < json_array_length(langs); ++i) {
        const JsonValue *exts = json_obj_get_array(json_array_get(langs, i), "extensions");
        for (j = 0; exts != NULL && j < json_array_length(exts); ++j) {
            const JsonValue *e = json_array_get(exts, j);
            char *copy;
            if (e == NULL || e->type != JSON_STRING || sl.extension_count >= MAX_EXTENSIONS) continue;
            if ((copy = malloc(strlen(e->u.str_val) + 1)) == NULL) continue;
            strcpy(copy, e->u.str_val);
            sl.extensions[sl.extension_count++] = copy;
        }
    }
    json_value_free(v);
}

/**
 * @brief Handle one answer.
 * @param app The application state.
 * @param rq Its request.
 * @param ok The command succeeded.
 * @param result Its result (NULL without one).
 * @param code The error number (RC2) on a failure.
 */
static void answer(EditorApp *app, ScoutRequest *rq, int ok, const char *result, long code)
{
    Document *doc;
    switch (rq->kind) {
        case RQ_LANGUAGES:
            sl.asked_languages = 0;
            if (ok && result != NULL) {
                read_languages(result);
                sl.languages = 1;
            } else {
                /* Scout cannot be used: not asked again until the setting
                 * is switched on again */
                sl.state = SL_FAILED;
            }
            break;
        case RQ_OPEN:
            sl.opening = 0;
            if (ok && result != NULL) {
                sl.session = result_int(result, "session", 0);
                ++sl.project_generation;
                sl.indexing = 1;
                sl.status_ticks = STATUS_TICKS;     /* ask soon */
                sl.queue = -1;
                all_pending(app);
            }
            break;
        case RQ_STATUS:
            sl.asked_status = 0;
            if (ok && result != NULL) {
                long queue = result_int(result, "queue", 0);
                /* done when nothing waits and no project is walked */
                if (queue == 0 && result_int(result, "walks", 0) == 0 && sl.indexing) {
                    sl.indexing = 0;
                    ++sl.project_generation;
                }
                sl.queue = queue;
            }
            break;
        case RQ_SYMBOLS: {
            char *text = ok && rq->to != NULL ? read_file(rq->to) : NULL;
            symlist_symbols(app, rq->doc, text);
            if (text != NULL) FreeVec(text);
            break;
        }
        case RQ_DIAGNOSTICS: {
            char *text = ok && rq->to != NULL ? read_file(rq->to) : NULL;
            if (rq->doc == 0) {
                diaglist_project(app, text);
            } else if ((doc = find_document(app, rq->doc)) != NULL && (ok || doc->diag_count > 0)) {
                diag_set_document(app, doc, text);
            }
            if (text != NULL) FreeVec(text);
            break;
        }
        case RQ_ANALYZE:
            if ((doc = find_document(app, rq->doc)) == NULL) break;
            doc->scout_busy = 0;
            if (rq->session != sl.session) { doc->scout_pending = 1; break; }
            if (ok && result != NULL) {
                doc->scout_symbols = result_int(result, "symbols", -1);
                doc->scout_problems = result_int(result, "diagnostics", 0);
                ++doc->scout_generation;
                if (rq->version < 0) ++sl.project_generation;
                /* the list and the marks follow the problems */
                if (doc->scout_problems > 0 || doc->diag_count > 0) scoutlink_request_diagnostics(app, doc);
            } else if (code == SCOUT_E_NO_PARSER) {
                doc->scout_none = 1;
            }
            break;
        default:
            break;
    }
}

/**
 * @brief Start the timer unless it runs (timer.device is opened the first
 *        time).
 */
static void start_timer(void)
{
    if (!sl.timer_open) {
        if (sl.timer_port == NULL && (sl.timer_port = CreateMsgPort()) == NULL) return;
        if (sl.timer == NULL &&
            (sl.timer = (struct timerequest *)CreateIORequest(sl.timer_port, sizeof(struct timerequest))) == NULL)
            return;
        if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)sl.timer, 0) != 0) return;
        sl.timer_open = 1;
    }
    if (sl.timer_pending) return;
    sl.timer->tr_node.io_Command = TR_ADDREQUEST;
    sl.timer->tr_time.tv_secs = 0;
    sl.timer->tr_time.tv_micro = TICK_MICROS;
    SendIO((struct IORequest *)sl.timer);
    sl.timer_pending = 1;
}

/**
 * @brief Tell whether the timer is needed: Scout is starting, or a
 *        document waits for its analysis.
 * @param app The application state.
 * @return Non-zero if it is.
 */
static int timer_needed(EditorApp *app)
{
    Document *doc;
    if (!app->settings.scout || sl.state == SL_FAILED) return 0;
    if (sl.state == SL_STARTING || sl.indexing) return 1;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        if (doc->scout_pending && doc->path != NULL && !doc->scout_none) return 1;
    return 0;
}

/**
 * @brief One turn of the timer: start Scout when a document waits, look
 *        for the port of a starting Scout, send what is due.
 * @param app The application state.
 */
static void tick(EditorApp *app)
{
    if (!app->settings.scout) return;
    if (sl.state == SL_OFF) {
        Document *doc;
        /* something to analyse: Scout is needed */
        for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
             doc = (Document *)doc->node.ln_Succ)
            if (doc->scout_pending && doc->path != NULL) { connect(app); break; }
    }
    if (sl.state == SL_STARTING) {
        struct MsgPort *port;
        Forbid();
        port = FindPort((STRPTR)SCOUT_PORT);
        Permit();
        if (port != NULL) sl.state = SL_READY;
        else if (++sl.ticks > START_TICKS) sl.state = SL_FAILED;
    }
    when_ready();
    send_due(app);
    if (sl.indexing && sl.state == SL_READY && !sl.asked_status && ++sl.status_ticks >= STATUS_TICKS) {
        sl.status_ticks = 0;
        if (send_command("STATUS", RQ_STATUS, 0, -1)) sl.asked_status = 1;
    }
}

/* ---- Interface ---- */

void scoutlink_project_opened(EditorApp *app)
{
    char root[EDITOR_PATH_MAX], command[32];
    if (sl.session > 0) {
        snprintf(command, sizeof(command), "CLOSE %ld", sl.session);
        send_command(command, RQ_CLOSE, 0, -1);
    }
    sl.session = 0;
    sl.want_open = 0;
    sl.indexing = 0;
    sl.project[0] = '\0';
    if (!app->settings.scout || !tree_project_root(app, root, sizeof(root))) return;
    strcpy(sl.project, root);
    sl.want_open = 1;
    connect(app);
    when_ready();
    if (sl.state == SL_STARTING) start_timer();
}

void scoutlink_settings_changed(EditorApp *app)
{
    Document *doc;
    if (app->settings.scout) {
        if (sl.state == SL_FAILED) sl.state = SL_OFF;   /* try again */
        if (sl.session == 0 && !sl.opening) scoutlink_project_opened(app);
        all_pending(app);
        return;
    }
    scoutlink_project_opened(app);      /* closes the session */
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ) {
        doc->scout_pending = doc->scout_now = 0;
        doc->scout_symbols = -1;
        if (doc->diag_count > 0 || doc->diag_mark_count > 0) diag_set_document(app, doc, NULL);
    }
}

void scoutlink_document_changed(Document *doc)
{
    doc->scout_pending = 1;
    sl.idle = 0;
    start_timer();
}

void scoutlink_document_saved(Document *doc)
{
    doc->scout_pending = doc->scout_now = 1;
    doc->scout_none = 0;
    start_timer();
}

void scoutlink_document_closing(EditorApp *app, Document *doc)
{
    char command[EDITOR_PATH_MAX + 32];
    if (doc->scout_snapshot != NULL) {
        if (doc->path != NULL && sl.state == SL_READY) {
            if (sl.session > 0)
                snprintf(command, sizeof(command), "FORGET SESSION %ld FILE \"%s\"", sl.session, doc->path);
            else
                snprintf(command, sizeof(command), "FORGET FILE \"%s\"", doc->path);
            send_command(command, RQ_FORGET, 0, -1);
        }
        DeleteFile((STRPTR)doc->scout_snapshot);
        FreeVec(doc->scout_snapshot);
        doc->scout_snapshot = NULL;
    }
    (void)app;
}

int scoutlink_request_symbols(EditorApp *app, Document *doc)
{
    char command[3 * EDITOR_PATH_MAX], to[EDITOR_PATH_MAX], session[24];
    char *copy;
    if (scoutlink_unavailable(app) || !sl.languages || sl.opening || sl.want_open) return 0;
    if (doc != NULL && doc->path == NULL) return 0;
    if (doc == NULL && sl.session == 0) return 0;
    if (!temp_name(app, ".json", to, sizeof(to))) return 0;
    session[0] = '\0';
    if (sl.session > 0) snprintf(session, sizeof(session), "SESSION %ld ", sl.session);
    if (doc != NULL)
        snprintf(command, sizeof(command), "SYMBOLS %sFILE \"%s\" TO \"%s\"", session, doc->path, to);
    else
        snprintf(command, sizeof(command), "SYMBOLS %sTO \"%s\"", session, to);
    if ((copy = AllocVec(strlen(to) + 1, MEMF_ANY)) == NULL) return 0;
    strcpy(copy, to);
    if (!send_command(command, RQ_SYMBOLS, doc != NULL ? doc->number : 0, -1)) {
        FreeVec(copy);
        return 0;
    }
    sl.last->to = copy;
    return 1;
}

int scoutlink_request_diagnostics(EditorApp *app, Document *doc)
{
    char command[3 * EDITOR_PATH_MAX], to[EDITOR_PATH_MAX], session[24];
    char *copy;
    if (scoutlink_unavailable(app) || !sl.languages || sl.opening || sl.want_open) return 0;
    if (doc != NULL && doc->path == NULL) return 0;
    if (doc == NULL && sl.session == 0) return 0;
    if (!temp_name(app, ".json", to, sizeof(to))) return 0;
    session[0] = '\0';
    if (sl.session > 0) snprintf(session, sizeof(session), "SESSION %ld ", sl.session);
    if (doc != NULL)
        snprintf(command, sizeof(command), "DIAGNOSTICS %sFILE \"%s\" TO \"%s\"", session, doc->path, to);
    else
        snprintf(command, sizeof(command), "DIAGNOSTICS %sTO \"%s\"", session, to);
    if ((copy = AllocVec(strlen(to) + 1, MEMF_ANY)) == NULL) return 0;
    strcpy(copy, to);
    if (!send_command(command, RQ_DIAGNOSTICS, doc != NULL ? doc->number : 0, -1)) {
        FreeVec(copy);
        return 0;
    }
    sl.last->to = copy;
    return 1;
}

unsigned long scoutlink_project_generation(void)
{
    return sl.project_generation;
}

long scoutlink_indexing(void)
{
    return sl.indexing && sl.session > 0 ? (sl.queue > 0 ? sl.queue : 0) : -1;
}

int scoutlink_unavailable(EditorApp *app)
{
    if (!app->settings.scout) return 1;
    return sl.state == SL_READY ? 0 : 2;
}

ULONG scoutlink_sigmask(void)
{
    ULONG mask = 0;
    if (sl.port != NULL) mask |= 1UL << sl.port->mp_SigBit;
    if (sl.timer_port != NULL) mask |= 1UL << sl.timer_port->mp_SigBit;
    return mask;
}

/**
 * @brief Take the answers that arrived and handle them.
 * @param app The application state (NULL: only free them, when quitting).
 */
static void take_answers(EditorApp *app)
{
    struct RexxMsg *rx;
    if (sl.port == NULL) return;
    while ((rx = (struct RexxMsg *)GetMsg(sl.port)) != NULL) {
        ScoutRequest **p = &sl.requests, *rq;
        int ok = rx->rm_Result1 == RC_OK;
        const char *result = ok ? (const char *)rx->rm_Result2 : NULL;
        while (*p != NULL && (*p)->rx != rx) p = &(*p)->next;
        rq = *p;
        if (rq != NULL) {
            *p = rq->next;
            if (app != NULL) answer(app, rq, ok, result, ok ? 0 : rx->rm_Result2);
            free_request(rq);
        }
        if (result != NULL) DeleteArgstring((UBYTE *)result);
        DeleteArgstring((UBYTE *)rx->rm_Args[0]);
        DeleteRexxMsg(rx);
    }
}

void scoutlink_handle(EditorApp *app)
{
    take_answers(app);
    if (sl.timer_pending && CheckIO((struct IORequest *)sl.timer)) {
        WaitIO((struct IORequest *)sl.timer);
        sl.timer_pending = 0;
        if (sl.idle < PAUSE_TICKS) ++sl.idle;
        tick(app);
    } else if (app->settings.scout) {
        when_ready();
        send_due(app);
    }
    if (timer_needed(app)) start_timer();
}

void scoutlink_cleanup(EditorApp *app)
{
    Document *doc;
    char command[32];
    int tries;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        if (doc->scout_snapshot != NULL) {
            DeleteFile((STRPTR)doc->scout_snapshot);
            FreeVec(doc->scout_snapshot);
            doc->scout_snapshot = NULL;
        }
    if (sl.session > 0) {
        snprintf(command, sizeof(command), "CLOSE %ld", sl.session);
        send_command(command, RQ_CLOSE, 0, -1);
        sl.session = 0;
    }
    /* Scout answers every message (CLOSE answers the queued ones at once);
     * wait for them, at most about 10 seconds */
    for (tries = 0; sl.requests != NULL && tries < 200; ++tries) {
        take_answers(NULL);
        if (sl.requests != NULL) Delay(2);
    }
    if (sl.requests == NULL) {
        if (sl.port != NULL) DeleteMsgPort(sl.port);
        sl.port = NULL;
    }
    /* otherwise the port stays: a late answer must not reach freed memory */
    while (sl.extension_count > 0) free(sl.extensions[--sl.extension_count]);
    if (sl.timer_open) {
        if (sl.timer_pending) {
            AbortIO((struct IORequest *)sl.timer);
            WaitIO((struct IORequest *)sl.timer);
        }
        CloseDevice((struct IORequest *)sl.timer);
    }
    if (sl.timer != NULL) DeleteIORequest((struct IORequest *)sl.timer);
    if (sl.timer_port != NULL) DeleteMsgPort(sl.timer_port);
    sl.timer = NULL;
    sl.timer_port = NULL;
    sl.timer_open = sl.timer_pending = 0;
    if (sl.port == NULL && RexxSysBase != NULL) {
        CloseLibrary((struct Library *)RexxSysBase);
        RexxSysBase = NULL;
    }
}
