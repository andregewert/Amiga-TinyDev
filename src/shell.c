/**
 * @file shell.c
 * @brief Project/New Shell: a Shell window in the project drawer.
 *
 * Like the HelpViewer, the Shell runs on its own and stays open when
 * TinyDev quits: NewShell is started asynchronously (SystemTags() with
 * SYS_Asynch) and opens the window itself, so TinyDev holds nothing of it.
 * The new Shell takes over the current directory of the caller, so it is
 * changed to the project drawer around the call (as for Run and Debug in
 * build.c).
 *
 * The setting SHELLWINDOW names the console: a handler such as CON:,
 * VNC: or KCON:, which gets the box below the main window (that of the
 * output window), a title, a close gadget and the screen TinyDev is on;
 * or, if it contains a '/', a whole window specification used as it is.
 * A handler that is not mounted is reported before NewShell would ask
 * for it with a "Please insert volume" requester.
 */
#include "editor.h"

#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <intuition/screens.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <stdio.h>
#include <string.h>

/** Smallest box of the Shell window. */
#define MIN_WIDTH 200
/** Smallest height of the Shell window. */
#define MIN_HEIGHT 60

/**
 * @brief Tell whether a device (handler) is mounted.
 * @param name The device name, with or without the colon.
 * @return Non-zero if it is in the DOS list.
 */
static int device_mounted(const char *name)
{
    char dev[32];
    size_t n = 0;
    struct DosList *dl;
    int found;

    while (name[n] != '\0' && name[n] != ':' && n < sizeof(dev) - 1) {
        dev[n] = name[n];
        ++n;
    }
    dev[n] = '\0';
    if (n == 0) return 0;
    dl = LockDosList(LDF_DEVICES | LDF_READ);
    found = FindDosEntry(dl, (STRPTR)dev, LDF_DEVICES) != NULL;
    UnLockDosList(LDF_DEVICES | LDF_READ);
    return found;
}

/**
 * @brief Find the public name of TinyDev's screen.
 * @param app The application state.
 * @param[out] name The name; empty for the Workbench screen or if the
 *        screen is not found.
 * @param size Size of @p name.
 */
static void screen_name(EditorApp *app, char *name, size_t size)
{
    struct List *list;
    struct PubScreenNode *psn;

    name[0] = '\0';
    if (app->screen == NULL || (list = LockPubScreenList()) == NULL) return;
    for (psn = (struct PubScreenNode *)list->lh_Head; psn->psn_Node.ln_Succ != NULL;
         psn = (struct PubScreenNode *)psn->psn_Node.ln_Succ) {
        if (psn->psn_Screen == app->screen && psn->psn_Node.ln_Name != NULL) {
            strncpy(name, psn->psn_Node.ln_Name, size - 1);
            name[size - 1] = '\0';
            break;
        }
    }
    UnlockPubScreenList();
    if (Stricmp((STRPTR)name, (STRPTR)"Workbench") == 0) name[0] = '\0';
}

/**
 * @brief Lock the drawer the Shell starts in: the project drawer, the
 *        drawer of the active document, or none.
 * @param app The application state.
 * @return A shared lock, or 0 to keep TinyDev's current directory.
 */
static BPTR start_drawer(EditorApp *app)
{
    static char path[EDITOR_PATH_MAX];      /* too large for the stack */
    if (!tree_project_root(app, path, sizeof(path))) {
        if (app->active == NULL || app->active->path == NULL) return 0;
        strncpy(path, app->active->path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        *PathPart((STRPTR)path) = '\0';
    }
    return Lock((STRPTR)path, SHARED_LOCK);
}

void shell_open(EditorApp *app)
{
    static char window[sizeof(app->settings.shellwindow) + 160];
    static char command[sizeof(window) + 24];
    static const long automatic[4] = { 0, 0, 0, 0 };
    const char *con = app->settings.shellwindow;
    char screen[MAXPUBSCREENNAME + 1], msg[200];
    WORD box[4];
    BPTR lock, old, in;
    LONG rc = -1;

    if (con[0] == '\0') con = "CON:";
    if (!device_mounted(con)) {
        snprintf(msg, sizeof(msg), S(MSG_SHELL_NODEVICE), con);
        ui_error(app, APP_NAME, msg);
        return;
    }
    if (strchr(con, '/') != NULL) {
        strcpy(window, con);
    } else {
        /* the box of the output window: below the main window, as wide */
        toolwin_box(app, automatic, MIN_WIDTH, MIN_HEIGHT, 1, box);
        screen_name(app, screen, sizeof(screen));
        snprintf(window, sizeof(window), "%s%s%ld/%ld/%ld/%ld/%s/CLOSE%s%s", con,
                 strchr(con, ':') != NULL ? "" : ":", (long)box[0], (long)box[1],
                 (long)box[2], (long)box[3], S(MSG_SHELL_TITLE),
                 screen[0] != '\0' ? "/SCREEN" : "", screen);
    }
    snprintf(command, sizeof(command), "NewShell WINDOW \"%s\"", window);
    if ((in = Open((STRPTR)"NIL:", MODE_OLDFILE)) != 0) {
        lock = start_drawer(app);
        old = lock != 0 ? CurrentDir(lock) : 0;
        rc = SystemTags((STRPTR)command, SYS_Input, (ULONG)in, SYS_Output, 0,
                        SYS_Asynch, TRUE, TAG_DONE);
        if (lock != 0) {
            CurrentDir(old);
            UnLock(lock);
        }
        /* asynchronous: the system closes the input when NewShell ends */
        if (rc == -1) Close(in);
    }
    if (rc == -1) ui_error(app, APP_NAME, S(MSG_SHELL_FAILED));
}
