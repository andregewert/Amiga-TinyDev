/**
 * @file test_projconf.c
 * @brief Host-native tests of the reader of a project's settings.
 */
#include "projconf.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Expand a text and compare the result.
 * @param text The text.
 * @param file The active document, or NULL.
 * @param expected The expected result.
 * @param expect_missing The expected missing flag.
 */
static void check_expand(const char *text, const char *file, const char *expected,
                         int expect_missing)
{
    int missing = -1;
    char *s = projconf_expand(text, "Work:My Project", file, &missing);
    assert(s != NULL);
    assert(strcmp(s, expected) == 0);
    assert(missing == expect_missing);
    free(s);
}

/**
 * @brief Host-native tests of the project settings: the three actions,
 *        names, drawers, command lines as string or array, the default
 *        command, skipped entries, refused files, and the placeholders.
 * @return 0 when all assertions pass.
 */
int main(void)
{
    ProjConf c;
    const char *json =
        "{\n"
        "  \"version\": 1,\n"
        "  \"editor\": { \"tabs\": 4 },\n"
        "  \"build\": [\n"
        "    { \"name\": \"Build\", \"command\": \"make\" },\n"
        "    { \"name\": \"Clean\", \"command\": [\"Delete build ALL\", \"Echo \\\"Done\\\"\"],"
        " \"default\": true, \"extra\": 1 },\n"
        "    { \"command\": \"make docs\", \"default\": true },\n"
        "    { \"name\": \"No command\" },\n"
        "    { \"name\": \"Empty\", \"command\": \"\" },\n"
        "    { \"name\": \"Empty list\", \"command\": [] },\n"
        "    { \"name\": \"Number\", \"command\": [\"a\", 2] },\n"
        "    \"make\"\n"
        "  ],\n"
        "  \"run\": [ { \"name\": \"Gr\xF6\xDF" "e\", \"command\": \"TinyDev\", \"dir\": \"bin\" } ],\n"
        "  \"debug\": { \"command\": \"not a list\" }\n"
        "}\n";

    assert(strcmp(projconf_action_key(PROJ_BUILD), "build") == 0);
    assert(strcmp(projconf_action_key(PROJ_RUN), "run") == 0);
    assert(strcmp(projconf_action_key(PROJ_DEBUG), "debug") == 0);

    projconf_init(&c);
    assert(projconf_default(&c, PROJ_BUILD) == -1);
    assert(projconf_parse(&c, json) == 1);

    /* build: three commands, invalid entries skipped */
    assert(c.count[PROJ_BUILD] == 3);
    assert(strcmp(c.commands[PROJ_BUILD][0].name, "Build") == 0);
    assert(c.commands[PROJ_BUILD][0].line_count == 1);
    assert(strcmp(c.commands[PROJ_BUILD][0].lines[0], "make") == 0);
    assert(c.commands[PROJ_BUILD][0].dir == NULL && !c.commands[PROJ_BUILD][0].is_default);
    assert(c.commands[PROJ_BUILD][1].line_count == 2);
    assert(strcmp(c.commands[PROJ_BUILD][1].lines[1], "Echo \"Done\"") == 0);
    /* without a name, the first line */
    assert(strcmp(c.commands[PROJ_BUILD][2].name, "make docs") == 0);
    /* the first marked one is the default */
    assert(projconf_default(&c, PROJ_BUILD) == 1);

    /* run: drawer, Latin-1 name, first command as default */
    assert(c.count[PROJ_RUN] == 1);
    assert(strcmp(c.commands[PROJ_RUN][0].name, "Gr\xF6\xDF" "e") == 0);
    assert(strcmp(c.commands[PROJ_RUN][0].dir, "bin") == 0);
    assert(projconf_default(&c, PROJ_RUN) == 0);

    /* debug: no list */
    assert(c.count[PROJ_DEBUG] == 0 && projconf_default(&c, PROJ_DEBUG) == -1);

    /* other texts are refused and change nothing */
    assert(projconf_parse(&c, "[1, 2]") == 0);
    assert(projconf_parse(&c, "not json") == 0);
    assert(projconf_parse(&c, "{\"build\": [") == 0);
    assert(c.count[PROJ_BUILD] == 3 && c.count[PROJ_RUN] == 1);

    /* an empty object replaces the settings */
    assert(projconf_parse(&c, "{}") == 1);
    assert(c.count[PROJ_BUILD] == 0 && c.count[PROJ_RUN] == 0);
    projconf_clear(&c);

    /* placeholders */
    check_expand("make", NULL, "make", 0);
    check_expand("cd \"{project}\"", NULL, "cd \"Work:My Project\"", 0);
    check_expand("vc -c {file} -o {base}.o", "Work:src/main.c", "vc -c Work:src/main.c -o Work:src/main.o", 0);
    check_expand("run {base}", "Work:a.b/name", "run Work:a.b/name", 0);
    check_expand("run {base}", "Work:src/.hidden", "run Work:src/.hidden", 0);
    check_expand("run {base}", "Work:x.tar.gz", "run Work:x.tar", 0);
    check_expand("vc {file}", NULL, "vc ", 1);
    check_expand("{base}{project}", NULL, "Work:My Project", 1);
    check_expand("{other} {proj {file", "f", "{other} {proj {file", 0);
    check_expand("", "f", "", 0);
    {
        char *s = projconf_expand("{file}", NULL, NULL, NULL);
        assert(s != NULL && s[0] == '\0');
        free(s);
    }

    puts("project settings tests passed");
    return 0;
}
