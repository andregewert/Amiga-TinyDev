/**
 * @file classlib.h
 * @brief Shared library frame of the TinyDev gadget classes.
 *
 * Every class in src/classes/ is built as its own disk-based class library
 * (SYS:Classes/Gadgets/\<name\>.gadget). The library frame in classlib.c is
 * the same for all of them; the class supplies its name and version in
 * class_config.h (found through the class directory on the include path)
 * and implements the three functions declared below.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef CLASSLIB_H
#define CLASSLIB_H

#include <exec/types.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <exec/lists.h>
#include <utility/hooks.h>
#include <proto/utility.h>

/**
 * exec.library.
 */
extern struct ExecBase      *SysBase;
/**
 * dos.library.
 */
extern struct DosLibrary    *DOSBase;
/**
 * intuition.library.
 */
extern struct IntuitionBase *IntuitionBase;
/**
 * graphics.library.
 */
extern struct GfxBase       *GfxBase;
/**
 * utility.library.
 */
extern struct Library       *UtilityBase;

/**
 * @brief Open the libraries and classes the class needs beyond the frame's.
 *
 * Called from LibInit() after exec, dos, intuition, graphics and utility are
 * open. Must leave nothing open when it fails.
 *
 * @return TRUE on success, FALSE if a required library is missing.
 */
BOOL class_open_libraries(void);

/** @brief Close everything class_open_libraries() opened (NULL-safe). */
void class_close_libraries(void);

/**
 * @brief Create the BOOPSI class (MakeClass() plus dispatcher).
 *
 * The frame publishes the class with AddClass() and frees it on expunge.
 *
 * @param name Public class name, identical to the library name.
 * @return The new class, or NULL on failure.
 */
Class *class_create(const char *name);

/**
 * @brief Initialise an empty exec list (amiga.lib NewList replacement).
 * @param list The list.
 */
static inline void class_new_list(struct List *list)
{
    list->lh_Head = (struct Node *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
}

/**
 * @brief Send a message to an object (amiga.lib DoMethodA replacement).
 * @param o The object.
 * @param msg The message.
 * @return The method's result.
 */
static inline ULONG class_dm(Object *o, Msg msg)
{
    return CallHookPkt(&OCLASS(o)->cl_Dispatcher, o, msg);
}

/**
 * @brief Pass a message to the superclass (amiga.lib DoSuperMethodA replacement).
 * @param cl The class whose superclass receives the message.
 * @param o The object.
 * @param msg The message.
 * @return The superclass method's result.
 */
static inline ULONG class_dsm(Class *cl, Object *o, Msg msg)
{
    return CallHookPkt(&cl->cl_Super->cl_Dispatcher, o, msg);
}

#endif
