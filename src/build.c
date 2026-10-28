/**
 * @file build.c
 * @brief The project's actions Build, Run and Debug: their default
 *        commands from .tinydev/project.json, run through the Shell.
 *
 * Each action runs a script written to the drawer of the temporary files
 * (TEMPDIR, named with temp_name(); so several lines, Stack and
 * the like work alike); it starts with "Stack 65536", as TinyDev started
 * from the Workbench has no Shell whose stack the commands would get (a
 * Stack line of the command itself goes after it). The placeholders are
 * replaced first (projconf_expand()); the command runs in its drawer
 * (the project directory if none is given).
 *
 * Build saves the modified documents that have a name, then runs in a
 * process of its own, which calls SystemTags() synchronously and reports
 * the return code with a message; so TinyDev goes on while it runs. The
 * output goes to a temporary file that TinyDev reads four times a second
 * (timer.device) into the output window (outwin.c): it opens the file,
 * reads what came after the part already read and closes it again. The
 * file is opened with MODE_READWRITE for writing (a shared lock, so it can
 * be read meanwhile) and line-buffered: the Shell and the commands write
 * through this handle, and with its buffer the text would only arrive in
 * the file when the buffer is full or the handle closed. It stays after
 * the build (for a look with Type) until the next build or quit.
 * Stop sends Ctrl-C to the Shell of the build, found as the CLI whose
 * output is that file. Only one build runs at a time; TinyDev waits for
 * it before quitting.
 *
 * Run and Debug start asynchronously in a console window of their own
 * and are independent of TinyDev; their scripts stay until TinyDev quits
 * (a script may still be running then and is left).
 *
 * Untested assumption: the Shell started by SystemTags() keeps SYS_Output
 * as cli_StandardOutput (Stop relies on it).
 *
 * Amiga+M is not used for Build: texteditor.gadget takes it itself (it
 * sets a bookmark) before the menu sees it.
 */
#include "editor.h"
#include "projconf.h"

#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/stdio.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The project file in \<project\>/.tinydev/. */
#define PROJECT_FILE "project.json"
/** Stack the commands get (bytes). */
#define COMMAND_STACK 65536
/** Interval of reading the output (microseconds). */
#define POLL_MICROS 250000

/** Names of the actions (MSG_), in the order of ProjAction. */
static const LONG action_names[PROJ_ACTIONS] = { MSG_PJ_CAT_BUILD, MSG_PJ_CAT_RUN, MSG_PJ_CAT_DEBUG };

/** State of the build (there is one at a time). */
static struct {
    int running;
    int stopping;                       /* Stop was used */
    char name[128];                     /* the command's name */
    char script[EDITOR_PATH_MAX];       /* the build's script */
    char output[EDITOR_PATH_MAX];       /* its output */
    char command[EDITOR_PATH_MAX + 16]; /* what the Shell runs */
    struct MsgPort *port;               /* the build process reports here */
    struct Message msg;
    LONG rc;                            /* return code (set by the build process) */
    BPTR in, out_w;                     /* NIL:, the output file for writing */
    LONG out_read;                      /* bytes of the output file read so far */
    struct MsgPort *timer_port;
    struct timerequest *timer;
    int timer_open, timer_pending;
} bj;

/** A script written for Run or Debug, deleted when TinyDev quits. */
typedef struct RunScript {
    struct RunScript *next;             /**< the next one */
    char name[EDITOR_PATH_MAX];         /**< its name */
} RunScript;

/** The scripts written for Run and Debug. */
static RunScript *run_scripts;

/**
 * @brief Report a problem with an action.
 * @param app The application state.
 * @param action The action (title).
 * @param text The message.
 */
static void report(EditorApp *app, ProjAction action, const char *text)
{
    ui_error(app, S(action_names[action]), text);
}

/**
 * @brief Read the project's settings.
 * @param app The application state.
 * @param action The action (for messages).
 * @param[out] root The project directory (EDITOR_PATH_MAX).
 * @param[out] c The settings (projconf_clear() them).
 * @return Non-zero on success (otherwise a message was shown).
 */
static int load_settings(EditorApp *app, ProjAction action, char *root, ProjConf *c)
{
    char *text = NULL, path[EDITOR_PATH_MAX], message[EDITOR_PATH_MAX + 200];
    int r;
    projconf_init(c);
    if (!tree_project_root(app, root, EDITOR_PATH_MAX)) {
        report(app, action, S(MSG_PJ_NOPROJECT));
        return 0;
    }
    r = projdata_read(root, PROJECT_FILE, &text);
    if (r > 0) {
        r = projconf_parse(c, text) > 0 ? 1 : -1;
        free(text);
    }
    if (r < 0) {
        if (!projdata_path(root, PROJECT_FILE, path, sizeof(path))) strcpy(path, PROJECT_FILE);
        snprintf(message, sizeof(message), S(MSG_PJ_READ), path);
        report(app, action, message);
        return 0;
    }
    return 1;
}

/**
 * @brief Write the script of a command: the stack, then its lines with the
 *        placeholders replaced.
 * @param app The application state.
 * @param action The action (for messages).
 * @param cmd The command.
 * @param root The project directory.
 * @param file The active document's path, or NULL.
 * @param script The script's name.
 * @return Non-zero on success (otherwise a message was shown).
 */
static int write_script(EditorApp *app, ProjAction action, const ProjCommand *cmd,
                        const char *root, const char *file, const char *script)
{
    char stack[32];
    BPTR fh;
    long i;
    int ok, missing = 0;

    if ((fh = Open((STRPTR)script, MODE_NEWFILE)) == 0) {
        report(app, action, S(MSG_BUILD_SCRIPT));
        return 0;
    }
    snprintf(stack, sizeof(stack), "Stack %ld\n", (long)COMMAND_STACK);
    ok = FPuts(fh, (STRPTR)stack) == 0;
    for (i = 0; ok && i < cmd->line_count; ++i) {
        int m = 0;
        char *line = projconf_expand(cmd->lines[i], root, file, &m);
        if (m) missing = 1;
        ok = line != NULL && FPuts(fh, (STRPTR)line) == 0 && FPutC(fh, '\n') >= 0;
        free(line);
    }
    ok = Close(fh) && ok;
    if (missing) {
        DeleteFile((STRPTR)script);
        report(app, action, S(MSG_BUILD_NOFILE));
        return 0;
    }
    if (!ok) {
        DeleteFile((STRPTR)script);
        report(app, action, S(MSG_BUILD_SCRIPT));
    }
    return ok;
}

/**
 * @brief Lock the drawer a command runs in.
 * @param app The application state.
 * @param action The action (for messages).
 * @param cmd The command.
 * @param root The project directory.
 * @param file The active document's path, or NULL.
 * @return The lock (UnLock() it), or 0 (a message was shown).
 */
static BPTR lock_drawer(EditorApp *app, ProjAction action, const ProjCommand *cmd,
                        const char *root, const char *file)
{
    char path[EDITOR_PATH_MAX], message[EDITOR_PATH_MAX + 100];
    char *dir = NULL;
    BPTR lock;
    int missing = 0;

    strcpy(path, root);
    if (cmd->dir != NULL) {
        if ((dir = projconf_expand(cmd->dir, root, file, &missing)) == NULL) {
            report(app, action, S(MSG_PJ_NOMEM));
            return 0;
        }
        if (missing) {
            free(dir);
            report(app, action, S(MSG_BUILD_NOFILE));
            return 0;
        }
        /* relative to the project; AddPart() takes an absolute one as it is */
        if (!AddPart((STRPTR)path, (STRPTR)dir, sizeof(path))) strcpy(path, dir);
    }
    if ((lock = Lock((STRPTR)path, ACCESS_READ)) == 0) {
        snprintf(message, sizeof(message), S(MSG_BUILD_NODIR), dir != NULL ? dir : path);
        report(app, action, message);
    }
    free(dir);
    return lock;
}

/**
 * @brief Prepare the default command of an action: read the settings,
 *        write the script and lock the drawer.
 * @param app The application state.
 * @param action The action.
 * @param script The script's name.
 * @param[out] name The command's name (128 characters).
 * @return The lock of the drawer (UnLock() it), or 0 (a message was shown).
 */
static BPTR prepare(EditorApp *app, ProjAction action, const char *script, char *name)
{
    char root[EDITOR_PATH_MAX], message[300];
    const char *file = app->active != NULL ? app->active->path : NULL;
    ProjConf c;
    BPTR lock = 0;
    long index;

    if (!load_settings(app, action, root, &c)) return 0;
    index = projconf_default(&c, action);
    if (index < 0) {
        snprintf(message, sizeof(message), S(MSG_BUILD_NOCOMMAND), S(action_names[action]));
        report(app, action, message);
    } else {
        const ProjCommand *cmd = &c.commands[action][index];
        strncpy(name, cmd->name, 127);
        name[127] = '\0';
        if (write_script(app, action, cmd, root, file, script) &&
            (lock = lock_drawer(app, action, cmd, root, file)) == 0)
            DeleteFile((STRPTR)script);
    }
    projconf_clear(&c);
    return lock;
}

/* ---- Build ------------------------------------------------------------- */

/**
 * @brief The build process: runs the script through the Shell and reports
 *        the return code.
 *
 * Its current directory is the drawer of the command (NP_CurrentDir), which
 * the Shell takes over. The message goes out under Forbid(), so the process
 * is gone before TinyDev can go on (and maybe quit).
 */
static void build_process(void)
{
    bj.rc = SystemTags((STRPTR)bj.command, SYS_Input, (ULONG)bj.in, SYS_Output, (ULONG)bj.out_w,
                       SYS_UserShell, TRUE, TAG_DONE);
    Forbid();
    PutMsg(bj.port, &bj.msg);
}

/**
 * @brief Ask timer.device to wake TinyDev for the next reading.
 */
static void start_timer(void)
{
    if (!bj.timer_open || bj.timer_pending) return;
    bj.timer->tr_node.io_Command = TR_ADDREQUEST;
    bj.timer->tr_time.tv_secs = 0;
    bj.timer->tr_time.tv_micro = POLL_MICROS;
    SendIO((struct IORequest *)bj.timer);
    bj.timer_pending = 1;
}

/**
 * @brief Open timer.device for reading the output regularly.
 * @return Non-zero on success.
 */
static int open_timer(void)
{
    if ((bj.timer_port = CreateMsgPort()) == NULL) return 0;
    bj.timer = (struct timerequest *)CreateIORequest(bj.timer_port, sizeof(struct timerequest));
    if (bj.timer == NULL) return 0;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)bj.timer, 0) != 0) return 0;
    bj.timer_open = 1;
    return 1;
}

/**
 * @brief Close timer.device (a pending request is aborted).
 */
static void close_timer(void)
{
    if (bj.timer_open) {
        if (bj.timer_pending) {
            AbortIO((struct IORequest *)bj.timer);
            WaitIO((struct IORequest *)bj.timer);
        }
        CloseDevice((struct IORequest *)bj.timer);
    }
    if (bj.timer != NULL) DeleteIORequest((struct IORequest *)bj.timer);
    if (bj.timer_port != NULL) DeleteMsgPort(bj.timer_port);
    bj.timer = NULL;
    bj.timer_port = NULL;
    bj.timer_open = bj.timer_pending = 0;
}

/**
 * @brief Read what the build wrote since the last time into the output
 *        window.
 * @param app The application state.
 */
static void read_output(EditorApp *app)
{
    static char buffer[1024];
    LONG n;
    BPTR fh = Open((STRPTR)bj.output, MODE_OLDFILE);
    if (fh == 0) return;
    if (Seek(fh, bj.out_read, OFFSET_BEGINNING) >= 0)
        while ((n = Read(fh, buffer, sizeof(buffer))) > 0) {
            bj.out_read += n;
            outwin_add_text(app, buffer, n);
        }
    Close(fh);
}

/**
 * @brief Free what the build used (files, port, timer).
 */
static void release(void)
{
    close_timer();
    if (bj.out_w != 0) Close(bj.out_w);
    if (bj.in != 0) Close(bj.in);
    bj.out_w = bj.in = 0;
    if (bj.port != NULL) DeleteMsgPort(bj.port);
    bj.port = NULL;
    if (bj.script[0] != '\0') DeleteFile((STRPTR)bj.script);
}

/**
 * @brief The build has ended: the rest of its output, its result in the
 *        status line, and the files go.
 * @param app The application state.
 */
static void finished(EditorApp *app)
{
    char text[200];
    /* closing writes what is still in the handle's buffer */
    if (bj.out_w != 0) Close(bj.out_w);
    bj.out_w = 0;
    read_output(app);
    outwin_flush(app);
    if (bj.rc == 0) snprintf(text, sizeof(text), S(MSG_BUILD_DONE), bj.name);
    else if (bj.stopping) snprintf(text, sizeof(text), S(MSG_BUILD_STOPPED), bj.name);
    else if (bj.rc < 0) snprintf(text, sizeof(text), S(MSG_BUILD_NOSTART), bj.name);
    else snprintf(text, sizeof(text), S(MSG_BUILD_FAILED), bj.name, (long)bj.rc);
    outwin_set_status(text, 0);
    if (bj.rc != 0 && !bj.stopping && app->screen != NULL) DisplayBeep(app->screen);
    bj.running = bj.stopping = 0;
    release();
}

/**
 * @brief Save the modified documents that have a name.
 * @param app The application state.
 * @return Non-zero if all were saved.
 */
static int save_documents(EditorApp *app)
{
    Document *doc;
    for (doc = (Document *)app->documents.lh_Head; doc->node.ln_Succ != NULL;
         doc = (Document *)doc->node.ln_Succ)
        if (doc->dirty && doc->path != NULL && !file_save(app, doc, doc->path)) return 0;
    return 1;
}

/**
 * @brief Start the Build action.
 * @param app The application state.
 */
static void start_build(EditorApp *app)
{
    char text[200];
    struct Process *proc;
    BPTR lock;

    if (bj.running) {
        outwin_open(app);
        ui_set_menu_check(app, MID_OUTPUT, outwin_is_open());
        report(app, PROJ_BUILD, S(MSG_BUILD_BUSY));
        return;
    }
    if (!save_documents(app)) return;
    /* the output of the last build goes now */
    if (bj.output[0] != '\0') DeleteFile((STRPTR)bj.output);
    bj.output[0] = '\0';
    if (!temp_name(app, ".script", bj.script, sizeof(bj.script)) ||
        !temp_name(app, ".out", bj.output, sizeof(bj.output))) {
        bj.script[0] = bj.output[0] = '\0';
        report(app, PROJ_BUILD, S(MSG_BUILD_SCRIPT));
        return;
    }
    if ((lock = prepare(app, PROJ_BUILD, bj.script, bj.name)) == 0) return;

    snprintf(bj.command, sizeof(bj.command), "Execute \"%s\"", bj.script);
    if ((bj.port = CreateMsgPort()) == NULL || !open_timer() ||
        (bj.in = Open((STRPTR)"NIL:", MODE_OLDFILE)) == 0 ||
        (bj.out_w = Open((STRPTR)bj.output, MODE_READWRITE)) == 0) {
        UnLock(lock);
        release();
        report(app, PROJ_BUILD, S(MSG_BUILD_SCRIPT));
        return;
    }
    SetVBuf(bj.out_w, NULL, BUF_LINE, -1);
    bj.out_read = 0;
    memset(&bj.msg, 0, sizeof(bj.msg));
    bj.msg.mn_Node.ln_Type = NT_MESSAGE;
    bj.msg.mn_Length = sizeof(bj.msg);
    bj.rc = -1;
    bj.stopping = 0;

    outwin_clear(app);
    snprintf(text, sizeof(text), S(MSG_BUILD_RUNNING), bj.name);
    outwin_set_status(text, 1);
    outwin_open(app);
    ui_set_menu_check(app, MID_OUTPUT, outwin_is_open());

    /* the lock goes to the process, which unlocks it when it ends */
    proc = CreateNewProcTags(NP_Entry, (ULONG)build_process, NP_Name, (ULONG)"TinyDev Build",
                             NP_StackSize, 8192, NP_CurrentDir, (ULONG)lock, TAG_DONE);
    if (proc == NULL) {
        UnLock(lock);
        snprintf(text, sizeof(text), S(MSG_BUILD_NOSTART), bj.name);
        outwin_set_status(text, 0);
        release();
        return;
    }
    bj.running = 1;
    start_timer();
}

void build_stop(EditorApp *app)
{
    ULONG n, max;
    if (!bj.running) return;
    /* the Shell of the build is the CLI whose output is our file */
    Forbid();
    max = MaxCli();
    for (n = 1; n <= max; ++n) {
        struct Process *p = FindCliProc(n);
        struct CommandLineInterface *cli;
        if (p == NULL || (cli = (struct CommandLineInterface *)BADDR(p->pr_CLI)) == NULL) continue;
        if (cli->cli_StandardOutput == bj.out_w || cli->cli_CurrentOutput == bj.out_w)
            Signal(&p->pr_Task, SIGBREAKF_CTRL_C);
    }
    Permit();
    if (!bj.stopping) {
        bj.stopping = 1;
        outwin_set_status(S(MSG_BUILD_STOPPING), 1);
    }
    (void)app;
}

int build_running(void)
{
    return bj.running;
}

ULONG build_sigmask(void)
{
    ULONG mask = 0;
    if (!bj.running) return 0;
    if (bj.port != NULL) mask |= 1UL << bj.port->mp_SigBit;
    if (bj.timer_port != NULL) mask |= 1UL << bj.timer_port->mp_SigBit;
    return mask;
}

void build_handle(EditorApp *app, ULONG signals)
{
    if (!bj.running) return;
    if (bj.timer_port != NULL && (signals & (1UL << bj.timer_port->mp_SigBit)) != 0 &&
        GetMsg(bj.timer_port) != NULL) {
        bj.timer_pending = 0;
        read_output(app);
        start_timer();
    }
    if (GetMsg(bj.port) != NULL) finished(app);
}

int build_may_quit(EditorApp *app)
{
    struct EasyStruct es = {sizeof(es), 0, (STRPTR)S(MSG_PJ_CAT_BUILD), (STRPTR)S(MSG_BUILD_QUIT),
                            (STRPTR)S(MSG_BUILD_QUIT_GADGETS)};
    if (!bj.running) return 1;
    return EasyRequestArgs(app->window, &es, NULL, NULL) == 1;
}

void build_cleanup(EditorApp *app)
{
    if (bj.running) {
        /* stop it and wait: the process runs code of TinyDev; Ctrl-C again
         * every turn of the timer, in case the first came too early */
        build_stop(app);
        while (GetMsg(bj.port) == NULL) {
            ULONG got = Wait(build_sigmask());
            if (bj.timer_port != NULL && (got & (1UL << bj.timer_port->mp_SigBit)) != 0 &&
                GetMsg(bj.timer_port) != NULL) {
                bj.timer_pending = 0;
                build_stop(app);
                start_timer();
            }
        }
        bj.running = 0;
        release();
    }
    if (bj.output[0] != '\0') DeleteFile((STRPTR)bj.output);
    while (run_scripts != NULL) {
        RunScript *next = run_scripts->next;
        DeleteFile((STRPTR)run_scripts->name);
        FreeVec(run_scripts);
        run_scripts = next;
    }
}

/* ---- Run and Debug ----------------------------------------------------- */

/**
 * @brief Start the Run or Debug action in a console window of its own.
 * @param app The application state.
 * @param action PROJ_RUN or PROJ_DEBUG.
 */
static void start_detached(EditorApp *app, ProjAction action)
{
    char command[EDITOR_PATH_MAX + 16], name[128], console[200];
    RunScript *script = AllocVec(sizeof(RunScript), MEMF_ANY);
    BPTR lock, old, con;
    LONG rc = -1;

    if (script == NULL || !temp_name(app, ".script", script->name, sizeof(script->name))) {
        if (script != NULL) FreeVec(script);
        report(app, action, S(MSG_BUILD_SCRIPT));
        return;
    }
    if ((lock = prepare(app, action, script->name, name)) == 0) {
        FreeVec(script);
        return;
    }
    script->next = run_scripts;
    run_scripts = script;
    snprintf(command, sizeof(command), "Execute \"%s\"", script->name);
    snprintf(console, sizeof(console), "CON:0/%ld/640/200/%s: %s/AUTO/CLOSE/WAIT",
             app->screen != NULL ? (long)app->screen->BarHeight + 1 : 0L, APP_NAME, name);
    if ((con = Open((STRPTR)console, MODE_NEWFILE)) != 0) {
        /* the Shell takes over the current directory of the caller */
        old = CurrentDir(lock);
        rc = SystemTags((STRPTR)command, SYS_Input, (ULONG)con, SYS_Output, 0,
                        SYS_Asynch, TRUE, SYS_UserShell, TRUE, TAG_DONE);
        CurrentDir(old);
        /* asynchronous: the system closes the console when the command ends */
        if (rc == -1) Close(con);
    }
    UnLock(lock);
    if (rc == -1) {
        snprintf(console, sizeof(console), S(MSG_BUILD_NOSTART), name);
        report(app, action, console);
    }
}

void build_action(EditorApp *app, ULONG id)
{
    switch (id) {
        case MID_BUILD: start_build(app); break;
        case MID_RUN: start_detached(app, PROJ_RUN); break;
        case MID_DEBUG: start_detached(app, PROJ_DEBUG); break;
        case MID_STOP: build_stop(app); break;
    }
}
