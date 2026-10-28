/**
 * @file scout_rexx.c
 * @brief Scout's ARexx port TINYDEV_SCOUT and its commands.
 *
 * A command's arguments are read with ReadArgs() from the command string,
 * so they follow the AmigaDOS templates of src/scout/docs/Scout.md. A
 * result is JSON (RC 0); a failure is RC 10 with the error number as RC2,
 * its text kept for ERROR. A command that needs a parser run keeps its
 * message until the job is done; a query on a file without a current
 * result first queues its analysis and is carried out again afterwards.
 * Commands that need the parsers wait while the parser drawer is read.
 */
#include "scout_server.h"
#include "scout_protocol.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>
#include <rexx/errors.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Longest result ARexx can take (RexxArg::ra_Length is a UWORD). */
#define REXX_MAX 65535

/** Most values a template has. */
#define MAX_ARGS 8

/** Items COMPLETE lists without MAX. */
#define COMPLETE_MAX 50

/** The values ReadArgs() read for a command. */
typedef struct Args {
    struct RDArgs *rda;     /**< ReadArgs()'s state */
    char *buf;              /**< the arguments, ended by a line feed */
    LONG v[MAX_ARGS];       /**< the values */
} Args;

/** A command of the port. */
typedef struct Command {
    const char *name;       /**< its name */
    const char *tmpl;       /**< its template ("" for none) */
    void (*run)(ScoutServer *srv, struct RexxMsg *rx, LONG *v);  /**< carries it out */
    int parsers;            /**< it needs the parsers (waits while they are read) */
} Command;

int scout_rexx_init(ScoutServer *srv)
{
    Forbid();
    if (FindPort((STRPTR)SCOUT_AREXX_PORT_NAME)) {
        Permit();
        return 0;
    }
    srv->rexx_port = CreateMsgPort();
    if (!srv->rexx_port) {
        Permit();
        return 0;
    }
    srv->rexx_port->mp_Node.ln_Name = (char *)SCOUT_AREXX_PORT_NAME;
    srv->rexx_port->mp_Node.ln_Pri = 0;
    AddPort(srv->rexx_port);
    Permit();
    return 1;
}

void scout_reply(struct RexxMsg *rx, const char *result)
{
    rx->rm_Result1 = RC_OK;
    rx->rm_Result2 = 0;
    if (result && (rx->rm_Action & RXFF_RESULT)) {
        rx->rm_Result2 = (LONG)CreateArgstring((STRPTR)result, (ULONG)strlen(result));
    }
    ReplyMsg((struct Message *)rx);
}

void scout_fail(ScoutServer *srv, struct RexxMsg *rx, long session, long code, const char *text)
{
    ScoutSession *s = session > 0 ? scout_session_find(&srv->store, session) : NULL;
    ScoutSession *none = scout_session_find(&srv->store, 0);
    /* session 0 keeps the last error of all, so ERROR without a session finds it */
    if (s) scout_session_set_error(s, code, text);
    if (none) scout_session_set_error(none, code, text);
    rx->rm_Result1 = RC_ERROR;
    rx->rm_Result2 = code;
    ReplyMsg((struct Message *)rx);
}

/**
 * @brief Answer with a written result: into a file (TO), or as the result
 *        if it fits.
 * @param srv The server.
 * @param rx The message.
 * @param session The session (for the error).
 * @param w The result.
 * @param to The file to write it to, NULL.
 */
static void finish(ScoutServer *srv, struct RexxMsg *rx, long session, JsonWriter *w, const char *to)
{
    size_t len = json_writer_length(w);
    if (w->error) {
        scout_fail(srv, rx, session, SCOUT_E_MEMORY, "out of memory");
    } else if (to) {
        BPTR fh = Open((STRPTR)to, MODE_NEWFILE);
        LONG done = fh ? Write(fh, (APTR)json_writer_get(w), (LONG)len) : -1;
        if (fh) Close(fh);
        if (done != (LONG)len) {
            scout_fail(srv, rx, session, SCOUT_E_FILE, "cannot write the result file");
        } else {
            JsonWriter r;
            json_writer_init_alloc(&r, 128);
            json_write_raw(&r, "{\"file\":");
            json_write_string(&r, to);
            json_write_raw(&r, ",\"size\":");
            json_write_int(&r, (long)len);
            json_write_raw(&r, "}");
            scout_reply(rx, json_writer_get(&r));
            json_writer_free(&r);
        }
    } else if (len > REXX_MAX) {
        scout_fail(srv, rx, session, SCOUT_E_TOO_LARGE, "result too large for ARexx (use TO)");
    } else {
        scout_reply(rx, json_writer_get(w));
    }
    json_writer_free(w);
}

/**
 * @brief Return the full name of a file or drawer (unchanged if it does
 *        not exist).
 * @param path The path.
 * @param[out] out The full name.
 * @param size Size of @p out.
 */
static void full_name(const char *path, char *out, size_t size)
{
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (!lock || !NameFromLock(lock, (STRPTR)out, (LONG)size)) {
        strncpy(out, path, size - 1);
        out[size - 1] = '\0';
    }
    if (lock) UnLock(lock);
}

/**
 * @brief Find the session a command names (SESSION/K/N); session 0
 *        without one. Fails the command if it does not exist.
 * @param srv The server.
 * @param rx The message.
 * @param id The value of SESSION (may be NULL).
 * @return The session, or NULL (the message is answered then).
 */
static ScoutSession *get_session(ScoutServer *srv, struct RexxMsg *rx, LONG *id)
{
    ScoutSession *s = scout_session_find(&srv->store, id ? *id : 0);
    if (!s) scout_fail(srv, rx, -1, SCOUT_E_SESSION, "unknown session");
    return s;
}

/**
 * @brief Return a file of a session with a current result, or queue its
 *        analysis (the command is carried out again when it is done).
 * @param srv The server.
 * @param rx The message.
 * @param s The session.
 * @param path The file (full name).
 * @return The file, or NULL if the command waits or failed (the message
 *         is taken or answered then).
 */
static ScoutFile *ready_file(ScoutServer *srv, struct RexxMsg *rx, ScoutSession *s, const char *path)
{
    ScoutFile *f = scout_file_find(s, path);
    const ScoutLanguage *lang;
    ScoutStamp stamp;
    ScoutItem item;
    int exists = scout_file_stamp(path, &stamp);

    if (f && f->valid && (f->version >= 0 || !exists || memcmp(&f->stamp, &stamp, sizeof(stamp)) == 0)) {
        return f;
    }
    if (!exists) {
        scout_fail(srv, rx, s->id, SCOUT_E_FILE, "file not found");
        return NULL;
    }
    lang = f && f->language ? scout_store_language(&srv->store, f->language) : NULL;
    if (!lang) lang = scout_store_language_for(&srv->store, path);
    if (!lang) {
        scout_fail(srv, rx, s->id, SCOUT_E_NO_PARSER, "no parser for this file");
        return NULL;
    }
    memset(&item, 0, sizeof(item));
    item.path = (char *)path;
    item.language = lang->id;
    item.version = -1;
    item.stamp = stamp;
    item.in_project = 1;
    item.rx = rx;
    item.requery = 1;
    if (!scout_jobs_analyze(srv, s->id, &item, 0)) {
        scout_fail(srv, rx, s->id, SCOUT_E_MEMORY, "out of memory");
        return NULL;
    }
    scout_jobs_next(srv);
    return NULL;
}

/**
 * @brief Read a line of a file.
 * @param path The file.
 * @param line The line (1-based).
 * @param[out] buf The line, without its line end (cut if too long).
 * @param size Size of @p buf.
 * @return 1 if the file has the line.
 */
static int read_line(const char *path, long line, char *buf, size_t size)
{
    char chunk[256];
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    long n = 1;
    size_t len = 0;
    int found = 0;
    if (!fh) return 0;
    buf[0] = '\0';
    while (FGets(fh, (STRPTR)chunk, sizeof(chunk))) {
        size_t c = strlen(chunk);
        int end = c > 0 && chunk[c - 1] == '\n';
        if (n == line) {
            size_t take = c - (size_t)end;
            if (len + take >= size) take = size - 1 - len;
            memcpy(buf + len, chunk, take);
            len += take;
            buf[len] = '\0';
            found = 1;
            if (end) break;
        }
        if (end) n++;
    }
    Close(fh);
    if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';
    return found;
}

/**
 * @brief Find the word at a position of a file (or the part before it).
 * @param srv The server.
 * @param f The file (its snapshot is read if it has one).
 * @param line The line.
 * @param col The column.
 * @param before Only the part before the column.
 * @param[out] word The word.
 * @param size Size of @p word.
 */
static void word_at(ScoutServer *srv, const ScoutFile *f, long line, long col, int before,
                    char *word, size_t size)
{
    char text[1024];
    const char *path = f->snapshot ? f->snapshot : f->path;
    word[0] = '\0';
    if (read_line(path, line, text, sizeof(text)) ||
        (f->snapshot && read_line(f->path, line, text, sizeof(text)))) {
        scout_word_at(f->language ? scout_store_language(&srv->store, f->language) : NULL,
                      text, col, before, word, size);
    }
}

/* ---- Commands ---- */

/**
 * @brief OPEN PROJECT/A,INCLUDE/M: start a session and walk its project.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_open(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    char path[SCOUT_PATH_MAX];
    char **inc = (char **)v[1];
    ScoutSession *s;
    BPTR lock = Lock((STRPTR)v[0], SHARED_LOCK);
    if (!lock) {
        scout_fail(srv, rx, -1, SCOUT_E_FILE, "project drawer not found");
        return;
    }
    if (!NameFromLock(lock, (STRPTR)path, sizeof(path))) {
        strncpy(path, (const char *)v[0], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    UnLock(lock);
    s = scout_session_open(&srv->store, path);
    if (!s) {
        scout_fail(srv, rx, -1, SCOUT_E_MEMORY, "out of memory");
        return;
    }
    for (; inc && *inc; inc++) {
        full_name(*inc, path, sizeof(path));
        scout_session_add_include(s, path);
    }
    scout_walk_start(srv, s);
    {
        char result[32];
        snprintf(result, sizeof(result), "{\"session\":%ld}", s->id);
        scout_reply(rx, result);
    }
}

/**
 * @brief CLOSE SESSION/N/A: end a session.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_close(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    long id = *(LONG *)v[0];
    if (!scout_session_find(&srv->store, id)) {
        scout_fail(srv, rx, -1, SCOUT_E_SESSION, "unknown session");
        return;
    }
    scout_walk_stop(srv, id);
    scout_jobs_drop_session(srv, id);
    scout_session_close(&srv->store, id);
    scout_reply(rx, "{}");
}

/**
 * @brief INCLUDE SESSION/N/A,PATHS/M,CLEAR/S: the include paths of a
 *        session.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_include(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    char **paths = (char **)v[1];
    char path[SCOUT_PATH_MAX];
    JsonWriter w;
    long i;
    if (!s) return;
    if (v[2]) scout_session_clear_include(s);
    for (; paths && *paths; paths++) {
        full_name(*paths, path, sizeof(path));
        scout_session_add_include(s, path);
    }
    json_writer_init_alloc(&w, 256);
    json_write_raw(&w, "{\"include\":[");
    for (i = 0; i < s->include_count; i++) {
        if (i > 0) json_write_raw(&w, ",");
        json_write_string(&w, s->include[i]);
    }
    json_write_raw(&w, "]}");
    finish(srv, rx, s->id, &w, NULL);
}

/**
 * @brief ERROR SESSION/K/N: the last error of a session.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_error(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    JsonWriter w;
    if (!s) return;
    json_writer_init_alloc(&w, 128);
    json_write_raw(&w, "{\"code\":");
    json_write_int(&w, s->error_code);
    json_write_raw(&w, ",\"message\":");
    json_write_string(&w, s->error_text ? s->error_text : "");
    json_write_raw(&w, "}");
    finish(srv, rx, s->id, &w, NULL);
}

/**
 * @brief ANALYZE SESSION/K/N,FILE/A,TEXT/K,VERSION/K/N,LANG/K: analyse a
 *        file (or a client's snapshot of it); answered when done.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_analyze(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    const char *text = (const char *)v[2];
    const ScoutLanguage *lang;
    char path[SCOUT_PATH_MAX];
    ScoutItem item;
    ScoutFile *f;
    if (!s) return;
    full_name((const char *)v[1], path, sizeof(path));
    lang = v[4] ? scout_store_language(&srv->store, (const char *)v[4]) : scout_store_language_for(&srv->store, path);
    if (!lang) {
        scout_fail(srv, rx, s->id, SCOUT_E_NO_PARSER, "no parser for this file or language");
        return;
    }
    memset(&item, 0, sizeof(item));
    item.path = path;
    item.language = lang->id;
    item.in_project = 1;
    item.rx = rx;
    if (text) {
        BPTR lock = Lock((STRPTR)text, SHARED_LOCK);
        if (!lock) {
            scout_fail(srv, rx, s->id, SCOUT_E_FILE, "snapshot not found");
            return;
        }
        UnLock(lock);
        item.text = (char *)text;
        item.version = v[3] ? *(LONG *)v[3] : 0;
        scout_file_stamp(path, &item.stamp);
    } else {
        item.version = -1;
        if (!scout_file_stamp(path, &item.stamp)) {
            scout_fail(srv, rx, s->id, SCOUT_E_FILE, "file not found");
            return;
        }
        f = scout_file_find(s, path);
        if (f && f->valid && f->version < 0 && strcmp(f->language, lang->id) == 0 &&
            memcmp(&f->stamp, &item.stamp, sizeof(item.stamp)) == 0) {
            JsonWriter w;
            f->in_project = 1;
            json_writer_init_alloc(&w, 256);
            scout_write_analyze(&w, f);
            finish(srv, rx, s->id, &w, NULL);
            return;
        }
    }
    if (!scout_jobs_analyze(srv, s->id, &item, 0)) {
        scout_fail(srv, rx, s->id, SCOUT_E_MEMORY, "out of memory");
        return;
    }
    scout_jobs_next(srv);
}

/**
 * @brief FORGET SESSION/K/N,FILE/A: drop a snapshot's result.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_forget(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    char path[SCOUT_PATH_MAX];
    ScoutFile *f;
    if (!s) return;
    full_name((const char *)v[1], path, sizeof(path));
    f = scout_file_find(s, path);
    if (f) scout_file_forget(f);
    scout_reply(rx, "{}");
}

/**
 * @brief Match callback of SYMBOLS: an AmigaDOS pattern, without regard
 *        to case.
 * @param context The parsed pattern.
 * @param name A symbol's name.
 * @return Non-zero if it matches.
 */
static int match_pattern(void *context, const char *name)
{
    return MatchPatternNoCase((STRPTR)context, (STRPTR)name) != 0;
}

/**
 * @brief SYMBOLS SESSION/K/N,FILE/K,NAME/K,TO/K: the symbols of a file or
 *        of the project.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_symbols(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    const char *name = (const char *)v[2];
    char *pattern = NULL;
    ScoutFile *f = NULL;
    JsonWriter w;
    if (!s) return;
    if (v[1]) {
        char path[SCOUT_PATH_MAX];
        full_name((const char *)v[1], path, sizeof(path));
        if ((f = ready_file(srv, rx, s, path)) == NULL) return;
    }
    if (name) {
        size_t n = strlen(name) * 2 + 2;
        pattern = (char *)malloc(n);
        if (!pattern || ParsePatternNoCase((STRPTR)name, (STRPTR)pattern, (LONG)n) < 0) {
            free(pattern);
            scout_fail(srv, rx, s->id, SCOUT_E_ARGS, "bad pattern");
            return;
        }
    }
    json_writer_init_alloc(&w, 4096);
    scout_write_symbols(&w, s, f, pattern ? match_pattern : NULL, pattern);
    free(pattern);
    finish(srv, rx, s->id, &w, (const char *)v[3]);
}

/**
 * @brief DIAGNOSTICS SESSION/K/N,FILE/K,TO/K: the diagnostics of a file or
 *        of the project.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_diagnostics(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    ScoutFile *f = NULL;
    JsonWriter w;
    if (!s) return;
    if (v[1]) {
        char path[SCOUT_PATH_MAX];
        full_name((const char *)v[1], path, sizeof(path));
        if ((f = ready_file(srv, rx, s, path)) == NULL) return;
    }
    json_writer_init_alloc(&w, 1024);
    scout_write_diagnostics(&w, s, f);
    finish(srv, rx, s->id, &w, (const char *)v[2]);
}

/**
 * @brief TOKENS SESSION/K/N,FILE/A,TO/K: the highlighting tokens of a file.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_tokens(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    char path[SCOUT_PATH_MAX];
    ScoutFile *f;
    JsonWriter w;
    if (!s) return;
    full_name((const char *)v[1], path, sizeof(path));
    if ((f = ready_file(srv, rx, s, path)) == NULL) return;
    json_writer_init_alloc(&w, 4096);
    scout_write_tokens(&w, f);
    finish(srv, rx, s->id, &w, (const char *)v[2]);
}

/**
 * @brief DEFINITION SESSION/K/N,FILE/A,LINE/N/A,COLUMN/N/A,NAME/K: where
 *        the name at a position is defined.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_definition(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    char path[SCOUT_PATH_MAX], word[128];
    long line = *(LONG *)v[2], col = *(LONG *)v[3];
    ScoutFile *f;
    JsonWriter w;
    if (!s) return;
    full_name((const char *)v[1], path, sizeof(path));
    if ((f = ready_file(srv, rx, s, path)) == NULL) return;
    if (v[4]) {
        strncpy(word, (const char *)v[4], sizeof(word) - 1);
        word[sizeof(word) - 1] = '\0';
    } else {
        word_at(srv, f, line, col, 0, word, sizeof(word));
    }
    json_writer_init_alloc(&w, 512);
    scout_write_definition(&w, s, f, line, col, word);
    finish(srv, rx, s->id, &w, NULL);
}

/**
 * @brief COMPLETE SESSION/K/N,FILE/A,LINE/N/A,COLUMN/N/A,PREFIX/K,MAX/K/N:
 *        names that could be completed at a position.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_complete(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    ScoutSession *s = get_session(srv, rx, (LONG *)v[0]);
    char path[SCOUT_PATH_MAX], prefix[128];
    long line = *(LONG *)v[2], col = *(LONG *)v[3];
    long max = v[5] ? *(LONG *)v[5] : COMPLETE_MAX;
    ScoutFile *f;
    JsonWriter w;
    if (!s) return;
    full_name((const char *)v[1], path, sizeof(path));
    if ((f = ready_file(srv, rx, s, path)) == NULL) return;
    if (v[4]) {
        strncpy(prefix, (const char *)v[4], sizeof(prefix) - 1);
        prefix[sizeof(prefix) - 1] = '\0';
    } else {
        word_at(srv, f, line, col, 1, prefix, sizeof(prefix));
    }
    json_writer_init_alloc(&w, 1024);
    scout_write_complete(&w, &srv->store, s, f, line, col, prefix, max);
    finish(srv, rx, s->id, &w, NULL);
}

/**
 * @brief LANGUAGES: the languages and their parsers.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments (none).
 */
static void cmd_languages(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    JsonWriter w;
    (void)v;
    json_writer_init_alloc(&w, 512);
    scout_write_languages(&w, &srv->store);
    finish(srv, rx, -1, &w, NULL);
}

/**
 * @brief LANGUAGE FILE/A: the language of a file.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_language(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    const ScoutLanguage *lang = scout_store_language_for(&srv->store, (const char *)v[0]);
    JsonWriter w;
    json_writer_init_alloc(&w, 64);
    json_write_raw(&w, "{\"language\":");
    if (lang) {
        json_write_string(&w, lang->id);
    } else {
        json_write_null(&w);
    }
    json_write_raw(&w, "}");
    finish(srv, rx, -1, &w, NULL);
}

/**
 * @brief SYNTAX LANG/A,TO/K: the lexical rules of a language.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_syntax(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    const ScoutLanguage *lang = scout_store_language(&srv->store, (const char *)v[0]);
    JsonWriter w;
    if (!lang) {
        scout_fail(srv, rx, -1, SCOUT_E_NO_PARSER, "unknown language");
        return;
    }
    if (!lang->syntax) {
        scout_fail(srv, rx, -1, SCOUT_E_CAPABILITY, "the parser has no lexical rules");
        return;
    }
    json_writer_init_alloc(&w, 1024);
    json_write_raw(&w, lang->syntax);
    finish(srv, rx, -1, &w, (const char *)v[1]);
}

/**
 * @brief REGISTER EXTENSION/A,PARSER/A: let a parser handle an extension;
 *        answered when its description is read.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments.
 */
static void cmd_register(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    const char *ext = (const char *)v[0];
    char path[SCOUT_PATH_MAX];
    if (ext[0] != '.' || strlen(ext) > 30) {
        scout_fail(srv, rx, -1, SCOUT_E_ARGS, "the extension must start with a dot");
        return;
    }
    full_name((const char *)v[1], path, sizeof(path));
    if (!scout_jobs_register(srv, path, ext, rx)) {
        scout_fail(srv, rx, -1, SCOUT_E_MEMORY, "out of memory");
        return;
    }
    scout_jobs_next(srv);
}

/**
 * @brief Let a message wait until the parsers are read.
 * @param srv The server.
 * @param rx The message.
 * @param languages Answer it with LANGUAGES' result then.
 */
static void defer(ScoutServer *srv, struct RexxMsg *rx, int languages)
{
    ScoutDeferred *d = (ScoutDeferred *)calloc(1, sizeof(ScoutDeferred)), **p;
    if (!d) {
        scout_fail(srv, rx, -1, SCOUT_E_MEMORY, "out of memory");
        return;
    }
    d->rx = rx;
    d->languages = languages;
    for (p = &srv->deferred; *p; p = &(*p)->next);
    *p = d;
}

/**
 * @brief RESCAN: read the parser drawer again; answered with LANGUAGES'
 *        result when done.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments (none).
 */
static void cmd_rescan(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    (void)v;
    scout_store_clear_parsers(&srv->store);
    defer(srv, rx, 1);
    scout_jobs_scan(srv);
}

/**
 * @brief STATUS: Scout's state.
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments (none).
 */
static void cmd_status(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    const ScoutSession *s;
    long sessions = 0;
    JsonWriter w;
    (void)v;
    for (s = srv->store.sessions; s; s = s->next) {
        if (s->id != 0) sessions++;
    }
    json_writer_init_alloc(&w, 512);
    json_write_raw(&w, "{\"version\":\"" SCOUT_VERSION "\",\"protocol\":");
    json_write_int(&w, SCOUT_PROTOCOL_VERSION);
    json_write_raw(&w, ",\"enabled\":");
    json_write_bool(&w, srv->enabled);
    json_write_raw(&w, ",\"scanning\":");
    json_write_bool(&w, srv->scanning > 0);
    json_write_raw(&w, ",\"tempdir\":");
    json_write_string(&w, srv->tempdir);
    json_write_raw(&w, ",\"sessions\":");
    json_write_int(&w, sessions);
    json_write_raw(&w, ",\"queue\":");
    json_write_int(&w, scout_jobs_count(srv));
    scout_write_parsers(&w, &srv->store);
    json_write_raw(&w, "}");
    finish(srv, rx, -1, &w, NULL);
}

/**
 * @brief QUIT: end Scout (after answering the waiting messages).
 * @param srv The server.
 * @param rx The message.
 * @param v The arguments (none).
 */
static void cmd_quit(ScoutServer *srv, struct RexxMsg *rx, LONG *v)
{
    (void)v;
    srv->running = 0;
    scout_reply(rx, "{}");
}

/** The commands. */
static const Command commands[] = {
    { "OPEN",        "PROJECT/A,INCLUDE/M",                              cmd_open,        1 },
    { "CLOSE",       "SESSION/N/A",                                      cmd_close,       0 },
    { "INCLUDE",     "SESSION/N/A,PATHS/M,CLEAR/S",                      cmd_include,     0 },
    { "ERROR",       "SESSION/K/N",                                      cmd_error,       0 },
    { "ANALYZE",     "SESSION/K/N,FILE/A,TEXT/K,VERSION/K/N,LANG/K",     cmd_analyze,     1 },
    { "FORGET",      "SESSION/K/N,FILE/A",                               cmd_forget,      0 },
    { "SYMBOLS",     "SESSION/K/N,FILE/K,NAME/K,TO/K",                   cmd_symbols,     1 },
    { "DIAGNOSTICS", "SESSION/K/N,FILE/K,TO/K",                          cmd_diagnostics, 1 },
    { "TOKENS",      "SESSION/K/N,FILE/A,TO/K",                          cmd_tokens,      1 },
    { "DEFINITION",  "SESSION/K/N,FILE/A,LINE/N/A,COLUMN/N/A,NAME/K",    cmd_definition,  1 },
    { "COMPLETE",    "SESSION/K/N,FILE/A,LINE/N/A,COLUMN/N/A,PREFIX/K,MAX/K/N", cmd_complete, 1 },
    { "LANGUAGES",   "",                                                 cmd_languages,   1 },
    { "LANGUAGE",    "FILE/A",                                           cmd_language,    1 },
    { "SYNTAX",      "LANG/A,TO/K",                                      cmd_syntax,      1 },
    { "REGISTER",    "EXTENSION/A,PARSER/A",                             cmd_register,    1 },
    { "RESCAN",      "",                                                 cmd_rescan,      1 },
    { "STATUS",      "",                                                 cmd_status,      0 },
    { "QUIT",        "",                                                 cmd_quit,        0 },
    { NULL, NULL, NULL, 0 }
};

/**
 * @brief Read the arguments of a command with ReadArgs().
 * @param tmpl The template.
 * @param line The arguments.
 * @param[out] a The values (FreeArgs() them with free_args()).
 * @return 1 on success, 0 if they do not fit the template.
 */
static int read_args(const char *tmpl, const char *line, Args *a)
{
    size_t n = strlen(line);
    memset(a, 0, sizeof(*a));
    a->buf = (char *)malloc(n + 2);
    a->rda = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL);
    if (!a->buf || !a->rda) return 0;
    memcpy(a->buf, line, n);
    a->buf[n] = '\n';
    a->buf[n + 1] = '\0';
    a->rda->RDA_Source.CS_Buffer = (STRPTR)a->buf;
    a->rda->RDA_Source.CS_Length = (LONG)n + 1;
    a->rda->RDA_Source.CS_CurChr = 0;
    a->rda->RDA_Flags |= RDAF_NOPROMPT;
    if (!ReadArgs((STRPTR)tmpl, a->v, a->rda)) {
        FreeDosObject(DOS_RDARGS, a->rda);
        a->rda = NULL;
        return 0;
    }
    return 1;
}

/**
 * @brief Free what read_args() read.
 * @param a The values.
 */
static void free_args(Args *a)
{
    if (a->rda) {
        FreeArgs(a->rda);
        FreeDosObject(DOS_RDARGS, a->rda);
    }
    free(a->buf);
    a->rda = NULL;
    a->buf = NULL;
}

void scout_rexx_command(ScoutServer *srv, struct RexxMsg *rx, int languages)
{
    const char *line = (const char *)rx->rm_Args[0];
    const Command *c;
    char name[16];
    size_t n = 0;
    Args a;

    if (languages) {
        cmd_languages(srv, rx, NULL);
        return;
    }
    while (*line == ' ' || *line == '\t') line++;
    while (*line && *line != ' ' && *line != '\t' && n + 1 < sizeof(name)) name[n++] = *line++;
    name[n] = '\0';
    while (*line == ' ' || *line == '\t') line++;
    for (c = commands; c->name; c++) {
        if (scout_path_equal(c->name, name)) break;
    }
    if (!c->name) {
        scout_fail(srv, rx, -1, SCOUT_E_ARGS, "unknown command");
        return;
    }
    if (!srv->enabled && c->run != cmd_status && c->run != cmd_quit) {
        scout_fail(srv, rx, -1, SCOUT_E_PARSER, "Scout is disabled");
        return;
    }
    if (srv->scanning > 0 && c->parsers) {
        defer(srv, rx, 0);
        return;
    }
    if (!c->tmpl[0]) {
        c->run(srv, rx, NULL);
        return;
    }
    if (!read_args(c->tmpl, line, &a)) {
        free_args(&a);
        scout_fail(srv, rx, -1, SCOUT_E_ARGS, c->tmpl);
        return;
    }
    c->run(srv, rx, a.v);
    free_args(&a);
}

void scout_rexx_handle(ScoutServer *srv)
{
    struct RexxMsg *rx;
    if (!srv->rexx_port) return;
    while ((rx = (struct RexxMsg *)GetMsg(srv->rexx_port)) != NULL) {
        /* not IsRexxMsg(): it refuses messages a program made with
         * CreateRexxMsg() (seen with TinyDev's), only rx's pass */
        if (((ULONG)rx->rm_Action & RXCODEMASK) != RXCOMM || !rx->rm_Args[0]) {
            /* reported in STATUS, as a client gets no text for it */
            scout_store_add_problem(&srv->store, "a message was refused: no ARexx command", NULL);
            rx->rm_Result1 = RC_ERROR;
            rx->rm_Result2 = SCOUT_E_ARGS;
            ReplyMsg((struct Message *)rx);
            continue;
        }
        scout_rexx_command(srv, rx, 0);
    }
}

void scout_rexx_cleanup(ScoutServer *srv)
{
    struct RexxMsg *rx;
    if (!srv->rexx_port) return;
    Forbid();
    RemPort(srv->rexx_port);
    Permit();
    while ((rx = (struct RexxMsg *)GetMsg(srv->rexx_port)) != NULL) {
        rx->rm_Result1 = RC_ERROR;
        rx->rm_Result2 = SCOUT_E_PARSER;
        ReplyMsg((struct Message *)rx);
    }
    DeleteMsgPort(srv->rexx_port);
    srv->rexx_port = NULL;
}
