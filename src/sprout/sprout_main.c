/**
 * @file sprout_main.c
 * @brief Sprout, the Shell command that creates a project (or files of a
 *        file template) from a template:
 *        arguments, finding the template, copying its files with the
 *        placeholders replaced, and removing what it made on an error.
 *
 * The format and the command line are described in
 * src/sprout/docs/Sprout.md; the portable parts are sprout_tmpl.c
 * (metadata, placeholders) and sprout_lha.c (archives). A template is a
 * drawer or an LhA archive; an archive is read twice, once for
 * template.json (which may come after the files), once for the files.
 * LIST and INFO write JSON to the standard output, built with json.c.
 *
 * Messages go to the error stream of the Shell (pr_CES, else Output()),
 * in English, prefixed "Sprout: ". The created drawers and files are kept
 * in a list (newest first), so an error or Ctrl-C deletes them again in
 * reverse order; files that existed before (FORCE) stay as written.
 */
#include "sprout_tmpl.h"
#include "sprout_lha.h"
#include "json.h"

#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdlib.h>
#include <string.h>

/** libnix swaps to a stack of this size at the start (-Wl,-u,___stkinit). */
unsigned long __stack = 32768;

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
#define ARG_TEMPLATE "TEMPLATE,TO,SET/M,VALUES/K,DIR/K,ICON/K,LIST/S,INFO/S,FORCE/S,QUIET/S"

/** The arguments, in the order of #ARG_TEMPLATE. */
enum {
    ARG_TMPL,       /**< the template (identifier or path) */
    ARG_TO,         /**< the project drawer */
    ARG_SET,        /**< values as KEY=value */
    ARG_VALUES,     /**< a JSON file with values */
    ARG_DIR,        /**< the templates drawer */
    ARG_ICON,       /**< with INFO: where the template's icon goes; with LIST: the drawer for all icons */
    ARG_LIST,       /**< list the templates */
    ARG_INFO,       /**< describe a template */
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
    char *capture;                  /**< if set, the first message goes here instead (LIST) */
    size_t capsize;                 /**< its size */
    char to[PATHMAX];               /**< the project drawer */
} sp;

/** The default templates drawer. */
#define TEMPLATES_DIR "PROGDIR:Templates"

/** Where a template is: a drawer or an LhA archive. */
typedef struct Source {
    char path[PATHMAX];     /**< the drawer or the archive */
    int archive;            /**< non-zero for an archive */
    char prefix[PATHMAX];   /**< archive: "" or the drawer (with '/') that holds template.json */
} Source;

/**
 * @brief Write a message of up to three parts to the error stream.
 * @param a First part.
 * @param b Second part (may be NULL).
 * @param c Third part (may be NULL).
 */
static void msg(const char *a, const char *b, const char *c)
{
    if (sp.capture != NULL) {
        if (sp.capture[0] == '\0') {
            const char *parts[3];
            size_t n = 0;
            int i;
            parts[0] = a;
            parts[1] = b;
            parts[2] = c;
            for (i = 0; i < 3; ++i) {
                const char *q = parts[i];
                while (q != NULL && *q != '\0' && n + 1 < sp.capsize) sp.capture[n++] = *q++;
            }
            sp.capture[n] = '\0';
        }
        return;
    }
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
 * @brief Compare the starts of two names without case, as AmigaDOS does
 *        (ASCII and ISO-8859-1 letters).
 * @param a First name.
 * @param b Second name.
 * @param n The most characters compared.
 * @return Less than, equal to or greater than zero, like strncmp().
 */
static int name_order(const char *a, const char *b, size_t n)
{
    for (; n > 0; ++a, ++b, --n) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if ((x >= 'a' && x <= 'z') || (x >= 0xE0 && x <= 0xFE && x != 0xF7)) x = (unsigned char)(x - 0x20);
        if ((y >= 'a' && y <= 'z') || (y >= 0xE0 && y <= 0xFE && y != 0xF7)) y = (unsigned char)(y - 0x20);
        if (x != y) return (int)x - (int)y;
        if (x == '\0') return 0;
    }
    return 0;
}

/**
 * @brief Compare two names without case.
 * @param a First name.
 * @param b Second name.
 * @return Non-zero if they are the same.
 */
static int same_name(const char *a, const char *b)
{
    return name_order(a, b, (size_t)-1) == 0;
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
 * @brief Check the destination of a file and note it as created (before
 *        writing, so a half written file is removed too).
 * @param dst The destination.
 * @return Non-zero if it may be written, zero otherwise (reported).
 */
static int prepare_file(const char *dst)
{
    int is_dir, existed;
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
    return existed || note_created(dst);
}

/**
 * @brief Write the contents of a file, with the placeholders replaced in a
 *        text file.
 * @param rel Its path below files/ (for the "verbatim" patterns).
 * @param dst The destination.
 * @param data The contents from the template.
 * @param len Their length.
 * @return Non-zero on success, zero on an error (reported).
 */
static int put_contents(const char *rel, const char *dst, const char *data, LONG len)
{
    char *expanded;
    size_t n = 0;
    int ok;
    if (is_verbatim(rel) || !sprout_is_text(data, (size_t)len)) return write_file(dst, data, len);
    if ((expanded = sprout_expand(data, (size_t)len, sp.values, &n)) == NULL) {
        msg("out of memory", NULL, NULL);
        return 0;
    }
    ok = write_file(dst, expanded, (LONG)n);
    free(expanded);
    return ok;
}

/**
 * @brief Give a written file its protection bits and file note.
 * @param dst The file.
 * @param has_prot Non-zero if @p prot is known.
 * @param prot The protection bits (the archive bit is cleared: the file
 *        is new).
 * @param comment The file note ("" for none).
 */
static void finish_file(const char *dst, int has_prot, LONG prot, const char *comment)
{
    if (has_prot) SetProtection((STRPTR)dst, prot & ~FIBF_ARCHIVE);
    if (comment[0] != '\0') SetComment((STRPTR)dst, (STRPTR)comment);
    progress(dst);
}

/**
 * @brief Report why a path of the template could not be expanded.
 * @param bad The reason from sprout_expand_path().
 * @param rel The path below files/.
 */
static void path_error(int bad, const char *rel)
{
    if (bad == SPROUT_PATH_UNSAFE) msg("name not allowed in the project: ", rel, NULL);
    else if (bad == SPROUT_PATH_BADCOND) msg("malformed condition in ", rel, NULL);
    else msg("out of memory", NULL, NULL);
}

/**
 * @brief Copy one file of a template drawer, replacing placeholders in a
 *        text file, and take over its protection bits and file note.
 * @param src The source.
 * @param rel Its path below files/ (for the "verbatim" patterns).
 * @param dst The destination.
 * @param fib The source's FileInfoBlock.
 * @return Non-zero on success, zero on an error (reported).
 */
static int copy_file(const char *src, const char *rel, const char *dst, const struct FileInfoBlock *fib)
{
    int ok;
    if (!prepare_file(dst)) return 0;
    if (fib->fib_Size > SPROUT_TEXT_MAX) {
        ok = stream_copy(src, dst);
    } else {
        char *data;
        LONG len;
        if (!read_file(src, SPROUT_TEXT_MAX, &data, &len)) return 0;
        ok = put_contents(rel, dst, data, len);
        free(data);
    }
    if (!ok) return 0;
    finish_file(dst, 1, fib->fib_Protection, (const char *)fib->fib_Comment);
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
            if (bad == SPROUT_PATH_LEFTOUT) continue;       /* a condition does not hold */
            path_error(bad, lv->rel);
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
 * @brief Find a template: a path, or an identifier in the templates
 *        drawer (a drawer of that name first, then the archive
 *        \<Id\>.lha).
 * @param arg The TEMPLATE argument.
 * @param dir The templates drawer (DIR), or NULL for PROGDIR:Templates.
 * @param[out] s Where the template is.
 * @return Non-zero if found, zero otherwise (reported).
 */
static int find_template(const char *arg, const char *dir, Source *s)
{
    int is_dir;
    size_t n;
    memset(s, 0, sizeof(*s));
    if (strchr(arg, ':') != NULL || strchr(arg, '/') != NULL) {
        strncpy(s->path, arg, PATHMAX - 1);
    } else if (!join(s->path, dir != NULL ? dir : TEMPLATES_DIR, arg)) {
        return 0;
    }
    if (exists(s->path, &is_dir)) {
        s->archive = !is_dir;
        return 1;
    }
    n = strlen(s->path);
    if (n + 4 < PATHMAX) {
        strcpy(s->path + n, ".lha");
        if (exists(s->path, &is_dir) && !is_dir) {
            s->archive = 1;
            return 1;
        }
    }
    msg("template not found: ", arg, NULL);
    return 0;
}

/** An open archive. */
typedef struct Archive {
    BPTR fh;            /**< the file */
    SproutLha lha;      /**< the reader */
} Archive;

/**
 * @brief Read callback of the LhA reader: reads the archive file.
 * @param user Points to the file handle.
 * @param buf Where the bytes go.
 * @param len How many are wanted.
 * @return The number read, 0 at the end, -1 on an error.
 */
static long fh_read(void *user, void *buf, long len)
{
    return Read(*(BPTR *)user, buf, len);
}

/**
 * @brief Write callback of the LhA reader: writes to a file, stops on
 *        Ctrl-C.
 * @param user Points to the file handle.
 * @param buf The bytes.
 * @param len Their number.
 * @return Non-zero on success.
 */
static int fh_write(void *user, const void *buf, long len)
{
    if (Write(*(BPTR *)user, (APTR)buf, len) != len) return 0;
    return !break_pressed();
}

/** Bytes collected in memory by mem_write(). */
typedef struct MemOut {
    char *data;         /**< the buffer */
    LONG len;           /**< bytes in it */
    LONG cap;           /**< its size */
} MemOut;

/**
 * @brief Write callback of the LhA reader: collects into a MemOut.
 * @param user The MemOut.
 * @param buf The bytes.
 * @param len Their number.
 * @return Non-zero on success, zero if they do not fit.
 */
static int mem_write(void *user, const void *buf, long len)
{
    MemOut *m = (MemOut *)user;
    if (len > m->cap - m->len) return 0;
    memcpy(m->data + m->len, buf, (size_t)len);
    m->len += len;
    return 1;
}

/**
 * @brief Open an archive.
 * @param path The archive.
 * @return The archive (archive_close() it), or NULL on an error (reported).
 */
static Archive *archive_open(const char *path)
{
    Archive *a = (Archive *)malloc(sizeof(Archive));
    if (a == NULL) {
        msg("out of memory", NULL, NULL);
        return NULL;
    }
    if ((a->fh = Open((STRPTR)path, MODE_OLDFILE)) == 0) {
        dos_error("cannot open ", path);
        free(a);
        return NULL;
    }
    sprout_lha_init(&a->lha, fh_read, &a->fh);
    return a;
}

/**
 * @brief Close an archive.
 * @param a The archive.
 */
static void archive_close(Archive *a)
{
    Close(a->fh);
    free(a);
}

/**
 * @brief Report an error of the LhA reader.
 * @param a The archive.
 * @param path Its path.
 */
static void archive_error(const Archive *a, const char *path)
{
    msg(path, ": ", a->lha.error);
}

/**
 * @brief Extract an entry into memory.
 * @param a The archive.
 * @param e The entry.
 * @param path The archive's path (for messages).
 * @param max The largest size accepted.
 * @param[out] data The contents, NUL-terminated (free() them).
 * @param[out] len Their length.
 * @return Non-zero on success, zero on an error (reported).
 */
static int extract_memory(Archive *a, const SproutLhaEntry *e, const char *path, LONG max, char **data, LONG *len)
{
    MemOut m;
    if (e->size > (unsigned long)max) {
        msg("file too large: ", e->path, NULL);
        return 0;
    }
    m.cap = (LONG)e->size;
    m.len = 0;
    if ((m.data = (char *)malloc((size_t)m.cap + 1)) == NULL) {
        msg("out of memory", NULL, NULL);
        return 0;
    }
    if (sprout_lha_extract(&a->lha, e, mem_write, &m) < 0) {
        archive_error(a, path);
        free(m.data);
        return 0;
    }
    m.data[m.len] = '\0';
    *data = m.data;
    *len = m.len;
    return 1;
}

/**
 * @brief Read a file of a template whole: template.json, its icon.
 *
 * For an archive, looking for template.json also finds the drawer that
 * holds it (Source::prefix): the top of the archive or a single drawer.
 *
 * @param s The template.
 * @param name The file, relative to the template (for an archive, below
 *        Source::prefix).
 * @param find_prefix Non-zero to look for template.json and set
 *        Source::prefix (@p name is then ignored).
 * @param max The largest size accepted.
 * @param[out] data The contents, NUL-terminated (free() them).
 * @param[out] len Their length.
 * @return 1 if read; 0 if there is no such file (not reported); -1 on an
 *         error (reported).
 */
static int template_file(Source *s, const char *name, int find_prefix, LONG max, char **data, LONG *len)
{
    SproutLhaEntry e;
    Archive *a;
    int r;
    *data = NULL;
    *len = 0;
    if (!s->archive) {
        char path[PATHMAX];
        int is_dir;
        if (!join(path, s->path, find_prefix ? "template.json" : name)) return -1;
        if (!exists(path, &is_dir) || is_dir) return 0;
        return read_file(path, max, data, len) ? 1 : -1;
    }
    if ((a = archive_open(s->path)) == NULL) return -1;
    while ((r = sprout_lha_next(&a->lha, &e)) == 1) {
        int match;
        if (e.is_dir) continue;
        if (find_prefix) {
            const char *slash = strchr(e.path, '/');
            match = same_name(e.path, "template.json") ||
                    (slash != NULL && slash > e.path && same_name(slash + 1, "template.json") && (size_t)(slash - e.path) + 1 < PATHMAX);
            if (match) {
                size_t n = slash != NULL && !same_name(e.path, "template.json") ? (size_t)(slash - e.path) + 1 : 0;
                memcpy(s->prefix, e.path, n);
                s->prefix[n] = '\0';
            }
        } else {
            size_t n = strlen(s->prefix);
            match = name_order(e.path, s->prefix, n) == 0 && same_name(e.path + n, name);
        }
        if (match) {
            r = extract_memory(a, &e, s->path, max, data, len) ? 1 : -1;
            archive_close(a);
            return r;
        }
    }
    if (r < 0) archive_error(a, s->path);
    archive_close(a);
    return r < 0 ? -1 : 0;
}

/**
 * @brief Read and check a template's metadata.
 * @param s The template (for an archive, Source::prefix is set).
 * @param[out] t The metadata (sprout_template_free() it).
 * @return 1 on success; 0 on an error (reported); -1 if out of memory.
 */
static int load_meta(Source *s, SproutTemplate *t)
{
    char *text;
    LONG len;
    int r;
    memset(t, 0, sizeof(*t));
    r = template_file(s, NULL, 1, JSONMAX, &text, &len);
    if (r == 0) msg("no template.json in ", s->path, NULL);
    if (r <= 0) return 0;
    r = sprout_template_parse(t, text, report, NULL);
    free(text);
    if (r < 0) msg("out of memory", NULL, NULL);
    return r;
}

/**
 * @brief Make the drawers above a file or drawer of the project (an
 *        archive need not have entries for them).
 * @param dest Its path in the project ('/' separated).
 * @return Non-zero on success, zero on an error (reported).
 */
static int make_parents(const char *dest)
{
    char part[PATHMAX], path[PATHMAX];
    size_t i;
    strncpy(part, dest, PATHMAX - 1);
    part[PATHMAX - 1] = '\0';
    for (i = 0; part[i] != '\0'; ++i) {
        if (part[i] != '/') continue;
        part[i] = '\0';
        if (!join(path, sp.to, part) || !make_drawer(path)) return 0;
        part[i] = '/';
    }
    return 1;
}

/**
 * @brief Write one file of an archive into the project.
 * @param a The archive.
 * @param e The entry.
 * @param apath The archive's path (for messages).
 * @param rel Its path below files/ (for the "verbatim" patterns).
 * @param dst The destination.
 * @return Non-zero on success, zero on an error (reported).
 */
static int archive_file(Archive *a, const SproutLhaEntry *e, const char *apath, const char *rel, const char *dst)
{
    int ok;
    if (!prepare_file(dst)) return 0;
    if (e->size > (unsigned long)SPROUT_TEXT_MAX) {
        BPTR fh = Open((STRPTR)dst, MODE_NEWFILE);
        if (fh == 0) {
            dos_error("cannot create ", dst);
            return 0;
        }
        ok = sprout_lha_extract(&a->lha, e, fh_write, &fh) > 0;
        if (!ok) archive_error(a, apath);
        if (!Close(fh) && ok) {
            dos_error("cannot write ", dst);
            ok = 0;
        }
    } else {
        char *data;
        LONG len;
        if (!extract_memory(a, e, apath, SPROUT_TEXT_MAX, &data, &len)) return 0;
        ok = put_contents(rel, dst, data, len);
        free(data);
    }
    if (!ok) return 0;
    /* Amiga LhA keeps the protection bits in the attribute byte */
    finish_file(dst, e->os_id == 'A' && e->level < 2, (LONG)e->attribute, e->comment);
    return 1;
}

/**
 * @brief Copy the files/ of an archive into the project.
 * @param s The template (Source::prefix set by load_meta()).
 * @return Non-zero on success, zero on an error (reported).
 */
static int copy_archive(const Source *s)
{
    char files[PATHMAX], dst[PATHMAX];
    SproutLhaEntry *e;
    Archive *a;
    size_t n;
    int r, ok = 1;

    if (strlen(s->prefix) + 7 > PATHMAX) return 0;
    strcpy(files, s->prefix);
    strcat(files, "files/");
    n = strlen(files);
    if ((e = (SproutLhaEntry *)malloc(sizeof(SproutLhaEntry))) == NULL) {
        msg("out of memory", NULL, NULL);
        return 0;
    }
    if ((a = archive_open(s->path)) == NULL) {
        free(e);
        return 0;
    }
    while (ok && (r = sprout_lha_next(&a->lha, e)) != 0) {
        const char *rel = e->path + n;
        char *dest;
        int bad;
        if (r < 0) {
            archive_error(a, s->path);
            ok = 0;
            break;
        }
        /* only what is below files/ ("files" itself is shorter) */
        if (strlen(e->path) <= n || name_order(e->path, files, n) != 0) continue;
        if ((dest = sprout_expand_path(rel, sp.values, &bad)) == NULL) {
            if (bad == SPROUT_PATH_LEFTOUT) continue;       /* a condition does not hold */
            path_error(bad, rel);
            ok = 0;
            break;
        }
        ok = join(dst, sp.to, dest) && make_parents(dest);
        free(dest);
        if (ok) ok = e->is_dir ? make_drawer(dst) : archive_file(a, e, s->path, rel, dst);
        if (ok && break_pressed()) ok = 0;
    }
    archive_close(a);
    free(e);
    return ok;
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
 * @brief Check the drawer a file template writes into: it must exist (a
 *        file template makes no drawer of its own; files that exist are
 *        errors unless FORCE).
 * @return Non-zero if it is a drawer, zero otherwise (reported).
 */
static int check_file_drawer(void)
{
    int is_dir;
    if (!exists(sp.to, &is_dir) || !is_dir) {
        msg("not a drawer: ", sp.to, NULL);
        return 0;
    }
    return 1;
}

/**
 * @brief Read the values given with VALUES and SET.
 * @param args The ReadArgs results.
 * @param[out] given The values (initialised here; sprout_values_free() it).
 * @return Non-zero on success, zero on an error (reported).
 */
static int given_values(LONG *args, SproutValues *given)
{
    char *text;
    LONG len;
    int r;
    sprout_values_init(given);
    if (args[ARG_VALUES]) {
        if (!read_file((const char *)args[ARG_VALUES], JSONMAX, &text, &len)) return 0;
        r = sprout_values_parse_json(given, text);
        free(text);
        if (r <= 0) {
            msg(r < 0 ? "out of memory" : "not a JSON object: ", r < 0 ? NULL : (const char *)args[ARG_VALUES], NULL);
            return 0;
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
                r = sprout_values_set_arg(given, item, strlen(item), (const char *)*++set);
            else
                r = sprout_values_parse_assign(given, item);
            if (r <= 0) {
                msg(r < 0 ? "out of memory" : "not KEY=value: ", r < 0 ? NULL : item, NULL);
                return 0;
            }
        }
    }
    return 1;
}

/**
 * @brief Create the project: check the template, work out the values, make
 *        the drawer and copy the files. A file template ("kind": "file")
 *        copies its files into the existing drawer TO instead.
 * @param args The ReadArgs results.
 * @return The return code (RETURN_OK, _WARN, _ERROR or _FAIL).
 */
static LONG create(LONG *args)
{
    SproutTemplate t;
    SproutValues given, values;
    struct DateStamp now;
    Source src;
    char path[PATHMAX], name[PATHMAX];
    LONG rc = RETURN_ERROR;
    int r, is_dir;

    strncpy(sp.to, (const char *)args[ARG_TO], PATHMAX - 1);
    sp.force = args[ARG_FORCE] != 0;
    sp.quiet = args[ARG_QUIET] != 0;

    if (!given_values(args, &given)) {
        sprout_values_free(&given);
        return RETURN_FAIL;
    }
    if (!find_template((const char *)args[ARG_TMPL], (const char *)args[ARG_DIR], &src) ||
        (r = load_meta(&src, &t)) == 0) {
        sprout_values_free(&given);
        return RETURN_ERROR;
    }
    if (r < 0) {
        sprout_values_free(&given);
        return RETURN_FAIL;
    }

    /* the values of all placeholders */
    sprout_drawer_name(sp.to, name, PATHMAX);
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
    if (parse_patterns(&t) && (strcmp(t.kind, "file") == 0 ? check_file_drawer() : make_project_drawer())) {
        int ok = 1;
        if (src.archive) {
            ok = copy_archive(&src);
        } else if (join(path, src.path, "files")) {
            if (exists(path, &is_dir)) {
                if (is_dir) ok = copy_drawer(path, "", 0);
                else {
                    msg("not a drawer: ", path, NULL);
                    ok = 0;
                }
            }
        } else {
            ok = 0;
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
 * @brief Write a JSON tree to the standard output, indented.
 * @param root The tree.
 * @return Non-zero on success, zero if out of memory (reported).
 */
static int print_json(const JsonValue *root)
{
    JsonWriter w;
    int ok;
    json_writer_init_alloc(&w, 4096);
    json_write_value(&w, root, 0);
    ok = !w.error;
    if (ok) {
        FPuts(Output(), (STRPTR)json_writer_get(&w));
        FPuts(Output(), (STRPTR)"\n");
    } else {
        msg("out of memory", NULL, NULL);
    }
    json_writer_free(&w);
    return ok;
}

/**
 * @brief Describe a template as an entry of LIST or INFO: its metadata
 *        (as in template.json) with "id" and "path", or "id", "path" and
 *        "error".
 * @param id Its identifier.
 * @param path Its drawer or archive.
 * @param t Its metadata, or NULL if it could not be read.
 * @param error Why not (with @p t NULL).
 * @return The object, or NULL if out of memory.
 */
static JsonValue *template_entry(const char *id, const char *path, const SproutTemplate *t, const char *error)
{
    JsonValue *o = t != NULL ? sprout_template_to_json(t) : json_new_object();
    if (o == NULL) return NULL;
    if (!json_obj_set(o, "id", json_new_string(id)) || !json_obj_set(o, "path", json_new_string(path)) ||
        (t == NULL && !json_obj_set(o, "error", json_new_string(error)))) {
        json_value_free(o);
        return NULL;
    }
    return o;
}

/**
 * @brief The identifier of a template: its drawer's or archive's name
 *        without ".lha".
 * @param path The drawer or archive.
 * @param[out] id The identifier (#PATHMAX bytes).
 */
static void template_id(const char *path, char *id)
{
    size_t n;
    sprout_drawer_name(path, id, PATHMAX);
    n = strlen(id);
    if (n > 4 && same_name(id + n - 4, ".lha")) id[n - 4] = '\0';
}

/** A template found by LIST. */
typedef struct Found {
    char id[108];           /**< its identifier */
    int archive;            /**< an archive (a drawer otherwise) */
} Found;

/**
 * @brief Collect the templates of a drawer: drawers with a template.json
 *        and archives *.lha; a drawer wins over an archive of the same
 *        identifier. Sorted by identifier.
 * @param dir The templates drawer.
 * @param[out] found The templates (free() them).
 * @param[out] count Their number.
 * @return Non-zero on success, zero on an error (reported).
 */
static int collect_templates(const char *dir, Found **found, int *count)
{
    struct FileInfoBlock *fib;
    char sub[PATHMAX], path[PATHMAX];
    BPTR lock;
    int cap = 0, ok = 1;

    *found = NULL;
    *count = 0;
    if ((lock = Lock((STRPTR)dir, SHARED_LOCK)) == 0) {
        dos_error("cannot read ", dir);
        return 0;
    }
    if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL)) == NULL) {
        UnLock(lock);
        msg("out of memory", NULL, NULL);
        return 0;
    }
    if (!Examine(lock, fib)) {
        dos_error("cannot read ", dir);
        ok = 0;
    }
    while (ok && ExNext(lock, fib)) {
        const char *name = (const char *)fib->fib_FileName;
        size_t n = strlen(name);
        LONG type = fib->fib_DirEntryType;
        int archive, i, is_dir;
        Found f;
        if (type == ST_USERDIR) {
            if (!join(sub, dir, name) || !join(path, sub, "template.json") || !exists(path, &is_dir) || is_dir)
                continue;
            archive = 0;
            strncpy(f.id, name, sizeof(f.id) - 1);
            f.id[sizeof(f.id) - 1] = '\0';
        } else if (type == ST_FILE && n > 4 && same_name(name + n - 4, ".lha")) {
            archive = 1;
            memcpy(f.id, name, n - 4);
            f.id[n - 4] = '\0';
        } else {
            continue;
        }
        f.archive = archive;
        for (i = 0; i < *count && !same_name((*found)[i].id, f.id); ++i)
            ;
        if (i < *count) {           /* the same identifier: the drawer wins */
            if (!archive) (*found)[i] = f;
            continue;
        }
        if (*count == cap) {
            Found *bigger = (Found *)realloc(*found, (size_t)(cap + 16) * sizeof(Found));
            if (bigger == NULL) {
                msg("out of memory", NULL, NULL);
                ok = 0;
                break;
            }
            *found = bigger;
            cap += 16;
        }
        /* sorted by identifier */
        for (i = *count; i > 0 && name_order((*found)[i - 1].id, f.id, (size_t)-1) > 0; --i)
            (*found)[i] = (*found)[i - 1];
        (*found)[i] = f;
        ++*count;
    }
    if (ok && IoErr() != ERROR_NO_MORE_ENTRIES) {
        dos_error("cannot read ", dir);
        ok = 0;
    }
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    if (!ok) {
        free(*found);
        *found = NULL;
        *count = 0;
    }
    return ok;
}

/**
 * @brief Copy a template's icon file (from "icon") to a file. A template
 *        without one writes nothing; a missing or unsafe one is a warning.
 * @param s The template's source.
 * @param t Its metadata.
 * @param icon Where the icon goes.
 * @return Non-zero unless reading or writing failed (reported).
 */
static int write_icon(Source *s, const SproutTemplate *t, const char *icon)
{
    char *data;
    LONG len;
    int r, ok = 1;
    if (t->icon_file == NULL) return 1;
    if (!sprout_path_safe(t->icon_file)) {
        ++sp.warnings;
        msg("warning: icon file not allowed: ", t->icon_file, NULL);
    } else if ((r = template_file(s, t->icon_file, 0, JSONMAX, &data, &len)) == 0) {
        ++sp.warnings;
        msg("warning: icon file missing in the template: ", t->icon_file, NULL);
    } else if (r < 0) {
        ok = 0;
    } else {
        ok = write_file(icon, data, len);
        free(data);
    }
    return ok;
}

/**
 * @brief LIST: write all templates of a drawer as JSON. A template that
 *        cannot be read is listed with the reason. With ICON, the icons of
 *        the templates that have one are copied to that drawer as
 *        \<id\>.info; a failure there is only a warning.
 * @param dir The templates drawer (DIR), or NULL for PROGDIR:Templates.
 * @param icons The drawer for the icons (ICON), or NULL.
 * @return The return code.
 */
static LONG list_templates(const char *dir, const char *icons)
{
    JsonValue *root, *list;
    Found *found;
    char error[160];
    int count, i, ok;

    if (dir == NULL) dir = TEMPLATES_DIR;
    if (!collect_templates(dir, &found, &count)) return RETURN_ERROR;
    root = json_new_object();
    list = json_new_array();
    ok = root != NULL && list != NULL && json_obj_set(root, "version", json_new_int(1));
    for (i = 0; ok && i < count; ++i) {
        SproutTemplate t;
        Source s;
        int r;
        memset(&s, 0, sizeof(s));
        s.archive = found[i].archive;
        ok = join(s.path, dir, found[i].id);
        if (ok && s.archive) {
            if (strlen(s.path) + 4 >= PATHMAX) continue;
            strcat(s.path, ".lha");
        }
        if (!ok) break;
        error[0] = '\0';
        sp.capture = error;
        sp.capsize = sizeof(error);
        r = load_meta(&s, &t);
        sp.capture = NULL;
        if (r < 0) ok = 0;
        else ok = json_array_append(list, template_entry(found[i].id, s.path, r > 0 ? &t : NULL, error));
        if (ok && r > 0 && icons != NULL && t.icon_file != NULL) {
            char icon[PATHMAX];
            if (!join(icon, icons, found[i].id) || strlen(icon) + 5 >= PATHMAX) {
                ++sp.warnings;
                msg("warning: path too long for the icon of ", found[i].id, NULL);
            } else {
                strcat(icon, ".info");
                if (!write_icon(&s, &t, icon)) ++sp.warnings;
            }
        }
        if (r > 0) sprout_template_free(&t);
    }
    free(found);
    if (ok) {
        ok = json_obj_set(root, "templates", list);
        list = NULL;
    }
    json_value_free(list);
    if (!ok) msg("out of memory", NULL, NULL);
    else ok = print_json(root);
    json_value_free(root);
    return !ok ? RETURN_FAIL : sp.warnings > 0 ? RETURN_WARN : RETURN_OK;
}

/**
 * @brief INFO: write a template's metadata as JSON; with ICON also write
 *        its icon file (from "icon") to a file.
 * @param arg The TEMPLATE argument.
 * @param dir The templates drawer (DIR), or NULL.
 * @param icon Where the icon goes, or NULL.
 * @return The return code.
 */
static LONG info_template(const char *arg, const char *dir, const char *icon)
{
    SproutTemplate t;
    JsonValue *root, *entry;
    Source s;
    char id[PATHMAX];
    LONG rc = RETURN_OK;
    int r;

    if (!find_template(arg, dir, &s) || (r = load_meta(&s, &t)) == 0) return RETURN_ERROR;
    if (r < 0) return RETURN_FAIL;
    template_id(s.path, id);
    root = json_new_object();
    entry = template_entry(id, s.path, &t, NULL);
    if (root == NULL || entry == NULL || !json_obj_set(root, "version", json_new_int(1))) {
        json_value_free(entry);
        entry = NULL;
    }
    if (entry == NULL || !json_obj_set(root, "template", entry)) {   /* takes entry over */
        json_value_free(root);
        sprout_template_free(&t);
        msg("out of memory", NULL, NULL);
        return RETURN_FAIL;
    }
    if (!print_json(root)) rc = RETURN_FAIL;
    json_value_free(root);

    if (rc == RETURN_OK && icon != NULL && !write_icon(&s, &t, icon)) rc = RETURN_ERROR;
    sprout_template_free(&t);
    if (rc == RETURN_OK && sp.warnings > 0) rc = RETURN_WARN;
    return rc;
}

/**
 * @brief Read the arguments and list, describe or create.
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
    if (args[ARG_LIST]) {
        rc = list_templates((const char *)args[ARG_DIR], (const char *)args[ARG_ICON]);
    } else if (args[ARG_INFO]) {
        if (args[ARG_TMPL] == 0) {
            msg("INFO needs a TEMPLATE", NULL, NULL);
            rc = RETURN_FAIL;
        } else {
            rc = info_template((const char *)args[ARG_TMPL], (const char *)args[ARG_DIR],
                               (const char *)args[ARG_ICON]);
        }
    } else if (args[ARG_TMPL] == 0 || args[ARG_TO] == 0) {
        msg("TEMPLATE and TO are needed (or LIST, or INFO with TEMPLATE)", NULL, NULL);
        rc = RETURN_FAIL;
    } else {
        rc = create(args);
    }
    FreeArgs(rda);
    return (int)rc;
}
