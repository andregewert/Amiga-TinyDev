/**
 * @file git.c
 * @brief git through am-git: running its commands, the status in the
 *        folder tree and the status line, and the Git menu.
 *
 * TinyDev links no git code: it runs am-git, the command of the setting
 * "git"/"command" (am-git, found in the command path, by default), and
 * only when the setting "git"/"enabled" is on. The Git menu is shown only
 * then (ui_update_git_menu()).
 *
 * A command runs synchronously (busy pointer) through a script in the
 * drawer of the temporary files: "Stack 100000" first, as am-git wants
 * that much and TinyDev started from the Workbench has no Shell whose
 * stack it would get; then am-git with its arguments. The Shell gets
 * TinyDev's command path (cmdpath_copy()) and runs in the drawer given.
 * Output and messages go to one file, read afterwards, as with Sprout
 * (newproj.c): pr_CES and NP_Error point at it for the time of the call.
 *
 * am-git's init needs the drawer as an argument (it makes it if needed),
 * so TinyDev always passes the project drawer's full name; a drawer that
 * is already a project can be made a repository as well.
 *
 * The status ("am-git status --porcelain", read with gitstatus.c) runs
 * in a process of its own, so TinyDev goes on meanwhile: when a project
 * that holds .git is opened, after a document was saved, when the folder
 * tree reports a change and on Git/Refresh Status. One run at a time; a
 * request while it runs makes it run once more afterwards. A change the
 * tree reports while a run goes or within GIT_QUIET seconds after it is
 * left alone: am-git writes into .git, and an expanded .git in the tree
 * would otherwise start one run after the other. The process
 * calls SystemTags() synchronously and reports with a message, sent under
 * Forbid() as in build.c, so it is gone before TinyDev can quit. The
 * result colours the tree's entries through dirtree.gadget's
 * DIRTREE_PenHook (pens obtained from the setting "git"/"colours"); the
 * branch, read from .git/HEAD, goes to the status line.
 *
 * Git/Fetch, Pull and Push run in the background like Build
 * (build_run_job()), their output in the output window, with the remote
 * "origin" of .git/config (set in the project settings). No status runs
 * meanwhile; one follows. Pull saves the documents first and remembers
 * the date and size of their files; afterwards a changed file is loaded
 * again (a document changed meanwhile only if the user agrees) and the
 * document of a deleted file is marked changed. am-git asks nothing for
 * https (the credentials come from its environment variables), but
 * bebbossh's question whether to trust a new host would find no input:
 * that is answered once in a Shell. am-git 0.12.1 writes the files of a
 * merge into a drawer "." (git_fix_dot_drawer()); after a Pull TinyDev offers
 * to move them to their places, before the documents are compared. Git/Clone Repository... runs
 * "am-git clone <url> <name>" the same way in the drawer chosen (the name
 * alone, as am-git's paths with "./" make drawers named "."), and opens
 * the new drawer as the project when it holds .git afterwards.
 *
 * Seen on the Amiga with am-git 0.12.1: file names come through as the
 * bytes on disk (ISO-8859-1), so no conversion is needed; commit messages
 * have a single line (see TECHNICAL.md).
 */
#include "editor.h"

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/layout.h>
#include <gadgets/dirtree.h>
#include <gadgets/texteditor.h>
#include <clib/alib_protos.h>

#include "gitstatus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Stack am-git gets (bytes); it asks for 100000. */
#define GIT_STACK 100000
/** The longest output shown in a message (characters). */
#define GIT_SHOWN 1200
/** Seconds after a status run in which the tree's changes start none. */
#define GIT_QUIET 2

/** State of the status runs and their result. */
static struct {
    int running;                        /* a status process runs */
    int pending;                        /* run again when it is done */
    char run_root[EDITOR_PATH_MAX];     /* the repository of the run */
    char script[EDITOR_PATH_MAX];       /* its script */
    char output[EDITOR_PATH_MAX];       /* its output */
    char command[EDITOR_PATH_MAX + 16]; /* what the Shell runs */
    struct MsgPort *port;               /* the process reports here */
    struct Message msg;
    LONG rc;                            /* return code (set by the process) */
    BPTR in, out;                       /* NIL:, the output file */
    BPTR path;                          /* command path for the Shell */
    ULONG finished;                     /* when the last run ended (seconds) */
    char root[EDITOR_PATH_MAX];         /* the repository the status is of, "" if none */
    GitStatus status;                   /* its changed files */
    char branch[64];                    /* its branch, "" if not known */
    LONG pens[GIT_ST_COUNT];            /* pens of the states, -1 if none */
    int pens_open;                      /* the pens are obtained */
    struct Screen *pen_screen;          /* the screen they are obtained on */
    struct Hook pen_hook;               /* DIRTREE_PenHook */
    int job;                            /* Fetch, Pull, Push or Clone runs (build_run_job()) */
    char clone_target[EDITOR_PATH_MAX]; /* the drawer a Clone makes, "" if none runs */
    struct PullDoc *pull_docs;          /* the documents' files when a Pull started */
    long pull_count;                    /* their number */
    int pulling;                        /* the job is a Pull */
    char job_root[EDITOR_PATH_MAX];     /* the repository of Fetch, Pull or Push */
} gs;

/** A document's file as it was when a Pull started. */
typedef struct PullDoc {
    char path[EDITOR_PATH_MAX];         /**< the file (the document's path) */
    struct DateStamp date;              /**< its date */
    LONG size;                          /**< its size */
} PullDoc;

/**
 * @brief Put a string in double quotes for the Shell (BCPL escapes: *",
 *        ** and *N).
 * @param[out] out The result.
 * @param size Its size.
 * @param s The string.
 * @return Non-zero if it fitted.
 */
static int quote(char *out, size_t size, const char *s)
{
    size_t n = 0;
    if (size < 3) return 0;
    out[n++] = '"';
    for (; *s != '\0'; ++s) {
        const char *put = *s == '"' ? "*\"" : *s == '*' ? "**" : *s == '\n' ? "*N" : NULL;
        size_t len = put != NULL ? 2 : 1;
        if (n + len + 2 > size) return 0;
        if (put != NULL) memcpy(out + n, put, 2);
        else out[n] = *s;
        n += len;
    }
    out[n++] = '"';
    out[n] = '\0';
    return 1;
}

int git_enabled(EditorApp *app)
{
    return app->settings.git;
}

int git_quote(char *out, size_t size, const char *s)
{
    return quote(out, size, s);
}

/**
 * @brief The text of a script that runs am-git: "Stack 100000", then the
 *        command with its arguments.
 * @param app The application state (the command).
 * @param args am-git's arguments, quoted as needed.
 * @param[out] text The script.
 * @param size Size of @p text.
 * @return Non-zero if it fitted.
 */
static int script_text(EditorApp *app, const char *args, char *text, size_t size)
{
    char cmd[sizeof(app->settings.gitcommand) * 2 + 4];
    const char *command = app->settings.gitcommand[0] != '\0' ? app->settings.gitcommand : "am-git";
    /* a name without spaces stays as it is, so the Shell searches the path
     * for it as for any command */
    if (strpbrk(command, " \"*;") == NULL) strcpy(cmd, command);
    else if (!quote(cmd, sizeof(cmd), command)) return 0;
    return snprintf(text, size, "Stack %ld\n%s %s\n", (long)GIT_STACK, cmd, args) < (int)size;
}

/**
 * @brief Write the script that runs am-git.
 * @param app The application state (the command).
 * @param script The script's name.
 * @param args am-git's arguments, quoted as needed.
 * @return Non-zero on success.
 */
static int write_script(EditorApp *app, const char *script, const char *args)
{
    char text[sizeof(app->settings.gitcommand) * 2 + EDITOR_PATH_MAX * 2 + 64];
    BPTR fh;
    int ok;
    if (!script_text(app, args, text, sizeof(text))) return 0;
    if ((fh = Open((STRPTR)script, MODE_NEWFILE)) == 0) return 0;
    ok = FPuts(fh, (STRPTR)text) == 0;
    ok = Close(fh) && ok;
    if (!ok) DeleteFile((STRPTR)script);
    return ok;
}

LONG git_run(EditorApp *app, const char *dir, const char *args, char **output)
{
    char script[EDITOR_PATH_MAX], out[EDITOR_PATH_MAX], command[EDITOR_PATH_MAX + 16];
    struct Process *me = (struct Process *)FindTask(NULL);
    BPTR in = 0, ofh = 0, lock = 0, old_dir, old_ces, path;
    LONG rc = -1;

    if (output != NULL) *output = NULL;
    if (!temp_name(app, ".script", script, sizeof(script)) || !temp_name(app, ".txt", out, sizeof(out)))
        return -1;
    if (!write_script(app, script, args)) return -1;
    snprintf(command, sizeof(command), "Execute \"%s\"", script);
    if ((lock = Lock((STRPTR)dir, ACCESS_READ)) == 0 ||
        (in = Open((STRPTR)"NIL:", MODE_OLDFILE)) == 0 ||
        (ofh = Open((STRPTR)out, MODE_NEWFILE)) == 0)
        goto done;
    if (app->window_object != NULL) SetAttrs(app->window_object, WA_BusyPointer, TRUE, TAG_END);
    path = cmdpath_copy();
    old_dir = CurrentDir(lock);
    old_ces = me->pr_CES;
    me->pr_CES = ofh;
    rc = SystemTags((STRPTR)command, SYS_Input, (ULONG)in, SYS_Output, (ULONG)ofh,
                    SYS_UserShell, TRUE, NP_Error, (ULONG)ofh, NP_CloseError, FALSE,
                    path != 0 ? NP_Path : TAG_IGNORE, (ULONG)path, TAG_DONE);
    me->pr_CES = old_ces;
    CurrentDir(old_dir);
    if (rc == -1) cmdpath_free(path);
    if (app->window_object != NULL) SetAttrs(app->window_object, WA_BusyPointer, FALSE, TAG_END);
done:
    if (ofh != 0) Close(ofh);
    if (in != 0) Close(in);
    if (lock != 0) UnLock(lock);
    if (rc != -1 && output != NULL && datafile_read(out, output) <= 0) *output = NULL;
    DeleteFile((STRPTR)out);
    DeleteFile((STRPTR)script);
    return rc;
}

void git_report(EditorApp *app, struct Window *win, LONG id, const char *output)
{
    struct EasyStruct es = {sizeof(es), 0, NULL, (STRPTR)"%s", NULL};
    ULONG args[1];
    char shown[GIT_SHOWN + 4], *text;
    size_t n = output != NULL ? strlen(output) : 0;
    if (n > GIT_SHOWN) n = GIT_SHOWN;
    if (n > 0) memcpy(shown, output, n);
    while (n > 0 && (shown[n - 1] == '\n' || shown[n - 1] == '\r' || shown[n - 1] == ' ')) --n;
    shown[n] = '\0';
    if (output != NULL && strlen(output) > GIT_SHOWN) strcpy(shown + n, "...");
    if ((text = malloc(strlen(S(id)) + strlen(shown) + 1)) == NULL) return;
    sprintf(text, S(id), shown);
    es.es_Title = (STRPTR)S(MSG_GIT_TITLE);
    es.es_GadgetFormat = (STRPTR)S(MSG_OK);
    args[0] = (ULONG)text;
    /* the output through "%s": a '%' in it is no format */
    EasyRequestArgs(win != NULL ? win : app->window, &es, NULL, args);
    free(text);
}

int git_is_repository(const char *drawer)
{
    char path[EDITOR_PATH_MAX];
    BPTR lock;
    strcpy(path, drawer);
    if (!AddPart((STRPTR)path, (STRPTR)".git", sizeof(path)) || (lock = Lock((STRPTR)path, ACCESS_READ)) == 0)
        return 0;
    UnLock(lock);
    return 1;
}

int git_init(EditorApp *app, const char *drawer)
{
    char full[EDITOR_PATH_MAX], args[EDITOR_PATH_MAX + 16], *output = NULL;
    BPTR lock;
    LONG rc;
    int ok;

    if ((lock = Lock((STRPTR)drawer, ACCESS_READ)) == 0) return 0;
    ok = NameFromLock(lock, (STRPTR)full, sizeof(full)) != 0;
    UnLock(lock);
    if (!ok) return 0;
    strcpy(args, "init ");
    if (!quote(args + 5, sizeof(args) - 5, full)) {
        ui_error(app, S(MSG_GIT_TITLE), S(MSG_ERR_PATHTOOLONG));
        return 0;
    }
    rc = git_run(app, full, args, &output);
    /* am-git may answer 0 although it failed: the drawer must hold .git */
    ok = rc == 0 && git_is_repository(full);
    if (rc == -1) ui_error(app, S(MSG_GIT_TITLE), S(MSG_GIT_NORUN));
    else if (!ok) git_report(app, NULL, MSG_GIT_INITFAILED, output);
    else ui_status_message(app, S(MSG_GIT_CREATED));
    free(output);
    if (ok) git_project_changed(app);
    return ok;
}

/**
 * @brief Git/Create Repository: make the project a repository, after
 *        asking.
 * @param app The application state.
 */
static void init_project(EditorApp *app)
{
    char root[EDITOR_PATH_MAX];
    struct EasyStruct es = {sizeof(es), 0, NULL, NULL, NULL};
    ULONG args[1];

    if (!tree_project_root(app, root, sizeof(root))) {
        ui_error(app, S(MSG_GIT_TITLE), S(MSG_GIT_NOPROJECT));
        return;
    }
    if (git_is_repository(root)) {
        char text[EDITOR_PATH_MAX + 100];
        snprintf(text, sizeof(text), S(MSG_GIT_ALREADY), root);
        ui_error(app, S(MSG_GIT_TITLE), text);
        return;
    }
    es.es_Title = (STRPTR)S(MSG_GIT_TITLE);
    es.es_TextFormat = (STRPTR)S(MSG_GIT_INITASK);
    es.es_GadgetFormat = (STRPTR)S(MSG_GIT_INITGADGETS);
    args[0] = (ULONG)root;
    if (EasyRequestArgs(app->window, &es, NULL, args) != 1) return;
    git_init(app, root);
}

/* ---- the status --------------------------------------------------------- */

/**
 * @brief The status process: runs the script through the Shell and reports
 *        the return code (see build.c's build_process()).
 */
static void status_process(void)
{
    gs.rc = SystemTags((STRPTR)gs.command, SYS_Input, (ULONG)gs.in, SYS_Output, (ULONG)gs.out,
                       SYS_UserShell, TRUE, NP_Error, (ULONG)gs.out, NP_CloseError, FALSE,
                       gs.path != 0 ? NP_Path : TAG_IGNORE, (ULONG)gs.path, TAG_DONE);
    if (gs.rc == -1) cmdpath_free(gs.path);
    gs.path = 0;
    Forbid();
    PutMsg(gs.port, &gs.msg);
}

/**
 * @brief Close what a status run used and delete its files.
 */
static void status_release(void)
{
    if (gs.out != 0) Close(gs.out);
    if (gs.in != 0) Close(gs.in);
    gs.out = gs.in = 0;
    if (gs.script[0] != '\0') DeleteFile((STRPTR)gs.script);
    if (gs.output[0] != '\0') DeleteFile((STRPTR)gs.output);
    gs.script[0] = gs.output[0] = '\0';
}

/**
 * @brief Read the branch of the repository from .git/HEAD.
 * @param root The repository's drawer.
 */
static void read_branch(const char *root)
{
    char path[EDITOR_PATH_MAX], *text = NULL;
    gs.branch[0] = '\0';
    strcpy(path, root);
    if (!AddPart((STRPTR)path, (STRPTR)".git/HEAD", sizeof(path))) return;
    if (datafile_read(path, &text) > 0) gitstatus_head(text, gs.branch, sizeof(gs.branch));
    free(text);
}

/**
 * @brief Let the folder tree ask for its entries' pens again.
 * @param app The application state.
 */
static void update_tree(EditorApp *app)
{
    if (app->tree == NULL) return;
    /* the tree may be on the hidden page of its column */
    if (app->window != NULL)
        SetPageGadgetAttrs((struct Gadget *)app->tree, app->tree_column, app->window, NULL,
                           DIRTREE_UpdatePens, TRUE, TAG_END);
    else SetAttrs(app->tree, DIRTREE_UpdatePens, TRUE, TAG_END);
}

/**
 * @brief Start a status run of the project, if it is a repository.
 * @param app The application state.
 */
static void status_start(EditorApp *app)
{
    char root[EDITOR_PATH_MAX];
    BPTR lock = 0;
    struct Process *proc;

    if (!git_enabled(app) || !tree_project_root(app, root, sizeof(root)) || !git_is_repository(root)) return;
    if (gs.job) {               /* after Fetch, Pull or Push (job_done()) */
        gs.pending = 1;
        return;
    }
    strcpy(gs.run_root, root);
    if (!temp_name(app, ".script", gs.script, sizeof(gs.script)) ||
        !temp_name(app, ".txt", gs.output, sizeof(gs.output))) {
        gs.script[0] = gs.output[0] = '\0';
        return;
    }
    if (!write_script(app, gs.script, "status --porcelain")) {
        gs.script[0] = '\0';
        status_release();
        return;
    }
    snprintf(gs.command, sizeof(gs.command), "Execute \"%s\"", gs.script);
    if ((gs.port == NULL && (gs.port = CreateMsgPort()) == NULL) ||
        (lock = Lock((STRPTR)root, ACCESS_READ)) == 0 ||
        (gs.in = Open((STRPTR)"NIL:", MODE_OLDFILE)) == 0 ||
        (gs.out = Open((STRPTR)gs.output, MODE_NEWFILE)) == 0) {
        if (lock != 0) UnLock(lock);
        status_release();
        return;
    }
    memset(&gs.msg, 0, sizeof(gs.msg));
    gs.msg.mn_Node.ln_Type = NT_MESSAGE;
    gs.msg.mn_Length = sizeof(gs.msg);
    gs.rc = -1;
    /* the lock and the path go to the process, which frees them */
    gs.path = cmdpath_copy();
    proc = CreateNewProcTags(NP_Entry, (ULONG)status_process, NP_Name, (ULONG)"TinyDev Git Status",
                             NP_StackSize, 8192, NP_CurrentDir, (ULONG)lock, TAG_DONE);
    if (proc == NULL) {
        UnLock(lock);
        cmdpath_free(gs.path);
        gs.path = 0;
        status_release();
        return;
    }
    gs.running = 1;
    gs.pending = 0;
}

void git_status_request(EditorApp *app)
{
    if (!git_enabled(app)) return;
    if (gs.running) gs.pending = 1;
    else status_start(app);
}

void git_tree_changed(EditorApp *app)
{
    ULONG secs = 0, micros = 0;
    if (!git_enabled(app) || gs.running || gs.job || gs.root[0] == '\0') return;
    CurrentTime(&secs, &micros);
    if (secs >= gs.finished && secs - gs.finished < GIT_QUIET) return;
    status_start(app);
}

void git_project_changed(EditorApp *app)
{
    char root[EDITOR_PATH_MAX];
    int repo = git_enabled(app) && tree_project_root(app, root, sizeof(root)) && git_is_repository(root);
    /* a run of the old project is still taken, then dropped (gs.run_root) */
    gitstatus_clear(&gs.status);
    gs.root[0] = gs.branch[0] = '\0';
    if (repo) {
        strcpy(gs.root, root);
        read_branch(root);
    }
    update_tree(app);
    if (repo) git_status_request(app);
}

ULONG git_sigmask(void)
{
    return gs.running && gs.port != NULL ? 1UL << gs.port->mp_SigBit : 0;
}

void git_handle(EditorApp *app, ULONG signals)
{
    char *text = NULL;
    if (!gs.running || gs.port == NULL || (signals & (1UL << gs.port->mp_SigBit)) == 0) return;
    if (GetMsg(gs.port) == NULL) return;
    gs.running = 0;
    {
        ULONG micros;
        CurrentTime(&gs.finished, &micros);
    }
    if (gs.out != 0) {
        Close(gs.out);
        gs.out = 0;
    }
    /* only the result of the project still open counts */
    if (gs.rc == 0 && gs.root[0] != '\0' && Stricmp((STRPTR)gs.run_root, (STRPTR)gs.root) == 0 &&
        datafile_read(gs.output, &text) >= 0) {
        if (!gitstatus_parse(&gs.status, text != NULL ? text : "")) gitstatus_clear(&gs.status);
        read_branch(gs.root);
        update_tree(app);
    }
    free(text);
    status_release();
    if (gs.pending) status_start(app);
}

void git_append_status(EditorApp *app, char *text, size_t size)
{
    size_t n = strlen(text);
    if (!git_enabled(app) || gs.root[0] == '\0' || gs.branch[0] == '\0') return;
    snprintf(text + n, size - n, S(MSG_STATUS_GIT), gs.branch);
}

/* ---- the pens of the tree ------------------------------------------------ */

/**
 * @brief DIRTREE_PenHook: the pen of an entry's git state.
 * @param hook The hook.
 * @param tree The folder tree.
 * @param msg The entry.
 * @return A pen, or -1 for the normal one.
 */
static ULONG pen_entry(struct Hook *hook, Object *tree, struct DirTreePenMsg *msg)
{
    const char *rel;
    int state;
    (void)hook;
    (void)tree;
    if (!gs.pens_open || gs.root[0] == '\0' || gs.status.count == 0) return (ULONG)-1;
    if ((rel = gitstatus_relative(gs.root, (const char *)msg->dtpm_Path)) == NULL) return (ULONG)-1;
    /* the repository itself is not part of its status */
    if (Strnicmp((STRPTR)rel, (STRPTR)".git", 4) == 0 && (rel[4] == '\0' || rel[4] == '/')) return (ULONG)-1;
    state = gitstatus_state(&gs.status, rel, msg->dtpm_Directory != 0);
    return state > GIT_ST_CLEAN && state < GIT_ST_COUNT ? (ULONG)gs.pens[state] : (ULONG)-1;
}

struct Hook *git_pen_hook(void)
{
    gs.pen_hook.h_Entry = (ULONG (*)())HookEntry;
    gs.pen_hook.h_SubEntry = (ULONG (*)())pen_entry;
    return &gs.pen_hook;
}

/**
 * @brief A colour from 0xRRGGBB to the 32-bit value of a gun.
 * @param rgb The colour.
 * @param shift 16 for red, 8 for green, 0 for blue.
 * @return The gun's value.
 */
static ULONG gun(unsigned long rgb, int shift)
{
    return (ULONG)((rgb >> shift) & 0xFF) * 0x01010101UL;
}

void git_release_pens(EditorApp *app)
{
    int i;
    (void)app;
    if (!gs.pens_open || gs.pen_screen == NULL) return;
    for (i = GIT_ST_UNTRACKED; i < GIT_ST_COUNT; ++i)
        if (gs.pens[i] >= 0) ReleasePen(gs.pen_screen->ViewPort.ColorMap, (ULONG)gs.pens[i]);
    gs.pens_open = 0;
    gs.pen_screen = NULL;
}

void git_obtain_pens(EditorApp *app)
{
    int i;
    git_release_pens(app);
    if (app->screen == NULL) return;
    gs.pens[GIT_ST_CLEAN] = -1;
    for (i = GIT_ST_UNTRACKED; i < GIT_ST_COUNT; ++i) {
        unsigned long rgb = app->settings.gitcolours[i - GIT_ST_UNTRACKED];
        gs.pens[i] = ObtainBestPen(app->screen->ViewPort.ColorMap, gun(rgb, 16), gun(rgb, 8), gun(rgb, 0),
                                   OBP_Precision, PRECISION_GUI, TAG_END);
    }
    gs.pen_screen = app->screen;
    gs.pens_open = 1;
    update_tree(app);
}

void git_cleanup(EditorApp *app)
{
    /* the process reports before it ends: wait for it */
    while (gs.running) {
        gs.pending = 0;
        WaitPort(gs.port);
        git_handle(app, 1UL << gs.port->mp_SigBit);
    }
    status_release();
    if (gs.port != NULL) DeleteMsgPort(gs.port);
    gs.port = NULL;
    gitstatus_clear(&gs.status);
    free(gs.pull_docs);
    gs.pull_docs = NULL;
    gs.pull_count = 0;
    git_release_pens(app);
}

int git_repository(EditorApp *app, char *root, size_t size)
{
    if (!tree_project_root(app, root, (LONG)size)) {
        ui_error(app, S(MSG_GIT_TITLE), S(MSG_GIT_NOPROJECT));
        return 0;
    }
    if (!git_is_repository(root)) {
        char text[EDITOR_PATH_MAX + 100];
        snprintf(text, sizeof(text), S(MSG_GIT_NOREPO), root);
        ui_error(app, S(MSG_GIT_TITLE), text);
        return 0;
    }
    return 1;
}

LONG git_pen(int state)
{
    return gs.pens_open && state > GIT_ST_CLEAN && state < GIT_ST_COUNT ? gs.pens[state] : -1;
}

/* ---- Fetch, Pull and Push ---------------------------------------------------- */

int git_remote_url(const char *root, char *url, size_t size)
{
    char path[EDITOR_PATH_MAX], *text = NULL;
    int found = 0;
    strncpy(path, root, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (AddPart((STRPTR)path, (STRPTR)".git/config", sizeof(path)) && datafile_read(path, &text) > 0)
        found = gitconfig_get(text, "remote", "origin", "url", url, size);
    free(text);
    if (!found && size > 0) url[0] = '\0';
    return found;
}

/**
 * @brief Remember the date and size of the files of the open documents in
 *        the repository (before a Pull).
 * @param app The application state.
 * @param root The repository.
 */
static void snapshot_documents(EditorApp *app, const char *root)
{
    Document *doc;
    long n = 0;
    free(gs.pull_docs);
    gs.pull_docs = NULL;
    gs.pull_count = 0;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL; doc = (Document *)doc->node.ln_Succ)
        ++n;
    if (n == 0 || (gs.pull_docs = malloc(sizeof(PullDoc) * (size_t)n)) == NULL) return;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL; doc = (Document *)doc->node.ln_Succ) {
        PullDoc *p = &gs.pull_docs[gs.pull_count];
        struct FileInfoBlock *fib;
        BPTR lock;
        if (doc->path == NULL || gitstatus_relative(root, doc->path) == NULL ||
            strlen(doc->path) >= sizeof(p->path) || (lock = Lock((STRPTR)doc->path, ACCESS_READ)) == 0)
            continue;
        if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL && Examine(lock, fib)) {
            strcpy(p->path, doc->path);
            p->date = fib->fib_Date;
            p->size = fib->fib_Size;
            ++gs.pull_count;
        }
        if (fib != NULL) FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
    }
}

/**
 * @brief After a Pull: load the documents whose files changed again and
 *        mark those whose files are gone as changed.
 * @param app The application state.
 */
static void compare_documents(EditorApp *app)
{
    long i, reloaded = 0, gone = 0;
    char text[200];
    for (i = 0; i < gs.pull_count; ++i) {
        const PullDoc *p = &gs.pull_docs[i];
        Document *doc = document_find_path(app, p->path);
        struct FileInfoBlock *fib;
        BPTR lock;
        int same = 0;
        ULONG y = 0;
        if (doc == NULL) continue;                  /* closed meanwhile */
        if ((lock = Lock((STRPTR)p->path, ACCESS_READ)) == 0) {
            if (!doc->dirty) {
                document_set_dirty(app, doc, 1);    /* to save it again or close it */
                ++gone;
            }
            continue;
        }
        if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL && Examine(lock, fib))
            same = fib->fib_Size == p->size && CompareDates(&fib->fib_Date, &p->date) == 0;
        else
            same = 1;                               /* cannot tell: leave it */
        if (fib != NULL) FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        if (same) continue;
        if (doc->dirty) {
            /* changed in the editor while the Pull ran */
            struct EasyStruct es = {sizeof(es), 0, NULL, NULL, NULL};
            ULONG args[1];
            es.es_Title = (STRPTR)S(MSG_GIT_TITLE);
            es.es_TextFormat = (STRPTR)S(MSG_GIT_PULLASK);
            es.es_GadgetFormat = (STRPTR)S(MSG_GIT_PULLASKGADGETS);
            args[0] = (ULONG)FilePart((STRPTR)p->path);
            if (EasyRequestArgs(app->window, &es, NULL, args) != 1) continue;
        }
        GetAttr(GA_TEXTEDITOR_CursorY, doc->editor, &y);
        if (file_load(app, doc, p->path)) {
            ++reloaded;
            SetGadgetAttrs((struct Gadget *)doc->editor, doc == app->active ? app->window : NULL, NULL,
                           GA_TEXTEDITOR_CursorY, y, GA_TEXTEDITOR_CursorX, 0, TAG_END);
        }
    }
    free(gs.pull_docs);
    gs.pull_docs = NULL;
    gs.pull_count = 0;
    if (gone > 0) snprintf(text, sizeof(text), S(MSG_GIT_PULLGONE), reloaded, gone);
    else if (reloaded > 0) snprintf(text, sizeof(text), S(MSG_GIT_PULLRELOADED), reloaded);
    else return;
    ui_status_message(app, text);
}

/** An entry of a drawer, collected before anything in it is moved. */
typedef struct DotEntry {
    struct DotEntry *next;              /**< the next one */
    int directory;                      /**< it is a drawer */
    char name[108];                     /**< its name */
} DotEntry;

/**
 * @brief Move what is in a drawer into another, replacing files there;
 *        the emptied drawers are deleted.
 *
 * The names are collected first: ExNext() must not run while the
 * drawer changes.
 *
 * @param from The drawer emptied.
 * @param to The drawer filled (made if needed).
 * @return Non-zero if everything was moved.
 */
static int move_contents(const char *from, const char *to)
{
    char src[EDITOR_PATH_MAX], dst[EDITOR_PATH_MAX];
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    DotEntry *list = NULL, *e;
    BPTR lock;
    int ok = 1;
    if (fib == NULL) return 0;
    if ((lock = Lock((STRPTR)from, ACCESS_READ)) == 0) {
        FreeDosObject(DOS_FIB, fib);
        return 0;
    }
    if (Examine(lock, fib))
        while (ExNext(lock, fib)) {
            if ((e = malloc(sizeof(*e))) == NULL) { ok = 0; break; }
            e->directory = fib->fib_DirEntryType > 0;
            strncpy(e->name, fib->fib_FileName, sizeof(e->name) - 1);
            e->name[sizeof(e->name) - 1] = '\0';
            e->next = list;
            list = e;
        }
    UnLock(lock);
    FreeDosObject(DOS_FIB, fib);
    if ((lock = Lock((STRPTR)to, ACCESS_READ)) != 0) UnLock(lock);
    else if ((lock = CreateDir((STRPTR)to)) != 0) UnLock(lock);
    else ok = 0;
    while (list != NULL) {
        e = list;
        list = e->next;
        strcpy(src, from);
        strcpy(dst, to);
        if (ok && AddPart((STRPTR)src, (STRPTR)e->name, sizeof(src)) &&
            AddPart((STRPTR)dst, (STRPTR)e->name, sizeof(dst))) {
            if (e->directory) {
                ok = move_contents(src, dst) && ok;
            } else {
                DeleteFile((STRPTR)dst);            /* may not be there */
                if (!Rename((STRPTR)src, (STRPTR)dst)) ok = 0;
            }
        } else {
            ok = 0;
        }
        free(e);
    }
    if (ok && !DeleteFile((STRPTR)from)) ok = 0;
    return ok;
}

void git_fix_dot_drawer(EditorApp *app, const char *root)
{
    struct EasyStruct es = {sizeof(es), 0, NULL, NULL, NULL};
    char dot[EDITOR_PATH_MAX];
    BPTR lock;
    strncpy(dot, root, sizeof(dot) - 1);
    dot[sizeof(dot) - 1] = '\0';
    if (!AddPart((STRPTR)dot, (STRPTR)".", sizeof(dot)) || (lock = Lock((STRPTR)dot, ACCESS_READ)) == 0) return;
    UnLock(lock);
    es.es_Title = (STRPTR)S(MSG_GIT_TITLE);
    es.es_TextFormat = (STRPTR)S(MSG_GIT_DOTASK);
    es.es_GadgetFormat = (STRPTR)S(MSG_GIT_DOTGADGETS);
    if (EasyRequestArgs(app->window, &es, NULL, NULL) != 1) return;
    if (!move_contents(dot, root)) ui_error(app, S(MSG_GIT_TITLE), S(MSG_GIT_DOTFAILED));
}

/**
 * @brief The end of Fetch, Pull or Push (build_run_job()).
 * @param app The application state.
 * @param rc The return code (am-git returns 0 also when it failed).
 */
static void job_done(EditorApp *app, LONG rc)
{
    ULONG micros;
    gs.job = 0;
    CurrentTime(&gs.finished, &micros);    /* its own writes into .git start no status */
    if (gs.pulling && rc != -1) git_fix_dot_drawer(app, gs.job_root);
    gs.pulling = 0;
    if (gs.pull_docs != NULL) {
        if (rc != -1) compare_documents(app);
        free(gs.pull_docs);
        gs.pull_docs = NULL;
        gs.pull_count = 0;
    }
    if (gs.root[0] != '\0') read_branch(gs.root);
    git_status_request(app);
}

/**
 * @brief The branch HEAD is on (not a detached HEAD).
 * @param root The repository.
 * @param[out] branch The branch.
 * @param size Size of @p branch.
 * @return Non-zero if HEAD is on a branch.
 */
static int head_branch(const char *root, char *branch, size_t size)
{
    char path[EDITOR_PATH_MAX], *text = NULL;
    int ok = 0;
    strncpy(path, root, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    if (AddPart((STRPTR)path, (STRPTR)".git/HEAD", sizeof(path)) && datafile_read(path, &text) > 0)
        ok = strncmp(text, "ref: refs/heads/", 16) == 0 && gitstatus_head(text, branch, size);
    free(text);
    return ok;
}

/**
 * @brief Git/Fetch, Git/Pull or Git/Push in the background.
 *
 * Pull gets the address and the branch: am-git 0.12.1, the latest
 * release, fails without them ("NullArgumentError"; fixed in its
 * unreleased sources).
 * @param app The application state.
 * @param id MID_GIT_FETCH, MID_GIT_PULL or MID_GIT_PUSH.
 */
static void remote_action(EditorApp *app, ULONG id)
{
    char root[EDITOR_PATH_MAX], url[512], branch[128], args[1400];
    char text[sizeof(app->settings.gitcommand) * 2 + sizeof(args) + 32];
    LONG name = id == MID_GIT_FETCH ? MSG_GIT_JOB_FETCH : id == MID_GIT_PULL ? MSG_GIT_JOB_PULL : MSG_GIT_JOB_PUSH;
    if (!git_repository(app, root, sizeof(root))) return;
    if (gs.job || build_running()) {
        outwin_open(app);
        ui_set_menu_check(app, MID_OUTPUT, outwin_is_open());
        ui_error(app, S(name), S(MSG_BUILD_BUSY));
        return;
    }
    if (!git_remote_url(root, url, sizeof(url))) {
        ui_error(app, S(MSG_GIT_TITLE), S(MSG_GIT_NOREMOTE));
        return;
    }
    strcpy(args, id == MID_GIT_FETCH ? "fetch" : id == MID_GIT_PULL ? "pull" : "push");
    if (id == MID_GIT_PULL) {
        size_t n;
        if (!head_branch(root, branch, sizeof(branch))) {
            ui_error(app, S(name), S(MSG_GIT_DETACHED));
            return;
        }
        n = strlen(args);
        args[n++] = ' ';
        if (!git_quote(args + n, sizeof(args) - n, url) || (n = strlen(args)) + 2 > sizeof(args)) {
            ui_error(app, S(name), S(MSG_BUILD_SCRIPT));
            return;
        }
        args[n++] = ' ';
        if (!git_quote(args + n, sizeof(args) - n, branch)) {
            ui_error(app, S(name), S(MSG_BUILD_SCRIPT));
            return;
        }
    }
    if (!script_text(app, args, text, sizeof(text))) {
        ui_error(app, S(name), S(MSG_BUILD_SCRIPT));
        return;
    }
    /* Pull changes files: the documents go to disk first, then are compared */
    if (id == MID_GIT_PULL) {
        if (!build_save_documents(app)) return;
        snapshot_documents(app, root);
    }
    gs.job = 1;
    gs.pulling = id == MID_GIT_PULL;
    strcpy(gs.job_root, root);
    if (!build_run_job(app, S(name), text, root, job_done)) {
        gs.job = gs.pulling = 0;
        free(gs.pull_docs);
        gs.pull_docs = NULL;
        gs.pull_count = 0;
    }
}

/**
 * @brief The end of a Clone: the new drawer becomes the project if it is
 *        a repository now (am-git returns 0 also when it failed).
 * @param app The application state.
 * @param rc The return code.
 */
static void clone_done(EditorApp *app, LONG rc)
{
    char target[EDITOR_PATH_MAX];
    ULONG micros;
    gs.job = 0;
    CurrentTime(&gs.finished, &micros);
    strcpy(target, gs.clone_target);
    gs.clone_target[0] = '\0';
    if (rc == 0 && git_is_repository(target)) {
        if (tree_open_project(app, target)) ui_status_message(app, S(MSG_GITCL_DONE));
    } else if (rc != -1) {
        ui_error(app, S(MSG_GIT_TITLE), S(MSG_GITCL_FAILED));
    }
}

int git_clone(EditorApp *app, const char *url, const char *drawer, const char *name)
{
    char args[1400], text[sizeof(app->settings.gitcommand) * 2 + sizeof(args) + 32];
    size_t n;
    if (gs.job || build_running()) {
        outwin_open(app);
        ui_set_menu_check(app, MID_OUTPUT, outwin_is_open());
        ui_error(app, S(MSG_GIT_JOB_CLONE), S(MSG_BUILD_BUSY));
        return 0;
    }
    strncpy(gs.clone_target, drawer, sizeof(gs.clone_target) - 1);
    gs.clone_target[sizeof(gs.clone_target) - 1] = '\0';
    /* the drawer's name only, run in the drawer above it: am-git turns
     * paths with "./" into drawers named "." */
    strcpy(args, "clone ");
    n = strlen(args);
    if (!AddPart((STRPTR)gs.clone_target, (STRPTR)name, sizeof(gs.clone_target)) ||
        !git_quote(args + n, sizeof(args) - n, url) || (n = strlen(args)) + 2 > sizeof(args) ||
        (args[n++] = ' ', !git_quote(args + n, sizeof(args) - n, name)) ||
        !script_text(app, args, text, sizeof(text))) {
        gs.clone_target[0] = '\0';
        ui_error(app, S(MSG_GIT_JOB_CLONE), S(MSG_BUILD_SCRIPT));
        return 0;
    }
    gs.job = 1;
    if (!build_run_job(app, S(MSG_GIT_JOB_CLONE), text, drawer, clone_done)) {
        gs.job = 0;
        gs.clone_target[0] = '\0';
        return 0;
    }
    return 1;
}

void git_menu(EditorApp *app, ULONG id)
{
    if (!git_enabled(app)) return;
    switch (id) {
        case MID_GIT_INIT: init_project(app); break;
        case MID_GIT_REFRESH: git_project_changed(app); break;
        case MID_GIT_COMMIT: gitwin_commit(app); break;
        case MID_GIT_DIFF: gitwin_diff_active(app); break;
        case MID_GIT_FETCH: case MID_GIT_PULL: case MID_GIT_PUSH: remote_action(app, id); break;
        case MID_GIT_CLONE: gitwin_clone(app); break;
        case MID_GIT_CONFLICTS: gitwin_conflicts(app); break;
    }
}
