/**
 * @file test_buildmsg.c
 * @brief Host tests of the reader of compiler messages.
 */
#include "buildmsg.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;    /**< failed checks */
static int tests = 0;       /**< checks made */

/**
 * @brief Check the place found in a line.
 * @param text The line.
 * @param file The expected file ("" for none), NULL if no place is expected.
 * @param line The expected line.
 * @param col The expected column.
 */
static void check(const char *text, const char *file, long line, long col)
{
    BuildMessage m;
    int found = buildmsg_parse(text, strlen(text), &m);
    ++tests;
    if (file == NULL) {
        if (found) {
            printf("FAIL: \"%s\": found %s %ld %ld\n", text, m.file, m.line, m.col);
            ++failures;
        }
    } else if (!found || strcmp(m.file, file) != 0 || m.line != line || m.col != col) {
        printf("FAIL: \"%s\": expected %s %ld %ld, got %d %s %ld %ld\n", text, file, line, col,
               found, m.file, m.line, m.col);
        ++failures;
    }
}

/**
 * @brief Run the tests.
 * @return 0 if all passed.
 */
int main(void)
{
    /* gcc */
    check("hello.c:12:5: error: 'x' undeclared (first use in this function)", "hello.c", 12, 5);
    check("hello.c:7: warning: implicit declaration of function `printf'", "hello.c", 7, 0);
    check("Work:Projects/Hello/hello.c:3:1: error: expected ';'", "Work:Projects/Hello/hello.c", 3, 1);
    check("In file included from main.c:4:", "main.c", 4, 0);
    check("                 from lib/util.h:10,", "lib/util.h", 10, 0);
    check("hello.c: In function `main':", NULL, 0, 0);
    check("make: *** [Makefile:12: hello] Error 1", "Makefile", 12, 0);
    check("Started at 12:30:45 today", NULL, 0, 0);
    /* vbcc and vasm */
    check("error 18 in line 12 of \"hello.c\": unknown identifier <x>", "hello.c", 12, 0);
    check("warning 214 in line 5 of \"src/x.c\": suspicious format string", "src/x.c", 5, 0);
    check("error 2 in line 40 of \"Hello.s\": unknown mnemonic <mvoe>", "Hello.s", 40, 0);
    check("        included from line 3 of \"main.s\"", "main.s", 3, 0);
    check(">        x = 1;", NULL, 0, 0);
    /* BAsm */
    check("Error in Line 35 in 'HelloAsm.s'.", "HelloAsm.s", 35, 0);
    check("Warning in Line 7 in 'inc/macros.i'.", "inc/macros.i", 7, 0);
    check("Error 12: Can't find symbol.", NULL, 0, 0);
    check("Errors: 1 Warnings: 0", NULL, 0, 0);
    /* SAS/C */
    check("HelloWorld3.c 8 Error 3: file not found \"<stdio.h>\"", "HelloWorld3.c", 8, 0);
    check("HelloWorld3.c 12 Warning 100: no prototype declared for function \"printf\"",
          "HelloWorld3.c", 12, 0);
    check("sc fehlgeschlagen R\xfc" "ckgabewert 20", NULL, 0, 0);
    /* Free Pascal */
    check("hello.pas(12,5) Error: Identifier not found \"x\"", "hello.pas", 12, 5);
    check("util.pas(3) Fatal: Syntax error", "util.pas", 3, 0);
    check("Hello.pas(7,1) Note: Local variable \"i\" not used", "Hello.pas", 7, 1);
    check("Compiling hello.pas", NULL, 0, 0);
    /* E-VO and ec */
    check("ERROR: LINE 12: unknown identifier", "", 12, 0);
    check("LINE 5: WriteF('hi')", "", 5, 0);
    check("WARNING: code outside PROCs (line 3)", "", 3, 0);
    check("ERROR: syntax error", NULL, 0, 0);
    /* others */
    check("", NULL, 0, 0);
    check("Build finished.", NULL, 0, 0);
    check("x.c:0: odd", NULL, 0, 0);
    if (failures == 0) printf("build message tests passed (%d)\n", tests);
    return failures != 0;
}
