/**
 * @file scout_watch.c
 * @brief Scout watches the drawers of each session's project with
 *        StartNotify() and brings the index up to date when files change.
 *
 * A notification on a drawer comes when a file in it is written, made or
 * deleted (not for its subdrawers' files), so every drawer the walk enters
 * is watched, at most SCOUT_MAX_WATCHES per session. Saving a file often
 * gives several notifications; a changed drawer is checked only after it
 * has been quiet for a second (the settle timer): files that are gone and
 * drawers that are gone leave the index, then the drawer is walked again
 * (scout_walk_refresh()), which queues new and changed files and enters
 * new subdrawers. Files a client analysed as a snapshot stay, the client
 * owns them.
 */
#include "scout_server.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>

/** Seconds a changed drawer must be quiet before it is checked. */
#define SETTLE_SECONDS 1

/**
 * @brief Tell whether a file or drawer exists.
 * @param path Its path.
 * @return Non-zero if it does.
 */
static int exists(const char *path)
{
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
}

/**
 * @brief Tell whether a path lies directly in a drawer (not below).
 * @param drawer The drawer (full name).
 * @param path The path.
 * @return Non-zero if it does.
 */
static int directly_in(const char *drawer, const char *path)
{
    const char *rest;
    if (!scout_path_inside(drawer, path) || strlen(path) <= strlen(drawer)) return 0;
    rest = path + strlen(drawer);
    if (*rest == '/') rest++;
    return strchr(rest, '/') == NULL;
}

/**
 * @brief Start (or start again) the settle timer.
 * @param srv The server.
 */
static void start_settle(ScoutServer *srv)
{
    if (!srv->settle_open) return;
    if (srv->settle_pending) {
        AbortIO((struct IORequest *)srv->settle);
        WaitIO((struct IORequest *)srv->settle);
    }
    srv->settle->tr_node.io_Command = TR_ADDREQUEST;
    srv->settle->tr_time.tv_secs = SETTLE_SECONDS;
    srv->settle->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)srv->settle);
    srv->settle_pending = 1;
}

/**
 * @brief End a watch and free it; notifications of it still in the port
 *        are answered first (they point to its request).
 * @param srv The server.
 * @param w The watch.
 */
static void end_watch(ScoutServer *srv, ScoutWatch *w)
{
    ScoutWatch **p = &srv->watches;
    struct Node *n, *next;
    while (*p && *p != w) p = &(*p)->next;
    if (*p) *p = w->next;
    if (w->active) EndNotify(&w->nr);
    Forbid();
    for (n = srv->watch_port->mp_MsgList.lh_Head; (next = n->ln_Succ) != NULL; n = next) {
        if (((struct NotifyMessage *)n)->nm_NReq == &w->nr) {
            Remove(n);
            ReplyMsg((struct Message *)n);
        }
    }
    Permit();
    free(w);
}

int scout_watch_init(ScoutServer *srv)
{
    if ((srv->watch_port = CreateMsgPort()) == NULL) return 0;
    if ((srv->settle_port = CreateMsgPort()) == NULL) return 0;
    srv->settle = (struct timerequest *)CreateIORequest(srv->settle_port, sizeof(struct timerequest));
    if (!srv->settle) return 0;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)srv->settle, 0) != 0) return 0;
    srv->settle_open = 1;
    return 1;
}

int scout_watch_known(const ScoutServer *srv, long session, const char *path)
{
    const ScoutWatch *w;
    for (w = srv->watches; w; w = w->next) {
        if (w->session == session && scout_path_equal(w->path, path)) return 1;
    }
    return 0;
}

void scout_watch_add(ScoutServer *srv, long session, const char *path)
{
    ScoutWatch *w;
    int count = 0;
    if (!srv->watch_port || strlen(path) >= SCOUT_PATH_MAX) return;
    for (w = srv->watches; w; w = w->next) {
        if (w->session != session) continue;
        if (scout_path_equal(w->path, path) || ++count >= SCOUT_MAX_WATCHES) return;
    }
    if ((w = (ScoutWatch *)calloc(1, sizeof(ScoutWatch))) == NULL) return;
    strcpy(w->path, path);
    w->session = session;
    w->nr.nr_Name = (STRPTR)w->path;
    w->nr.nr_UserData = (ULONG)w;
    w->nr.nr_Flags = NRF_SEND_MESSAGE | NRF_WAIT_REPLY;
    w->nr.nr_stuff.nr_Msg.nr_Port = srv->watch_port;
    w->active = StartNotify(&w->nr) != 0;
    if (!w->active) {
        /* a file system without notification: nothing to watch */
        free(w);
        return;
    }
    w->next = srv->watches;
    srv->watches = w;
}

void scout_watch_drop_session(ScoutServer *srv, long session)
{
    ScoutWatch *w = srv->watches, *next;
    for (; w; w = next) {
        next = w->next;
        if (w->session == session) end_watch(srv, w);
    }
}

/**
 * @brief Remove the files of a session in a drawer from the index (those
 *        on disk, not a client's snapshots).
 * @param s The session.
 * @param drawer The drawer.
 * @param below Also the files in its subdrawers.
 * @param check Only files that no longer exist.
 * @return Number of files removed.
 */
static long remove_files(ScoutSession *s, const char *drawer, int below, int check)
{
    ScoutFile *f = s->files, *next;
    long n = 0;
    for (; f; f = next) {
        next = f->next;
        if (f->valid && f->version >= 0) continue;
        if (below ? !scout_path_inside(drawer, f->path) : !directly_in(drawer, f->path)) continue;
        if (check && exists(f->path)) continue;
        n += scout_file_remove(s, f->path);
    }
    return n;
}

/**
 * @brief Check a changed drawer: what is gone leaves the index, then it is
 *        walked again.
 * @param srv The server.
 * @param w Its watch.
 */
static void check_drawer(ScoutServer *srv, ScoutWatch *w)
{
    ScoutSession *s = scout_session_find(&srv->store, w->session);
    char path[SCOUT_PATH_MAX];
    ScoutWatch *v, *next;
    int gone;
    if (!s) {
        end_watch(srv, w);
        return;
    }
    strcpy(path, w->path);
    gone = !exists(path);
    /* drawers in it (or itself) that were deleted: their files go, and
     * their watches end */
    for (v = srv->watches; v; v = next) {
        next = v->next;
        if (v->session != s->id || !scout_path_inside(path, v->path) || exists(v->path)) continue;
        s->changes += remove_files(s, v->path, 1, 0) > 0;
        end_watch(srv, v);
        next = srv->watches;    /* the list changed */
    }
    if (gone) return;
    if (remove_files(s, path, 0, 1) > 0) s->changes++;
    scout_walk_refresh(srv, s, path);
}

void scout_watch_handle(ScoutServer *srv, ULONG signals)
{
    struct NotifyMessage *nm;
    ScoutWatch *w;
    if (srv->watch_port && (signals & (1UL << srv->watch_port->mp_SigBit))) {
        int any = 0;
        while ((nm = (struct NotifyMessage *)GetMsg(srv->watch_port)) != NULL) {
            w = (ScoutWatch *)nm->nm_NReq->nr_UserData;
            ReplyMsg((struct Message *)nm);
            w->dirty = 1;
            any = 1;
        }
        if (any) start_settle(srv);
    }
    if (srv->settle_port && (signals & (1UL << srv->settle_port->mp_SigBit))) {
        if (!srv->settle_pending || !CheckIO((struct IORequest *)srv->settle)) return;
        WaitIO((struct IORequest *)srv->settle);
        srv->settle_pending = 0;
        /* checking may end watches: look for the next dirty one from the start */
        for (;;) {
            for (w = srv->watches; w && !w->dirty; w = w->next);
            if (!w) break;
            w->dirty = 0;
            check_drawer(srv, w);
        }
        scout_jobs_next(srv);
    }
}

ULONG scout_watch_sigmask(const ScoutServer *srv)
{
    ULONG mask = 0;
    if (srv->watch_port) mask |= 1UL << srv->watch_port->mp_SigBit;
    if (srv->settle_port) mask |= 1UL << srv->settle_port->mp_SigBit;
    return mask;
}

void scout_watch_cleanup(ScoutServer *srv)
{
    while (srv->watches) end_watch(srv, srv->watches);
    if (srv->settle_pending) {
        AbortIO((struct IORequest *)srv->settle);
        WaitIO((struct IORequest *)srv->settle);
    }
    if (srv->settle_open) CloseDevice((struct IORequest *)srv->settle);
    if (srv->settle) DeleteIORequest((struct IORequest *)srv->settle);
    if (srv->settle_port) DeleteMsgPort(srv->settle_port);
    if (srv->watch_port) DeleteMsgPort(srv->watch_port);
    srv->settle = NULL;
    srv->settle_port = srv->watch_port = NULL;
    srv->settle_open = srv->settle_pending = 0;
}
