#include "bmstore.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Host-native tests of the bookmark store: adding and replacing,
 *        removing a file, writing and reading the JSON file (sorted, line
 *        numbers from 1, Latin-1 and quotes in texts), rejecting other
 *        files, and finding a line again in a changed text.
 * @return 0 when all assertions pass.
 */
int main(void)
{
    BmStore s, t;
    char *json;
    const char *text = "int a;\nvoid f(void)\r\n{\n}\nvoid f(void)\n";

    bmstore_init(&s);
    assert(bmstore_add(&s, "src/ui.c", 98, "void tree(\"x\")", 14));
    assert(bmstore_add(&s, "SRC/main.c", 4, "int main(void)", 14));
    assert(bmstore_add(&s, "src/ui.c", 10, "\xC4rger", 5));
    assert(bmstore_add(&s, "src/UI.c", 98, "replaced", 8));   /* same file and line */
    assert(s.count == 3);

    json = bmstore_format(&s);
    assert(json != NULL);
    /* sorted by file (ignoring case), then line; lines from 1 */
    assert(strstr(json, "\"version\": 1") != NULL);
    assert(strstr(json, "\"SRC/main.c\", \"line\": 5") != NULL);
    assert(strstr(json, "\"line\": 5") < strstr(json, "\"line\": 11"));
    assert(strstr(json, "\"line\": 11") < strstr(json, "\"line\": 99"));
    assert(strstr(json, "\"text\": \"replaced\"") != NULL);

    bmstore_init(&t);
    assert(bmstore_parse(&t, json));
    assert(t.count == 3);
    assert(strcmp(t.entries[0].file, "SRC/main.c") == 0 && t.entries[0].line == 4);
    assert(strcmp(t.entries[1].text, "\xC4rger") == 0 && t.entries[1].line == 10);
    free(json);

    /* quotes survive */
    bmstore_clear(&t);
    assert(bmstore_add(&s, "a b.c", 0, "say \"hi\" \\ there", 16));
    json = bmstore_format(&s);
    assert(bmstore_parse(&t, json));
    assert(t.count == 4 && strcmp(t.entries[0].text, "say \"hi\" \\ there") == 0);
    free(json);

    assert(bmstore_remove(&t, "SRC/UI.C", 10));
    assert(!bmstore_remove(&t, "src/ui.c", 10));
    assert(bmstore_add(&t, "src/ui.c", 10, "x", 1));
    assert(bmstore_compare_files("a/B.c", "a/b.C") == 0 && bmstore_compare_files("a", "b") < 0);
    assert(bmstore_remove_file(&t, "src/ui.c") == 2);
    assert(t.count == 2);
    assert(bmstore_same_file("Src/\xC4.c", "src/\xE4.C"));
    assert(!bmstore_same_file("src/a.c", "src/a.cc"));

    /* other files are refused and change nothing */
    assert(!bmstore_parse(&t, "{\"version\": 1}"));
    assert(!bmstore_parse(&t, "[1, 2]"));
    assert(!bmstore_parse(&t, "not json"));
    assert(t.count == 2);
    /* invalid entries are skipped */
    assert(bmstore_parse(&t, "{\"bookmarks\": [{\"file\": \"x\", \"line\": 0}, 3,"
                             " {\"file\": \"y\", \"line\": 2}]}"));
    assert(t.count == 3);

    /* texts are cut */
    {
        char long_line[300];
        memset(long_line, 'x', sizeof(long_line));
        assert(bmstore_add(&t, "z", 1, long_line, sizeof(long_line)));
        assert(strlen(t.entries[t.count - 1].text) == BMSTORE_TEXT_MAX);
        assert(bmstore_relocate(long_line, sizeof(long_line), 5, t.entries[t.count - 1].text) == 0);
    }

    /* finding a line again */
    assert(bmstore_relocate(text, (long)strlen(text), 1, "void f(void)") == 1);
    assert(bmstore_relocate(text, (long)strlen(text), 3, "void f(void)") == 4);   /* nearest */
    assert(bmstore_relocate(text, (long)strlen(text), 0, "void f(void)") == 1);
    assert(bmstore_relocate(text, (long)strlen(text), 2, "gone") == 2);           /* kept */
    assert(bmstore_relocate(text, (long)strlen(text), 40, "gone") == 5);          /* last line */
    assert(bmstore_relocate(text, (long)strlen(text), 3, "") == 3);
    assert(bmstore_relocate(text, (long)strlen(text), 3, "void") == 3);          /* whole line only */

    bmstore_clear(&s);
    bmstore_clear(&t);
    puts("bookmark store tests passed");
    return 0;
}
