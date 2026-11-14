/**
 * @file gitstatus.h
 * @brief Portable reading of am-git's status and diff: the lines of
 *        "status --porcelain", a file's or drawer's state, the branch of
 *        .git/HEAD, the kinds of a diff's lines, a value of .git/config.
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
    char index;         /**< the line's first letter: the index (' ', 'M', 'A', 'D', 'R', '?', ...) */
    char worktree;      /**< its second letter: the work tree */
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
 * Lines that are no status lines (messages) are left out. A deleted file
 * counts as changed (in the work tree) or staged (in the index); the tree
 * does not show it, but its drawer takes its state.
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

/** Kinds of the lines of a diff, for colouring them. */
enum GitDiffLine {
    GIT_DIFF_CONTEXT = 0,   /**< an unchanged line (or anything else) */
    GIT_DIFF_ADDED,         /**< "+..." */
    GIT_DIFF_REMOVED,       /**< "-..." */
    GIT_DIFF_HUNK,          /**< "@@ ... @@" */
    GIT_DIFF_HEADER         /**< "diff --git", "index", "---", "+++", "new file mode", ... */
};

/**
 * @brief The kind of a line of a unified diff.
 * @param line The line (need not be terminated after its end).
 * @param length Its length.
 * @param in_header Non-zero while in a file's header (before its first
 *        hunk), where "---" and "+++" name the files.
 * @return A GitDiffLine.
 */
int gitdiff_line_kind(const char *line, size_t length, int in_header);

/**
 * @brief Read a value of a git configuration file (.git/config).
 *
 * Sections and keys are compared ignoring case, subsections exactly
 * ([remote "origin"]). A value may be quoted ("..." with \" and \\);
 * a comment (# or ;) outside quotes and spaces around it are left out.
 *
 * @param text The file.
 * @param section The section, e.g. "remote".
 * @param sub The subsection, e.g. "origin", or NULL for none.
 * @param key The key, e.g. "url".
 * @param[out] out The value (the last one if it is there several times).
 * @param size Size of @p out.
 * @return Non-zero if the key is there (and its value fitted).
 */
int gitconfig_get(const char *text, const char *section, const char *sub, const char *key,
                  char *out, size_t size);

#endif
