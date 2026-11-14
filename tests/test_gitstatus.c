/**
 * @file test_gitstatus.c
 * @brief Host-native tests of the reading of am-git's status.
 */
#include "gitstatus.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief The output of am-git 0.12.1 as seen on the Amiga, and git's forms.
 */
static void test_parse(void)
{
    GitStatus st;
    gitstatus_init(&st);
    /* am-git 0.12.1 (a file name with an ISO-8859-1 umlaut) */
    assert(gitstatus_parse(&st, "A  README.md\n M d\xF6ku.md\n?? test.md\n") == 1);
    assert(st.count == 3);
    assert(gitstatus_state(&st, "README.md", 0) == GIT_ST_STAGED);
    assert(gitstatus_state(&st, "D\xD6KU.md", 0) == GIT_ST_MODIFIED);   /* case ignored */
    assert(gitstatus_state(&st, "test.md", 0) == GIT_ST_UNTRACKED);
    assert(gitstatus_state(&st, "other.md", 0) == GIT_ST_CLEAN);
    assert(gitstatus_state(&st, "", 1) == GIT_ST_MODIFIED);             /* the root */

    /* drawers, renames, quotes, deleted files, messages, CRLF */
    assert(gitstatus_parse(&st,
        "warning: something\r\n"
        "MM src/main.c\r\n"
        "M  src/sub/a.c\n"
        "R  old.c -> src/new.c\n"
        "?? build/\n"
        " D gone.c\n"
        "D  removed.c\n"
        "UU merge.c\n"
        "?? \"sp ace\\tx\\303.c\"\n") == 1);
    assert(st.count == 8);
    assert(gitstatus_state(&st, "src/main.c", 0) == GIT_ST_MODIFIED);
    {
        long k;
        for (k = 0; k < st.count && strcmp(st.entries[k].path, "src/main.c") != 0; ++k) ;
        assert(k < st.count && st.entries[k].index == 'M' && st.entries[k].worktree == 'M');
    }
    assert(gitstatus_state(&st, "src/new.c", 0) == GIT_ST_STAGED);
    assert(gitstatus_state(&st, "old.c", 0) == GIT_ST_CLEAN);
    assert(gitstatus_state(&st, "gone.c", 0) == GIT_ST_MODIFIED);
    assert(gitstatus_state(&st, "removed.c", 0) == GIT_ST_STAGED);
    assert(gitstatus_state(&st, "merge.c", 0) == GIT_ST_CONFLICT);
    assert(gitstatus_state(&st, "sp ace\tx\303.c", 0) == GIT_ST_UNTRACKED);
    assert(gitstatus_state(&st, "src", 1) == GIT_ST_MODIFIED);
    assert(gitstatus_state(&st, "src/sub", 1) == GIT_ST_STAGED);
    assert(gitstatus_state(&st, "sr", 1) == GIT_ST_CLEAN);              /* not a prefix of "src/" */
    assert(gitstatus_state(&st, "build", 1) == GIT_ST_UNTRACKED);
    assert(gitstatus_state(&st, "build/x/y.o", 0) == GIT_ST_UNTRACKED);
    assert(gitstatus_state(&st, "build/x", 1) == GIT_ST_UNTRACKED);
    assert(gitstatus_state(&st, "", 1) == GIT_ST_CONFLICT);

    /* nothing changed, or no status at all */
    assert(gitstatus_parse(&st, "") == 1 && st.count == 0);
    assert(gitstatus_state(&st, "a", 0) == GIT_ST_CLEAN);
    assert(gitstatus_parse(&st, "fatal: not a git repository\n") == 1 && st.count == 0);
    gitstatus_clear(&st);
}

/**
 * @brief Paths relative to the repository.
 */
static void test_relative(void)
{
    assert(strcmp(gitstatus_relative("Work:Projects/Foo", "Work:Projects/Foo/src/a.c"), "src/a.c") == 0);
    assert(strcmp(gitstatus_relative("work:projects/foo", "Work:Projects/Foo/a.c"), "a.c") == 0);
    assert(strcmp(gitstatus_relative("Work:Projects/Foo", "Work:Projects/Foo"), "") == 0);
    assert(gitstatus_relative("Work:Projects/Foo", "Work:Projects/FooBar/a.c") == NULL);
    assert(gitstatus_relative("Work:Projects/Foo", "Work:Other/a.c") == NULL);
    assert(strcmp(gitstatus_relative("Dh1:", "Dh1:src/a.c"), "src/a.c") == 0);
    assert(gitstatus_relative("", "Dh1:a") == NULL);
}

/**
 * @brief The branch of .git/HEAD.
 */
static void test_head(void)
{
    char b[16];
    assert(gitstatus_head("ref: refs/heads/main\n", b, sizeof(b)) && strcmp(b, "main") == 0);
    assert(gitstatus_head("ref: refs/heads/feature/x\r\n", b, sizeof(b)) && strcmp(b, "feature/x") == 0);
    assert(gitstatus_head("ref: refs/heads/a-very-long-branch-name\n", b, sizeof(b)) && strlen(b) == 15);
    assert(gitstatus_head("0123456789abcdef0123456789abcdef01234567\n", b, sizeof(b)) && strcmp(b, "0123456") == 0);
    assert(!gitstatus_head("nonsense", b, sizeof(b)) && b[0] == '\0');
    assert(!gitstatus_head("ref: \n", b, sizeof(b)));
}

/**
 * @brief Diffs: the kinds of lines.
 */
static void test_diff(void)
{
    assert(gitdiff_line_kind("@@ -1 +1 @@", 11, 1) == GIT_DIFF_HUNK);
    assert(gitdiff_line_kind("diff --git a/x b/x", 18, 0) == GIT_DIFF_HEADER);
    assert(gitdiff_line_kind("--- a/x", 7, 1) == GIT_DIFF_HEADER);
    assert(gitdiff_line_kind("--- a/x", 7, 0) == GIT_DIFF_REMOVED);
    assert(gitdiff_line_kind("+y", 2, 0) == GIT_DIFF_ADDED);
    assert(gitdiff_line_kind(" z", 2, 0) == GIT_DIFF_CONTEXT);
    assert(gitdiff_line_kind("", 0, 0) == GIT_DIFF_CONTEXT);
}

/**
 * @brief Values of .git/config.
 */
static void test_config(void)
{
    static const char cfg[] =
        "[core]\n"
        "\trepositoryformatversion = 0\n"
        "[remote \"upstream\"]\n"
        "\turl = https://example.org/other.git\n"
        "[Remote \"origin\"]\n"
        "\tURL = https://github.com/user/repo.git ; a comment\n"
        "\tfetch = +refs/heads/*:refs/remotes/origin/*\n"
        "[branch \"main\"]\n"
        "\tremote = origin\n"
        "[user]\n"
        "\tname = \"Andr\xE9 \\\"G\\\" # not a comment\"\r\n";
    char v[64];
    assert(gitconfig_get(cfg, "remote", "origin", "url", v, sizeof(v)) &&
           strcmp(v, "https://github.com/user/repo.git") == 0);
    assert(gitconfig_get(cfg, "remote", "upstream", "url", v, sizeof(v)) &&
           strcmp(v, "https://example.org/other.git") == 0);
    assert(!gitconfig_get(cfg, "remote", "ORIGIN", "url", v, sizeof(v)));
    assert(!gitconfig_get(cfg, "remote", NULL, "url", v, sizeof(v)));
    assert(gitconfig_get(cfg, "core", NULL, "repositoryformatversion", v, sizeof(v)) && strcmp(v, "0") == 0);
    assert(gitconfig_get(cfg, "user", NULL, "name", v, sizeof(v)) &&
           strcmp(v, "Andr\xE9 \"G\" # not a comment") == 0);
    assert(!gitconfig_get(cfg, "remote", "origin", "url", v, 10));
    assert(!gitconfig_get("", "remote", "origin", "url", v, sizeof(v)));
}

/**
 * @brief Run the tests.
 * @return 0.
 */
int main(void)
{
    test_parse();
    test_relative();
    test_head();
    test_diff();
    test_config();
    puts("git status tests passed");
    return 0;
}
