/**
 * @file test_filetypes.c
 * @brief Host-native tests of the settings per file type.
 */
#include "filetypes.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Reading: defaults, partial entries, wrong values.
 */
static void test_parse(void)
{
    FileTypes t;
    const FileTypeOptions *o;

    filetypes_init(&t);
    assert(t.standard.tab_size == 8 && t.standard.indent == 4 && t.standard.auto_indent);
    assert(t.standard.tab_after && t.standard.line_numbers && !t.standard.cut_copy_line);

    /* not an object: nothing changes */
    assert(filetypes_parse(&t, "[1]") == 0);
    assert(filetypes_parse(&t, "nonsense") == 0);
    /* no filetypes: the built-in settings */
    assert(filetypes_parse(&t, "{ \"version\": 1 }") == 1);
    assert(t.count == 0 && t.standard.tab_size == 8);

    assert(filetypes_parse(&t,
        "{ \"filetypes\": {"
        "  \"default\": { \"tabsize\": 4, \"indent\": 2, \"autoindent\": false,"
        "                \"tabkey\": \"line\", \"linenumbers\": false, \"cutcopyline\": true },"
        "  \"languages\": {"
        "    \"amigados\": { \"indent\": \"tab\" },"
        "    \"c\": { \"tabsize\": 99, \"indent\": 0, \"tabkey\": \"after\", \"linenumbers\": 1 },"
        "    \"bad\": 5,"
        "    \"\": { \"tabsize\": 3 }"
        "  } } }") == 1);
    assert(t.standard.tab_size == 4 && t.standard.indent == 2 && !t.standard.auto_indent);
    assert(!t.standard.tab_after && !t.standard.line_numbers && t.standard.cut_copy_line);
    assert(t.count == 2);
    /* missing keys come from default */
    o = filetypes_find(&t, "amigados");
    assert(o != NULL && o->indent == 0 && o->tab_size == 4 && !o->auto_indent && !o->line_numbers);
    /* wrong values are ignored */
    o = filetypes_find(&t, "c");
    assert(o != NULL && o->tab_size == 4 && o->indent == 2 && o->tab_after && !o->line_numbers);
    assert(filetypes_find(&t, "bad") == NULL && filetypes_find(&t, "") == NULL);
    /* plain text and unknown languages use default */
    assert(filetypes_get(&t, NULL) == &t.standard);
    assert(filetypes_get(&t, "") == &t.standard);
    assert(filetypes_get(&t, "pascal") == &t.standard);
    assert(filetypes_get(&t, "c") == filetypes_find(&t, "c"));
    filetypes_clear(&t);
    assert(t.count == 0 && t.entries == NULL);
}

/**
 * @brief Changing, copying and writing.
 */
static void test_set_and_format(void)
{
    FileTypes t, u;
    FileTypeOptions o;
    char *text, *again;

    filetypes_init(&t);
    filetypes_defaults(&o);
    o.tab_size = 40;            /* brought into range */
    o.indent = -3;
    o.auto_indent = 7;
    assert(filetypes_set(&t, "c", &o) == 1);
    assert(t.count == 1);
    assert(filetypes_find(&t, "c")->tab_size == FILETYPES_MAX_WIDTH);
    assert(filetypes_find(&t, "c")->indent == 0);
    assert(filetypes_find(&t, "c")->auto_indent == 1);
    o.tab_size = 2;
    assert(filetypes_set(&t, "c", &o) == 1 && t.count == 1);
    assert(filetypes_find(&t, "c")->tab_size == 2);
    o.tab_size = 3;
    assert(filetypes_set(&t, "amigados", &o) == 1 && t.count == 2);
    o.tab_size = 6;
    assert(filetypes_set(&t, NULL, &o) == 1 && t.standard.tab_size == 6);

    assert(filetypes_copy(&u, &t) == 1);
    assert(u.count == 2 && u.entries != t.entries && strcmp(u.entries[1].id, "amigados") == 0);
    assert(filetypes_set(&u, "c", NULL) == 1 && u.count == 1);
    assert(filetypes_find(&u, "c") == NULL && filetypes_find(&u, "amigados")->tab_size == 3);
    assert(filetypes_set(&u, "none", NULL) == 1 && u.count == 1);

    /* a new file */
    text = filetypes_format(&t, NULL);
    assert(text != NULL);
    assert(strstr(text, "\"version\": 1") != NULL);
    assert(strstr(text, "\"indent\": \"tab\"") != NULL);
    assert(filetypes_parse(&u, text) == 1);
    assert(u.count == 2 && u.standard.tab_size == 6);
    assert(filetypes_find(&u, "c")->tab_size == 2 && filetypes_find(&u, "c")->indent == 0);
    again = filetypes_format(&u, text);
    assert(again != NULL && strcmp(again, text) == 0);
    free(again);
    free(text);

    /* other keys stay; a language without settings loses its entry */
    text = filetypes_format(&t,
        "{ \"version\": 1, \"other\": [1, 2],"
        "  \"filetypes\": { \"note\": \"x\","
        "    \"default\": { \"colour\": \"red\" },"
        "    \"languages\": { \"pascal\": { \"tabsize\": 2 }, \"c\": { \"mine\": true } } } }");
    assert(text != NULL);
    assert(strstr(text, "\"other\"") != NULL && strstr(text, "\"note\"") != NULL);
    assert(strstr(text, "\"colour\"") != NULL && strstr(text, "\"mine\"") != NULL);
    assert(strstr(text, "pascal") == NULL && strstr(text, "amigados") != NULL);
    free(text);
    assert(filetypes_format(&t, "[1]") == NULL);

    filetypes_clear(&u);
    filetypes_clear(&t);
}

/**
 * @brief Run the tests.
 * @return 0.
 */
int main(void)
{
    test_parse();
    test_set_and_format();
    puts("file type settings tests passed");
    return 0;
}
