/**
 * @file scout_main.c
 * @brief Entry point of TinyDev-Scout, the source analysis commodity: its
 *        settings, start-up and main loop.
 *
 * Settings are tool types of Scout's icon or Shell arguments of the same
 * names (ArgArrayInit()): TEMPDIR, PARSERS, TIMEOUT, MINFREE, CX_PRIORITY,
 * CX_POPKEY (the hotkey of the window, "ctrl alt s"), CX_POPUP (show the
 * window at the start, NO).
 *
 * Memory: when free memory falls below MINFREE (KB, 0: never), sessions
 * no command has named for a minute are closed, the least recently used
 * first, and no more files are indexed until memory is free again.
 * Clients poll STATUS with their session, so a client that runs keeps
 * its session; a client that quit without CLOSE loses it.
 */
#include "scout_server.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <clib/alib_protos.h>
#include <string.h>

/** commodities.library (with ArgArrayInit() also icon.library). */
struct Library *CxBase = NULL;
/** icon.library, for the tool types. */
struct Library *IconBase = NULL;
/** rexxsyslib.library, for the ARexx results. */
struct RxsLib *RexxSysBase = NULL;

/** libnix swaps to a stack of this size at the start (-Wl,-u,___stkinit). */
unsigned long __stack = 16384;

ScoutServer g_scout;

/** Scout was started from a Shell (not from the Workbench). */
static int from_shell = 0;

/**
 * @brief Print a message if Scout was started from a Shell; from the
 *        Workbench nothing is shown (writing would open a console).
 * @param text The message (with its line end).
 */
static void say(const char *text)
{
    BPTR out = from_shell ? Output() : 0;
    if (out) Write(out, (APTR)text, (LONG)strlen(text));
}

/**
 * @brief Copy a setting, made a full path if the drawer exists (PROGDIR:
 *        means nothing to the parsers' processes).
 * @param value The setting.
 * @param[out] out The path.
 * @param size Size of @p out.
 */
static void set_drawer(const char *value, char *out, size_t size)
{
    BPTR lock = Lock((STRPTR)value, SHARED_LOCK);
    if (!lock || !NameFromLock(lock, (STRPTR)out, (LONG)size)) {
        strncpy(out, value, size - 1);
        out[size - 1] = '\0';
    }
    if (lock) UnLock(lock);
}

/**
 * @brief Read the settings from the tool types or the Shell arguments.
 * @param srv The server.
 * @param argc The argument count (0 from the Workbench).
 * @param argv The arguments, or the WBStartup message.
 */
static void read_settings(ScoutServer *srv, int argc, char **argv)
{
    STRPTR *tt = ArgArrayInit(argc, (CONST_STRPTR *)argv);
    set_drawer((const char *)ArgString((CONST_STRPTR *)tt, (STRPTR)"TEMPDIR", (STRPTR)"T:"),
               srv->tempdir, sizeof(srv->tempdir));
    set_drawer((const char *)ArgString((CONST_STRPTR *)tt, (STRPTR)"PARSERS", (STRPTR)"PROGDIR:Parsers"),
               srv->parsers, sizeof(srv->parsers));
    srv->timeout = ArgInt((CONST_STRPTR *)tt, (STRPTR)"TIMEOUT", 10);
    srv->priority = ArgInt((CONST_STRPTR *)tt, (STRPTR)"CX_PRIORITY", 0);
    srv->minfree = ArgInt((CONST_STRPTR *)tt, (STRPTR)"MINFREE", 1024);
    strncpy(srv->popkey, (const char *)ArgString((CONST_STRPTR *)tt, (STRPTR)"CX_POPKEY", (STRPTR)"ctrl alt s"),
            sizeof(srv->popkey) - 1);
    srv->popup = scout_path_equal((const char *)ArgString((CONST_STRPTR *)tt, (STRPTR)"CX_POPUP", (STRPTR)"NO"), "YES");
    if (srv->minfree < 0) srv->minfree = 0;
    if (srv->timeout < 1) srv->timeout = 1;
    ArgArrayDone();
}

unsigned long scout_now(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (unsigned long)ds.ds_Days * 86400UL + (unsigned long)ds.ds_Minute * 60UL +
           (unsigned long)ds.ds_Tick / TICKS_PER_SECOND;
}

int scout_memory_low(const ScoutServer *srv)
{
    return srv->minfree > 0 && AvailMem(MEMF_ANY) < (ULONG)srv->minfree * 1024UL;
}

void scout_session_end(ScoutServer *srv, long id)
{
    scout_walk_stop(srv, id);
    scout_jobs_drop_session(srv, id);
    scout_watch_drop_session(srv, id);
    scout_session_close(&srv->store, id);
}

void scout_memory_check(ScoutServer *srv)
{
    ScoutSession *s;
    unsigned long now;
    if (!scout_memory_low(srv)) return;
    now = scout_now();
    while (scout_memory_low(srv) && (s = scout_session_idlest(&srv->store, now - 60)) != NULL)
        scout_session_end(srv, s->id);
    s = scout_session_find(&srv->store, 0);
    if (scout_memory_low(srv) && s && s->files) scout_session_close(&srv->store, 0);
}

/**
 * @brief Close the libraries.
 */
static void close_libraries(void)
{
    if (RexxSysBase) CloseLibrary((struct Library *)RexxSysBase);
    if (IconBase) CloseLibrary(IconBase);
    if (CxBase) CloseLibrary(CxBase);
    RexxSysBase = NULL;
    IconBase = CxBase = NULL;
}

/**
 * @brief Start the commodity and its ARexx port and serve them until it is
 *        killed or told to quit.
 * @param argc The argument count (0 from the Workbench).
 * @param argv The arguments, or the WBStartup message.
 * @return 0, or 20 if it could not start.
 */
int main(int argc, char **argv)
{
    ScoutServer *srv = &g_scout;
    int ok;

    memset(srv, 0, sizeof(*srv));
    from_shell = argc > 0;
    CxBase = OpenLibrary((STRPTR)"commodities.library", 37);
    IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);
    if (!CxBase || !IconBase || !RexxSysBase) {
        say("TinyDev-Scout: cannot open commodities, icon or rexxsyslib.library\n");
        close_libraries();
        return 20;
    }
    read_settings(srv, argc, argv);
    if (!scout_store_init(&srv->store)) {
        close_libraries();
        return 20;
    }

    ok = scout_broker_init(srv);
    if (ok < 0) {
        /* runs already: that Scout shows its window, this one just ends */
        scout_store_free(&srv->store);
        close_libraries();
        return 0;
    }
    if (!ok) say("TinyDev-Scout: the commodity cannot be created\n");
    if (ok && !(ok = scout_rexx_init(srv))) say("TinyDev-Scout: the ARexx port " SCOUT_AREXX_PORT_NAME " exists already\n");
    if (ok && !(ok = scout_jobs_init(srv) && scout_watch_init(srv) && scout_window_init(srv)))
        say("TinyDev-Scout: cannot open timer.device\n");

    if (ok) {
        srv->running = 1;
        scout_jobs_scan(srv);
        if (srv->popup) scout_window_show(srv);
    }
    while (srv->running) {
        ULONG mask = SIGBREAKF_CTRL_C | scout_jobs_sigmask(srv) | scout_watch_sigmask(srv) |
                     scout_window_sigmask(srv);
        ULONG got;
        if (srv->broker_port) mask |= 1UL << srv->broker_port->mp_SigBit;
        if (srv->rexx_port) mask |= 1UL << srv->rexx_port->mp_SigBit;
        /* while a project is walked, look for signals without waiting */
        if (srv->walks && !srv->scanning) {
            got = SetSignal(0, mask) & mask;
        } else {
            got = Wait(mask);
        }
        if (got & SIGBREAKF_CTRL_C) srv->running = 0;
        scout_broker_handle(srv);
        scout_rexx_handle(srv);
        scout_jobs_handle(srv, got);
        scout_watch_handle(srv, got);
        scout_window_handle(srv, got);
        scout_memory_check(srv);
        if (srv->running && srv->walks && !srv->scanning) scout_walk_step(srv);
    }

    while (srv->walks) scout_walk_stop(srv, srv->walks->session);
    scout_window_cleanup(srv);
    scout_watch_cleanup(srv);
    scout_jobs_cleanup(srv);
    scout_rexx_cleanup(srv);
    scout_broker_cleanup(srv);
    scout_store_free(&srv->store);
    close_libraries();
    return ok ? 0 : 20;
}
