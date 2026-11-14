/**
 * @file settings.c
 * @brief TinyDev's global settings in PROGDIR:TinyDev.json: reading them at
 *        the start and writing them back.
 *
 * The settings are the "settings" of PROGDIR:TinyDev.json (format and
 * keys in appprefs.h), next to the settings per file type (ftprefs.c);
 * the tool types of TinyDev's icon are not read, so an update may replace
 * the icon. The settings window writes them together with the settings
 * per file type (settings_save()), in one go. The view at
 * the start (folder tree, minimap) is only set there: the View menu
 * changes the view of the session, not the settings. The tool windows are
 * written by View/Snapshot Tool Windows and View/Unsnapshot Tool Windows
 * only (settings_save_toolwindows()).
 *
 * A file that cannot be read at the start (ftprefs_load() reports it) is
 * never written, so nothing in it is lost; TinyDev then uses the
 * defaults.
 */
#include "editor.h"

#include <workbench/startup.h>
#include <proto/dos.h>

#include <stdlib.h>
#include <string.h>

/** The file could not be read at the start: it is never written. */
static int unreadable;

/**
 * @brief Remember the program icon as PROGDIR:\<program name\>.
 * @param s The settings.
 * @param program The program's name or path.
 */
static void set_icon_name(Settings *s, CONST_STRPTR program)
{
    strcpy(s->icon_name, "PROGDIR:");
    if (!AddPart((STRPTR)s->icon_name, FilePart((STRPTR)program), sizeof(s->icon_name)))
        s->icon_name[0] = '\0';
}

void settings_load(Settings *s, int argc, char **argv)
{
    char *text = NULL;
    int r;

    if (argc == 0) {
        struct WBStartup *wbs = (struct WBStartup *)argv;
        if (wbs != NULL && wbs->sm_NumArgs >= 1)
            set_icon_name(s, (CONST_STRPTR)wbs->sm_ArgList[0].wa_Name);
    } else {
        char program[256];
        if (!GetProgramName((STRPTR)program, sizeof(program))) strcpy(program, APP_NAME);
        set_icon_name(s, (CONST_STRPTR)program);
    }
    r = datafile_read(PREFS_FILE, &text);
    if (r > 0 && !appprefs_parse(s, text)) r = -1;
    free(text);
    unreadable = r < 0;
}

int settings_save(const Settings *s, const FileTypes *ft)
{
    char *text = NULL, *json = NULL, *all = NULL;
    Settings check;
    int r, ok = 0;
    if (unreadable) return 0;
    r = datafile_read(PREFS_FILE, &text);
    settings_default(&check);
    /* changed by hand meanwhile into something unreadable: kept too */
    if (r == 0 || (r > 0 && appprefs_parse(&check, text))) {
        json = appprefs_format(s, text);
        if (json != NULL && ft != NULL) {
            all = filetypes_format(ft, json);
            free(json);
            json = all;
        }
        ok = json != NULL && datafile_write(PREFS_FILE, json);
    }
    free(json);
    free(text);
    return ok;
}

int settings_save_toolwindows(const Settings *cur)
{
    Settings s;
    char *text = NULL;
    int r;
    if (unreadable) return 0;
    settings_default(&s);
    r = datafile_read(PREFS_FILE, &text);
    if (r < 0 || (r > 0 && !appprefs_parse(&s, text))) {
        free(text);
        return 0;
    }
    free(text);
    memcpy(s.bmwin, cur->bmwin, sizeof(s.bmwin));
    s.bmlist = cur->bmlist;
    memcpy(s.outwin, cur->outwin, sizeof(s.outwin));
    s.outlist = cur->outlist;
    memcpy(s.symwin, cur->symwin, sizeof(s.symwin));
    s.symlist = cur->symlist;
    memcpy(s.diagwin, cur->diagwin, sizeof(s.diagwin));
    s.diaglist = cur->diaglist;
    return settings_save(&s, NULL);
}
