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
 * @brief Editing commands and writing the file: copy, add, change, remove,
 *        move, default, the text of a command, unknown keys kept, a new
 *        file, and reading back what was written.
 */
static void test_edit_and_format(void)
{
    ProjConf c, d;
    char *text, *json, *again;
    const char *old =
        "{\n"
        "  \"version\": 1,\n"
        "  \"editor\": { \"tabs\": 4 },\n"
        "  \"build\": [\n"
        "    { \"name\": \"Build\", \"command\": \"make\", \"env\": \"x\" },\n"
        "    { \"command\": \"make docs\", \"default\": true }\n"
        "  ],\n"
        "  \"later\": [1, 2.5]\n"
        "}\n";

    projconf_init(&c);
    assert(projconf_parse(&c, old) == 1);

    /* copy */
    assert(projconf_copy(&d, &c));
    assert(d.count[PROJ_BUILD] == 2 && strcmp(d.commands[PROJ_BUILD][1].name, "make docs") == 0);
    assert(d.commands[PROJ_BUILD][1].is_default);
    projconf_clear(&d);

    /* change: blank lines and CRs dropped, extra keys kept */
    assert(projconf_set(&c, PROJ_BUILD, 0, "Build all", "make\r\n  \nmake test\n\n", "", 1) == 1);
    assert(c.commands[PROJ_BUILD][0].line_count == 2);
    assert(strcmp(c.commands[PROJ_BUILD][0].lines[1], "make test") == 0);
    assert(c.commands[PROJ_BUILD][0].dir == NULL);
    assert(projconf_default(&c, PROJ_BUILD) == 0 && !c.commands[PROJ_BUILD][1].is_default);
    text = projconf_command_text(&c.commands[PROJ_BUILD][0]);
    assert(text != NULL && strcmp(text, "make\nmake test") == 0);
    free(text);

    /* empty commands are refused */
    assert(projconf_set(&c, PROJ_BUILD, 0, "x", " \n\t\n", NULL, 0) == 0);
    assert(projconf_set(&c, PROJ_BUILD, -1, "x", "", NULL, 0) == 0);
    assert(projconf_set(&c, PROJ_BUILD, -1, "x", NULL, NULL, 0) == 0);
    assert(c.count[PROJ_BUILD] == 2);

    /* add */
    assert(projconf_set(&c, PROJ_RUN, -1, NULL, "TinyDev", "bin", 0) == 1);
    assert(projconf_set(&c, PROJ_RUN, -1, "Gr\xF6\xDF" "e", "TinyDev LARGE", NULL, 1) == 1);
    assert(c.count[PROJ_RUN] == 2 && strcmp(c.commands[PROJ_RUN][0].name, "TinyDev") == 0);
    assert(projconf_default(&c, PROJ_RUN) == 1);
    assert(projconf_set(&c, PROJ_DEBUG, -1, "Debugger", "BDebug bin/TinyDev", NULL, 0) == 1);

    /* move and remove */
    assert(projconf_move(&c, PROJ_RUN, 1, -1) == 0);
    assert(projconf_move(&c, PROJ_RUN, 0, -1) == 0);
    assert(projconf_move(&c, PROJ_RUN, 1, 1) == 1);
    assert(strcmp(c.commands[PROJ_RUN][0].name, "Gr\xF6\xDF" "e") == 0);
    projconf_set_default(&c, PROJ_RUN, 1);
    assert(projconf_default(&c, PROJ_RUN) == 1 && !c.commands[PROJ_RUN][0].is_default);
    projconf_remove(&c, PROJ_DEBUG, 0);
    assert(c.count[PROJ_DEBUG] == 0 && c.commands[PROJ_DEBUG] == NULL);

    /* write: the lists replace the old ones, the rest stays */
    json = projconf_format(&c, old);
    assert(json != NULL);
    assert(strcmp(json,
        "{\n"
        "  \"version\": 1,\n"
        "  \"editor\": { \"tabs\": 4 },\n"
        "  \"build\": [\n"
        "    { \"name\": \"Build all\", \"command\": [\"make\", \"make test\"], \"env\": \"x\", \"default\": true },\n"
        "    { \"command\": \"make docs\" }\n"
        "  ],\n"
        "  \"later\": [1, 2.5],\n"
        "  \"run\": [\n"
        "    { \"name\": \"Gr\xF6\xDF" "e\", \"command\": \"TinyDev LARGE\" },\n"
        "    { \"command\": \"TinyDev\", \"dir\": \"bin\", \"default\": true }\n"
        "  ],\n"
        "  \"debug\": []\n"
        "}\n") == 0);

    /* read back and write again: the same text */
    projconf_init(&d);
    assert(projconf_parse(&d, json) == 1);
    assert(d.count[PROJ_BUILD] == 2 && d.count[PROJ_RUN] == 2 && projconf_default(&d, PROJ_RUN) == 1);
    again = projconf_format(&d, json);
    assert(again != NULL && strcmp(again, json) == 0);
    free(again);
    projconf_clear(&d);
    free(json);

    /* a new file; other texts are refused */
    json = projconf_format(&c, NULL);
    assert(json != NULL && strncmp(json, "{\n  \"version\": 1,\n  \"build\": [\n", 31) == 0);
    assert(strstr(json, "\"debug\": []\n}\n") != NULL);
    free(json);
    json = projconf_format(&c, "");
    assert(json != NULL);
    free(json);
    assert(projconf_format(&c, "[1]") == NULL);
    assert(projconf_format(&c, "{") == NULL);

    projconf_clear(&c);
}

/**
 * @brief The include drawers: read, add, change, move, remove, full paths,
 *        written only when there are some or the file had them.
 */
static void test_include(void)
{
    ProjConf c, d;
    char *json, *full;
    projconf_init(&c);
    assert(projconf_parse(&c, "{\"include\": [\"include\", 3, \"NDK:Include_H\"]}") == 1);
    assert(c.include_count == 2 && strcmp(c.include[1], "NDK:Include_H") == 0);
    full = projconf_include_full(&c, 0, "Work:Proj");
    assert(full != NULL && strcmp(full, "Work:Proj/include") == 0);
    free(full);
    full = projconf_include_full(&c, 0, "Work:");
    assert(full != NULL && strcmp(full, "Work:include") == 0);
    free(full);
    full = projconf_include_full(&c, 1, "Work:Proj");
    assert(full != NULL && strcmp(full, "NDK:Include_H") == 0);
    free(full);
    assert(projconf_include_set(&c, -1, "") == 0);
    assert(projconf_include_set(&c, -1, "sdk") == 1 && c.include_count == 3);
    assert(projconf_include_set(&c, 0, "inc") == 1 && strcmp(c.include[0], "inc") == 0);
    assert(projconf_include_move(&c, 2, -1) == 1 && strcmp(c.include[1], "sdk") == 0);
    assert(projconf_include_move(&c, 0, -1) == 0);
    assert(projconf_copy(&d, &c) && d.include_count == 3 && strcmp(d.include[2], "NDK:Include_H") == 0);
    projconf_clear(&d);
    json = projconf_format(&c, "{\"version\": 1}");
    assert(json != NULL && strstr(json, "\"include\": [\"inc\", \"sdk\", \"NDK:Include_H\"]") != NULL);
    free(json);
    projconf_include_remove(&c, 0);
    projconf_include_remove(&c, 0);
    projconf_include_remove(&c, 0);
    assert(c.include_count == 0);
    /* none, and the file had none: no key; the file had some: an empty list */
    json = projconf_format(&c, "{\"version\": 1}");
    assert(json != NULL && strstr(json, "include") == NULL);
    free(json);
    json = projconf_format(&c, "{\"version\": 1, \"include\": [\"x\"]}");
    assert(json != NULL && strstr(json, "\"include\": []") != NULL);
    free(json);
    projconf_clear(&c);
}

/**
 * @brief The chosen languages: read, set, change, remove, written only
 *        when there are some or the file had them; paths below the project.
 */
static void test_languages(void)
{
    ProjConf c, d;
    char *json;
    projconf_init(&c);
    assert(projconf_parse(&c, "{\"languages\": {\"s/Startup-Sequence\": \"amigados\", \"x\": 3, \"notes\": \"\"}}") == 1);
    assert(c.lang_count == 2);
    assert(strcmp(projconf_language(&c, "S/startup-sequence"), "amigados") == 0);
    assert(strcmp(projconf_language(&c, "notes"), "") == 0 && projconf_language(&c, "x") == NULL);
    assert(projconf_language_set(&c, "notes", "c") == 1 && c.lang_count == 2);
    assert(strcmp(projconf_language(&c, "notes"), "c") == 0);
    assert(projconf_language_set(&c, "build", "amigados") == 1 && c.lang_count == 3);
    assert(projconf_copy(&d, &c) && d.lang_count == 3 && strcmp(projconf_language(&d, "build"), "amigados") == 0);
    projconf_clear(&d);
    json = projconf_format(&c, "{\"version\": 1}");
    assert(json != NULL && strstr(json, "\"s/Startup-Sequence\": \"amigados\"") != NULL);
    assert(strstr(json, "\"build\": \"amigados\"") != NULL);
    free(json);
    assert(projconf_language_set(&c, "nothing", NULL) == 1 && c.lang_count == 3);
    assert(projconf_language_set(&c, "notes", NULL) == 1 && projconf_language_set(&c, "build", NULL) == 1);
    assert(projconf_language_set(&c, "S/Startup-Sequence", NULL) == 1 && c.lang_count == 0);
    json = projconf_format(&c, "{\"version\": 1}");
    assert(json != NULL && strstr(json, "languages") == NULL);
    free(json);
    json = projconf_format(&c, "{\"version\": 1, \"languages\": {\"a\": \"c\"}}");
    assert(json != NULL && strstr(json, "\"languages\": {}") != NULL);
    free(json);
    projconf_clear(&c);

    assert(strcmp(projconf_relative("Work:Proj", "work:proj/s/x"), "s/x") == 0);
    assert(strcmp(projconf_relative("Work:", "Work:s/x"), "s/x") == 0);
    assert(strcmp(projconf_relative("Work:Proj/", "Work:Proj/a"), "a") == 0);
    assert(projconf_relative("Work:Proj", "Work:Project/a") == NULL);
    assert(projconf_relative("Work:Proj", "Work:Proj") == NULL);
    assert(projconf_relative("", "Work:a") == NULL);
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

    test_edit_and_format();
    test_include();
    test_languages();
    puts("project settings tests passed");
    return 0;
}
