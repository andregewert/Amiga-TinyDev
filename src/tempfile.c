/**
 * @file tempfile.c
 * @brief Names of TinyDev's temporary files (in the drawer of the setting
 *        TEMPDIR), unique among the programs running.
 */
#include "editor.h"

#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

int temp_name(EditorApp *app, const char *suffix, char *out, size_t size)
{
    static unsigned long count;
    char name[48];
    snprintf(name, sizeof(name), APP_NAME "-%08lX-%lu%s", (unsigned long)FindTask(NULL), ++count, suffix);
    strncpy(out, app->settings.tempdir[0] != '\0' ? app->settings.tempdir : "T:", size - 1);
    out[size - 1] = '\0';
    return AddPart((STRPTR)out, (STRPTR)name, (ULONG)size) != 0;
}
