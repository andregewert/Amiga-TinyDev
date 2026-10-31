/**
 * @file sprout_main.c
 * @brief Sprout, the Shell command that creates a project from a template:
 *        arguments, finding the template, copying its files with the
 *        placeholders replaced, and removing what it made on an error.
 *
 * The format and the command line are described in
 * src/sprout/docs/Sprout.md; the portable part is sprout_tmpl.c. This
 * version reads templates that are drawers; archives (.lha) follow.
 *
 * Messages go to the error stream of the Shell (pr_CES, else Output()),
 * in English, prefixed "Sprout: ". The created drawers and files are kept
 * in a list (newest first), so an error or Ctrl-C deletes them again in
 * reverse order; files that existed before (FORCE) stay as written.
 */
#include "sprout_tmpl.h"

#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdlib.h>
#include <string.h>

/** libnix swaps to a stack of this size at the start (-Wl,-u,___stkinit). */
unsigned long __stack = 16384;

/** The version string for the Version command. */
static const char version[] __attribute__((used)) = "$VER: Sprout 1.0 (31.10.2026)";

/** The size of path buffers. */
#define PATHMAX 512
/** The deepest drawer level copied below files/. */
#define MAXDEPTH 32
/** The buffer for copying files too large to be read whole. */
#define COPYBUF 32768L
/** The largest template.json or VALUES file read. */
#define JSONMAX (256L * 1024L)

/** The ReadArgs template. */
#define ARG_TEMPLATE "TEMPLATE/A,TO/A,SET/M,VALUES/K,DIR/K,FORCE/S,QUIET/S"

/** The arguments, in the order of #ARG_TEMPLATE. */
enum {
    ARG_TMPL,       /**< the template (identifier or path) */
    ARG_TO,         /**< the project drawer */
    ARG_SET,        /**< values as KEY=value */
    ARG_VALUES,     /**< a JSON file with values */
    ARG_DIR,        /**< the templates drawer */
    ARG_FORCE,      /**< write into an existing drawer */
    ARG_QUIET,      /**< no progress lines */
    ARG_COUNT       /**< number of arguments */
};

/** Something Sprout created (a node of a list, newest first). */
typedef struct Created {
    struct Created *next;   /**< the one created before it */
    char path[1];           /**< its path (allocated with the node) */
} Created;

/** The state of a run. */
static struct {
    BPTR err;                       /**< where messages go */
    int warnings;                   /**< warnings so far */
    int quiet;                      /**< QUIET */
    int force;                      /**< FORCE */
    Created *created;               /**< what was created, newest first */
    const SproutValues *values;     /**< the values of the placeholders */
    char **patterns;                /**< the parsed "verbatim" patterns */
    int npatterns;                  /**< their number */
    char to[PATHMAX];               /**< the project drawer */
} sp;

/**
 * @brief Write a message of up to three parts to the error stream.
 * @param a First part.
 * @param b Second part (may be NULL).
 * @param c Third part (may be NULL).
 */
static void msg(const char *a, const char *b, const char *c)
{
    FPuts(sp.err, (STRPTR)"Sprout: ");
    FPuts(sp.err, (STRPTR)a);
    if (b != NULL) FPuts(sp.err, (STRPTR)b);
    if (c != NULL) FPuts(sp.err, (STRPTR)c);
    FPuts(sp.err, (STRPTR)"\n");
}

/**
 * @brief Report a failed DOS call with the reason from IoErr().
 * @param what What failed, e.g. "cannot read ".
 * @param path The file or drawer.
 */
static void dos_error(const char *what, const char *path)
{
    char reason[80];
    LONG code = IoErr();
    reason[0] = '\0';
    if (code != 0) {
        reason[0] = ':';
        reason[1] = ' ';
        Fault(code, NULL, (STRPTR)reason + 2, (LONG)sizeof(reason) - 2);
    }
    msg(what, path, reason);
}

/**
 * @brief Report callback of the core: prints the message, counts warnings.
 * @param user Unused.
 * @param error Non-zero for an error.
 * @param text The message.
 */
static void report(void *user, int error, const char *text)
{
    (void)user;
    if (error) {
        msg(text, NULL, NULL);
    } else {
        ++sp.warnings;
        msg("warning: ", text, NULL);
    }
}

/**
 * @brief Write a progress line (unless QUIET).
 * @param path What was created.
 */
static void progress(const char *path)
{
    if (sp.quiet) return;
    FPuts(Output(), (STRPTR)"  ");
    FPuts(Output(), (STRPTR)path);
    FPuts(Output(), (STRPTR)"\n");
}

/**
 * @brief Note that something was created, so it is removed on an error.
 * @param path Its path.
 * @return Non-zero on success, zero if out of memory (reported).
 */
static int note_created(const char *path)
{
    Created *c = (Created *)malloc(sizeof(Created) + strlen(path));
    if (c == NULL) {
        msg("out of memory", NULL, NULL);
        return 0;
    }
    strcpy(c->path, path);
    c->next = sp.created;
    sp.created = c;
    return 1;
}

/**
 * @brief Compare two names without case, as AmigaDOS does (ASCII and
 *        ISO-8859-1 letters).
 * @param a First name.
 * @param b Second name.
 * @return Non-zero if they are the same.
 */
static int same_name(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if ((x >= 'a' && x <= 'z') || (x >= 0xE0 && x <= 0xFE && x != 0xF7)) x = (unsigned char)(x - 0x20);
        if ((y >= 'a' && y <= 'z') || (y >= 0xE0 && y <= 0xFE && y != 0xF7)) y = (unsigned char)(y - 0x20);
        if (x != y) return 0;
        if (x == '\0') return 1;
    }
}

/**
 * @brief Tell whether a path is one Sprout created in this run.
 * @param path The path.
 * @return Non-zero if it is in the list.
 */
static int was_created(const char *path)
{
    const Created *c;
    for (c = sp.created; c != NULL; c = c->next)
        if (same_name(c->path, path)) return 1;
    return 0;
}

/**
 * @brief Delete everything created, newest first, and empty the list.
 * @param remove Non-zero to delete the files and drawers, zero to keep
 *        them (only the list is freed).
 */
static void rollback(int remove)
{
    while (sp.created != NULL) {
        Created *c = sp.created;
        sp.created = c->next;
        if (remove && !DeleteFile((STRPTR)c->path)) dos_error("cannot remove ", c->path);
        free(c);
    }
}

/**
 * @brief Tell whether Ctrl-C was pressed (the signal is cleared).
 * @return Non-zero if it was.
 */
static int break_pressed(void)
{
    if ((SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) == 0) return 0;
    msg("***Break", NULL, NULL);
    return 1;
}

/**
 * @brief Join a drawer and a name.
 * @param[out] out The result (#PATHMAX bytes).
 * @param dir The drawer.
 * @param name The name (may contain '/').
 * @return Non-zero on success, zero if too long (reported).
 */
static int join(char *out, const char *dir, const char *name)
{
    strncpy(out, dir, PATHMAX - 1);
    out[PATHMAX - 1] = '\0';
    if (!AddPart((STRPTR)out, (STRPTR)name, PATHMAX)) {
        msg("path too long: ", name, NULL);
        return 0;
    }
    return 1;
}

/**
 * @brief Read a file whole.
 * @param path The file.
 * @param max The largest size accepted.
 * @param[out] data The contents, NUL-terminated (free() them).
 * @param[out] len Their length.
 * @return Non-zero on success, zero on an error (reported).
 */
static int read_file(const char *path, LONG max, char **data, LONG *len)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG size;
    *data = NULL;
    *len = 0;
    if (fh == 0) {
        dos_error("cannot open ", path);
        return 0;
    }
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size < 0 || size > max) {
        Close(fh);
        msg("file too large: ", path, NULL);
        return 0;
    }
    if ((*data = (char *)malloc((size_t)size + 1)) == NULL) {
        Close(fh);
        msg("out of memory", NULL, NULL);
        return 0;
    }
    if (Read(fh, *data, size) != size) {
        dos_error("cannot read ", path);
        Close(fh);
        free(*data);
        *data = NULL;
        return 0;
    }
    Close(fh);
    (*data)[size] = '\0';
    *len = size;
    return 1;
}

/**
 * @brief Write a file whole.
 * @param path The file.
 * @param data The contents.
 * @param len Their length.
 * @return Non-zero on success, zero on an error (reported).
 */
static int write_file(const char *path, const char *data, LONG len)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    if (fh == 0) {
        dos_error("cannot create ", path);
        return 0;
    }
    if (len > 0 && Write(fh, (APTR)data, len) != len) {
        dos_error("cannot write ", path);
        Close(fh);
        return 0;
    }
    if (!Close(fh)) {
        dos_error("cannot write ", path);
        return 0;
    }
    return 1;
}

/**
 * @brief Copy a file that is too large to be read whole, unchanged.
 * @param src The source.
 * @param dst The destination.
 * @return Non-zero on success, zero on an error (reported).
 */
static int stream_copy(const char *src, const char *dst)
{
    BPTR in, out;
    char *buf;
    LONG n;
    int ok = 1;
    if ((buf = (char *)malloc(COPYBUF)) == NULL) {
        msg("out of memory", NULL, NULL);
        return 0;
    }
    if ((in = Open((STRPTR)src, MODE_OLDFILE)) == 0) {
        dos_error("cannot open ", src);
        free(buf);
        return 0;
    }
    if ((out = Open((STRPTR)dst, MODE_NEWFILE)) == 0) {
        dos_error("cannot create ", dst);
        Close(in);
        free(buf);
        return 0;
    }
    while (ok && (n = Read(in, buf, COPYBUF)) != 0) {
        if (n < 0) {
            dos_error("cannot read ", src);
            ok = 0;
        } else if (Write(out, buf, n) != n) {
            dos_error("cannot write ", dst);
            ok = 0;
        } else if (break_pressed()) {
            ok = 0;
        }
    }
    Close(in);
    if (!Close(out) && ok) {
        dos_error("cannot write ", dst);
        ok = 0;
    }
    free(buf);
    return ok;
}

/**
 * @brief Tell whether a file of the template is copied unchanged because
 *        it matches a "verbatim" pattern.
 * @param rel Its path below files/.
 * @return Non-zero if it matches.
 */
static int is_verbatim(const char *rel)
{
    int i;
    for (i = 0; i < sp.npatterns; ++i)
        if (MatchPatternNoCase((STRPTR)sp.patterns[i], (STRPTR)rel)) return 1;
    return 0;
}

/**
 * @brief Parse the "verbatim" patterns of a template.
 * @param t The template.
 * @return Non-zero on success, zero on an error (reported).
 */
static int parse_patterns(const SproutTemplate *t)
{
    int i;
    if (t->nverbatim == 0) return 1;
    if ((sp.patterns = (char **)calloc((size_t)t->nverbatim, sizeof(char *))) == NULL) {
        msg("out of memory", NULL, NULL);
        return 0;
    }
    for (i = 0; i < t->nverbatim; ++i) {
        LONG size = (LONG)strlen(t->verbatim[i]) * 2 + 2;
        if ((sp.patterns[i] = (char *)malloc((size_t)size)) == NULL) {
            msg("out of memory", NULL, NULL);
            return 0;
        }
        ++sp.npatterns;
        if (ParsePatternNoCase((STRPTR)t->verbatim[i], (STRPTR)sp.patterns[i], size) < 0) {
            msg("bad pattern in \"verbatim\": ", t->verbatim[i], NULL);
            return 0;
        }
    }
    return 1;
}

/** @brief Free the parsed patterns. */
static void free_patterns(void)
{
    int i;
    for (i = 0; i < sp.npatterns; ++i) free(sp.patterns[i]);
    free(sp.patterns);
    sp.patterns = NULL;
    sp.npatterns = 0;
}

/**
 * @brief Find out whether a path exists.
 * @param path The path.
 * @param[out] is_dir Set to non-zero if it is a drawer.
 * @return Non-zero if it exists.
 */
static int exists(const char *path, int *is_dir)
{
    struct FileInfoBlock *fib;
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    *is_dir = 0;
    if (lock == 0) return 0;
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib)) *is_dir = fib->fib_DirEntryType > 0;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return 1;
}

/**
 * @brief Make a drawer of the new project (an existing one is used).
 * @param path The drawer.
 * @return Non-zero on success, zero on an error (reported).
 */
static int make_drawer(const char *path)
{
    BPTR lock;
    int is_dir;
    if (exists(path, &is_dir)) {
        if (is_dir) return 1;
        msg("a file is in the way of drawer ", path, NULL);
        return 0;
    }
    if ((lock = CreateDir((STRPTR)path)) == 0) {
        dos_error("cannot create drawer ", path);
        return 0;
    }
    UnLock(lock);
    if (!note_created(path)) return 0;
    progress(path);
    return 1;
}

/**
 * @brief Copy one file of the template, replacing placeholders in a text
 *        file, and take over its protection bits and file note.
 * @param src The source.
 * @param rel Its path below files/ (for the "verbatim" patterns).
 * @param dst The destination.
 * @param fib The source's FileInfoBlock.
 * @return Non-zero on success, zero on an error (reported).
 */
static int copy_file(const char *src, const char *rel, const char *dst, const struct FileInfoBlock *fib)
{
    int is_dir, existed, ok;
    if ((existed = exists(dst, &is_dir)) != 0) {
        if (is_dir) {
            msg("a drawer is in the way of file ", dst, NULL);
            return 0;
        }
        if (was_created(dst)) {
            msg("two files of the template become ", dst, NULL);
            return 0;
        }
        if (!sp.force) {
            msg("file exists: ", dst, NULL);
            return 0;
        }
    }
    /* noted before writing, so a half written file is removed too */
    if (!existed && !note_created(dst)) return 0;
    if (fib->fib_Size > SPROUT_TEXT_MAX) {
        ok = stream_copy(src, dst);
    } else {
        char *data, *expanded = NULL;
        LONG len;
        size_t n = 0;
        if (!read_file(src, SPROUT_TEXT_MAX, &data, &len)) return 0;
        if (!is_verbatim(rel) && sprout_is_text(data, (size_t)len)) {
            if ((expanded = sprout_expand(data, (size_t)len, sp.values, &n)) == NULL) {
                free(data);
                msg("out of memory", NULL, NULL);
                return 0;
            }
        }
        ok = expanded != NULL ? write_file(dst, expanded, (LONG)n) : write_file(dst, data, len);
        free(expanded);
        free(data);
    }
    if (!ok) return 0;
    /* a new file is not archived yet */
    SetProtection((STRPTR)dst, fib->fib_Protection & ~FIBF_ARCHIVE);
    if (fib->fib_Comment[0] != '\0') SetComment((STRPTR)dst, (STRPTR)fib->fib_Comment);
    progress(dst);
    return 1;
}

/** The paths of one drawer level of copy_drawer() (kept off the stack). */
typedef struct Level {
    char src[PATHMAX];      /**< the source of the entry */
    char rel[PATHMAX];      /**< its path below files/ */
    char dst[PATHMAX];      /**< its destination */
} Level;

/**
 * @brief Copy a drawer of the template's files/ into the project, with
 *        its subdrawers.
 * @param src The drawer.
 * @param rel Its path below files/ ("" for files/ itself).
 * @param depth Its level (0 for files/).
 * @return Non-zero on success, zero on an error (reported).
 */
static int copy_drawer(const char *src, const char *rel, int depth)
{
    struct FileInfoBlock *fib;
    Level *lv;
    BPTR lock;
    int ok = 1;

    if (depth > MAXDEPTH) {
        msg("drawers nested too deeply: ", src, NULL);
        return 0;
    }
    if ((lock = Lock((STRPTR)src, SHARED_LOCK)) == 0) {
        dos_error("cannot read ", src);
        return 0;
    }
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    lv = (Level *)malloc(sizeof(Level));
    if (fib == NULL || lv == NULL) {
        msg("out of memory", NULL, NULL);
        ok = 0;
    } else if (!Examine(lock, fib)) {
        dos_error("cannot read ", src);
        ok = 0;
    }
    while (ok && ExNext(lock, fib)) {
        char *dest;
        int bad;
        LONG type = fib->fib_DirEntryType;
        if (type == ST_SOFTLINK || type == ST_LINKDIR || type == ST_LINKFILE) {
            ++sp.warnings;
            msg("warning: link left out: ", fib->fib_FileName, NULL);
            continue;
        }
        if (!join(lv->src, src, fib->fib_FileName) || !join(lv->rel, rel, fib->fib_FileName)) {
            ok = 0;
            break;
        }
        if ((dest = sprout_expand_path(lv->rel, sp.values, &bad)) == NULL) {
            msg(bad ? "name not allowed in the project: " : "out of memory", bad ? lv->rel : NULL, NULL);
            ok = 0;
            break;
        }
        ok = join(lv->dst, sp.to, dest);
        free(dest);
        if (ok) {
            if (type > 0) ok = make_drawer(lv->dst) && copy_drawer(lv->src, lv->rel, depth + 1);
            else ok = copy_file(lv->src, lv->rel, lv->dst, fib);
        }
        if (ok && break_pressed()) ok = 0;
    }
    if (ok && IoErr() != ERROR_NO_MORE_ENTRIES) {
        dos_error("cannot read ", src);
        ok = 0;
    }
    free(lv);
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return ok;
}

/**
 * @brief Find a template: a path, or an identifier in the templates drawer.
 * @param arg The TEMPLATE argument.
 * @param dir The templates drawer (DIR), or NULL for PROGDIR:Templates.
 * @param[out] path The template's drawer (#PATHMAX bytes).
 * @return Non-zero if found, zero otherwise (reported).
 */
static int find_template(const char *arg, const char *dir, char *path)
{
    char lha[PATHMAX];
    int is_dir;
    size_t n;
    if (strchr(arg, ':') != NULL || strchr(arg, '/') != NULL) {
        strncpy(path, arg, PATHMAX - 1);
        path[PATHMAX - 1] = '\0';
    } else if (!join(path, dir != NULL ? dir : "PROGDIR:Templates", arg)) {
        return 0;
    }
    if (exists(path, &is_dir) && is_dir) return 1;
    n = strlen(path);
    strcpy(lha, path);
    if (n + 4 < PATHMAX) strcpy(lha + n, ".lha");
    if ((n > 4 && same_name(path + n - 4, ".lha") && exists(path, &is_dir)) ||
        exists(lha, &is_dir)) {
        msg("template archives (.lha) are not supported yet: ", arg, NULL);
        return 0;
    }
    msg("template not found: ", arg, NULL);
    return 0;
}

/**
 * @brief The name of the project drawer (DRAWER, and PROJECT unless one
 *        is given): the last part of its path.
 * @param to The project drawer.
 * @param[out] name The name (#PATHMAX bytes).
 */
static void drawer_name(const char *to, char *name)
{
    size_t n;
    const char *part;
    strncpy(name, to, PATHMAX - 1);
    name[PATHMAX - 1] = '\0';
    n = strlen(name);
    while (n > 0 && name[n - 1] == '/') name[--n] = '\0';
    part = (const char *)FilePart((STRPTR)name);
    if (*part == '\0') {            /* "Work:" -> "Work" */
        if (n > 0 && name[n - 1] == ':') name[--n] = '\0';
        return;
    }
    memmove(name, part, strlen(part) + 1);
}

/**
 * @brief Make the project drawer, or check an existing one.
 * @return Non-zero on success, zero on an error (reported).
 */
static int make_project_drawer(void)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    int is_dir, empty = 1;
    if (!exists(sp.to, &is_dir)) return make_drawer(sp.to);
    if (!is_dir) {
        msg("not a drawer: ", sp.to, NULL);
        return 0;
    }
    if (sp.force) return 1;
    if ((lock = Lock((STRPTR)sp.to, SHARED_LOCK)) == 0) {
        dos_error("cannot read ", sp.to);
        return 0;
    }
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib) && ExNext(lock, fib)) empty = 0;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    if (!empty) {
        msg("drawer is not empty (use FORCE): ", sp.to, NULL);
        return 0;
    }
    return 1;
}

/**
 * @brief Create the project: check the template, work out the values, make
 *        the drawer and copy the files.
 * @param args The ReadArgs results.
 * @return The return code (RETURN_OK, _WARN, _ERROR or _FAIL).
 */
static LONG run(LONG *args)
{
    SproutTemplate t;
    SproutValues given, values;
    struct DateStamp now;
    char tmpl[PATHMAX], path[PATHMAX], name[PATHMAX];
    char *text;
    LONG len, rc = RETURN_ERROR;
    int r, is_dir;

    strncpy(sp.to, (const char *)args[ARG_TO], PATHMAX - 1);
    sp.force = args[ARG_FORCE] != 0;
    sp.quiet = args[ARG_QUIET] != 0;

    /* the values given */
    sprout_values_init(&given);
    if (args[ARG_VALUES]) {
        if (!read_file((const char *)args[ARG_VALUES], JSONMAX, &text, &len)) return RETURN_FAIL;
        r = sprout_values_parse_json(&given, text);
        free(text);
        if (r <= 0) {
            msg(r < 0 ? "out of memory" : "not a JSON object: ", r < 0 ? NULL : (const char *)args[ARG_VALUES], NULL);
            sprout_values_free(&given);
            return RETURN_FAIL;
        }
    }
    if (args[ARG_SET]) {
        /* ReadArgs() splits KEY=value at the '=' (unless it is all in
         * quotes): a key alone takes the next item as its value; keys
         * may be written in any case */
        STRPTR *set;
        for (set = (STRPTR *)args[ARG_SET]; *set != NULL; ++set) {
            const char *item = (const char *)*set;
            if (strchr(item, '=') == NULL && set[1] != NULL)
                r = sprout_values_set_arg(&given, item, strlen(item), (const char *)*++set);
            else
                r = sprout_values_parse_assign(&given, item);
            if (r == 0) {
                msg("not KEY=value: ", item, NULL);
                sprout_values_free(&given);
                return RETURN_FAIL;
            }
            if (r < 0) {
                msg("out of memory", NULL, NULL);
                sprout_values_free(&given);
                return RETURN_FAIL;
            }
        }
    }

    /* the template */
    if (!find_template((const char *)args[ARG_TMPL], (const char *)args[ARG_DIR], tmpl) ||
        !join(path, tmpl, "template.json") || !read_file(path, JSONMAX, &text, &len)) {
        sprout_values_free(&given);
        return RETURN_ERROR;
    }
    r = sprout_template_parse(&t, text, report, NULL);
    free(text);
    if (r <= 0) {
        if (r < 0) msg("out of memory", NULL, NULL);
        sprout_values_free(&given);
        return r < 0 ? RETURN_FAIL : RETURN_ERROR;
    }
    if (strcmp(t.kind, "project") != 0) {
        msg("file templates are not supported yet: ", (const char *)args[ARG_TMPL], NULL);
        goto out;
    }

    /* the values of all placeholders */
    drawer_name(sp.to, name);
    DateStamp(&now);
    r = sprout_resolve(&t, &given, name, now.ds_Days, &values, report, NULL);
    if (r <= 0) {
        if (r < 0) {
            msg("out of memory", NULL, NULL);
            rc = RETURN_FAIL;
        }
        goto out;
    }
    sp.values = &values;

    /* the drawer and the files */
    if (parse_patterns(&t) && make_project_drawer() && join(path, tmpl, "files")) {
        int ok = 1;
        if (exists(path, &is_dir)) {
            if (is_dir) ok = copy_drawer(path, "", 0);
            else {
                msg("not a drawer: ", path, NULL);
                ok = 0;
            }
        }
        if (ok) rc = sp.warnings > 0 ? RETURN_WARN : RETURN_OK;
    }
    rollback(rc >= RETURN_ERROR);
    free_patterns();
    sprout_values_free(&values);
out:
    sprout_template_free(&t);
    sprout_values_free(&given);
    return rc;
}

/**
 * @brief Read the arguments and create the project.
 * @return The return code for the Shell.
 */
int main(void)
{
    struct RDArgs *rda;
    LONG args[ARG_COUNT];
    LONG rc;

    memset(&sp, 0, sizeof(sp));
    sp.err = ((struct Process *)FindTask(NULL))->pr_CES;
    if (sp.err == 0) sp.err = Output();
    memset(args, 0, sizeof(args));
    if ((rda = ReadArgs((STRPTR)ARG_TEMPLATE, args, NULL)) == NULL) {
        PrintFault(IoErr(), (STRPTR)"Sprout");
        return RETURN_FAIL;
    }
    rc = run(args);
    FreeArgs(rda);
    return (int)rc;
}
