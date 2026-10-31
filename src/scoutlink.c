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
 * Scout is started (PROGDIR:TinyDev-Scout, asynchronously) with TinyDev
 * when the setting is on (its languages fill the File Type menu, also
 * for documents without a file), or later when it is needed and its
 * port does not exist; TinyDev then looks for the
 * port for up to ten seconds. Without Scout, or with the setting off
 * (NOSCOUT), TinyDev works on as before. A timer of its own (four times a
 * second while something is to be done) drives the waiting and the pause
 * in typing, as IntuiTicks only come while the main window is active.
 *
 * The project (the folder tree's root) gets a session (OPEN, CLOSE when
 * the project changes or TinyDev quits), with the include drawers of its
 * settings (project.json); saving the settings opens it anew. A document is analysed when it
 * is loaded or saved (the file) and after a pause in typing (a snapshot
 * of the editor's text, written to the drawer of the temporary files, with
 * a version raised for every snapshot). One analysis per document is on
 * its way at a time; a change meanwhile is sent after the answer.
 *
 * Languages: before a document's file is first analysed, Scout is asked
 * for its language (LANGUAGE), which it finds by the extension, the
 * script bit or a .KEY line; files without one are not sent. Until the
 * answer, the extensions of LANGUAGES decide the highlighting. A language
 * chosen in the File Type menu (or "plain text") goes to Scout with
 * LANGUAGE ... SET (TEXT; AUTO takes it back), and for a file of the
 * project into its settings ("languages" of project.json). When the
 * session opens, the project's choices are sent to Scout, so its index
 * uses them too, and a document of the project gets its file's choice.
 *
 * The answers' counts of symbols and diagnostics are shown in the status
 * line. The symbol list asks for the symbols of a document or of the
 * project (SYMBOLS with TO: the answer can exceed what ARexx takes); the
 * file is read, handed to symlist_symbols() and deleted. While the
 * session is open, STATUS with it is asked every two seconds (this also
 * keeps Scout from closing it when memory runs low): while Scout indexes,
 * the number of jobs waiting is shown by the symbol list, and when it
 * drops to nought, or when the session's changes counter moves (Scout
 * noticed files changed on disk), the project's symbols count as changed.
 * A command that fails because Scout no longer knows the session (it
 * closed it when memory ran low) has the session opened again. After an analysis that
 * found problems (or none where there were some), the document's
 * diagnostics are asked for (DIAGNOSTICS ... TO) and go to diag.c.
 *
 * Highlighting: the lexical rules of each language (SYNTAX) are fetched
 * after LANGUAGES and given to the highlighter of the documents with its
 * extensions; they are kept until TinyDev quits (a hook may use them).
 * After each analysis the document's tokens are fetched (TOKENS ... TO)
 * and mapped onto the text that was analysed (kept for that, tokenmap.c);
 * the highlighter's map is swapped under Forbid() and the old one freed
 * at the next swap. The editor highlights anew only when the tokens
 * changed.
 */
#include "editor.h"
#include "json.h"
#include "rules.h"
#include "tokenmap.h"
#include "projconf.h"

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
/** Most languages remembered from LANGUAGES. */
#define MAX_LANGUAGES 16
/** Timer ticks between two STATUS while a session is open (2 s). */
#define STATUS_TICKS 8
/** Scout's error number when no parser handles a file. */
#define SCOUT_E_NO_PARSER 4
/** Scout's error number (RC2): the session is unknown. */
#define SCOUT_E_SESSION 2

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
    RQ_DIAGNOSTICS,     /**< DIAGNOSTICS of a document or the project */
    RQ_DEFINITION,      /**< DEFINITION of a name (Go to Definition) */
    RQ_SYNTAX,          /**< SYNTAX of a language */
    RQ_TOKENS,          /**< TOKENS of a document */
    RQ_COMPLETE,        /**< COMPLETE of a prefix (Edit/Complete) */
    RQ_LANGUAGE         /**< LANGUAGE of a document's file (or a choice of the project) */
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
    char *include;              /**< its include drawers as OPEN's arguments, quoted (malloc()), NULL */
    int languages;              /**< LANGUAGES was answered */
    int asked_languages;        /**< LANGUAGES is on its way */
    char *extensions[MAX_EXTENSIONS];   /**< extensions with a parser (malloc()) */
    int ext_language[MAX_EXTENSIONS];   /**< the language of each (index in languages) */
    int extension_count;        /**< number of them */
    char lang_ids[MAX_LANGUAGES][32];   /**< identifiers of the languages */
    char lang_names[MAX_LANGUAGES][32]; /**< their names */
    ProjConf choices;           /**< the project's settings, for its chosen languages */
    SyntaxRules *rules[MAX_LANGUAGES];  /**< their lexical rules (kept until quitting), NULL */
    int language_count;         /**< number of languages */
    unsigned long project_generation;   /**< see scoutlink_project_generation() */
    int indexing;               /**< the project is being indexed (STATUS is asked) */
    int asked_status;           /**< STATUS is on its way */
    int status_ticks;           /**< timer ticks since the last STATUS */
    long queue;                 /**< Scout's jobs waiting, by the last STATUS */
    long changes;               /**< the session's changes counter by the last STATUS, -1 */
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
    if (sl.state != SL_READY) return;
    if (!sl.languages && !sl.asked_languages && send_command("LANGUAGES", RQ_LANGUAGES, 0, -1))
        sl.asked_languages = 1;
    if (sl.want_open && !sl.opening) {
        size_t n = strlen(sl.project) + (sl.include != NULL ? strlen(sl.include) : 0) + 16;
        char *open = malloc(n);
        if (open == NULL) return;
        snprintf(open, n, "OPEN \"%s\"%s", sl.project, sl.include != NULL ? sl.include : "");
        if (send_command(open, RQ_OPEN, 0, -1)) {
            sl.opening = 1;
            sl.want_open = 0;
        }
        free(open);
    }
}

/**
 * @brief Find one of Scout's languages by its identifier.
 * @param id The identifier.
 * @return Its index, or -1.
 */
static int language_index(const char *id)
{
    int i;
    for (i = 0; i < sl.language_count; ++i)
        if (strcmp(sl.lang_ids[i], id) == 0) return i;
    return -1;
}

/**
 * @brief Return the language of a document: the one chosen, else Scout's,
 *        else (until Scout answered) the one of its extension.
 * @param doc The document.
 * @return The language's index, or -1 for none.
 */
static int doc_language(const Document *doc)
{
    const char *dot;
    int i;
    if (doc->lang_chosen) return language_index(doc->lang_choice);
    if (doc->lang_state == 2) return language_index(doc->lang_detected);
    dot = doc->path != NULL ? strrchr(doc->path, '.') : NULL;
    if (dot == NULL || strchr(dot, '/') != NULL || strchr(dot, ':') != NULL) return -1;
    for (i = 0; i < sl.extension_count; ++i)
        if (Stricmp((STRPTR)sl.extensions[i], (STRPTR)dot) == 0) return sl.ext_language[i];
    return -1;
}

/**
 * @brief Make a file's full path from its path relative to the project.
 * @param file The relative path.
 * @param[out] out The full path.
 * @param size Size of @p out.
 * @return Non-zero on success (zero if too long).
 */
static int project_path(const char *file, char *out, size_t size)
{
    if (strlen(sl.project) >= size) return 0;
    strcpy(out, sl.project);
    return AddPart((STRPTR)out, (STRPTR)file, (ULONG)size) != 0;
}

/**
 * @brief Make a document's editor highlight anew (setting its hook again).
 * @param app The application state.
 * @param doc The document.
 */
static void rehighlight(EditorApp *app, Document *doc)
{
    if (app->window == NULL || doc != app->active) return;
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL, GA_TEXTEDITOR_HighlighterHook, 0, TAG_END);
    SetGadgetAttrs((struct Gadget *)doc->editor, app->window, NULL,
                   GA_TEXTEDITOR_HighlighterHook, (ULONG)&doc->highlight.hook, TAG_END);
    minimap_request(app);
}

/**
 * @brief Give a document the lexical rules of its language (if they
 *        changed, it is highlighted anew).
 * @param app The application state.
 * @param doc The document.
 */
static void apply_rules(EditorApp *app, Document *doc)
{
    int k = app->settings.scout ? doc_language(doc) : -1;
    const SyntaxRules *r = k >= 0 ? sl.rules[k] : NULL;
    /* the editor settings of the file type follow the language too */
    ftprefs_apply(app, doc, 0);
    if (r == doc->highlight.rules) return;
    Forbid();
    doc->highlight.rules = r;
    Permit();
    rehighlight(app, doc);
}

/**
 * @brief Give a document's highlighter new tokens; the old ones are freed
 *        at the next change, as a hook may still read them.
 * @param app The application state.
 * @param doc The document.
 * @param map The new tokens (taken over; NULL for none).
 */
static void set_tokens(EditorApp *app, Document *doc, TokenMap *map)
{
    const TokenMap *old = doc->highlight.tokens;
    int same = (old == NULL && map == NULL) ||
               (old != NULL && map != NULL && old->line_count == map->line_count &&
                old->span_count == map->span_count &&
                memcmp(old->lines, map->lines, (size_t)map->line_count * sizeof(TokenLine)) == 0 &&
                memcmp(old->spans, map->spans, (size_t)map->span_count * sizeof(TokenSpan)) == 0);
    if (same) {
        tokenmap_free(map);
        return;
    }
    tokenmap_free(doc->old_tokens);
    Forbid();
    doc->old_tokens = (TokenMap *)old;
    doc->highlight.tokens = map;
    Permit();
    rehighlight(app, doc);
}

/**
 * @brief Keep a copy of the text an analysis is of (for its tokens).
 * @param doc The document.
 * @param text The text (taken over, AllocVec(); NULL drops the copy).
 * @param version Its version, -1 for the file.
 */
static void keep_text(Document *doc, char *text, long version)
{
    if (doc->scout_text != NULL) FreeVec(doc->scout_text);
    doc->scout_text = text;
    doc->scout_text_version = version;
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
    keep_text(doc, text, doc->scout_version + 1);   /* the version analyze() gives it */
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
        /* the editor's text is the file's: the tokens are mapped onto it */
        keep_text(doc, (char *)DoMethod(doc->editor, GM_TEXTEDITOR_ExportText, NULL), -1);
    }
    if (send_command(command, RQ_ANALYZE, doc->number, version)) {
        doc->scout_busy = 1;
        doc->scout_pending = 0;
        doc->scout_now = 0;
    }
}

/**
 * @brief Report a failure to read or write the project's settings (once
 *        per project).
 * @param app The application state.
 * @param id The message (MSG_, with %s for the file).
 */
static void report_choice(EditorApp *app, LONG id)
{
    static char reported[EDITOR_PATH_MAX];
    char name[EDITOR_PATH_MAX], text[EDITOR_PATH_MAX + 160];
    if (strcmp(reported, sl.project) == 0) return;
    strcpy(reported, sl.project);
    if (!projdata_path(sl.project, "project.json", name, sizeof(name))) return;
    snprintf(text, sizeof(text), S(id), name);
    ui_error(app, S(MSG_MENU_FILETYPE), text);
}

/**
 * @brief Write a document's chosen language (or that it has none) into
 *        the project's settings, if its file is in the project and the
 *        settings say otherwise.
 * @param app The application state.
 * @param doc The document.
 */
static void store_choice(EditorApp *app, Document *doc)
{
    const char *id = doc->lang_chosen ? doc->lang_choice : NULL, *file, *old;
    char *text = NULL, *json = NULL;
    ProjConf c;
    int r;
    doc->lang_store = 0;
    if (sl.project[0] == '\0' || doc->path == NULL || (file = projconf_relative(sl.project, doc->path)) == NULL)
        return;
    old = projconf_language(&sl.choices, file);
    if ((old == NULL && id == NULL) || (old != NULL && id != NULL && strcmp(old, id) == 0)) return;
    r = projdata_read(sl.project, "project.json", &text);
    projconf_init(&c);
    if (r < 0 || (r > 0 && projconf_parse(&c, text) <= 0)) {
        /* a file that cannot be read is never overwritten */
        report_choice(app, MSG_ERR_FT_READ);
    } else if (projconf_language_set(&c, file, id) < 0 || (json = projconf_format(&c, text)) == NULL ||
               !projdata_write(sl.project, "project.json", json)) {
        report_choice(app, MSG_ERR_FT_WRITE);
    } else {
        projconf_language_set(&sl.choices, file, id);
    }
    free(json);
    projconf_clear(&c);
    free(text);
}

/**
 * @brief Ask Scout for the language of a document's file, or tell it the
 *        one chosen. A document of the project without a choice gets its
 *        file's choice from the project's settings first.
 * @param app The application state.
 * @param doc The document (with a file).
 */
static void ask_language(EditorApp *app, Document *doc)
{
    char command[2 * EDITOR_PATH_MAX + 64], session[24];
    const char *file, *id;
    if (!doc->lang_chosen && !doc->lang_unset && sl.project[0] != '\0' &&
        (file = projconf_relative(sl.project, doc->path)) != NULL &&
        (id = projconf_language(&sl.choices, file)) != NULL && strlen(id) < sizeof(doc->lang_choice)) {
        strcpy(doc->lang_choice, id);
        doc->lang_chosen = 1;
    }
    if (doc->lang_store) store_choice(app, doc);
    session[0] = '\0';
    if (sl.session > 0) snprintf(session, sizeof(session), "SESSION %ld ", sl.session);
    if (doc->lang_chosen && doc->lang_choice[0] != '\0')
        snprintf(command, sizeof(command), "LANGUAGE %sFILE \"%s\" SET \"%s\"", session, doc->path, doc->lang_choice);
    else if (doc->lang_chosen)
        snprintf(command, sizeof(command), "LANGUAGE %sFILE \"%s\" TEXT", session, doc->path);
    else
        snprintf(command, sizeof(command), "LANGUAGE %sFILE \"%s\"%s", session, doc->path,
                 doc->lang_unset ? " AUTO" : "");
    if (send_command(command, RQ_LANGUAGE, doc->number, -1)) {
        doc->lang_state = 1;
        doc->lang_unset = 0;
    }
}

/**
 * @brief Tell Scout the languages chosen in the project's settings, so its
 *        index uses them (after the session opened).
 */
static void send_choices(void)
{
    char command[2 * EDITOR_PATH_MAX + 64], path[EDITOR_PATH_MAX];
    long i;
    for (i = 0; i < sl.choices.lang_count; ++i) {
        const char *id = sl.choices.lang_ids[i];
        if (!project_path(sl.choices.lang_files[i], path, sizeof(path))) continue;
        if (id[0] != '\0')
            snprintf(command, sizeof(command), "LANGUAGE SESSION %ld FILE \"%s\" SET \"%s\"", sl.session, path, id);
        else
            snprintf(command, sizeof(command), "LANGUAGE SESSION %ld FILE \"%s\" TEXT", sl.session, path);
        send_command(command, RQ_LANGUAGE, 0, -1);
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
        if (doc->lang_state == 0) ask_language(app, doc);
        if (doc->lang_state != 2) continue;
        if (doc->scout_none || doc_language(doc) < 0) { doc->scout_pending = 0; continue; }
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
        doc->lang_state = 0;
    }
}

/**
 * @brief Ask for the tokens of a document's analysis.
 * @param app The application state.
 * @param doc The document.
 * @param version The analysis's version (-1 for the file).
 */
static void request_tokens(EditorApp *app, Document *doc, long version)
{
    char command[3 * EDITOR_PATH_MAX], to[EDITOR_PATH_MAX], session[24];
    char *copy;
    if (!temp_name(app, ".json", to, sizeof(to))) return;
    session[0] = '\0';
    if (sl.session > 0) snprintf(session, sizeof(session), "SESSION %ld ", sl.session);
    snprintf(command, sizeof(command), "TOKENS %sFILE \"%s\" TO \"%s\"", session, doc->path, to);
    if ((copy = AllocVec(strlen(to) + 1, MEMF_ANY)) == NULL) return;
    strcpy(copy, to);
    if (!send_command(command, RQ_TOKENS, doc->number, version)) {
        FreeVec(copy);
        return;
    }
    sl.last->to = copy;
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
        const JsonValue *lang = json_array_get(langs, i);
        const JsonValue *exts = json_obj_get_array(lang, "extensions");
        const char *id = json_obj_get_string(lang, "id", "");
        int k;
        /* a language known before keeps its rules (a hook may use them) */
        for (k = 0; k < sl.language_count && strcmp(sl.lang_ids[k], id) != 0; ++k) ;
        if (k == sl.language_count) {
            if (k == MAX_LANGUAGES || strlen(id) >= sizeof(sl.lang_ids[0])) continue;
            strcpy(sl.lang_ids[k], id);
            sl.rules[k] = NULL;
            ++sl.language_count;
        }
        strncpy(sl.lang_names[k], json_obj_get_string(lang, "name", id), sizeof(sl.lang_names[k]) - 1);
        sl.lang_names[k][sizeof(sl.lang_names[k]) - 1] = '\0';
        for (j = 0; exts != NULL && j < json_array_length(exts); ++j) {
            const JsonValue *e = json_array_get(exts, j);
            char *copy;
            if (e == NULL || e->type != JSON_STRING || sl.extension_count >= MAX_EXTENSIONS) continue;
            if ((copy = malloc(strlen(e->u.str_val) + 1)) == NULL) continue;
            strcpy(copy, e->u.str_val);
            sl.ext_language[sl.extension_count] = k;
            sl.extensions[sl.extension_count++] = copy;
        }
        if (sl.rules[k] == NULL) {
            char command[64];
            snprintf(command, sizeof(command), "SYNTAX LANG \"%s\"", id);
            send_command(command, RQ_SYNTAX, (unsigned long)k, -1);
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
    if (!ok && code == SCOUT_E_SESSION && rq->session > 0 && rq->session == sl.session) {
        /* Scout closed the session (memory ran low): open it again */
        sl.session = 0;
        sl.indexing = 0;
        sl.want_open = sl.project[0] != '\0';
    }
    switch (rq->kind) {
        case RQ_LANGUAGES:
            sl.asked_languages = 0;
            if (ok && result != NULL) {
                read_languages(result);
                sl.languages = 1;
                ui_update_file_types(app);
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
                sl.changes = -1;
                send_choices();
                all_pending(app);
            }
            break;
        case RQ_STATUS:
            sl.asked_status = 0;
            if (ok && result != NULL && rq->session == sl.session) {
                long queue = result_int(result, "queue", 0);
                long changes = result_int(result, "changes", -1);
                if (queue > 0 || result_int(result, "walks", 0) > 0) {
                    sl.indexing = 1;
                } else if (sl.indexing) {
                    /* done: nothing waits and no project is walked */
                    sl.indexing = 0;
                    ++sl.project_generation;
                } else if (changes != sl.changes && sl.changes >= 0) {
                    ++sl.project_generation;
                }
                sl.changes = changes;
                sl.queue = queue;
            }
            break;
        case RQ_SYMBOLS: {
            char *text = ok && rq->to != NULL ? read_file(rq->to) : NULL;
            symlist_symbols(app, rq->doc, text);
            if (text != NULL) FreeVec(text);
            break;
        }
        case RQ_DEFINITION:
            navigate_definition_answer(app, ok ? result : NULL);
            break;
        case RQ_COMPLETE:
            complete_answer(app, ok ? result : NULL);
            break;
        case RQ_SYNTAX:
            if (ok && result != NULL && rq->doc < (unsigned long)sl.language_count && sl.rules[rq->doc] == NULL) {
                sl.rules[rq->doc] = rules_parse(result);
                for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
                     doc = (Document *)doc->node.ln_Succ)
                    apply_rules(app, doc);
            }
            break;
        case RQ_TOKENS:
            if ((doc = find_document(app, rq->doc)) != NULL && ok && rq->to != NULL &&
                doc->scout_text != NULL && rq->version == doc->scout_text_version) {
                char *text = read_file(rq->to);
                long count = 0, *values = text != NULL ? tokenmap_read(text, &count) : NULL;
                if (text != NULL) FreeVec(text);
                set_tokens(app, doc, values != NULL ?
                           tokenmap_build(doc->scout_text, strlen(doc->scout_text), values, count) : NULL);
                free(values);
            }
            break;
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
        case RQ_LANGUAGE:
            if (rq->doc == 0 || (doc = find_document(app, rq->doc)) == NULL) break;
            if (rq->session != sl.session) { doc->lang_state = 0; break; }
            doc->lang_state = 2;
            doc->lang_detected[0] = '\0';
            if (ok && result != NULL) {
                JsonValue *v = json_parse(result, NULL);
                const char *id = v != NULL ? json_obj_get_string(v, "language", "") : "";
                if (strlen(id) < sizeof(doc->lang_detected)) strcpy(doc->lang_detected, id);
                json_value_free(v);
            }
            apply_rules(app, doc);
            break;
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
                /* the tokens of the text that was analysed */
                if (doc->scout_text != NULL && rq->version == doc->scout_text_version) request_tokens(app, doc, rq->version);
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
 * @brief Tell whether the timer is needed: Scout is starting, a session
 *        is open (STATUS), or a document waits for its analysis.
 * @param app The application state.
 * @return Non-zero if it is.
 */
static int timer_needed(EditorApp *app)
{
    Document *doc;
    if (!app->settings.scout || sl.state == SL_FAILED) return 0;
    if (sl.state == SL_STARTING || sl.indexing || (sl.session > 0 && sl.state == SL_READY)) return 1;
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
    if (sl.session > 0 && sl.state == SL_READY && !sl.asked_status && ++sl.status_ticks >= STATUS_TICKS) {
        char command[32];
        sl.status_ticks = 0;
        snprintf(command, sizeof(command), "STATUS SESSION %ld", sl.session);
        if (send_command(command, RQ_STATUS, 0, -1)) sl.asked_status = 1;
    }
}

/**
 * @brief Read a project's settings: the include drawers into OPEN's
 *        arguments (sl.include), the chosen languages into sl.choices.
 * @param root The project directory.
 */
static void read_project(const char *root)
{
    char *text = NULL;
    ProjConf c;
    long i;
    free(sl.include);
    sl.include = NULL;
    projconf_clear(&sl.choices);
    if (projdata_read(root, "project.json", &text) <= 0) return;
    projconf_init(&c);
    if (projconf_parse(&c, text) > 0) {
        for (i = 0; i < c.lang_count; ++i)
            projconf_language_set(&sl.choices, c.lang_files[i], c.lang_ids[i]);
        for (i = 0; i < c.include_count; ++i) {
            char *full = projconf_include_full(&c, i, root);
            size_t old = sl.include != NULL ? strlen(sl.include) : 0;
            char *bigger;
            if (full == NULL) continue;
            bigger = realloc(sl.include, old + strlen(full) + 4);
            if (bigger != NULL) {
                sl.include = bigger;
                snprintf(sl.include + old, strlen(full) + 4, " \"%s\"", full);
            }
            free(full);
        }
    }
    projconf_clear(&c);
    free(text);
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
    read_project(root);
    sl.want_open = 1;
    connect(app);
    when_ready();
    if (sl.state == SL_STARTING) start_timer();
}

void scoutlink_start(EditorApp *app)
{
    if (!app->settings.scout) return;
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
        scoutlink_start(app);
        all_pending(app);
        return;
    }
    scoutlink_project_opened(app);      /* closes the session */
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ) {
        doc->scout_pending = doc->scout_now = 0;
        doc->scout_symbols = -1;
        if (doc->diag_count > 0 || doc->diag_mark_count > 0) diag_set_document(app, doc, NULL);
        set_tokens(app, doc, NULL);
        apply_rules(app, doc);
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
    /* the file may be another one now (Save As), or its bits changed */
    doc->lang_state = 0;
    doc->lang_store = doc->lang_chosen;
    start_timer();
}

void scoutlink_choose_language(EditorApp *app, Document *doc, const char *id)
{
    if (id != NULL) {
        if (strlen(id) >= sizeof(doc->lang_choice)) return;
        strcpy(doc->lang_choice, id);
        doc->lang_chosen = 1;
        doc->lang_unset = 0;
    } else {
        if (doc->lang_chosen) doc->lang_unset = 1;
        doc->lang_chosen = 0;
    }
    doc->lang_state = 0;
    doc->lang_detected[0] = '\0';
    doc->lang_store = 1;
    doc->scout_pending = doc->scout_now = 1;
    doc->scout_none = 0;
    doc->scout_symbols = -1;
    doc->scout_problems = 0;
    /* what the old language's parser found no longer counts */
    set_tokens(app, doc, NULL);
    if (doc->diag_count > 0 || doc->diag_mark_count > 0) diag_set_document(app, doc, NULL);
    apply_rules(app, doc);
    if (doc->path != NULL) store_choice(app, doc);
    start_timer();
}

int scoutlink_language_count(void)
{
    return sl.language_count;
}

const char *scoutlink_language_id(int index)
{
    return sl.lang_ids[index];
}

const char *scoutlink_language_name(int index)
{
    return sl.lang_names[index];
}

const char *scoutlink_document_language(const Document *doc)
{
    int k = doc_language(doc);
    return k >= 0 ? sl.lang_ids[k] : "";
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

int scoutlink_request_definition(EditorApp *app, Document *doc, long line, long col, const char *name)
{
    char command[2 * EDITOR_PATH_MAX], session[24];
    if (scoutlink_unavailable(app) || !sl.languages || sl.opening || sl.want_open || doc->path == NULL) return 0;
    session[0] = '\0';
    if (sl.session > 0) snprintf(session, sizeof(session), "SESSION %ld ", sl.session);
    snprintf(command, sizeof(command), "DEFINITION %sFILE \"%s\" LINE %ld COLUMN %ld NAME \"%s\"",
             session, doc->path, line, col, name);
    return send_command(command, RQ_DEFINITION, doc->number, -1);
}

int scoutlink_request_complete(EditorApp *app, Document *doc, long line, long col, const char *prefix)
{
    char command[2 * EDITOR_PATH_MAX], session[24];
    if (scoutlink_unavailable(app) || !sl.languages || sl.opening || sl.want_open || doc->path == NULL) return 0;
    session[0] = '\0';
    if (sl.session > 0) snprintf(session, sizeof(session), "SESSION %ld ", sl.session);
    snprintf(command, sizeof(command), "COMPLETE %sFILE \"%s\" LINE %ld COLUMN %ld PREFIX \"%s\" MAX 50",
             session, doc->path, line, col, prefix);
    return send_command(command, RQ_COMPLETE, doc->number, -1);
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

void scoutlink_document_free(Document *doc)
{
    keep_text(doc, NULL, -1);
    tokenmap_free((TokenMap *)doc->highlight.tokens);
    tokenmap_free(doc->old_tokens);
    doc->highlight.tokens = NULL;
    doc->old_tokens = NULL;
}

void scoutlink_free_rules(void)
{
    int i;
    for (i = 0; i < sl.language_count; ++i) {
        rules_free(sl.rules[i]);
        sl.rules[i] = NULL;
    }
    sl.language_count = 0;
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
    Document *doc;
    take_answers(app);
    /* a document that got a file (or another one) gets its language's rules */
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ;
         doc = (Document *)doc->node.ln_Succ)
        apply_rules(app, doc);
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
    free(sl.include);
    sl.include = NULL;
    projconf_clear(&sl.choices);
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
