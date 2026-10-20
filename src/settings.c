/**
 * @file settings.c
 * @brief Settings from the tool types of the program icon (and of project icons), and writing them back.
 *
 * Settings are tool types of the program icon (PROGDIR:\<program\>), also
 * when TinyDev is started from the Shell; on a Workbench start the tool
 * types of project icons follow and take precedence. The settings window
 * writes them back into the program icon (settings_save()).
 *
 *   TOOLBAR=IMAGES|BOTH|TEXT   what the toolbar buttons show
 *   TOOLBARFRAMES              frames around the toolbar buttons
 *   NOSPLASH                   no splash window while starting
 *   WIDTH=n, HEIGHT=n          size of the main window at the start
 *   LEFT=n, TOP=n              its position (otherwise centred)
 *   BOOKMARKLIST               open the bookmark list at the start
 *   BOOKMARKWINDOW=l,t,w,h     where the bookmark list opens (otherwise
 *                              next to the main window)
 *   COLOR_TEXT=RRGGBB ...      colours of the syntax highlighting:
 *                              COLOR_KEYWORD, COLOR_STRING, COLOR_COMMENT,
 *                              COLOR_PREPROCESSOR
 */
#include "editor.h"

#include <dos/dos.h>
#include <exec/memory.h>
#include <workbench/icon.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/utility.h>

#include <stdio.h>
#include <string.h>

/** Values of TOOLBAR=, in the order of enum ToolbarMode. */
static const char *const tbmode_names[TBMODE_COUNT] = { "IMAGES", "BOTH", "TEXT" };

/** Default colours of the highlighting, SettingsColour order. */
const unsigned long settings_default_colours[SCOL_COUNT] = {
    0x000000,                       /* text: black */
    0x1030a0,                       /* keywords: blue */
    0x107010,                       /* strings: green */
    0x506050,                       /* comments: grey green */
    0x802070,                       /* preprocessor: plum */
};

/** Tool type names of the colours, SettingsColour order. */
static const char *const colour_names[SCOL_COUNT] = {
    "COLOR_TEXT", "COLOR_KEYWORD", "COLOR_STRING", "COLOR_COMMENT", "COLOR_PREPROCESSOR"
};

void settings_default(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->tbmode = TBMODE_IMAGES;
    s->tbframes = 0;
    s->splash = 1;
    s->winwidth = s->winheight = 0;
    s->winleft = s->wintop = -1;
    s->bmwin[0] = s->bmwin[1] = -1;
    s->bmwin[2] = s->bmwin[3] = 0;
    s->bmlist = 0;
    memcpy(s->colours, settings_default_colours, sizeof(s->colours));
}

/**
 * @brief Value of a hex digit.
 * @param c The character.
 * @return 0..15, or -1.
 */
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)(c | 0x20);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/**
 * @brief Parse "RRGGBB" or "#RRGGBB".
 * @param v The text.
 * @param rgb Out: the colour.
 * @return Non-zero if it is a colour.
 */
static int parse_colour(CONST_STRPTR v, unsigned long *rgb)
{
    unsigned long c = 0;
    int i;
    if (*v == '#') ++v;
    for (i = 0; i < 6; ++i) {
        int d = hex_digit((char)v[i]);
        if (d < 0) return 0;
        c = c << 4 | (unsigned long)d;
    }
    if (v[6] != '\0' && v[6] != ' ') return 0;
    *rgb = c;
    return 1;
}

/**
 * @brief Parse a decimal number of a tool type.
 * @param v The text.
 * @param min Smallest allowed value.
 * @param out Out: the number.
 * @return Non-zero if it is a number >= min.
 */
static int parse_number(CONST_STRPTR v, long min, long *out)
{
    LONG n;
    if (StrToLong((STRPTR)v, &n) <= 0 || n < min) return 0;
    *out = n;
    return 1;
}

/**
 * @brief Parse "left,top,width,height" of a window.
 * @param v The text.
 * @param box Out: the four numbers (unchanged if the text is not valid).
 */
static void parse_box(CONST_STRPTR v, long box[4])
{
    long n[4];
    LONG value, used;
    int i;
    for (i = 0; i < 4; ++i) {
        if ((used = StrToLong((STRPTR)v, &value)) <= 0) return;
        n[i] = value;
        v += used;
        if (i < 3) {
            if (*v != ',') return;
            ++v;
        }
    }
    if (n[2] <= 0 || n[3] <= 0) return;
    for (i = 0; i < 4; ++i) box[i] = n[i];
}

/**
 * @brief Apply the tool types of one icon.
 * @param s The settings.
 * @param tt The icon's tool type array (may be NULL).
 */
static void from_tooltypes(Settings *s, STRPTR *tt)
{
    CONST_STRPTR *t = (CONST_STRPTR *)tt;
    STRPTR value;
    int i;
    if (tt == NULL) return;
    if ((value = FindToolType(t, (CONST_STRPTR)"TOOLBAR")) != NULL)
        for (i = 0; i < TBMODE_COUNT; ++i)
            if (Stricmp(value, (STRPTR)tbmode_names[i]) == 0) s->tbmode = i;
    if (FindToolType(t, (CONST_STRPTR)"TOOLBARFRAMES") != NULL) s->tbframes = 1;
    if (FindToolType(t, (CONST_STRPTR)"NOSPLASH") != NULL) s->splash = 0;
    if ((value = FindToolType(t, (CONST_STRPTR)"WIDTH")) != NULL) parse_number(value, 0, &s->winwidth);
    if ((value = FindToolType(t, (CONST_STRPTR)"HEIGHT")) != NULL) parse_number(value, 0, &s->winheight);
    if ((value = FindToolType(t, (CONST_STRPTR)"LEFT")) != NULL) parse_number(value, -1, &s->winleft);
    if ((value = FindToolType(t, (CONST_STRPTR)"TOP")) != NULL) parse_number(value, -1, &s->wintop);
    if (FindToolType(t, (CONST_STRPTR)"BOOKMARKLIST") != NULL) s->bmlist = 1;
    if ((value = FindToolType(t, (CONST_STRPTR)"BOOKMARKWINDOW")) != NULL) parse_box(value, s->bmwin);
    for (i = 0; i < SCOL_COUNT; ++i)
        if ((value = FindToolType(t, (CONST_STRPTR)colour_names[i])) != NULL)
            parse_colour(value, &s->colours[i]);
}

/**
 * @brief Apply the tool types of an icon given by directory and name.
 * @param s The settings.
 * @param dir Lock of the icon's directory (0 = current directory).
 * @param name The file name without ".info".
 */
static void from_icon(Settings *s, BPTR dir, CONST_STRPTR name)
{
    struct DiskObject *dob;
    BPTR old = 0;
    if (name == NULL || *name == '\0') return;
    if (dir != 0) old = CurrentDir(dir);
    dob = GetDiskObject((STRPTR)name);
    if (dir != 0) CurrentDir(old);
    if (dob == NULL) return;
    from_tooltypes(s, (STRPTR *)dob->do_ToolTypes);
    FreeDiskObject(dob);
}

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
    if (IconBase == NULL) return;
    if (argc == 0) {
        struct WBStartup *wbs = (struct WBStartup *)argv;
        LONG i;
        if (wbs == NULL || wbs->sm_NumArgs < 1) return;
        set_icon_name(s, (CONST_STRPTR)wbs->sm_ArgList[0].wa_Name);
        from_icon(s, wbs->sm_ArgList[0].wa_Lock, (CONST_STRPTR)wbs->sm_ArgList[0].wa_Name);
        for (i = 1; i < wbs->sm_NumArgs; ++i)
            from_icon(s, wbs->sm_ArgList[i].wa_Lock, (CONST_STRPTR)wbs->sm_ArgList[i].wa_Name);
    } else {
        char program[256];
        if (!GetProgramName((STRPTR)program, sizeof(program))) strcpy(program, APP_NAME);
        set_icon_name(s, (CONST_STRPTR)program);
        if (s->icon_name[0] != '\0') from_icon(s, 0, (CONST_STRPTR)s->icon_name);
    }
}

/* -- writing -------------------------------------------------------------- */

int settings_save_toolwindows(const Settings *cur)
{
    Settings s;
    settings_default(&s);
    strcpy(s.icon_name, cur->icon_name);
    from_icon(&s, 0, (CONST_STRPTR)s.icon_name);
    memcpy(s.bmwin, cur->bmwin, sizeof(s.bmwin));
    s.bmlist = cur->bmlist;
    return settings_save(&s);
}

#define NUMKEYS (9 + SCOL_COUNT)    /**< number of TinyDev's tool types */
#define ENTRYLEN 40                 /**< longest tool type TinyDev writes */

/** One of our tool types while writing. */
typedef struct Entry {
    const char *key;                /**< the tool type's name */
    int active;                     /**< not the default value */
    int done;                       /**< written */
    char text[ENTRYLEN];            /**< "KEY" or "KEY=value" */
} Entry;

/**
 * @brief Fill in an entry.
 * @param e The entry.
 * @param key The tool type name.
 * @param active Non-zero if the value is not the default.
 * @param value The value, or NULL for a switch.
 */
static void entry(Entry *e, const char *key, int active, const char *value)
{
    e->key = key;
    e->active = active;
    e->done = 0;
    if (value != NULL) snprintf(e->text, sizeof(e->text), "%s=%s", key, value);
    else snprintf(e->text, sizeof(e->text), "%s", key);
}

/**
 * @brief Report whether a tool type (also a disabled "(KEY=...)") has a key.
 * @param tt The tool type.
 * @param key The key.
 * @return Non-zero on a match (case-insensitive).
 */
static int has_key(CONST_STRPTR tt, const char *key)
{
    const char *p = (const char *)tt;
    if (*p == '(') ++p;
    while (*key != '\0' && (*p | 0x20) == (*key | 0x20)) ++p, ++key;
    return *key == '\0' && (*p == '\0' || *p == '=' || *p == ')' || *p == ' ');
}

int settings_save(const Settings *s)
{
    struct DiskObject *dob;
    Entry *e;
    STRPTR *old, *tt = NULL;
    ULONG nold = 0, n = 0, i, k, nicons;
    char wnum[12], hnum[12], lnum[12], tnum[12], buf[SCOL_COUNT][8], *disabled = NULL;
    char bmbox[48];
    int ok = 0;

    if (IconBase == NULL || s->icon_name[0] == '\0') return 0;
    if ((dob = GetDiskObject((STRPTR)s->icon_name)) == NULL &&
        (dob = GetDiskObjectNew((STRPTR)s->icon_name)) == NULL)
        return 0;
    if ((e = AllocVec(NUMKEYS * sizeof(*e), MEMF_ANY)) == NULL) {
        FreeDiskObject(dob);
        return 0;
    }

    k = 0;
    entry(&e[k++], "TOOLBAR", s->tbmode != TBMODE_IMAGES,
          tbmode_names[s->tbmode >= 0 && s->tbmode < TBMODE_COUNT ? s->tbmode : 0]);
    entry(&e[k++], "TOOLBARFRAMES", s->tbframes, NULL);
    entry(&e[k++], "NOSPLASH", !s->splash, NULL);
    snprintf(wnum, sizeof(wnum), "%ld", s->winwidth);
    snprintf(hnum, sizeof(hnum), "%ld", s->winheight);
    snprintf(lnum, sizeof(lnum), "%ld", s->winleft);
    snprintf(tnum, sizeof(tnum), "%ld", s->wintop);
    entry(&e[k++], "WIDTH", s->winwidth > 0, wnum);
    entry(&e[k++], "HEIGHT", s->winheight > 0, hnum);
    entry(&e[k++], "LEFT", s->winleft >= 0, lnum);
    entry(&e[k++], "TOP", s->wintop >= 0, tnum);
    entry(&e[k++], "BOOKMARKLIST", s->bmlist, NULL);
    snprintf(bmbox, sizeof(bmbox), "%ld,%ld,%ld,%ld", s->bmwin[0], s->bmwin[1], s->bmwin[2], s->bmwin[3]);
    entry(&e[k++], "BOOKMARKWINDOW", s->bmwin[2] > 0, bmbox);
    for (i = 0; i < SCOL_COUNT; ++i) {
        snprintf(buf[i], sizeof(buf[i]), "%06lX", s->colours[i] & 0xFFFFFFUL);
        entry(&e[k++], colour_names[i], s->colours[i] != settings_default_colours[i], buf[i]);
    }

    old = (STRPTR *)dob->do_ToolTypes;
    while (old != NULL && old[nold] != NULL) ++nold;
    /* new entries go in front of the NewIcons block */
    for (nicons = 0; nicons < nold; ++nicons)
        if (strncmp((const char *)old[nicons], "*** DON'T EDIT", 14) == 0) {
            if (nicons != 0 && strcmp((const char *)old[nicons - 1], " ") == 0) --nicons;
            break;
        }

    /* old entries + ours + disabled copies ("(" old ")") + NULL */
    if ((tt = AllocVec((nold + NUMKEYS + 1) * sizeof(STRPTR), MEMF_ANY | MEMF_CLEAR)) != NULL &&
        (disabled = AllocVec(NUMKEYS * (ENTRYLEN + 2), MEMF_ANY)) != NULL) {
        char *dp = disabled;
        for (i = 0; i <= nold; ++i) {
            if (i == nicons)                    /* not yet in the icon */
                for (k = 0; k < NUMKEYS; ++k)
                    if (!e[k].done && e[k].active) {
                        tt[n++] = (STRPTR)e[k].text;
                        e[k].done = 1;
                    }
            if (i == nold) break;
            for (k = 0; k < NUMKEYS && !has_key(old[i], e[k].key); ++k) ;
            if (k == NUMKEYS) {                 /* not ours */
                tt[n++] = old[i];
            } else if (!e[k].done) {
                e[k].done = 1;
                if (e[k].active) tt[n++] = (STRPTR)e[k].text;
                else if (old[i][0] == '(') tt[n++] = old[i];
                else {                          /* default now: disable it */
                    snprintf(dp, ENTRYLEN + 2, "(%s)", (const char *)old[i]);
                    tt[n++] = (STRPTR)dp;
                    dp += strlen(dp) + 1;
                }
            }                                   /* duplicates are dropped */
        }
        dob->do_ToolTypes = (STRPTR *)tt;
        ok = PutDiskObject((STRPTR)s->icon_name, dob) != 0;
        dob->do_ToolTypes = old;
    }
    if (disabled != NULL) FreeVec(disabled);
    if (tt != NULL) FreeVec(tt);
    FreeVec(e);
    FreeDiskObject(dob);
    return ok;
}
