/**
 * @file gitstatus.h
 * @brief Portable reading of am-git's status: the lines of
 *        "status --porcelain", a file's or drawer's state, the branch of
 *        .git/HEAD.
 *
 * No Amiga headers, so the host tests can use it. am-git 0.12.1 prints
 * one line "XY path" per file (X the index, Y the work tree, "??" for an
 * untracked file) and no branch line; paths are relative to the
 * repository with '/', as the bytes on disk (ISO-8859-1). The reader also
 * takes what git prints: a path in double quotes with C escapes, a rename
 * "old -> new", an untracked drawer as "dir/". Paths are compared
 * ignoring case (Latin-1), as the Amiga's file systems do.
 */
#ifndef TINYDEV_GITSTATUS_H
#define TINYDEV_GITSTATUS_H

#include <stddef.h>

/** State of a file or drawer, in the order of precedence (higher wins
 *  for a drawer). */
enum GitState {
    GIT_ST_CLEAN = 0,   /**< unchanged, or not known to the status */
    GIT_ST_UNTRACKED,   /**< not in the repository */
    GIT_ST_STAGED,      /**< changed in the index only (ready to commit) */
    GIT_ST_MODIFIED,    /**< changed in the work tree */
    GIT_ST_CONFLICT,    /**< unmerged */
    GIT_ST_COUNT        /**< number of states */
};

/** A file of the status. */
typedef struct GitStatusEntry {
    char *path;         /**< relative to the repository, '/' between drawers; a drawer ends with '/' */
    int state;          /**< GitState */
} GitStatusEntry;

/** The status of a repository: its changed files, sorted by path. */
typedef struct GitStatus {
    GitStatusEntry *entries;    /**< sorted, ignoring case */
    long count;                 /**< number of entries */
} GitStatus;

/**
 * @brief Set up an empty status.
 * @param st The status.
 */
void gitstatus_init(GitStatus *st);

/**
 * @brief Free a status's entries (it is empty afterwards).
 * @param st The status.
 */
void gitstatus_clear(GitStatus *st);

/**
 * @brief Read the output of "am-git status --porcelain" into a status.
 *
 * Lines that are no status lines (messages) are left out; files deleted
 * in the work tree as well, as the tree does not show them.
 *
 * @param st The status (its old entries are freed).
 * @param text The output.
 * @return 1 on success, 0 if out of memory (st is empty then).
 */
int gitstatus_parse(GitStatus *st, const char *text);

/**
 * @brief The state of a file or drawer.
 *
 * A file has its own state, or that of an untracked drawer above it. A
 * drawer has the highest state of the files below it.
 *
 * @param st The status.
 * @param path The path relative to the repository ('/' between drawers,
 *        without a trailing '/').
 * @param directory Non-zero for a drawer.
 * @return A GitState.
 */
int gitstatus_state(const GitStatus *st, const char *path, int directory);

/**
 * @brief The path of an entry relative to the repository.
 * @param root The repository's drawer, e.g. "Work:Projects/Foo" or "Dh1:".
 * @param path A full path below it.
 * @return The part of @p path after root and '/'; "" for the root itself;
 *         NULL if @p path is not below @p root.
 */
const char *gitstatus_relative(const char *root, const char *path);

/**
 * @brief The branch named in the text of .git/HEAD.
 * @param text The file's text ("ref: refs/heads/main" or a commit's id).
 * @param[out] out The branch, or the first 7 digits of a detached HEAD.
 * @param size Size of @p out.
 * @return Non-zero if the text was understood.
 */
int gitstatus_head(const char *text, char *out, size_t size);

#endif
