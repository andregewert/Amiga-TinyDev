/**
 * @file test_comment.c
 * @brief Host tests of Edit/Comment Lines' core.
 */
#include "comment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;    /**< failed checks */
static int tests = 0;       /**< checks made */

/**
 * @brief Make a comment form.
 * @param open The opening delimiter.
 * @param close The closing one ("" for none).
 * @param column It must stand in the first column.
 * @return The form.
 */
static CommentStyle style(const char *open, const char *close, int column)
{
    CommentStyle s;
    memset(&s, 0, sizeof(s));
    strcpy(s.open, open);
    strcpy(s.close, close);
    s.column = column;
    return s;
}

/**
 * @brief Check toggling some lines, and that toggling back gives them again.
 * @param s The comment form.
 * @param in The lines.
 * @param expected The expected result.
 * @param commented 1 if they are to be commented.
 * @param back Non-zero to check toggling back.
 */
static void check(CommentStyle s, const char *in, const char *expected, int commented, int back)
{
    int c = -1, c2 = -1;
    char *out = comment_toggle(in, strlen(in), &s, &c), *again;
    ++tests;
    if (out == NULL || strcmp(out, expected) != 0 || c != commented) {
        printf("FAIL: \"%s\" -> \"%s\" (%d), expected \"%s\" (%d)\n", in, out ? out : "(null)", c,
               expected, commented);
        ++failures;
    } else if (back) {
        again = comment_toggle(out, strlen(out), &s, &c2);
        if (again == NULL || strcmp(again, in) != 0 || c2 == c) {
            printf("FAIL back: \"%s\" -> \"%s\"\n", out, again ? again : "(null)");
            ++failures;
        }
        free(again);
    }
    free(out);
}

/**
 * @brief Run the tests.
 * @return 0 if all passed.
 */
int main(void)
{
    CommentStyle cpp = style("//", "", 0), semi = style(";", "", 0), block = style("/*", "*/", 0),
                 star = style("*", "", 1), html = style("<!--", "-->", 0);
    SyntaxRules r;

    check(cpp, "int x;", "// int x;", 1, 1);
    check(cpp, "    a();\n        b();", "    // a();\n    //     b();", 1, 1);
    check(cpp, "    // a();\n    //     b();", "    a();\n        b();", 0, 0);
    check(cpp, "a();\n\nb();", "// a();\n\n// b();", 1, 1);
    check(cpp, "// a();\nb();", "// // a();\n// b();", 1, 1);   /* not all: comment all */
    check(cpp, "//a();", "a();", 0, 0);                          /* no blank after it */
    check(cpp, "\t\tx = 1;\t", "\t\t// x = 1;\t", 1, 1);
    check(cpp, "", "", 1, 0);
    check(cpp, "   \n", "   \n", 1, 0);
    check(semi, "        move.l d0,d1", "        ; move.l d0,d1", 1, 1);
    check(star, "  nop", "*   nop", 1, 1);
    check(star, "* nop", "nop", 0, 0);
    check(block, "say 'hi'\n  exit", "/* say 'hi' */\n/*   exit */", 1, 1);
    check(block, "  say 'hi'  ", "  /* say 'hi' */  ", 1, 1);
    check(html, "<p>Text</p>", "<!-- <p>Text</p> -->", 1, 1);
    check(style("REM", "", 0), "PRINT 1", "REM PRINT 1", 1, 1);

    memset(&r, 0, sizeof(r));
    ++tests;
    if (comment_style(&r, &cpp)) { printf("FAIL: no comments\n"); ++failures; }
    strcpy(r.blocks[0].open, "/*");
    strcpy(r.blocks[0].close, "*/");
    r.block_count = 1;
    ++tests;
    if (!comment_style(&r, &cpp) || strcmp(cpp.open, "/*") != 0 || strcmp(cpp.close, "*/") != 0) {
        printf("FAIL: block\n"); ++failures;
    }
    strcpy(r.line_comments[0], "//");
    r.line_comment_count = 1;
    ++tests;
    if (!comment_style(&r, &cpp) || strcmp(cpp.open, "//") != 0 || cpp.close[0] != '\0') {
        printf("FAIL: line\n"); ++failures;
    }
    ++tests;
    if (comment_style(NULL, &cpp)) { printf("FAIL: NULL\n"); ++failures; }

    if (failures == 0) printf("comment tests passed (%d)\n", tests);
    return failures != 0;
}
