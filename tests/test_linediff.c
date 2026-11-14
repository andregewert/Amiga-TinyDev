/**
 * @file test_linediff.c
 * @brief Host tests of the line diff.
 */
#include "linediff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;    /**< failed checks */

/**
 * @brief Check the hunks of two texts.
 * @param old The old text.
 * @param new_text The new text.
 * @param expected The expected hunks.
 */
static void check(const char *old, const char *new_text, const char *expected)
{
    char *got = linediff_unified(old, strlen(old), new_text, strlen(new_text), 3);
    if (got == NULL || strcmp(got, expected) != 0) {
        printf("FAIL:\n--- old\n%s--- new\n%s--- expected\n%s--- got\n%s\n", old, new_text, expected,
               got != NULL ? got : "(null)");
        ++failures;
    }
    free(got);
}

/**
 * @brief Apply hunks to the old text and compare with the new one (for
 *        texts made at random).
 * @param old The old text.
 * @param new_text The new text.
 */
static void check_apply(const char *old, const char *new_text)
{
    char *hunks = linediff_unified(old, strlen(old), new_text, strlen(new_text), 2);
    char *out = malloc(strlen(old) + strlen(new_text) + 2), *o = out;
    const char *h = hunks, *src = old;
    long line = 0;
    if (hunks == NULL || out == NULL) { ++failures; free(hunks); free(out); return; }
    while (*h != '\0') {
        const char *nl = strchr(h, '\n');
        size_t len = (size_t)(nl - h);
        if (h[0] == '@') {
            long x = strtol(h + 4, NULL, 10), n = strtol(strchr(h, ',') + 1, NULL, 10);
            long start = n > 0 ? x - 1 : x;
            while (line < start) {          /* copy unchanged lines */
                const char *e = strchr(src, '\n');
                memcpy(o, src, (size_t)(e - src + 1)); o += e - src + 1; src = e + 1; ++line;
            }
        } else if (h[0] == ' ' || h[0] == '-') {
            const char *e = strchr(src, '\n');
            if ((size_t)(e - src) != len - 1 || memcmp(src, h + 1, len - 1) != 0) {
                printf("FAIL: hunk line does not match the old text\n");
                ++failures;
                break;
            }
            if (h[0] == ' ') { memcpy(o, src, (size_t)(e - src + 1)); o += e - src + 1; }
            src = e + 1;
            ++line;
        } else {
            memcpy(o, h + 1, len - 1); o += len - 1; *o++ = '\n';
        }
        h = nl + 1;
    }
    strcpy(o, src);
    if (strcmp(out, new_text) != 0) {
        printf("FAIL: applying the hunks gives\n%s\ninstead of\n%s\n", out, new_text);
        ++failures;
    }
    free(hunks);
    free(out);
}

/** @brief Run the tests. @return 0 if all passed. */
int main(void)
{
    int round;
    check("", "", "");
    check("a\nb\n", "a\nb\n", "");
    check("", "a\nb\n", "@@ -0,0 +1,2 @@\n+a\n+b\n");
    check("a\nb\n", "", "@@ -1,2 +0,0 @@\n-a\n-b\n");
    /* the change in the middle with three lines of context */
    check("1\n2\n3\n4\n5\n6\n7\n8\n9\n", "1\n2\n3\n4\nX\n6\n7\n8\n9\n",
          "@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+X\n 6\n 7\n 8\n");
    /* an inserted line at the start */
    check("a\nb\n", "new\na\nb\n", "@@ -1,2 +1,3 @@\n+new\n a\n b\n");
    /* a last line without '\n' */
    check("a\nb", "a\nc", "@@ -1,2 +1,2 @@\n a\n-b\n+c\n");
    /* two changes far apart give two hunks */
    check("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n", "X\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\nY\n",
          "@@ -1,4 +1,4 @@\n-1\n+X\n 2\n 3\n 4\n@@ -9,4 +9,4 @@\n 9\n 10\n 11\n-12\n+Y\n");
    /* two changes close together give one hunk */
    check("1\n2\n3\n4\n5\n6\n7\n", "X\n2\n3\n4\n5\n6\nY\n",
          "@@ -1,7 +1,7 @@\n-1\n+X\n 2\n 3\n 4\n 5\n 6\n-7\n+Y\n");
    /* a comment inserted, Latin-1 kept */
    check("int a;\n\nint b;\n", "int a;\n\n// Testkommentar \xE4\n\nint b;\n",
          "@@ -1,3 +1,5 @@\n int a;\n \n+// Testkommentar \xE4\n+\n int b;\n");
    /* CRLF: the '\r' belongs to the line */
    check("a\r\nb\r\n", "a\r\nc\r\n", "@@ -1,2 +1,2 @@\n a\r\n-b\r\n+c\r\n");

    /* random texts: the hunks turn the old text into the new one */
    srand(1);
    for (round = 0; round < 500; ++round) {
        char old[400], new_text[400];
        int i, n = rand() % 60, m = rand() % 60, k = 0;
        for (i = 0; i < n; ++i) { old[k++] = (char)('a' + rand() % 4); old[k++] = '\n'; }
        old[k] = '\0';
        k = 0;
        for (i = 0; i < m; ++i) { new_text[k++] = (char)('a' + rand() % 4); new_text[k++] = '\n'; }
        new_text[k] = '\0';
        check_apply(old, new_text);
    }

    if (failures == 0) printf("line diff tests passed\n");
    return failures != 0;
}
