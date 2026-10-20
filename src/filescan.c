/**
 * @file filescan.c
 * @brief Step-by-step walk through the files of a directory tree.
 *
 * Used by the find window to search the project files (the directory of
 * the folder tree) without blocking the event loop: filescan_next()
 * looks at a few directory entries per call and returns when it has found
 * a file, so the caller can search it and handle input in between.
 *
 * The walk is bounded to FILESCAN_DEPTH directory levels (like
 * dirtree.gadget), skips links (no loops) and icon files (.info), and
 * returns only files whose name matches an AmigaDOS pattern.
 */
#include "editor.h"

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdlib.h>
#include <string.h>

#define FILESCAN_DEPTH 16       /**< directory levels, as dirtree.gadget */
#define FILESCAN_STEP 32        /**< directory entries per filescan_next() */

/** State of a walk. */
struct FileScan {
    BPTR lock[FILESCAN_DEPTH];                  /**< the open directories ... */
    struct FileInfoBlock *fib[FILESCAN_DEPTH];  /**< ... and their ExNext() state */
    size_t path_length[FILESCAN_DEPTH];         /**< length of path for each level */
    int depth;                                  /**< number of open levels */
    int incomplete;                             /**< levels were skipped or unreadable */
    char *pattern;                              /**< parsed pattern, NULL = all files */
    char path[EDITOR_PATH_MAX];                 /**< the current directory */
};

/**
 * @brief Open a directory as a new level of the walk.
 * @param scan The walk.
 * @return Non-zero on success.
 */
static int push(FileScan *scan)
{
    int level = scan->depth;
    BPTR lock;
    struct FileInfoBlock *fib;
    if (level >= FILESCAN_DEPTH) {
        scan->incomplete = 1;
        return 0;
    }
    if ((lock = Lock((STRPTR)scan->path, ACCESS_READ)) == 0) {
        scan->incomplete = 1;
        return 0;
    }
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib == NULL || !Examine(lock, fib) || fib->fib_DirEntryType <= 0) {
        if (fib != NULL) FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        scan->incomplete = 1;
        return 0;
    }
    scan->lock[level] = lock;
    scan->fib[level] = fib;
    scan->path_length[level] = strlen(scan->path);
    scan->depth = level + 1;
    return 1;
}

/**
 * @brief Close the innermost directory of the walk.
 * @param scan The walk.
 */
static void pop(FileScan *scan)
{
    int level = --scan->depth;
    FreeDosObject(DOS_FIB, scan->fib[level]);
    UnLock(scan->lock[level]);
    if (level > 0) scan->path[scan->path_length[level - 1]] = '\0';
}

/**
 * @brief Tell whether a name is that of an icon file.
 * @param name The name.
 * @return Non-zero for "<name>.info".
 */
static int is_icon(const char *name)
{
    static const char suffix[] = ".info";
    size_t n = strlen(name), i;
    if (n < 5) return 0;
    for (i = 0; i < 5; ++i) {
        char c = name[n - 5 + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != suffix[i]) return 0;
    }
    return 1;
}

FileScan *filescan_start(const char *root, const char *pattern)
{
    FileScan *scan = calloc(1, sizeof(*scan));
    if (scan == NULL) return NULL;
    if (pattern != NULL && pattern[0] != '\0' && strcmp(pattern, "#?") != 0) {
        LONG size = (LONG)strlen(pattern) * 2 + 2;
        if ((scan->pattern = malloc((size_t)size)) == NULL ||
            ParsePatternNoCase((STRPTR)pattern, (STRPTR)scan->pattern, size) < 0) {
            filescan_end(scan);
            return NULL;
        }
    }
    strncpy(scan->path, root, sizeof(scan->path) - 1);
    if (!push(scan)) {
        filescan_end(scan);
        return NULL;
    }
    return scan;
}

int filescan_next(FileScan *scan, char *path, long size)
{
    int i;
    for (i = 0; i < FILESCAN_STEP; ++i) {
        struct FileInfoBlock *fib;
        if (scan->depth == 0) return -1;
        fib = scan->fib[scan->depth - 1];
        if (!ExNext(scan->lock[scan->depth - 1], fib)) {
            if (IoErr() != ERROR_NO_MORE_ENTRIES) scan->incomplete = 1;
            pop(scan);
            continue;
        }
        if (fib->fib_DirEntryType == ST_USERDIR) {
            size_t length = strlen(scan->path);
            if (AddPart((STRPTR)scan->path, (STRPTR)fib->fib_FileName, sizeof(scan->path))) {
                if (!push(scan)) scan->path[length] = '\0';
            } else {
                scan->path[length] = '\0';
                scan->incomplete = 1;
            }
            continue;
        }
        /* files only: no links (they could lead in circles) and no icons */
        if (fib->fib_DirEntryType != ST_FILE || is_icon((const char *)fib->fib_FileName)) continue;
        if (scan->pattern != NULL &&
            !MatchPatternNoCase((STRPTR)scan->pattern, (STRPTR)fib->fib_FileName)) continue;
        strncpy(path, scan->path, (size_t)size - 1);
        path[size - 1] = '\0';
        if (!AddPart((STRPTR)path, (STRPTR)fib->fib_FileName, (ULONG)size)) {
            scan->incomplete = 1;
            continue;
        }
        return 1;
    }
    return 0;
}

int filescan_incomplete(const FileScan *scan)
{
    return scan->incomplete;
}

void filescan_end(FileScan *scan)
{
    if (scan == NULL) return;
    while (scan->depth > 0) pop(scan);
    free(scan->pattern);
    free(scan);
}
