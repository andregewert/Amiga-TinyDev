/**
 * @file scout_jobs.c
 * @brief Scout's parser runs: the queue of jobs, the process that runs a
 *        parser, the timeout, and the handling of the answers.
 *
 * One job runs at a time. Its requests go to a temporary file that is the
 * parser's input, the answers to another that is its output (both in
 * TEMPDIR, named after Scout's process and a counter, so nothing running
 * in parallel shares a name). A small process of its own runs the parser
 * with a synchronous SystemTags() and sends the job back to Scout's port
 * when the parser has ended (under Forbid(), so the process is gone before
 * Scout can quit), so Scout keeps answering ARexx meanwhile.
 *
 * Clients' jobs run before indexing jobs. When a parser takes longer than
 * TIMEOUT seconds per request, the waiting clients get an error and the
 * parser gets Ctrl-C: the Shell running it is the CLI whose output is the
 * job's output file (untested assumption, as for TinyDev's Stop). If it
 * has not ended after another TIMEOUT, it is taken for hung: its parser is
 * disabled and the job is left behind (Scout waits for it when quitting),
 * so the next job can run.
 */
#include "scout_server.h"
#include "scout_protocol.h"
#include "scout_rpc.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Copy a string with malloc().
 * @param s The string (may be NULL).
 * @return The copy, or NULL (also for NULL).
 */
static char *dup_str(const char *s)
{
    size_t n;
    char *copy;
    if (!s) return NULL;
    n = strlen(s) + 1;
    copy = (char *)malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

void scout_temp_name(ScoutServer *srv, const char *suffix, char *out, size_t size)
{
    char name[48];
    snprintf(name, sizeof(name), "Scout-%08lX-%lu%s", (unsigned long)FindTask(NULL),
             ++srv->temp_count, suffix);
    strncpy(out, srv->tempdir, size - 1);
    out[size - 1] = '\0';
    if (!AddPart((STRPTR)out, (STRPTR)name, (ULONG)size)) out[0] = '\0';
}

int scout_file_stamp(const char *path, ScoutStamp *stamp)
{
    struct FileInfoBlock *fib;
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    int ok = 0;
    if (!lock) return 0;
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (fib && Examine(lock, fib) && fib->fib_DirEntryType < 0) {
        stamp->days = fib->fib_Date.ds_Days;
        stamp->minute = fib->fib_Date.ds_Minute;
        stamp->tick = fib->fib_Date.ds_Tick;
        stamp->size = fib->fib_Size;
        ok = 1;
    }
    if (fib) FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return ok;
}

/* ---- Jobs ---- */

/**
 * @brief Free the strings of an item.
 * @param it The item.
 */
static void free_item(ScoutItem *it)
{
    free(it->path);
    free(it->text);
    free(it->language);
    it->path = it->text = it->language = NULL;
}

/**
 * @brief Free a job (its messages must be answered).
 * @param job The job.
 */
static void free_job(ScoutJob *job)
{
    int i;
    for (i = 0; i < job->count; i++) free_item(&job->items[i]);
    free(job->program);
    free(job->extension);
    free(job);
}

/**
 * @brief Make an empty job.
 * @param type What it does.
 * @param program The parser.
 * @return The job, or NULL if out of memory.
 */
static ScoutJob *new_job(ScoutJobType type, const char *program)
{
    ScoutJob *job = (ScoutJob *)calloc(1, sizeof(ScoutJob));
    if (!job) return NULL;
    job->type = type;
    job->parser = -1;
    job->program = dup_str(program);
    if (!job->program) {
        free(job);
        return NULL;
    }
    return job;
}

/**
 * @brief Append a job to the queue.
 * @param srv The server.
 * @param job The job.
 */
static void enqueue(ScoutServer *srv, ScoutJob *job)
{
    ScoutJob **p = &srv->queue;
    while (*p) p = &(*p)->next;
    job->next = NULL;
    *p = job;
}

/**
 * @brief Count down the scan; when it is done, carry out the commands that
 *        waited for it.
 * @param srv The server.
 */
static void scan_done_one(ScoutServer *srv)
{
    ScoutDeferred *d;
    if (srv->scanning > 0) srv->scanning--;
    if (srv->scanning > 0) return;
    d = srv->deferred;
    srv->deferred = NULL;
    while (d) {
        ScoutDeferred *next = d->next;
        scout_rexx_command(srv, d->rx, d->languages);
        free(d);
        d = next;
    }
}

/**
 * @brief Answer the waiting messages of a job with an error and free it.
 * @param srv The server.
 * @param job The job.
 * @param code The error number.
 * @param text The error's text.
 */
static void fail_job(ScoutServer *srv, ScoutJob *job, long code, const char *text)
{
    int i;
    for (i = 0; i < job->count; i++) {
        if (job->items[i].rx) scout_fail(srv, job->items[i].rx, job->session, code, text);
    }
    if (job->rx) scout_fail(srv, job->rx, -1, code, text);
    if (job->scan) scan_done_one(srv);
    free_job(job);
}

/**
 * @brief Copy an item.
 * @param[out] dst The copy.
 * @param src The item (its message is taken over).
 * @return 1 on success, 0 if out of memory (nothing to free then).
 */
static int copy_item(ScoutItem *dst, const ScoutItem *src)
{
    *dst = *src;
    dst->path = dup_str(src->path);
    dst->text = dup_str(src->text);
    dst->language = dup_str(src->language);
    if (!dst->path || !dst->language || (src->text && !dst->text)) {
        free_item(dst);
        return 0;
    }
    return 1;
}

/**
 * @brief Remove an item from a queued job; a job left empty is removed
 *        from the queue and freed.
 * @param srv The server.
 * @param job The job.
 * @param i The item.
 * @return Non-zero if the job was freed.
 */
static int remove_item(ScoutServer *srv, ScoutJob *job, int i)
{
    free_item(&job->items[i]);
    memmove(&job->items[i], &job->items[i + 1], (size_t)(job->count - i - 1) * sizeof(ScoutItem));
    job->count--;
    if (job->count > 0) return 0;
    {
        ScoutJob **p = &srv->queue;
        while (*p && *p != job) p = &(*p)->next;
        if (*p) *p = job->next;
    }
    free_job(job);
    return 1;
}

int scout_jobs_analyze(ScoutServer *srv, long session, const ScoutItem *item, int background)
{
    const ScoutLanguage *lang = scout_store_language(&srv->store, item->language);
    ScoutJob *job, *next;
    const char *program;
    int i;

    if (!lang) return 0;
    program = srv->store.parsers[lang->parser].path;

    for (job = srv->queue; job; job = next) {
        next = job->next;
        if (job->type != SCOUT_JOB_ANALYZE || job->session != session) continue;
        for (i = 0; i < job->count; i++) {
            ScoutItem *old = &job->items[i];
            if (!scout_path_equal(old->path, item->path)) continue;
            if (background) return 1;   /* queued already */
            if (old->rx && !old->requery && item->rx && !item->requery) {
                scout_fail(srv, old->rx, session, SCOUT_E_SUPERSEDED, "superseded by a newer request");
                old->rx = NULL;
            }
            if (old->rx) continue;      /* a query waiting for it */
            if (remove_item(srv, job, i)) break;
            i--;
        }
    }

    if (background) {
        for (job = srv->queue; job; job = job->next) {
            if (job->type == SCOUT_JOB_ANALYZE && job->background && job->session == session &&
                job->count < SCOUT_BATCH && strcmp(job->program, program) == 0) {
                if (!copy_item(&job->items[job->count], item)) return 0;
                job->count++;
                return 1;
            }
        }
    }
    job = new_job(SCOUT_JOB_ANALYZE, program);
    if (!job) return 0;
    if (!copy_item(&job->items[0], item)) {
        free_job(job);
        return 0;
    }
    job->count = 1;
    job->background = background;
    job->session = session;
    job->parser = lang->parser;
    enqueue(srv, job);
    return 1;
}

void scout_jobs_drop_session(ScoutServer *srv, long session)
{
    ScoutJob **p = &srv->queue;
    while (*p) {
        ScoutJob *job = *p;
        if (job->type == SCOUT_JOB_ANALYZE && job->session == session) {
            *p = job->next;
            fail_job(srv, job, SCOUT_E_SESSION, "the session was closed");
        } else {
            p = &job->next;
        }
    }
}

long scout_jobs_count(const ScoutServer *srv)
{
    const ScoutJob *job;
    long n = srv->job ? 1 : 0;
    for (job = srv->queue; job; job = job->next) n++;
    return n;
}

int scout_jobs_register(ScoutServer *srv, const char *program, const char *extension,
                        struct RexxMsg *rx)
{
    ScoutJob *job = new_job(SCOUT_JOB_DESCRIBE, program);
    if (!job) return 0;
    job->extension = dup_str(extension);
    if (!job->extension) {
        free_job(job);
        return 0;
    }
    job->rx = rx;
    enqueue(srv, job);
    return 1;
}

void scout_jobs_scan(ScoutServer *srv)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    srv->scanning++;    /* holds the deferred commands until the end */
    lock = Lock((STRPTR)srv->parsers, SHARED_LOCK);
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!lock || !fib || !Examine(lock, fib)) {
        scout_store_add_problem(&srv->store, "parser drawer not found: ", srv->parsers);
    } else {
        while (ExNext(lock, fib)) {
            char path[SCOUT_PATH_MAX];
            size_t n = strlen(fib->fib_FileName);
            ScoutJob *job;
            if (fib->fib_DirEntryType >= 0) continue;
            if (n > 5 && scout_path_equal(fib->fib_FileName + n - 5, ".info")) continue;
            if (fib->fib_Protection & FIBF_EXECUTE) continue;   /* the bit is set when NOT executable */
            strncpy(path, srv->parsers, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            if (!AddPart((STRPTR)path, (STRPTR)fib->fib_FileName, sizeof(path))) continue;
            job = new_job(SCOUT_JOB_DESCRIBE, path);
            if (!job) break;
            job->scan = 1;
            srv->scanning++;
            enqueue(srv, job);
        }
    }
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (lock) UnLock(lock);
    scan_done_one(srv);
    scout_jobs_next(srv);
}

/* ---- Running ---- */

/**
 * @brief The process of a job: runs the parser with the job's files as
 *        input and output, then sends the job back.
 *
 * Its argument is the job's address in hexadecimal. The message goes out
 * under Forbid(), so the process is gone before Scout can quit.
 */
static void job_process(void)
{
    ScoutJob *job = (ScoutJob *)strtoul((const char *)GetArgStr(), NULL, 16);
    job->rc = SystemTags((STRPTR)job->command, SYS_Input, (ULONG)job->in, SYS_Output, (ULONG)job->out,
                         SYS_UserShell, FALSE, TAG_DONE);
    Forbid();
    PutMsg(g_scout.job_port, &job->msg);
}

/**
 * @brief Write the requests of a job into its input file.
 * @param job The job.
 * @return 1 on success, 0 if the file could not be written.
 */
static int write_requests(ScoutJob *job)
{
    JsonWriter w, p;
    BPTR fh;
    int i, ok;
    json_writer_init_alloc(&w, 512);
    if (job->type == SCOUT_JOB_DESCRIBE) {
        scout_rpc_format_request(&w, 1, "scout/describe", NULL);
        json_write_raw(&w, "\n");
    }
    for (i = 0; i < job->count; i++) {
        const ScoutItem *it = &job->items[i];
        json_writer_init_alloc(&p, 256);
        json_write_raw(&p, "{\"path\":");
        json_write_string(&p, it->path);
        if (it->text) {
            json_write_raw(&p, ",\"text\":");
            json_write_string(&p, it->text);
        }
        json_write_raw(&p, ",\"language\":");
        json_write_string(&p, it->language);
        json_write_raw(&p, "}");
        scout_rpc_format_request(&w, i + 1, "scout/analyze", json_writer_get(&p));
        json_write_raw(&w, "\n");
        json_writer_free(&p);
    }
    ok = !w.error;
    if (ok && (fh = Open((STRPTR)job->in_name, MODE_NEWFILE)) != 0) {
        LONG len = (LONG)json_writer_length(&w);
        ok = Write(fh, (APTR)json_writer_get(&w), len) == len;
        Close(fh);
    } else {
        ok = 0;
    }
    json_writer_free(&w);
    return ok;
}

/**
 * @brief Start the timeout timer.
 * @param srv The server.
 * @param seconds The time.
 */
static void start_timer(ScoutServer *srv, long seconds)
{
    if (!srv->timer_open || srv->timer_pending) return;
    srv->timer->tr_node.io_Command = TR_ADDREQUEST;
    srv->timer->tr_time.tv_secs = (ULONG)seconds;
    srv->timer->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)srv->timer);
    srv->timer_pending = 1;
}

/**
 * @brief Stop the timeout timer.
 * @param srv The server.
 */
static void stop_timer(ScoutServer *srv)
{
    if (!srv->timer_pending) return;
    AbortIO((struct IORequest *)srv->timer);
    WaitIO((struct IORequest *)srv->timer);
    SetSignal(0, 1UL << srv->timer_port->mp_SigBit);
    srv->timer_pending = 0;
}

/**
 * @brief Close a job's files and delete them.
 * @param job The job.
 */
static void close_files(ScoutJob *job)
{
    if (job->in) Close(job->in);
    if (job->out) Close(job->out);
    job->in = job->out = 0;
    if (job->in_name[0]) DeleteFile((STRPTR)job->in_name);
    if (job->out_name[0]) DeleteFile((STRPTR)job->out_name);
}

void scout_jobs_next(ScoutServer *srv)
{
    while (!srv->job && srv->queue) {
        ScoutJob *job = NULL, **p, **pick = NULL;
        struct Process *proc;
        for (p = &srv->queue; *p; p = &(*p)->next) {
            if (!(*p)->background) {
                pick = p;
                break;
            }
        }
        if (!pick) pick = &srv->queue;
        job = *pick;
        *pick = job->next;
        job->next = NULL;

        if (job->type == SCOUT_JOB_ANALYZE && job->parser < srv->store.parser_count &&
            srv->store.parsers[job->parser].disabled &&
            strcmp(srv->store.parsers[job->parser].path, job->program) == 0) {
            fail_job(srv, job, SCOUT_E_PARSER, "the parser is disabled (it hung)");
            continue;
        }
        scout_temp_name(srv, ".in", job->in_name, sizeof(job->in_name));
        scout_temp_name(srv, ".out", job->out_name, sizeof(job->out_name));
        if (!job->in_name[0] || !job->out_name[0] || !write_requests(job) ||
            (job->in = Open((STRPTR)job->in_name, MODE_OLDFILE)) == 0 ||
            (job->out = Open((STRPTR)job->out_name, MODE_NEWFILE)) == 0) {
            close_files(job);
            fail_job(srv, job, SCOUT_E_PARSER, "cannot write the temporary files");
            continue;
        }
        snprintf(job->command, sizeof(job->command), "\"%s\"", job->program);
        snprintf(job->arg, sizeof(job->arg), "%lx\n", (unsigned long)job);
        job->msg.mn_Node.ln_Type = NT_MESSAGE;
        job->msg.mn_Length = sizeof(struct Message);
        job->msg.mn_ReplyPort = NULL;
        proc = CreateNewProcTags(NP_Entry, (ULONG)job_process, NP_Name, (ULONG)"TinyDev-Scout parser",
                                 NP_StackSize, 8192, NP_Arguments, (ULONG)job->arg, TAG_DONE);
        if (!proc) {
            close_files(job);
            fail_job(srv, job, SCOUT_E_PARSER, "cannot start the parser");
            continue;
        }
        srv->job = job;
        start_timer(srv, srv->timeout * (job->count > 0 ? job->count : 1));
    }
}

/**
 * @brief Read a whole file.
 * @param path The file.
 * @return Its contents, terminated (free() it), or NULL.
 */
static char *read_file(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf = NULL;
    LONG size;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size >= 0 && (buf = (char *)malloc((size_t)size + 1)) != NULL) {
        LONG got = Read(fh, buf, size);
        buf[got > 0 ? got : 0] = '\0';
    }
    Close(fh);
    return buf;
}

/**
 * @brief Find the answer of a request among the answers.
 * @param answers The answers, one per line.
 * @param id The request's ID.
 * @param[out] len Length of the answer's line.
 * @return Its line, or NULL.
 */
static const char *find_answer(const char *answers, long id, size_t *len)
{
    const char *line = answers;
    while (*line) {
        const char *end = strchr(line, '\n');
        const char *v;
        size_t n, vl;
        n = end ? (size_t)(end - line) : strlen(line);
        if (scout_json_member(line, "id", &v, &vl) && strtol(v, NULL, 10) == id) {
            *len = n;
            return line;
        }
        if (!end) break;
        line = end + 1;
    }
    return NULL;
}

/**
 * @brief Take the result or the error out of an answer.
 * @param line The answer (its line, not terminated).
 * @param len Its length.
 * @param[out] result The result's text (allocated), NULL on an error.
 * @param[out] code The error number (SCOUT_E_) on an error.
 * @param[out] message The error's text.
 * @param size Size of @p message.
 */
static void read_answer(const char *line, size_t len, char **result, long *code, char *message, size_t size)
{
    char *copy = (char *)malloc(len + 1);
    const char *v;
    size_t vl;
    *result = NULL;
    *code = SCOUT_E_PARSER;
    snprintf(message, size, "invalid answer of the parser");
    if (!copy) {
        *code = SCOUT_E_MEMORY;
        snprintf(message, size, "out of memory");
        return;
    }
    memcpy(copy, line, len);
    copy[len] = '\0';
    if (scout_json_member(copy, "result", &v, &vl)) {
        *result = (char *)malloc(vl + 1);
        if (*result) {
            memcpy(*result, v, vl);
            (*result)[vl] = '\0';
        } else {
            *code = SCOUT_E_MEMORY;
            snprintf(message, size, "out of memory");
        }
    } else {
        ScoutRpcMessage m;
        if (scout_rpc_parse(copy, &m) && m.has_error) {
            *code = m.error_code == SCOUT_ERR_FILE ? SCOUT_E_FILE :
                    m.error_code == SCOUT_ERR_LANGUAGE ? SCOUT_E_NO_PARSER :
                    m.error_code == SCOUT_ERR_MEMORY ? SCOUT_E_MEMORY : SCOUT_E_PARSER;
            snprintf(message, size, "%s", m.error_msg ? m.error_msg : "the parser failed");
        }
        scout_rpc_free(&m);
    }
    free(copy);
}

/**
 * @brief Handle the answers of a describe job.
 * @param srv The server.
 * @param job The job.
 * @param answers The answers.
 */
static void describe_done(ScoutServer *srv, ScoutJob *job, const char *answers)
{
    char message[160], *result = NULL;
    const char *line;
    size_t len;
    long code = SCOUT_E_PARSER;
    int rc = 0;
    snprintf(message, sizeof(message), "the parser gave no description");
    line = answers ? find_answer(answers, 1, &len) : NULL;
    if (line) read_answer(line, len, &result, &code, message, sizeof(message));
    if (result) {
        rc = scout_store_add_parser(&srv->store, job->program, result, job->extension);
        if (rc == 0) snprintf(message, sizeof(message), "the parser's description is not usable");
        if (rc < 0) {
            code = SCOUT_E_MEMORY;
            snprintf(message, sizeof(message), "out of memory");
        }
        free(result);
    } else if (!job->rx) {
        scout_store_add_problem(&srv->store, "no description: ", job->program);
    }
    if (job->rx) {
        if (rc > 0) {
            scout_reply(job->rx, "{}");
        } else {
            scout_fail(srv, job->rx, -1, code, message);
        }
        job->rx = NULL;
    }
    if (job->scan) {
        job->scan = 0;
        scan_done_one(srv);
    }
}

/**
 * @brief Handle the answers of an analyze job.
 * @param srv The server.
 * @param job The job.
 * @param answers The answers (NULL if none could be read).
 */
static void analyze_done(ScoutServer *srv, ScoutJob *job, const char *answers)
{
    int i;
    for (i = 0; i < job->count; i++) {
        ScoutItem *it = &job->items[i];
        ScoutSession *s = scout_session_find(&srv->store, job->session);
        char message[160], *result = NULL;
        const char *line;
        size_t len;
        long code = SCOUT_E_PARSER;
        snprintf(message, sizeof(message), "the parser gave no answer (return code %ld)", (long)job->rc);
        line = answers ? find_answer(answers, i + 1, &len) : NULL;
        if (line) read_answer(line, len, &result, &code, message, sizeof(message));
        if (!s) {
            free(result);
            if (it->rx) scout_fail(srv, it->rx, -1, SCOUT_E_SESSION, "the session was closed");
            continue;
        }
        if (result) {
            ScoutFile *f = scout_file_get(s, it->path);
            int rc = 1;
            /* indexing does not replace the result of a client's snapshot */
            if (f && !(job->background && f->valid && f->version >= 0)) {
                rc = scout_file_set_result(f, it->language, it->version, &it->stamp, result);
                if (rc > 0 && it->text) f->snapshot = dup_str(it->text);
            }
            free(result);
            if (!f || rc < 0) {
                code = SCOUT_E_MEMORY;
                snprintf(message, sizeof(message), "out of memory");
            } else if (rc == 0) {
                snprintf(message, sizeof(message), "invalid result of the parser");
            } else {
                if (it->in_project) f->in_project = 1;
                if (job->background) s->changes++;
                if (s->id != 0) scout_walk_references(srv, s, f);
                if (it->rx) {
                    struct RexxMsg *rx = it->rx;
                    it->rx = NULL;
                    if (it->requery) {
                        scout_rexx_command(srv, rx, 0);
                    } else {
                        JsonWriter w;
                        json_writer_init_alloc(&w, 256);
                        scout_write_analyze(&w, f);
                        scout_reply(rx, json_writer_get(&w));
                        json_writer_free(&w);
                    }
                }
                continue;
            }
        }
        if (it->rx) {
            scout_fail(srv, it->rx, job->session, code, message);
            it->rx = NULL;
        }
    }
}

/**
 * @brief Send Ctrl-C to the Shell running a job's parser.
 * @param job The job.
 */
static void break_job(ScoutJob *job)
{
    ULONG n, max;
    Forbid();
    max = MaxCli();
    for (n = 1; n <= max; ++n) {
        struct Process *p = FindCliProc(n);
        struct CommandLineInterface *cli;
        if (!p || (cli = (struct CommandLineInterface *)BADDR(p->pr_CLI)) == NULL) continue;
        if (cli->cli_StandardOutput == job->out || cli->cli_CurrentOutput == job->out) {
            Signal(&p->pr_Task, SIGBREAKF_CTRL_C);
        }
    }
    Permit();
}

/**
 * @brief Handle the timeout of the running job: first Ctrl-C (the waiting
 *        clients get an error), then, if it still runs, leave it behind.
 * @param srv The server.
 */
static void job_timeout(ScoutServer *srv)
{
    ScoutJob *job = srv->job;
    int i;
    if (!job) return;
    if (!job->timed_out) {
        job->timed_out = 1;
        break_job(job);
        for (i = 0; i < job->count; i++) {
            if (job->items[i].rx) {
                scout_fail(srv, job->items[i].rx, job->session, SCOUT_E_PARSER, "the parser took too long");
                job->items[i].rx = NULL;
            }
        }
        if (job->rx) {
            scout_fail(srv, job->rx, -1, SCOUT_E_PARSER, "the parser took too long");
            job->rx = NULL;
        }
        if (job->scan) {
            job->scan = 0;
            scout_store_add_problem(&srv->store, "no description in time: ", job->program);
            scan_done_one(srv);
        }
        start_timer(srv, srv->timeout);
        return;
    }
    if (job->parser >= 0 && job->parser < srv->store.parser_count &&
        strcmp(srv->store.parsers[job->parser].path, job->program) == 0) {
        srv->store.parsers[job->parser].disabled = 1;
    }
    scout_store_add_problem(&srv->store, "parser hangs (disabled until RESCAN): ", job->program);
    job->next = srv->abandoned;
    srv->abandoned = job;
    srv->job = NULL;
    scout_jobs_next(srv);
}

/**
 * @brief Handle a job that came back.
 * @param srv The server.
 * @param job The job.
 */
static void job_done(ScoutServer *srv, ScoutJob *job)
{
    ScoutJob **p;
    char *answers;
    for (p = &srv->abandoned; *p; p = &(*p)->next) {
        if (*p == job) {
            *p = job->next;
            close_files(job);
            free_job(job);
            return;
        }
    }
    if (job != srv->job) return;
    srv->job = NULL;
    stop_timer(srv);
    if (job->in) Close(job->in);
    if (job->out) Close(job->out);
    job->in = job->out = 0;
    answers = read_file(job->out_name);
    close_files(job);
    if (job->type == SCOUT_JOB_DESCRIBE) {
        describe_done(srv, job, answers);
    } else {
        analyze_done(srv, job, answers);
    }
    free(answers);
    free_job(job);
    scout_jobs_next(srv);
}

void scout_jobs_handle(ScoutServer *srv, ULONG signals)
{
    struct Message *msg;
    if (srv->timer_port && (signals & (1UL << srv->timer_port->mp_SigBit))) {
        if (srv->timer_pending && CheckIO((struct IORequest *)srv->timer)) {
            WaitIO((struct IORequest *)srv->timer);
            srv->timer_pending = 0;
            job_timeout(srv);
        }
    }
    if (srv->job_port) {
        while ((msg = GetMsg(srv->job_port)) != NULL) job_done(srv, (ScoutJob *)msg);
    }
}

ULONG scout_jobs_sigmask(const ScoutServer *srv)
{
    ULONG mask = 0;
    if (srv->job_port) mask |= 1UL << srv->job_port->mp_SigBit;
    if (srv->timer_port) mask |= 1UL << srv->timer_port->mp_SigBit;
    return mask;
}

int scout_jobs_init(ScoutServer *srv)
{
    if ((srv->job_port = CreateMsgPort()) == NULL) return 0;
    if ((srv->timer_port = CreateMsgPort()) == NULL) return 0;
    srv->timer = (struct timerequest *)CreateIORequest(srv->timer_port, sizeof(struct timerequest));
    if (!srv->timer) return 0;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)srv->timer, 0) != 0) return 0;
    srv->timer_open = 1;
    return 1;
}

void scout_jobs_cleanup(ScoutServer *srv)
{
    ScoutDeferred *d = srv->deferred;
    srv->deferred = NULL;
    while (d) {
        ScoutDeferred *next = d->next;
        scout_fail(srv, d->rx, -1, SCOUT_E_PARSER, "Scout is quitting");
        free(d);
        d = next;
    }
    while (srv->queue) {
        ScoutJob *job = srv->queue;
        srv->queue = job->next;
        job->scan = 0;
        fail_job(srv, job, SCOUT_E_PARSER, "Scout is quitting");
    }
    if (srv->job) {
        job_timeout(srv);       /* answers the clients, Ctrl-C */
        if (srv->job) {
            srv->job->next = srv->abandoned;
            srv->abandoned = srv->job;
            srv->job = NULL;
        }
    }
    stop_timer(srv);
    /* the processes run code of Scout: wait until all are back */
    while (srv->abandoned) {
        ScoutJob *job;
        struct Message *msg;
        for (job = srv->abandoned; job; job = job->next) break_job(job);
        WaitPort(srv->job_port);
        while ((msg = GetMsg(srv->job_port)) != NULL) job_done(srv, (ScoutJob *)msg);
    }
    if (srv->timer_open) CloseDevice((struct IORequest *)srv->timer);
    if (srv->timer) DeleteIORequest((struct IORequest *)srv->timer);
    if (srv->timer_port) DeleteMsgPort(srv->timer_port);
    if (srv->job_port) DeleteMsgPort(srv->job_port);
    srv->timer = NULL;
    srv->timer_port = srv->job_port = NULL;
    srv->timer_open = 0;
}
