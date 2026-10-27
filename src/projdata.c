/**
 * @file projdata.c
 * @brief The files of a project in its directory \<project\>/.tinydev/
 *        (bookmarks, project settings).
 *
 * The directory is visible and made when a file is first written. Files
 * are read whole and written through a temporary file (\<name\>.tmp, then
 * renamed), so a failed write leaves the old file.
 */
#include "editor.h"

#include <dos/dos.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The project's directory for TinyDev's files. */
#define PROJDATA_DIR ".tinydev"
/** The largest file read (bytes). */
#define PROJDATA_MAX (4L * 1024 * 1024)

int projdata_path(const char *root, const char *name, char *out, LONG size)
{
    if (root == NULL || root[0] == '\0') return 0;
    strncpy(out, root, (size_t)size - 1);
    out[size - 1] = '\0';
    if (!AddPart((STRPTR)out, (STRPTR)PROJDATA_DIR, (ULONG)size)) return 0;
    return name == NULL || AddPart((STRPTR)out, (STRPTR)name, (ULONG)size);
}

int projdata_read(const char *root, const char *name, char **text)
{
    char path[EDITOR_PATH_MAX];
    struct FileInfoBlock *fib;
    BPTR fh;
    char *data = NULL;
    int rc = -1;

    *text = NULL;
    if (!projdata_path(root, name, path, sizeof(path))) return -1;
    if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0) {
        LONG err = IoErr();
        /* no .tinydev yet may also give "directory not found" */
        return err == ERROR_OBJECT_NOT_FOUND || err == ERROR_DIR_NOT_FOUND ? 0 : -1;
    }
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib != NULL && ExamineFH(fh, fib) && fib->fib_Size >= 0 && fib->fib_Size < PROJDATA_MAX &&
        (data = malloc((size_t)fib->fib_Size + 1)) != NULL &&
        Read(fh, data, fib->fib_Size) == fib->fib_Size) {
        data[fib->fib_Size] = '\0';
        *text = data;
        data = NULL;
        rc = 1;
    }
    if (fib != NULL) FreeDosObject(DOS_FIB, fib);
    Close(fh);
    free(data);
    return rc;
}

int projdata_exists(const char *root, const char *name)
{
    char path[EDITOR_PATH_MAX];
    BPTR lock;
    if (!projdata_path(root, name, path, sizeof(path))) return 0;
    if ((lock = Lock((STRPTR)path, ACCESS_READ)) == 0) return 0;
    UnLock(lock);
    return 1;
}

int projdata_write(const char *root, const char *name, const char *text)
{
    char dir[EDITOR_PATH_MAX], path[EDITOR_PATH_MAX], temp[EDITOR_PATH_MAX + 8];
    LONG length = (LONG)strlen(text);
    BPTR lock, fh;
    int ok;

    if (!projdata_path(root, NULL, dir, sizeof(dir)) ||
        !projdata_path(root, name, path, sizeof(path))) return 0;
    if ((lock = Lock((STRPTR)dir, ACCESS_READ)) == 0) lock = CreateDir((STRPTR)dir);
    if (lock == 0) return 0;
    UnLock(lock);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    if ((fh = Open((STRPTR)temp, MODE_NEWFILE)) == 0) return 0;
    ok = Write(fh, (APTR)text, length) == length;
    ok = Close(fh) && ok;
    if (ok) {
        DeleteFile((STRPTR)path);
        ok = Rename((STRPTR)temp, (STRPTR)path) != 0;
    }
    if (!ok) DeleteFile((STRPTR)temp);
    return ok;
}
