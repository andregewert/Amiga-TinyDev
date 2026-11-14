/**
 * @file test_appprefs.c
 * @brief Host-native tests of TinyDev's global settings in TinyDev.json.
 */
#include "appprefs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Reading: defaults, all keys, wrong values.
 */
static void test_parse(void)
{
    Settings s, d;

    settings_default(&d);
    assert(d.tbmode == TBMODE_IMAGES && !d.tbframes && d.splash && d.foldertree && !d.minimap);
    assert(d.winleft == -1 && d.winwidth == 0 && d.bmwin[0] == -1 && d.bmwin[2] == 0);
    assert(d.scout && strcmp(d.tempdir, "T:") == 0 && strcmp(d.shellwindow, "CON:") == 0);
    assert(d.colours[SCOL_KEYWORD] == settings_default_colours[SCOL_KEYWORD]);
    assert(d.outfont[0] == '\0' && d.outfontsize == 0);
    assert(!d.git && strcmp(d.gitcommand, "am-git") == 0);

    /* not an object: nothing changes */
    s = d;
    assert(appprefs_parse(&s, "[1]") == 0);
    assert(appprefs_parse(&s, "nonsense") == 0);
    /* no settings (a file of the file types only): the defaults */
    assert(appprefs_parse(&s, "{ \"version\": 1, \"filetypes\": {} }") == 1);
    assert(memcmp(&s, &d, sizeof(s)) == 0);

    strcpy(s.icon_name, "PROGDIR:TinyDev");
    assert(appprefs_parse(&s,
        "{ \"settings\": {"
        "  \"splash\": false, \"scout\": false,"
        "  \"toolbar\": { \"show\": \"both\", \"frames\": true },"
        "  \"window\": { \"left\": 10, \"top\": 20, \"width\": 640, \"height\": 400 },"
        "  \"view\": { \"foldertree\": false, \"minimap\": true, \"showinfo\": true },"
        "  \"toolwindows\": {"
        "    \"bookmarks\": { \"open\": true, \"left\": 700, \"top\": 30, \"width\": 200, \"height\": 300 },"
        "    \"output\": { \"open\": true },"
        "    \"symbols\": { \"left\": 5, \"top\": 5, \"width\": 100, \"height\": 0 },"
        "    \"problems\": 3 },"
        "  \"tempdir\": \"RAM:\", \"shellwindow\": \"\", \"projectdir\": \"Work:Projects\","
        "  \"author\": \"Andr\xe9 Gewert\", \"authormail\": \"a@b.c\","
        "  \"outputfont\": { \"name\": \"Holstein.font\", \"size\": 16 },"
        "  \"git\": { \"enabled\": true, \"command\": \"Work:am-git/am-git\" },"
        "  \"colours\": { \"text\": \"#102030\", \"keyword\": \"A0B0C0\", \"string\": \"#12345\","
        "                 \"comment\": 5, \"macro\": \"#GGGGGG\" }"
        "} }") == 1);
    assert(!s.splash && !s.scout && s.tbmode == TBMODE_BOTH && s.tbframes);
    assert(s.winleft == 10 && s.wintop == 20 && s.winwidth == 640 && s.winheight == 400);
    assert(!s.foldertree && s.minimap && s.showinfo);
    assert(s.bmlist && s.bmwin[0] == 700 && s.bmwin[1] == 30 && s.bmwin[2] == 200 && s.bmwin[3] == 300);
    assert(s.outlist && s.outwin[2] == 0);
    /* a box without a height is not taken */
    assert(!s.symlist && s.symwin[0] == -1 && s.symwin[2] == 0);
    assert(!s.diaglist);
    assert(strcmp(s.tempdir, "RAM:") == 0);
    assert(strcmp(s.shellwindow, "CON:") == 0);        /* "" keeps the default */
    assert(strcmp(s.projectdir, "Work:Projects") == 0);
    assert(strcmp(s.author, "Andr\xe9 Gewert") == 0 && strcmp(s.authormail, "a@b.c") == 0);
    assert(s.colours[SCOL_TEXT] == 0x102030 && s.colours[SCOL_KEYWORD] == 0xA0B0C0);
    assert(s.colours[SCOL_STRING] == settings_default_colours[SCOL_STRING]);
    assert(s.colours[SCOL_COMMENT] == settings_default_colours[SCOL_COMMENT]);
    assert(s.colours[SCOL_MACRO] == settings_default_colours[SCOL_MACRO]);
    assert(strcmp(s.outfont, "Holstein.font") == 0 && s.outfontsize == 16);
    assert(s.git && strcmp(s.gitcommand, "Work:am-git/am-git") == 0);
    assert(strcmp(s.icon_name, "PROGDIR:TinyDev") == 0);

    /* wrong values and an unknown toolbar mode keep what is there */
    s = d;
    assert(appprefs_parse(&s,
        "{ \"settings\": { \"splash\": 0, \"toolbar\": { \"show\": \"IMAGES\" },"
        "  \"window\": { \"left\": -5, \"width\": -1, \"height\": \"x\" },"
        "  \"projectdir\": 7, \"view\": [], \"outputfont\": { \"name\": 1, \"size\": -2 },"
        "  \"git\": { \"enabled\": 1, \"command\": \"\" } } }") == 1);
    assert(memcmp(&s, &d, sizeof(s)) == 0);

    /* a long text is cut to the setting's size */
    {
        char json[600];
        memset(json, 0, sizeof(json));
        strcpy(json, "{ \"settings\": { \"author\": \"");
        memset(json + strlen(json), 'x', 300);
        strcat(json, "\" } }");
        s = d;
        assert(appprefs_parse(&s, json) == 1);
        assert(strlen(s.author) == sizeof(s.author) - 1);
    }

    /* a snapshot box set back to automatic */
    s = d;
    s.bmwin[0] = 1; s.bmwin[1] = 2; s.bmwin[2] = 3; s.bmwin[3] = 4;
    assert(appprefs_parse(&s, "{ \"settings\": { \"toolwindows\": { \"bookmarks\": "
                              "{ \"left\": -1, \"top\": -1, \"width\": 0, \"height\": 0 } } } }") == 1);
    assert(s.bmwin[0] == -1 && s.bmwin[1] == -1 && s.bmwin[2] == 0 && s.bmwin[3] == 0);
}

/**
 * @brief Writing: a new file, round trip, keeping other keys.
 */
static void test_format(void)
{
    Settings s, r;
    char *text;

    settings_default(&s);
    s.tbmode = TBMODE_TEXT;
    s.minimap = 1;
    s.winwidth = 800;
    s.symlist = 1;
    s.symwin[0] = 3; s.symwin[1] = 4; s.symwin[2] = 150; s.symwin[3] = 250;
    strcpy(s.author, "Andr\xe9");
    strcpy(s.outfont, "Tamzen.font");
    s.outfontsize = 15;
    s.git = 1;
    strcpy(s.gitcommand, "C:am-git");
    s.colours[SCOL_TYPE] = 0x0a0b0c;
    strcpy(s.icon_name, "PROGDIR:TinyDev");

    /* a new file holds every setting, defaults included */
    text = appprefs_format(&s, NULL);
    assert(text != NULL);
    assert(strstr(text, "\"version\": 1") != NULL);
    assert(strstr(text, "\"show\": \"text\"") != NULL);
    assert(strstr(text, "\"tempdir\": \"T:\"") != NULL);
    assert(strstr(text, "\"type\": \"#0A0B0C\"") != NULL);
    assert(strstr(text, "\"keyword\": \"#0030D0\"") != NULL);
    assert(strstr(text, "\"name\": \"Tamzen.font\"") != NULL && strstr(text, "\"size\": 15") != NULL);
    assert(strstr(text, "\"enabled\": true") != NULL && strstr(text, "\"command\": \"C:am-git\"") != NULL);
    assert(strstr(text, "PROGDIR:TinyDev") == NULL);    /* not a setting */
    settings_default(&r);
    assert(appprefs_parse(&r, text) == 1);
    memset(s.icon_name, 0, sizeof(s.icon_name));    /* r's was never set */
    assert(memcmp(&r, &s, sizeof(s)) == 0);
    free(text);

    /* the rest of the file stays */
    text = appprefs_format(&s,
        "{ \"version\": 1, \"filetypes\": { \"default\": { \"tabsize\": 4 } },"
        "  \"settings\": { \"mine\": 1, \"toolbar\": { \"note\": \"x\", \"show\": \"images\" } } }");
    assert(text != NULL);
    assert(strstr(text, "\"tabsize\": 4") != NULL);
    assert(strstr(text, "\"mine\": 1") != NULL && strstr(text, "\"note\": \"x\"") != NULL);
    assert(strstr(text, "\"show\": \"text\"") != NULL && strstr(text, "\"images\"") == NULL);
    free(text);
    assert(appprefs_format(&s, "[1]") == NULL);
    assert(appprefs_format(&s, "nonsense") == NULL);
}

/**
 * @brief Run the tests.
 * @return 0.
 */
int main(void)
{
    test_parse();
    test_format();
    puts("global settings tests passed");
    return 0;
}
