/*
 * classlib.c - library frame of the TinyDev gadget classes
 *
 * A classic ReAction class library: OpenLibrary("gadgets/<name>.gadget")
 * creates and publishes the BOOPSI class "<name>.gadget"; the only library
 * function is <NAME>_GetClass(). Name and version come from the class's
 * class_config.h, the class itself from class_create().
 *
 * This object must be linked first: running the library as a program
 * returns immediately.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <intuition/classes.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/intuition.h>

#include "classlib.h"
#include "class_config.h"

/* Must be the first code in the file (and the object first in the link). */
__asm__(
    "   .text\n"
    "   .globl _LibStart\n"
    "_LibStart:\n"
    "   moveq   #-1,d0\n"
    "   rts\n");

struct ExecBase      *SysBase = NULL;
struct DosLibrary    *DOSBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase = NULL;
struct Library       *UtilityBase = NULL;

static BPTR SegList = 0;

#define STR(x) #x
#define XSTR(x) STR(x)

static const char LibName[] = LIBNAME;
static const char LibId[] = "$VER: " LIBNAME " " XSTR(LIBVERSION) "."
                            XSTR(LIBREVISION) " (" LIBDATE ")\r\n";

/** Library base: a ClassLibrary, whose cl_Class is the published class. */
struct ClassBase {
    struct ClassLibrary cl;
};

static struct Library *LibInit(struct Library *base __asm("d0"),
                               BPTR seglist __asm("a0"),
                               struct ExecBase *sysbase __asm("a6"));
static struct Library *LibOpen(struct Library *base __asm("a6"));
static BPTR LibClose(struct Library *base __asm("a6"));
static BPTR LibExpunge(struct Library *base __asm("a6"));
static ULONG LibNull(void);
static Class *LibGetClass(struct Library *base __asm("a6"));

static const APTR FuncTable[] = {
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibNull,
    (APTR)LibGetClass,
    (APTR)-1
};

static const ULONG InitTable[4] = {
    sizeof(struct ClassBase),
    (ULONG)FuncTable,
    0,
    (ULONG)LibInit
};

const struct Resident ROMTag = {
    RTC_MATCHWORD,
    (struct Resident *)&ROMTag,
    (APTR)(&ROMTag + 1),
    RTF_AUTOINIT,
    LIBVERSION,
    NT_LIBRARY,
    0,
    (char *)LibName,
    (char *)LibId + 6,
    (APTR)InitTable
};

/** @brief Close the frame's and the class's libraries (NULL-safe). */
static void close_libs(void)
{
    class_close_libraries();
    if (UtilityBase != NULL) CloseLibrary(UtilityBase);
    if (GfxBase != NULL) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase != NULL) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase != NULL) CloseLibrary((struct Library *)DOSBase);
    UtilityBase = NULL; GfxBase = NULL; IntuitionBase = NULL; DOSBase = NULL;
}

/**
 * @brief Initialise the library: open libraries and publish the class.
 * @param base The new library base.
 * @param seglist The library's segment list, returned on expunge.
 * @param sysbase exec.library.
 * @return The base on success, NULL (base freed) on failure.
 */
static struct Library *LibInit(struct Library *base __asm("d0"),
                               BPTR seglist __asm("a0"),
                               struct ExecBase *sysbase __asm("a6"))
{
    struct ClassBase *cb = (struct ClassBase *)base;
    Class *cl;

    SysBase = sysbase;
    SegList = seglist;
    base->lib_Node.ln_Type = NT_LIBRARY;
    base->lib_Node.ln_Name = (char *)LibName;
    base->lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib_Version = LIBVERSION;
    base->lib_Revision = LIBREVISION;
    base->lib_IdString = (APTR)(LibId + 6);

    if (sysbase->LibNode.lib_Version < 39 ||
        (DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 39)) == NULL ||
        (IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39)) == NULL ||
        (GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39)) == NULL ||
        (UtilityBase = OpenLibrary("utility.library", 39)) == NULL ||
        !class_open_libraries())
        goto fail;

    if ((cl = class_create(LibName)) == NULL) goto fail;
    cl->cl_UserData = (ULONG)base;
    AddClass(cl);
    cb->cl.cl_Class = cl;
    return base;

fail:
    close_libs();
    FreeMem((UBYTE *)base - base->lib_NegSize,
            (ULONG)(base->lib_NegSize + base->lib_PosSize));
    return NULL;
}

/**
 * @brief Open the library (count up, cancel a delayed expunge).
 * @param base The library base.
 * @return The library base.
 */
static struct Library *LibOpen(struct Library *base __asm("a6"))
{
    base->lib_OpenCnt++;
    base->lib_Flags &= (UBYTE)~LIBF_DELEXP;
    return base;
}

/**
 * @brief Expunge the library if it is unused and no object is alive.
 * @param base The library base.
 * @return The segment list to unload, or 0 to stay in memory.
 */
static BPTR LibExpunge(struct Library *base __asm("a6"))
{
    struct ClassBase *cb = (struct ClassBase *)base;
    Class *cl = cb->cl.cl_Class;
    BPTR seg;

    if (base->lib_OpenCnt != 0) {
        base->lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    if (cl != NULL) {
        RemoveClass(cl);
        if (!FreeClass(cl)) {         /* objects still alive */
            AddClass(cl);
            base->lib_Flags |= LIBF_DELEXP;
            return 0;
        }
        cb->cl.cl_Class = NULL;
    }
    seg = SegList;
    Remove(&base->lib_Node);
    close_libs();
    FreeMem((UBYTE *)base - base->lib_NegSize,
            (ULONG)(base->lib_NegSize + base->lib_PosSize));
    return seg;
}

/**
 * @brief Close the library; expunge it if an expunge was delayed.
 * @param base The library base.
 * @return The segment list if the library was expunged, otherwise 0.
 */
static BPTR LibClose(struct Library *base __asm("a6"))
{
    if (--base->lib_OpenCnt == 0 && (base->lib_Flags & LIBF_DELEXP) != 0)
        return LibExpunge(base);
    return 0;
}

/** @brief Reserved library vector. @return Always 0. */
static ULONG LibNull(void)
{
    return 0;
}

/**
 * @brief First public function: return the class pointer.
 * @param base The library base.
 * @return The published class.
 */
static Class *LibGetClass(struct Library *base __asm("a6"))
{
    return ((struct ClassBase *)base)->cl.cl_Class;
}
