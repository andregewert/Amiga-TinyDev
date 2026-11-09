/*
 * test{$NAME}.c - A window with {$NAME.lower}.gadget in it.
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/window.h>

#include <gadgets/{$NAME.lower}.h>

struct IntuitionBase *IntuitionBase = NULL;
struct Library *WindowBase = NULL;
struct Library *LayoutBase = NULL;
static struct Library *ClassBase = NULL;

int main(void)
{
    Object *window_object = NULL;
    int rc = RETURN_FAIL;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    WindowBase = OpenLibrary((STRPTR)"window.class", 44);
    LayoutBase = OpenLibrary((STRPTR)"gadgets/layout.gadget", 44);
    /* Opening the library makes its class public. */
    ClassBase = OpenLibrary((STRPTR)"PROGDIR:{$NAME.lower}.gadget", 1);

    if (IntuitionBase != NULL && WindowBase != NULL && LayoutBase != NULL && ClassBase != NULL) {
        window_object = NewObject(WINDOW_GetClass(), NULL,
            WA_Title, (ULONG)"{$PROJECT}",
            WA_Activate, TRUE,
            WA_DepthGadget, TRUE,
            WA_DragBar, TRUE,
            WA_CloseGadget, TRUE,
            WA_SizeGadget, TRUE,
            WINDOW_Position, WPOS_CENTERSCREEN,
            WINDOW_Layout, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_SpaceOuter, TRUE,
                LAYOUT_AddChild, (ULONG)NewObject(NULL, (STRPTR)"{$NAME.lower}.gadget",
                    GA_ID, 1,
                    GA_RelVerify, TRUE,
                    {$NAME}_On, TRUE,
                TAG_END),
                CHILD_MinWidth, 160,
                CHILD_MinHeight, 60,
            TAG_END),
        TAG_END);
    }

    if (window_object != NULL && RA_OpenWindow(window_object) != NULL) {
        ULONG window_signal = 0, result;
        WORD code;
        int running = 1;

        GetAttr(WINDOW_SigMask, window_object, &window_signal);
        while (running) {
            if (Wait(window_signal | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) running = 0;
            while ((result = RA_HandleInput(window_object, &code)) != WMHI_LASTMSG) {
                if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) running = 0;
            }
        }
        rc = RETURN_OK;
    }

    if (window_object != NULL) DisposeObject(window_object);
    CloseLibrary(ClassBase);
    CloseLibrary(LayoutBase);
    CloseLibrary(WindowBase);
    CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
