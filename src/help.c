/**
 * @file help.c
 * @brief Help command: starts the HelpViewer on PROGDIR:Help.
 *
 * The HelpViewer runs on its own, so TinyDev can be quit while the help
 * stays open: it is started asynchronously from a Shell of its own
 * (SystemTags() with SYS_Asynch). PORTNAME TinyDev keeps it to one viewer
 * for TinyDev: using Help again brings that viewer to the front instead of
 * opening another one (the HelpViewer picks its language drawer itself).
 * OpenWorkbenchObjectA() is not used, it cannot pass options.
 */
#include "editor.h"

#include <dos/dostags.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

void help_open(EditorApp *app)
{
    /* static: path buffers are too large for the stack */
    static char progdir[EDITOR_PATH_MAX], program[EDITOR_PATH_MAX], help[EDITOR_PATH_MAX];
    static char command[2 * EDITOR_PATH_MAX + 32];
    BPTR lock, in;
    int ok = 0;

    if (!NameFromLock(GetProgramDir(), (STRPTR)progdir, sizeof(progdir))) progdir[0] = '\0';
    strcpy(program, progdir);
    strcpy(help, progdir);
    if (progdir[0] != '\0' && AddPart((STRPTR)program, (STRPTR)"HelpViewer", sizeof(program)) &&
        AddPart((STRPTR)help, (STRPTR)"Help", sizeof(help)) &&
        (lock = Lock((STRPTR)program, ACCESS_READ)) != 0) {
        UnLock(lock);
        snprintf(command, sizeof(command), "\"%s\" \"%s\" PORTNAME " APP_NAME, program, help);
        if ((in = Open((STRPTR)"NIL:", MODE_OLDFILE)) != 0) {
            /* asynchronous: the system closes the input when the program ends */
            ok = SystemTags((STRPTR)command, SYS_Input, (ULONG)in, SYS_Output, 0,
                            SYS_Asynch, TRUE, TAG_DONE) != -1;
            if (!ok) Close(in);
        }
    }
    if (!ok) ui_error(app, APP_NAME, S(MSG_HELP_FAILED));
}
