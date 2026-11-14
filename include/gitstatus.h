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

/**
 * @brief Read a line of "am-git branch": "* name" for the current branch,
 *        "  name" for the others.
 * @param line The line (need not be terminated after its end; a CR at
 *        its end is left out).
 * @param length Its length.
 * @param[out] name The branch.
 * @param size Size of @p name.
 * @param[out] current Non-zero for the current branch.
 * @return Non-zero if the line names a branch (that fitted); messages
 *         such as "branch: ..." do not.
 */
int gitbranch_line(const char *line, size_t length, char *name, size_t size, int *current);

/**
 * @brief Whether a name is good for a new branch.
 *
 * Git's rules (git check-ref-format), plus what the Shell and AmigaDOS
 * would take amiss: no spaces or control characters, none of
 * ~ ^ : ? * [ \ # ( ) | % ' " ` ; no leading '-', no "..", "//",
 * "@{", no part starting with '.' or ending in ".lock", not "@", no
 * '/' or '.' at the end. Characters above 127 are allowed.
 *
 * @param name The name.
 * @return Non-zero if it is good.
 */
int gitbranch_valid(const char *name);

/**
 * @brief The commit a ref points at in a packed-refs file
 *        (lines "<sha> <ref>"; for an annotated tag, the commit of the
 *        "^<sha>" line after it).
 * @param text The file (.git/packed-refs).
 * @param ref The ref, e.g. "refs/heads/main".
 * @param[out] sha Its commit (40 hex digits).
 * @param size Size of @p sha (at least 41).
 * @return Non-zero if the ref is there.
 */
int gitref_packed(const char *text, const char *ref, char *sha, size_t size);

/**
 * @brief Encode an ISO-8859-1 text as UTF-8, as git keeps names and
 *        messages in commits.
 * @param in The text.
 * @param[out] out The UTF-8 text (twice as long at most).
 * @param size Size of @p out.
 * @return Non-zero if it fitted.
 */
int gittext_to_utf8(const char *in, char *out, size_t size);

/**
 * @brief Decode a UTF-8 text to ISO-8859-1, in place.
 *
 * Characters beyond ISO-8859-1 become '?'. Bytes that are no valid UTF-8
 * stay as they are: a name or message written in ISO-8859-1 (as by
 * am-git, which passes bytes on) still reads right.
 *
 * @param s The text; it gets no longer.
 */
void gittext_from_utf8(char *s);

/** A commit of "am-git log". */
typedef struct GitLogEntry {
    char sha[41];               /**< the commit */
    char *author;               /**< its author's name (allocated) */
    long time;                  /**< when it was written (seconds since 1970, UTC), -1 if not known */
    int tz;                     /**< the author's time zone (minutes east of UTC) */
    char *subject;              /**< the first line of its message (allocated) */
} GitLogEntry;

/** The commits of "am-git log", newest first. */
typedef struct GitLog {
    GitLogEntry *entries;       /**< the commits (allocated) */
    long count;                 /**< their number */
} GitLog;

/**
 * @brief Make a log empty (before its first use).
 * @param log The log.
 */
void gitlog_init(GitLog *log);

/**
 * @brief Free a log's commits; it is empty afterwards.
 * @param log The log.
 */
void gitlog_clear(GitLog *log);

/**
 * @brief Read the output of "am-git log" (git's medium format: "commit
 *        <sha>", "Author: ...", the message indented by four spaces).
 *
 * Other header lines (such as "Date:") and messages ("log: ...") are
 * left out. Author and subject are decoded with gittext_from_utf8().
 *
 * @param log The log (its commits are replaced).
 * @param text The output.
 * @return Non-zero on success, 0 if out of memory (the log is empty then).
 */
int gitlog_parse(GitLog *log, const char *text);

/**
 * @brief Split an author as git stores it: "Name <email> seconds +hhmm".
 * @param raw The author.
 * @param[out] name The name (the text before '<', without spaces at its end).
 * @param size Size of @p name.
 * @param[out] time The seconds since 1970 (UTC), -1 if there are none.
 * @param[out] tz The time zone in minutes east of UTC, 0 if there is none.
 */
void gitlog_author(const char *raw, char *name, size_t size, long *time, int *tz);

/**
 * @brief A time as "DD.MM.YYYY HH:MM" in a time zone.
 * @param time Seconds since 1970 (UTC); "" if negative.
 * @param tz The time zone in minutes east of UTC.
 * @param[out] out The text.
 * @param size Size of @p out (17 at least).
 */
void gitlog_date(long time, int tz, char *out, size_t size);

/** A file a commit changed ("am-git show"). */
typedef struct GitShowFile {
    char status;                /**< 'A' added, 'M' changed, 'D' deleted */
    char *path;                 /**< the file, relative to the repository (allocated) */
} GitShowFile;

/** What "am-git show <commit>" says of a commit. */
typedef struct GitShow {
    char parent[41];            /**< its first parent, "" for the first commit */
    GitShowFile *files;         /**< the files it changed (allocated) */
    long count;                 /**< their number */
} GitShow;

/**
 * @brief Make a show empty (before its first use).
 * @param show The show.
 */
void gitshow_init(GitShow *show);

/**
 * @brief Free a show's files; it is empty afterwards.
 * @param show The show.
 */
void gitshow_clear(GitShow *show);

/**
 * @brief Read the output of "am-git show <commit>": the "parent <sha>"
 *        lines of its header and the "X<tab>path" lines of the files.
 * @param show The show (replaced).
 * @param text The output.
 * @return Non-zero on success, 0 if out of memory (the show is empty then).
 */
int gitshow_parse(GitShow *show, const char *text);

#endif
