/**
 * @file scout_walk.c
 * @brief Scout's project index: walking a session's project drawer and
 *        following the includes, queueing background analyses.
 *
 * The walk is bounded (16 levels, SCOUT_MAX_FILES files per session),
 * leaves out links, icons and drawers whose names start with a dot, and
 * goes on a few entries at a time from the main loop, so ARexx commands
 * are answered meanwhile.
 */
#include "scout_server.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>

/** Directory entries read per step. */
#define WALK_STEP 16

int scout_index_file(ScoutServer *srv, ScoutSession *s, const char *path, int in_project)
{
    const ScoutLanguage *lang = scout_store_language_for(&srv->store, path);
    ScoutFile *f;
    ScoutStamp stamp;
    ScoutItem item;

    if (!lang || srv->store.parsers[lang->parser].disabled) return 0;
    f = scout_file_find(s, path);
    if (!f && s->file_count >= SCOUT_MAX_FILES) return 0;
    if (!scout_file_stamp(path, &stamp)) return 0;
    if (!f && (f = scout_file_get(s, path)) == NULL) return 0;
    if (in_project) f->in_project = 1;
    if (f->valid && (f->version >= 0 || memcmp(&f->stamp, &stamp, sizeof(stamp)) == 0)) return 0;
    memset(&item, 0, sizeof(item));
    item.path = (char *)path;
    item.language = lang->id;
    item.version = -1;
    item.stamp = stamp;
    item.in_project = in_project;
    return scout_jobs_analyze(srv, s->id, &item, 1);
}

/**
 * @brief Open a directory for reading and push it onto a walk.
 * @param w The walk.
 * @param path The directory.
 * @return 1 on success.
 */
static int push_dir(ScoutWalk *w, const char *path)
{
    ScoutWalkDir *d;
    if (w->depth >= (int)(sizeof(w->dirs) / sizeof(w->dirs[0]))) return 0;
    d = &w->dirs[w->depth];
    d->lock = Lock((STRPTR)path, SHARED_LOCK);
    d->fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!d->lock || !d->fib || !Examine(d->lock, d->fib) || d->fib->fib_DirEntryType <= 0) {
        if (d->fib) FreeDosObject(DOS_FIB, d->fib);
        if (d->lock) UnLock(d->lock);
        return 0;
    }
    strncpy(d->path, path, sizeof(d->path) - 1);
    d->path[sizeof(d->path) - 1] = '\0';
    w->depth++;
    return 1;
}

/**
 * @brief Close the innermost directory of a walk.
 * @param w The walk.
 */
static void pop_dir(ScoutWalk *w)
{
    ScoutWalkDir *d = &w->dirs[--w->depth];
    FreeDosObject(DOS_FIB, d->fib);
    UnLock(d->lock);
}

/**
 * @brief Remove a walk from the list and free it.
 * @param srv The server.
 * @param w The walk.
 */
static void free_walk(ScoutServer *srv, ScoutWalk *w)
{
    ScoutWalk **p = &srv->walks;
    while (*p && *p != w) p = &(*p)->next;
    if (*p) *p = w->next;
    while (w->depth > 0) pop_dir(w);
    free(w);
}

void scout_walk_start(ScoutServer *srv, ScoutSession *s)
{
    ScoutWalk *w = (ScoutWalk *)calloc(1, sizeof(ScoutWalk)), **p;
    if (!w) return;
    w->session = s->id;
    if (!push_dir(w, s->project)) {
        free(w);
        return;
    }
    for (p = &srv->walks; *p; p = &(*p)->next);
    *p = w;
}

void scout_walk_stop(ScoutServer *srv, long session)
{
    ScoutWalk *w = srv->walks, *next;
    for (; w; w = next) {
        next = w->next;
        if (w->session == session) free_walk(srv, w);
    }
}

int scout_walk_step(ScoutServer *srv)
{
    ScoutWalk *w = srv->walks;
    ScoutSession *s;
    int n;
    if (!w) return 0;
    s = scout_session_find(&srv->store, w->session);
    for (n = 0; s && n < WALK_STEP && w->depth > 0 && w->found < SCOUT_MAX_FILES; n++) {
        ScoutWalkDir *d = &w->dirs[w->depth - 1];
        char path[SCOUT_PATH_MAX];
        const char *name;
        size_t len;
        if (!ExNext(d->lock, d->fib)) {
            pop_dir(w);
            continue;
        }
        name = d->fib->fib_FileName;
        len = strlen(name);
        strncpy(path, d->path, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        if (!AddPart((STRPTR)path, (STRPTR)name, sizeof(path))) continue;
        if (d->fib->fib_DirEntryType == ST_USERDIR) {
            if (name[0] != '.') push_dir(w, path);
        } else if (d->fib->fib_DirEntryType == ST_FILE) {
            if (len > 5 && scout_path_equal(name + len - 5, ".info")) continue;
            if (scout_index_file(srv, s, path, 1)) w->found++;
        }
    }
    if (!s || w->depth == 0 || w->found >= SCOUT_MAX_FILES) free_walk(srv, w);
    scout_jobs_next(srv);
    return srv->walks != NULL;
}

/**
 * @brief Tell whether a file exists and return its full name.
 * @param path The file.
 * @param[out] full Its full name.
 * @param size Size of @p full.
 * @return 1 if it exists.
 */
static int find_file(const char *path, char *full, size_t size)
{
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    int ok;
    if (!lock) return 0;
    ok = NameFromLock(lock, (STRPTR)full, (LONG)size) != 0;
    UnLock(lock);
    return ok;
}

void scout_walk_references(ScoutServer *srv, ScoutSession *s, const ScoutFile *f)
{
    long i, j;
    for (i = 0; i < f->reference_count; i++) {
        const ScoutReference *r = &f->references[i];
        char path[SCOUT_PATH_MAX], full[SCOUT_PATH_MAX];
        int found = 0;
        if (!r->system) {
            strncpy(path, f->path, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            *PathPart((STRPTR)path) = '\0';
            found = AddPart((STRPTR)path, (STRPTR)r->target, sizeof(path)) && find_file(path, full, sizeof(full));
        }
        for (j = 0; !found && j < s->include_count; j++) {
            strncpy(path, s->include[j], sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            found = AddPart((STRPTR)path, (STRPTR)r->target, sizeof(path)) && find_file(path, full, sizeof(full));
        }
        if (found) scout_index_file(srv, s, full, 0);
    }
}
