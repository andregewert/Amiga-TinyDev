#include "editor.h"

#include <dos/dos.h>
#include <workbench/icon.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/utility.h>

#include <string.h>

/* Settings are tool types of the program icon (PROGDIR:<program>), also
 * when TinyDev is started from the Shell; on a Workbench start the tool
 * types of project icons follow and take precedence. Writing them (a
 * settings window) comes later.
 *
 *   TOOLBAR=IMAGES|BOTH|TEXT   what the toolbar buttons show
 *   TOOLBARFRAMES              frames around the toolbar buttons
 *   NOSPLASH                   no splash window while starting
 */

/** Values of TOOLBAR=, in the order of enum ToolbarMode. */
static const char *const tbmode_names[TBMODE_COUNT] = { "IMAGES", "BOTH", "TEXT" };

/**
 * @brief Set the default settings.
 * @param s The settings to fill in.
 */
void settings_default(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->tbmode = TBMODE_IMAGES;
    s->tbframes = 0;
    s->splash = 1;
}

/**
 * @brief Apply the tool types of one icon.
 * @param s The settings.
 * @param tt The icon's tool type array (may be NULL).
 */
static void from_tooltypes(Settings *s, STRPTR *tt)
{
    STRPTR value;
    int i;
    if (tt == NULL) return;
    if ((value = FindToolType((CONST_STRPTR *)tt, (CONST_STRPTR)"TOOLBAR")) != NULL)
        for (i = 0; i < TBMODE_COUNT; ++i)
            if (Stricmp(value, (STRPTR)tbmode_names[i]) == 0) s->tbmode = i;
    if (FindToolType((CONST_STRPTR *)tt, (CONST_STRPTR)"TOOLBARFRAMES") != NULL) s->tbframes = 1;
    if (FindToolType((CONST_STRPTR *)tt, (CONST_STRPTR)"NOSPLASH") != NULL) s->splash = 0;
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
 * @brief Remember the program icon as PROGDIR:<program name>.
 * @param s The settings.
 * @param program The program's name or path.
 */
static void set_icon_name(Settings *s, CONST_STRPTR program)
{
    strcpy(s->icon_name, "PROGDIR:");
    if (!AddPart((STRPTR)s->icon_name, FilePart((STRPTR)program), sizeof(s->icon_name)))
        s->icon_name[0] = '\0';
}

/**
 * @brief Read the settings from the program icon and project icons.
 * @param s The settings, preset with settings_default().
 * @param argc The argument count, 0 for a Workbench start.
 * @param argv The arguments, or the WBStartup message for a Workbench start.
 */
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
