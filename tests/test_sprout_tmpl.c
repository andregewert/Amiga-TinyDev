/**
 * @file test_sprout_tmpl.c
 * @brief Host-native tests of Sprout's core: metadata, values, placeholders
 *        and paths.
 */
#include "sprout_tmpl.h"
#include "json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Errors reported since reset(). */
static int errors;
/** Warnings reported since reset(). */
static int warnings;
/** The last message reported. */
static char last[256];

/**
 * @brief Report callback of the tests: counts and keeps the message.
 * @param user Unused.
 * @param error Non-zero for an error.
 * @param text The message.
 */
static void report(void *user, int error, const char *text)
{
    (void)user;
    if (error) ++errors;
    else ++warnings;
    strncpy(last, text, sizeof(last) - 1);
}

/** @brief Forget the messages reported so far. */
static void reset(void)
{
    errors = warnings = 0;
    last[0] = '\0';
}

/**
 * @brief Expand a text and compare the result.
 * @param v The values.
 * @param text The text.
 * @param expected The expected result.
 */
static void check_expand(const SproutValues *v, const char *text, const char *expected)
{
    size_t n = 0;
    char *s = sprout_expand(text, strlen(text), v, &n);
    assert(s != NULL);
    if (strcmp(s, expected) != 0) {
        printf("expand \"%s\": \"%s\", expected \"%s\"\n", text, s, expected);
        assert(0);
    }
    assert(n == strlen(expected));
    free(s);
}

static void check_path_bad(const SproutValues *v, const char *path, const char *expected, int why);

/** @brief The standard keys AUTHOR and AUTHORMAIL, declared or not. */
static void test_standard(void)
{
    SproutTemplate t;
    SproutValues given, out;
    const char *json =
        "{ \"name\": \"T\", \"placeholders\": [\n"
        "  { \"key\": \"AUTHOR\", \"default\": \"Nobody\" },\n"
        "  { \"key\": \"LINE\", \"default\": \"{$AUTHOR} <{$AUTHORMAIL}>\" } ] }";
    assert(sprout_key_standard("AUTHOR") && sprout_key_standard("AUTHORMAIL"));
    assert(!sprout_key_standard("PROJECT") && !sprout_key_builtin("AUTHOR"));
    assert(sprout_template_parse(&t, json, report, NULL) == 1);
    sprout_values_init(&given);
    reset();
    assert(sprout_resolve(&t, &given, "x", 0, &out, report, NULL) == 1);
    assert(errors == 0 && warnings == 0);
    assert(strcmp(sprout_values_get(&out, "AUTHOR"), "Nobody") == 0);
    assert(strcmp(sprout_values_get(&out, "AUTHORMAIL"), "") == 0);
    assert(strcmp(sprout_values_get(&out, "LINE"), "Nobody <>") == 0);
    sprout_values_free(&out);
    /* given ones win and are no warning, declared or not */
    assert(sprout_values_parse_assign(&given, "AUTHOR=Me") == 1);
    assert(sprout_values_parse_assign(&given, "authormail=me@example.org") == 1);
    assert(sprout_resolve(&t, &given, "x", 0, &out, report, NULL) == 1);
    assert(errors == 0 && warnings == 0);
    assert(strcmp(sprout_values_get(&out, "LINE"), "Me <me@example.org>") == 0);
    sprout_values_free(&out);
    sprout_values_free(&given);
    sprout_template_free(&t);
}

/**
 * @brief Expand a path and compare the result.
 * @param v The values.
 * @param path The path.
 * @param expected The expected result, or NULL if it must be refused.
 */
static void check_path(const SproutValues *v, const char *path, const char *expected)
{
    check_path_bad(v, path, expected, SPROUT_PATH_UNSAFE);
}

/**
 * @brief Expand a path and compare the result or the reason of a refusal.
 * @param v The values.
 * @param path The path.
 * @param expected The expected result, or NULL if it must be refused.
 * @param why The expected reason of a refusal (SPROUT_PATH_...).
 */
static void check_path_bad(const SproutValues *v, const char *path, const char *expected, int why)
{
    int bad = -1;
    char *s = sprout_expand_path(path, v, &bad);
    if (expected == NULL) {
        assert(s == NULL && bad == why);
        return;
    }
    assert(s != NULL && bad == 0);
    assert(strcmp(s, expected) == 0);
    free(s);
}

/** @brief Reading template.json: a full one, defaults, invalid ones. */
static void test_parse(void)
{
    SproutTemplate t;
    const char *full =
        "{ \"version\": 1, \"kind\": \"project\", \"name\": \"ReAction program\",\n"
        "  \"description\": \"A window\", \"category\": \"C\",\n"
        "  \"icon\": { \"type\": \"program\", \"file\": \"template.info\" },\n"
        "  \"placeholders\": [\n"
        "    { \"key\": \"NAME\", \"label\": \"Program name\", \"default\": \"{$PROJECT}\" },\n"
        "    { \"key\": \"AUTHOR\", \"required\": true, \"description\": \"Who\" },\n"
        "    { \"key\": \"CC\", \"choices\": [\"gcc\", \"vbcc\"] } ],\n"
        "  \"open\": [\"src/{$NAME}.c\", \"Makefile\"], \"verbatim\": \"#?.iff\", \"other\": 5 }";

    reset();
    assert(sprout_template_parse(&t, full, report, NULL) == 1);
    assert(errors == 0);
    assert(strcmp(t.kind, "project") == 0 && strcmp(t.name, "ReAction program") == 0);
    assert(strcmp(t.description, "A window") == 0 && strcmp(t.category, "C") == 0);
    assert(strcmp(t.icon_type, "program") == 0 && strcmp(t.icon_file, "template.info") == 0);
    assert(t.nph == 3);
    assert(strcmp(t.ph[0].key, "NAME") == 0 && strcmp(t.ph[0].label, "Program name") == 0);
    assert(strcmp(t.ph[0].def, "{$PROJECT}") == 0 && !t.ph[0].required && t.ph[0].nchoices == 0);
    assert(t.ph[1].required && t.ph[1].label == NULL && strcmp(t.ph[1].description, "Who") == 0);
    assert(t.ph[2].nchoices == 2 && strcmp(t.ph[2].choices[1], "vbcc") == 0);
    assert(t.nopen == 2 && strcmp(t.open[0], "src/{$NAME}.c") == 0);
    assert(t.nverbatim == 1 && strcmp(t.verbatim[0], "#?.iff") == 0);
    {   /* written as a tree and read back: the same */
        SproutTemplate u;
        JsonValue *tree = sprout_template_to_json(&t);
        assert(tree != NULL);
        assert(json_obj_get_int(tree, "version", 0) == 1);
        assert(sprout_template_from_json(&u, tree, report, NULL) == 1);
        assert(strcmp(u.kind, t.kind) == 0 && strcmp(u.name, t.name) == 0);
        assert(strcmp(u.description, t.description) == 0 && strcmp(u.category, t.category) == 0);
        assert(strcmp(u.icon_type, "program") == 0 && strcmp(u.icon_file, "template.info") == 0);
        assert(u.nph == 3 && strcmp(u.ph[0].def, "{$PROJECT}") == 0 && strcmp(u.ph[0].label, "Program name") == 0);
        assert(u.ph[1].required && u.ph[1].label == NULL && strcmp(u.ph[1].description, "Who") == 0);
        assert(!u.ph[0].required && u.ph[2].nchoices == 2 && strcmp(u.ph[2].choices[0], "gcc") == 0);
        assert(u.nopen == 2 && strcmp(u.open[1], "Makefile") == 0);
        assert(u.nverbatim == 1 && strcmp(u.verbatim[0], "#?.iff") == 0);
        sprout_template_free(&u);
        json_value_free(tree);
    }
    sprout_template_free(&t);
    assert(t.name == NULL && t.nph == 0);
    {   /* a minimal one: only what is there is written */
        JsonValue *tree;
        assert(sprout_template_parse(&t, "{ \"name\": \"X\" }", report, NULL) == 1);
        tree = sprout_template_to_json(&t);
        assert(tree != NULL && json_obj_get(tree, "icon") == NULL && json_obj_get(tree, "placeholders") == NULL);
        assert(json_obj_get(tree, "open") == NULL && strcmp(json_obj_get_string(tree, "category", ""), "Other") == 0);
        json_value_free(tree);
        sprout_template_free(&t);
        assert(sprout_template_from_json(&t, NULL, report, NULL) == 0);
    }

    /* only a name: kind project, category Other */
    assert(sprout_template_parse(&t, "{ \"name\": \"X\" }", report, NULL) == 1);
    assert(strcmp(t.kind, "project") == 0 && strcmp(t.category, "Other") == 0);
    assert(t.description == NULL && t.icon_type == NULL && t.nph == 0 && t.nopen == 0);
    sprout_template_free(&t);
    assert(sprout_template_parse(&t, "{ \"name\": \"X\", \"kind\": \"file\" }", report, NULL) == 1);
    sprout_template_free(&t);

    /* invalid ones are reported */
    {
        static const char *const bad[] = {
            "[1]",
            "{ \"name\": ",
            "{ }",
            "{ \"name\": \"\" }",
            "{ \"name\": \"X\", \"version\": 2 }",
            "{ \"name\": \"X\", \"kind\": \"folder\" }",
            "{ \"name\": \"X\", \"open\": 3 }",
            "{ \"name\": \"X\", \"verbatim\": [1] }",
            "{ \"name\": \"X\", \"placeholders\": {} }",
            "{ \"name\": \"X\", \"placeholders\": [ 1 ] }",
            "{ \"name\": \"X\", \"placeholders\": [ { \"label\": \"no key\" } ] }",
            "{ \"name\": \"X\", \"placeholders\": [ { \"key\": \"name\" } ] }",
            "{ \"name\": \"X\", \"placeholders\": [ { \"key\": \"1A\" } ] }",
            "{ \"name\": \"X\", \"placeholders\": [ { \"key\": \"DATE\" } ] }",
            "{ \"name\": \"X\", \"placeholders\": [ { \"key\": \"A\" }, { \"key\": \"A\" } ] }",
            "{ \"name\": \"X\", \"placeholders\": [ { \"key\": \"A\", \"choices\": [1] } ] }",
            NULL
        };
        int i;
        for (i = 0; bad[i] != NULL; ++i) {
            reset();
            assert(sprout_template_parse(&t, bad[i], report, NULL) == 0);
            assert(errors == 1);
            assert(t.name == NULL && t.nph == 0);
        }
    }
}

/** @brief Keys, values from JSON and from KEY=value, dates. */
static void test_values(void)
{
    SproutValues v;
    int y, m, d;

    assert(sprout_key_valid("A", 1) && sprout_key_valid("NAME_2", 6));
    assert(!sprout_key_valid("", 0) && !sprout_key_valid("a", 1) && !sprout_key_valid("_A", 2));
    assert(!sprout_key_valid("2A", 2) && !sprout_key_valid("A-B", 3) && !sprout_key_valid("Ab", 2));
    assert(sprout_key_valid("AB-", 2));                 /* only the length counts */
    assert(sprout_key_builtin("PROJECT") && sprout_key_builtin("DRAWER") && sprout_key_builtin("YEAR"));
    assert(!sprout_key_builtin("NAME") && !sprout_key_builtin("project"));

    sprout_values_init(&v);
    assert(sprout_values_get(&v, "A") == NULL);
    assert(sprout_values_parse_json(&v, "{ \"A\": \"1\", \"B\": 2, \"C\": \"x\\\"y\" }") == 1);
    assert(strcmp(sprout_values_get(&v, "A"), "1") == 0);
    assert(sprout_values_get(&v, "B") == NULL);
    assert(strcmp(sprout_values_get(&v, "C"), "x\"y") == 0);
    assert(sprout_values_parse_json(&v, "[]") == 0 && sprout_values_parse_json(&v, "{") == 0);
    assert(sprout_values_parse_assign(&v, "A=one=two") == 1);
    assert(strcmp(sprout_values_get(&v, "A"), "one=two") == 0);
    assert(sprout_values_parse_assign(&v, "D=") == 1 && strcmp(sprout_values_get(&v, "D"), "") == 0);
    assert(sprout_values_parse_assign(&v, "AUTHOR=Andr\xe9 Gewert") == 1);
    assert(strcmp(sprout_values_get(&v, "AUTHOR"), "Andr\xe9 Gewert") == 0);
    assert(sprout_values_parse_assign(&v, "novalue") == 0);
    assert(sprout_values_parse_assign(&v, "=x") == 0);
    /* keys in any case */
    assert(sprout_values_parse_assign(&v, "lower=x") == 1);
    assert(strcmp(sprout_values_get(&v, "LOWER"), "x") == 0 && sprout_values_get(&v, "lower") == NULL);
    assert(sprout_values_parse_assign(&v, "Author=y") == 1 && strcmp(sprout_values_get(&v, "AUTHOR"), "y") == 0);
    assert(sprout_values_set_arg(&v, "name", 4, "z") == 1 && strcmp(sprout_values_get(&v, "NAME"), "z") == 0);
    assert(sprout_values_set_arg(&v, "name2xx", 5, "w") == 1 && strcmp(sprout_values_get(&v, "NAME2"), "w") == 0);
    assert(sprout_values_set_arg(&v, "1a", 2, "z") == 0 && sprout_values_set_arg(&v, "a-b", 3, "z") == 0);
    assert(sprout_values_set_arg(&v, "", 0, "z") == 0);
    assert(sprout_values_parse_assign(&v, "\xe4=x") == 0);
    {   /* many values: the arrays grow */
        char arg[32];
        int i;
        for (i = 0; i < 40; ++i) {
            sprintf(arg, "K%d=%d", i, i);
            assert(sprout_values_parse_assign(&v, arg) == 1);
        }
        assert(strcmp(sprout_values_get(&v, "K39"), "39") == 0);
        assert(strcmp(sprout_values_get(&v, "A"), "one=two") == 0);
    }
    sprout_values_free(&v);
    assert(v.count == 0);

    sprout_date(0, &y, &m, &d);
    assert(y == 1978 && m == 1 && d == 1);
    sprout_date(1095, &y, &m, &d);
    assert(y == 1980 && m == 12 && d == 31);
    sprout_date(8094, &y, &m, &d);
    assert(y == 2000 && m == 2 && d == 29);
    sprout_date(17835, &y, &m, &d);
    assert(y == 2026 && m == 10 && d == 31);
    sprout_date(44619, &y, &m, &d);
    assert(y == 2100 && m == 3 && d == 1);
}

/** @brief Test the drawer name and the built-in values. */
static void test_builtins(void)
{
    char name[16];
    SproutValues v;
    static const char *const cases[][2] = {
        { "Work:Projects/Hello", "Hello" }, { "Work:Projects/Hello/", "Hello" },
        { "Work:Hello", "Hello" }, { "Work:", "Work" }, { "Hello", "Hello" },
        { "/Hello//", "Hello" }, { "", "" }, { "Work:Projects/AVeryLongDrawerName", "AVeryLongDrawer" },
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        sprout_drawer_name(cases[i][0], name, sizeof(name));
        assert(strcmp(name, cases[i][1]) == 0);
    }
    sprout_values_init(&v);
    assert(sprout_builtins(&v, NULL, "Hello", 17835) == 1);
    assert(strcmp(sprout_values_get(&v, "PROJECT"), "Hello") == 0);
    assert(strcmp(sprout_values_get(&v, "DATE"), "2026-10-31") == 0);
    assert(sprout_builtins(&v, "My Project", "Hello", 0) == 1);
    assert(strcmp(sprout_values_get(&v, "PROJECT"), "My Project") == 0);
    assert(strcmp(sprout_values_get(&v, "DRAWER"), "Hello") == 0);
    assert(strcmp(sprout_values_get(&v, "DATE"), "1978-01-01") == 0);
    assert(strcmp(sprout_values_get(&v, "YEAR"), "1978") == 0);
    assert(v.count == 4);
    sprout_values_free(&v);
}

/** @brief Working out the values: built-ins, defaults, choices, errors. */
static void test_resolve(void)
{
    SproutTemplate t;
    SproutValues given, out;
    const char *json =
        "{ \"name\": \"T\", \"placeholders\": [\n"
        "  { \"key\": \"NAME\", \"default\": \"{$PROJECT}\" },\n"
        "  { \"key\": \"GUARD\", \"default\": \"{$NAME:upper}_H\" },\n"
        "  { \"key\": \"AUTHOR\", \"required\": true },\n"
        "  { \"key\": \"CC\", \"choices\": [\"gcc\", \"vbcc\"] },\n"
        "  { \"key\": \"NOTE\" } ] }";

    assert(sprout_template_parse(&t, json, report, NULL) == 1);
    sprout_values_init(&given);
    assert(sprout_values_parse_assign(&given, "AUTHOR=Me") == 1);
    reset();
    assert(sprout_resolve(&t, &given, "Hello", 17835, &out, report, NULL) == 1);
    assert(errors == 0 && warnings == 0);
    assert(strcmp(sprout_values_get(&out, "PROJECT"), "Hello") == 0);
    assert(strcmp(sprout_values_get(&out, "DRAWER"), "Hello") == 0);
    assert(strcmp(sprout_values_get(&out, "DATE"), "2026-10-31") == 0);
    assert(strcmp(sprout_values_get(&out, "YEAR"), "2026") == 0);
    assert(strcmp(sprout_values_get(&out, "NAME"), "Hello") == 0);
    assert(strcmp(sprout_values_get(&out, "GUARD"), "HELLO_H") == 0);
    assert(strcmp(sprout_values_get(&out, "AUTHOR"), "Me") == 0);
    assert(strcmp(sprout_values_get(&out, "CC"), "gcc") == 0);
    assert(strcmp(sprout_values_get(&out, "NOTE"), "") == 0);
    assert(strcmp(sprout_values_get(&out, "AUTHORMAIL"), "") == 0);   /* standard, not declared */
    sprout_values_free(&out);

    /* a project name of its own: the default of NAME follows it */
    assert(sprout_values_parse_assign(&given, "PROJECT=Big Tool") == 1);
    reset();
    assert(sprout_resolve(&t, &given, "bt", 17835, &out, report, NULL) == 1);
    assert(errors == 0 && warnings == 0);
    assert(strcmp(sprout_values_get(&out, "PROJECT"), "Big Tool") == 0);
    assert(strcmp(sprout_values_get(&out, "DRAWER"), "bt") == 0);
    assert(strcmp(sprout_values_get(&out, "NAME"), "Big Tool") == 0);
    sprout_values_free(&out);
    /* an empty one counts as none */
    assert(sprout_values_parse_assign(&given, "PROJECT=") == 1);
    assert(sprout_resolve(&t, &given, "bt", 17835, &out, report, NULL) == 1);
    assert(strcmp(sprout_values_get(&out, "PROJECT"), "bt") == 0);
    sprout_values_free(&out);

    /* given values win; unknown keys and DRAWER, DATE, YEAR are warnings */
    assert(sprout_values_parse_assign(&given, "NAME=World") == 1);
    assert(sprout_values_parse_assign(&given, "CC=vbcc") == 1);
    assert(sprout_values_parse_assign(&given, "PROJECT=X") == 1);
    assert(sprout_values_parse_assign(&given, "DRAWER=D") == 1);
    assert(sprout_values_parse_assign(&given, "OTHER=Y") == 1);
    reset();
    assert(sprout_resolve(&t, &given, "Hello", 17835, &out, report, NULL) == 1);
    assert(errors == 0 && warnings == 2);
    assert(strcmp(sprout_values_get(&out, "PROJECT"), "X") == 0);
    assert(strcmp(sprout_values_get(&out, "DRAWER"), "Hello") == 0);
    assert(strcmp(sprout_values_get(&out, "GUARD"), "WORLD_H") == 0);
    assert(strcmp(sprout_values_get(&out, "CC"), "vbcc") == 0);
    assert(sprout_values_get(&out, "OTHER") == NULL);
    sprout_values_free(&out);

    /* a value not among the choices; a required value missing */
    assert(sprout_values_parse_assign(&given, "CC=sas") == 1);
    reset();
    assert(sprout_resolve(&t, &given, "Hello", 0, &out, report, NULL) == 0);
    assert(errors == 1 && strstr(last, "CC") != NULL && out.count == 0);
    sprout_values_free(&given);
    sprout_values_init(&given);
    reset();
    assert(sprout_resolve(&t, &given, "Hello", 0, &out, report, NULL) == 0);
    assert(errors == 1 && strstr(last, "AUTHOR") != NULL);
    assert(sprout_values_parse_assign(&given, "AUTHOR=") == 1);
    reset();
    assert(sprout_resolve(&t, &given, "Hello", 0, &out, report, NULL) == 0);
    assert(errors == 1);
    sprout_values_free(&given);
    sprout_template_free(&t);
}

/** @brief Replacing placeholders in text: modifiers, escapes, look-alikes. */
static void test_expand(void)
{
    SproutValues v;
    sprout_values_init(&v);
    assert(sprout_values_set(&v, "NAME", "Hello World") == 1);
    assert(sprout_values_set(&v, "LATIN", "\xc4rger \xe4\xdf\xff") == 1);
    assert(sprout_values_set(&v, "NUM", "3d") == 1);
    assert(sprout_values_set(&v, "EMPTY", "") == 1);

    check_expand(&v, "", "");
    check_expand(&v, "no placeholders", "no placeholders");
    check_expand(&v, "{$NAME}", "Hello World");
    check_expand(&v, "a {$NAME} b {$NAME}.", "a Hello World b Hello World.");
    check_expand(&v, "{$NAME:upper}_H", "HELLO WORLD_H");
    check_expand(&v, "{$NAME:lower}", "hello world");
    check_expand(&v, "{$NAME:ident}", "Hello_World");
    check_expand(&v, "{$NUM:ident}", "_3d");
    check_expand(&v, "{$EMPTY:ident}x", "_x");
    check_expand(&v, "{$EMPTY}x", "x");
    check_expand(&v, "{$LATIN:upper}", "\xc4RGER \xc4\xdf\xff");
    check_expand(&v, "{$LATIN:lower}", "\xe4rger \xe4\xdf\xff");
    check_expand(&v, "{$LATIN:ident}", "_rger____");
    check_expand(&v, "{{$NAME}}", "{Hello World}");
    check_expand(&v, "{$NAME}{$NAME}", "Hello WorldHello World");

    /* escapes: one $ fewer, defined or not */
    check_expand(&v, "{$$NAME}", "{$NAME}");
    check_expand(&v, "{$$NAME:upper}", "{$NAME:upper}");
    check_expand(&v, "{$$$NAME}", "{$$NAME}");
    check_expand(&v, "{$$OTHER}", "{$OTHER}");
    check_expand(&v, "x{$$NAME}y{$NAME}", "x{$NAME}yHello World");

    /* left as they are: undefined keys, other forms, Pascal directives */
    check_expand(&v, "{$OTHER}", "{$OTHER}");
    check_expand(&v, "{$NAME:shout}", "{$NAME:shout}");
    check_expand(&v, "{$NAME:}", "{$NAME:}");
    check_expand(&v, "{$name}", "{$name}");
    check_expand(&v, "{$MODE OBJFPC}{$H+}{$I file.inc}{$IFDEF NAME}", "{$MODE OBJFPC}{$H+}{$I file.inc}{$IFDEF NAME}");
    check_expand(&v, "{$} {$$} $$ ${NAME} $NAME {NAME} {project}", "{$} {$$} $$ ${NAME} $NAME {NAME} {project}");
    check_expand(&v, "{$NAME", "{$NAME");
    check_expand(&v, "{", "{");
    check_expand(&v, "{$", "{$");
    check_expand(&v, "{$NAME:upper", "{$NAME:upper");

    {   /* NUL bytes and a length shorter than the string */
        size_t n;
        char *s = sprout_expand("a\0{$NAME}", 9, &v, &n);
        assert(s != NULL && n == 13 && s[0] == 'a' && s[1] == '\0' && strcmp(s + 2, "Hello World") == 0);
        free(s);
        s = sprout_expand("{$NAME}tail", 7, &v, &n);
        assert(s != NULL && strcmp(s, "Hello World") == 0);
        free(s);
    }
    sprout_values_free(&v);
}

/** @brief Safe paths and replacing placeholders in them. */
static void test_paths(void)
{
    SproutValues v;
    sprout_values_init(&v);
    assert(sprout_values_set(&v, "NAME", "Hello") == 1);
    assert(sprout_values_set(&v, "SLASH", "a/b") == 1);
    assert(sprout_values_set(&v, "COLON", "Work:x") == 1);
    assert(sprout_values_set(&v, "DOTS", "..") == 1);
    assert(sprout_values_set(&v, "EMPTY", "") == 1);

    assert(sprout_path_safe("a") && sprout_path_safe("src/main.c") && sprout_path_safe(".tinydev/project.json"));
    assert(sprout_path_safe("a.b/..c"));
    assert(!sprout_path_safe("") && !sprout_path_safe("/a") && !sprout_path_safe("a/") && !sprout_path_safe("a//b"));
    assert(!sprout_path_safe("Work:a") && !sprout_path_safe("../a") && !sprout_path_safe("a/./b"));

    check_path(&v, "src/{$NAME}.c", "src/Hello.c");
    check_path(&v, "{$NAME}/{$NAME:lower}.h", "Hello/hello.h");
    check_path(&v, ".tinydev/project.json", ".tinydev/project.json");
    check_path(&v, "{$OTHER}.c", "{$OTHER}.c");
    check_path(&v, "{$$NAME}", "{$NAME}");
    check_path(&v, "src/{$SLASH}.c", NULL);
    check_path(&v, "{$COLON}", NULL);
    check_path(&v, "{$DOTS}/x", NULL);
    check_path(&v, "{$EMPTY}/x", NULL);
    check_path(&v, "/x", NULL);
    check_path(&v, "a//x", NULL);
    check_path(&v, "{$OTHER:upper}", NULL);
    check_path(&v, "{$NAME:upper}", "HELLO");

    /* conditions */
    assert(sprout_values_set(&v, "CC", "gcc") == 1);
    check_path(&v, "Makefile{?CC=gcc}", "Makefile");
    check_path(&v, "Makefile{?CC=vbcc|gcc}", "Makefile");
    check_path(&v, "{?CC=gcc}{$NAME}.c", "Hello.c");
    check_path(&v, "src{?CC!=vbcc}/a{?NAME=Hello}.c", "src/a.c");
    check_path_bad(&v, "Makefile{?CC=vbcc}", NULL, SPROUT_PATH_LEFTOUT);
    check_path_bad(&v, "Makefile{?CC!=gcc|sasc}", NULL, SPROUT_PATH_LEFTOUT);
    check_path_bad(&v, "gcc{?CC=vbcc}/Makefile", NULL, SPROUT_PATH_LEFTOUT);
    check_path_bad(&v, "a{?CC=gcc}{?NAME=x}", NULL, SPROUT_PATH_LEFTOUT);
    check_path_bad(&v, "a{?OTHER=x}", NULL, SPROUT_PATH_LEFTOUT);
    check_path(&v, "a{?OTHER=}", "a");
    check_path(&v, "a{?OTHER=x|}", "a");
    check_path(&v, "a{?OTHER!=x}", "a");
    check_path_bad(&v, "a{?CC}", NULL, SPROUT_PATH_BADCOND);
    check_path_bad(&v, "a{?CC=gcc", NULL, SPROUT_PATH_BADCOND);
    check_path_bad(&v, "a{?=gcc}", NULL, SPROUT_PATH_BADCOND);
    check_path_bad(&v, "a{?cc=gcc}", NULL, SPROUT_PATH_BADCOND);
    check_path_bad(&v, "a{?CC!gcc}", NULL, SPROUT_PATH_BADCOND);
    check_path_bad(&v, "{?CC=gcc}", NULL, SPROUT_PATH_UNSAFE);          /* nothing left of the name */
    check_path_bad(&v, "{?CC=gcc}/x", NULL, SPROUT_PATH_UNSAFE);
    sprout_values_free(&v);
}

/** @brief Telling text files from binary ones. */
static void test_text(void)
{
    static char big[SPROUT_TEXT_MAX + 1];
    assert(sprout_is_text("", 0));
    assert(sprout_is_text("plain\r\ntext\xff", 12));
    assert(!sprout_is_text("a\0b", 3));
    memset(big, 'x', sizeof(big));
    assert(sprout_is_text(big, SPROUT_TEXT_MAX));
    assert(!sprout_is_text(big, SPROUT_TEXT_MAX + 1));
}

/**
 * @brief The template templates/HelpProject: its metadata is valid and the
 *        project's commands keep TinyDev's own placeholder.
 */
static void test_shipped_help(void)
{
    SproutTemplate t;
    SproutValues given, out;
    char json[4096];
    char *name, *line;
    size_t n;
    int bad;
    FILE *f = fopen("templates/HelpProject/template.json", "rb");
    assert(f != NULL);
    n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';
    reset();
    assert(sprout_template_parse(&t, json, report, NULL) == 1);
    sprout_values_init(&given);
    assert(sprout_values_parse_assign(&given, "AUTHOR=Me") == 1);
    assert(sprout_resolve(&t, &given, "My Help", 17835, &out, report, NULL) == 1);
    assert(errors == 0 && warnings == 0);
    assert(strcmp(sprout_values_get(&out, "TITLE"), "My Help") == 0);
    assert(t.nopen == 1);
    name = sprout_expand_path(t.open[0], &out, &bad);
    assert(name != NULL && strcmp(name, "src/index.md") == 0);
    free(name);
    line = sprout_expand("{tinydev}mdtohtml $root$ {$TITLE}", 33, &out, NULL);
    assert(line != NULL && strcmp(line, "{tinydev}mdtohtml $root$ My Help") == 0);
    free(line);
    sprout_values_free(&out);
    sprout_values_free(&given);
    sprout_template_free(&t);
}

/**
 * @brief The template that comes with TinyDev (templates/HelloWorld): its
 *        metadata is valid, and its values and names come out right.
 */
static void test_shipped(void)
{
    SproutTemplate t;
    SproutValues given, out;
    char json[4096];
    char *name;
    size_t n;
    int bad;
    FILE *f = fopen("templates/HelloWorld/template.json", "rb");
    assert(f != NULL);
    n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';
    reset();
    assert(sprout_template_parse(&t, json, report, NULL) == 1);
    sprout_values_init(&given);
    assert(sprout_values_parse_assign(&given, "AUTHOR=Me") == 1);
    assert(sprout_resolve(&t, &given, "My Tool", 17835, &out, report, NULL) == 1);
    assert(errors == 0 && warnings == 0);
    assert(strcmp(sprout_values_get(&out, "NAME"), "My_Tool") == 0);
    assert(t.nopen == 1);
    name = sprout_expand_path(t.open[0], &out, &bad);
    assert(name != NULL && strcmp(name, "My_Tool.c") == 0);
    free(name);
    assert(strcmp(sprout_values_get(&out, "CC"), "vbcc") == 0);
    check_path(&out, "Makefile{?CC=vbcc}", "Makefile");
    check_path_bad(&out, "smakefile{?CC=sasc}", NULL, SPROUT_PATH_LEFTOUT);
    sprout_values_free(&out);
    assert(sprout_values_parse_assign(&given, "CC=sasc") == 1);
    assert(sprout_resolve(&t, &given, "My Tool", 17835, &out, report, NULL) == 1);
    check_path(&out, "smakefile{?CC=sasc}", "smakefile");
    check_path_bad(&out, ".tinydev/project.json{?CC=gcc}", NULL, SPROUT_PATH_LEFTOUT);
    sprout_values_free(&out);
    assert(sprout_values_parse_assign(&given, "CC=icc") == 1);
    assert(sprout_resolve(&t, &given, "My Tool", 17835, &out, report, NULL) == 0);    /* not a choice */
    reset();
    sprout_values_free(&given);
    sprout_template_free(&t);

    /* the file template CModule */
    f = fopen("templates/CModule/template.json", "rb");
    assert(f != NULL);
    n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';
    assert(sprout_template_parse(&t, json, report, NULL) == 1);
    assert(strcmp(t.kind, "file") == 0);
    sprout_values_init(&given);
    assert(sprout_resolve(&t, &given, "src", 17835, &out, report, NULL) == 0);     /* NAME is required */
    reset();
    assert(sprout_values_parse_assign(&given, "NAME=list-view") == 1);
    assert(sprout_values_parse_assign(&given, "PROJECT=TinyDev") == 1);
    assert(sprout_resolve(&t, &given, "src", 17835, &out, report, NULL) == 1);
    assert(errors == 0 && warnings == 0);
    assert(strcmp(sprout_values_get(&out, "GUARD"), "list_view_H") == 0);
    assert(strcmp(sprout_values_get(&out, "PROJECT"), "TinyDev") == 0);
    assert(t.nopen == 2);
    name = sprout_expand_path(t.open[1], &out, &bad);
    assert(name != NULL && strcmp(name, "list-view.h") == 0);
    free(name);
    sprout_values_free(&out);
    sprout_values_free(&given);
    sprout_template_free(&t);
}

/**
 * @brief Run the tests.
 * @return 0 (a failed test aborts).
 */
int main(void)
{
    test_parse();
    test_values();
    test_builtins();
    test_resolve();
    test_standard();
    test_expand();
    test_paths();
    test_text();
    test_shipped();
    test_shipped_help();
    puts("sprout template tests passed");
    return 0;
}
