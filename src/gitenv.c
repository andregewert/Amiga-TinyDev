/**
 * @file gitenv.c
 * @brief am-git's settings: its environment variables, edited in the
 *        settings window (page Git).
 *
 * am-git reads its identity and credentials from environment variables
 * (AM_GIT_USER_NAME, AM_GIT_USER_EMAIL, AM_GIT_USERNAME, AM_GIT_PASSWORD,
 * AM_GIT_SSH, AM_GIT_NETRC). TinyDev keeps none of them in TinyDev.json:
 * they stay where am-git and the user's own scripts expect them, so they
 * also count for am-git started from a Shell. "Use" writes them to ENV:,
 * "Save" to ENV: and ENVARC:, as the preferences editors do.
 *
 * The variables are read and written as the files ENV:\<name\> and
 * ENVARC:\<name\> (as SetEnv and SetEnv SAVE do), whole, so the lines of
 * AM_GIT_NETRC stay (GetVar() without GVF_BINARY_VAR would stop at the
 * first line). A value is shown from ENV:, else from ENVARC:. Only
 * variables whose value differs are written; an empty value deletes the
 * variable. Trailing line ends are not part of a value; AM_GIT_NETRC is
 * written with a line end after its last line, the others without.
 */
#include "editor.h"

#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdlib.h>
#include <string.h>

/** The variables' names, GitEnvVar order. */
static const char *const names[GITENV_COUNT] = {
    "AM_GIT_USER_NAME", "AM_GIT_USER_EMAIL", "AM_GIT_USERNAME", "AM_GIT_PASSWORD",
    "AM_GIT_SSH", "AM_GIT_NETRC"
};

const char *gitenv_name(int var)
{
    return var >= 0 && var < GITENV_COUNT ? names[var] : "";
}

size_t gitenv_max(int var)
{
    return var == GITENV_NETRC ? GITENV_NETRC_MAX : GITENV_LINE_MAX;
}

/**
 * @brief Read a variable from a drawer of variables.
 * @param drawer "ENV:" or "ENVARC:".
 * @param var The variable (GitEnvVar).
 * @return Its value (free() it; "" if it is not set), or NULL if out of
 *         memory.
 */
static char *read_var(const char *drawer, int var)
{
    char path[64];
    size_t max = gitenv_max(var), n;
    char *buf = malloc(max);
    BPTR fh;
    LONG got;

    if (buf == NULL) return NULL;
    buf[0] = '\0';
    strcpy(path, drawer);
    strcat(path, names[var]);
    if ((fh = Open((STRPTR)path, MODE_OLDFILE)) == 0) return buf;
    got = Read(fh, buf, (LONG)(max - 1));
    Close(fh);
    n = got > 0 ? (size_t)got : 0;
    buf[n] = '\0';
    /* a single line: whatever follows its end is not part of it */
    if (var != GITENV_NETRC) n = strcspn(buf, "\r\n");
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) --n;
    buf[n] = '\0';
    return buf;
}

/**
 * @brief Write a variable into a drawer of variables, or delete it.
 * @param drawer "ENV:" or "ENVARC:".
 * @param var The variable (GitEnvVar).
 * @param value Its value; "" deletes it.
 * @return Non-zero on success.
 */
static int write_var(const char *drawer, int var, const char *value)
{
    char path[64];
    size_t n = strlen(value);
    BPTR fh;
    int ok;

    strcpy(path, drawer);
    strcat(path, names[var]);
    if (n == 0) return DeleteFile((STRPTR)path) || IoErr() == ERROR_OBJECT_NOT_FOUND;
    if ((fh = Open((STRPTR)path, MODE_NEWFILE)) == 0) return 0;
    ok = Write(fh, (APTR)value, (LONG)n) == (LONG)n;
    if (ok && var == GITENV_NETRC) ok = Write(fh, (APTR)"\n", 1) == 1;
    Close(fh);
    return ok;
}

/**
 * @brief A value as it is stored: without trailing line ends, a single
 *        line cut at its end.
 * @param var The variable (GitEnvVar).
 * @param value The value; changed in place.
 */
static void normalise(int var, char *value)
{
    size_t n = var != GITENV_NETRC ? strcspn(value, "\r\n") : strlen(value);
    while (n > 0 && (value[n - 1] == '\n' || value[n - 1] == '\r')) --n;
    value[n] = '\0';
}

int gitenv_load(GitEnv *e)
{
    int i;
    memset(e, 0, sizeof(*e));
    for (i = 0; i < GITENV_COUNT; ++i) {
        if ((e->value[i] = read_var("ENV:", i)) == NULL) {
            gitenv_free(e);
            return 0;
        }
        if (e->value[i][0] == '\0') {
            char *arc = read_var("ENVARC:", i);
            if (arc == NULL) {
                gitenv_free(e);
                return 0;
            }
            free(e->value[i]);
            e->value[i] = arc;
        }
    }
    return 1;
}

int gitenv_set(GitEnv *e, int var, const char *value)
{
    size_t max = gitenv_max(var), n = strlen(value);
    char *copy;
    if (n >= max) n = max - 1;
    if ((copy = malloc(n + 1)) == NULL) return 0;
    memcpy(copy, value, n);
    copy[n] = '\0';
    normalise(var, copy);
    free(e->value[var]);
    e->value[var] = copy;
    return 1;
}

int gitenv_store(const GitEnv *e, int save)
{
    static const char *const drawers[2] = { "ENV:", "ENVARC:" };
    int i, d, ok = 1;
    for (i = 0; i < GITENV_COUNT; ++i) {
        const char *value = e->value[i] != NULL ? e->value[i] : "";
        for (d = 0; d < (save ? 2 : 1); ++d) {
            char *old = read_var(drawers[d], i);
            if (old == NULL) return 0;
            if (strcmp(old, value) != 0 && !write_var(drawers[d], i, value)) ok = 0;
            free(old);
        }
    }
    return ok;
}

void gitenv_free(GitEnv *e)
{
    int i;
    for (i = 0; i < GITENV_COUNT; ++i) {
        free(e->value[i]);
        e->value[i] = NULL;
    }
}
