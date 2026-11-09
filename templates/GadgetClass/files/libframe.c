/*
 * libframe.c - The frame of {$NAME.lower}.gadget: the resident tag and the
 * standard functions Open, Close and Expunge. It uses no C runtime.
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 *
 * This object must come first when linking.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <proto/exec.h>
#include <intuition/classes.h>
#include <proto/intuition.h>
#include "{$NAME}_private.h"

/* The first code: running the file as a program returns at once. */
LONG LibStart(void)
{
    return -1;
}

struct ExecBase *SysBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
struct Library *UtilityBase = NULL;
static BPTR seg_list = 0;

#define STR(x)  #x
#define XSTR(x) STR(x)

static const char lib_name[] = LIBNAME;
static const char lib_id[] = "$VER: " LIBNAME " " XSTR(LIBVERSION) "." XSTR(LIBREVISION) "\r\n";

static struct Library *LibInit(REG(d0, struct Library *base), REG(a0, BPTR seglist),
                               REG(a6, struct ExecBase *sysbase));
static struct Library *LibOpen(REG(a6, struct Library *base));
static BPTR LibClose(REG(a6, struct Library *base));
static BPTR LibExpunge(REG(a6, struct Library *base));
static ULONG LibNull(void);
static Class *LibGetClass(REG(a6, struct Library *base));

/* The jump table, in the order of the .sfd file after the four standard
 * vectors. */
static const APTR func_table[] = {
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibNull,
    (APTR)LibGetClass,
    (APTR)-1
};

static const ULONG init_table[4] = {
    sizeof(struct ClassLibrary),
    (ULONG)func_table,
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
    (char *)lib_name,
    (char *)lib_id + 6,
    (APTR)init_table
};

/* Close what LibInit opened; CloseLibrary() accepts NULL. */
static void close_libraries(void)
{
    CloseLibrary(UtilityBase);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
}

/* Free the library base. */
static void free_base(struct Library *base)
{
    FreeMem((UBYTE *)base - base->lib_NegSize,
            (ULONG)(base->lib_NegSize + base->lib_PosSize));
}

/* Called once, when the library is loaded. */
static struct Library *LibInit(REG(d0, struct Library *base), REG(a0, BPTR seglist),
                               REG(a6, struct ExecBase *sysbase))
{
    SysBase = sysbase;
    seg_list = seglist;
    base->lib_Node.ln_Type = NT_LIBRARY;
    base->lib_Node.ln_Name = (char *)lib_name;
    base->lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib_Version = LIBVERSION;
    base->lib_Revision = LIBREVISION;
    base->lib_IdString = (APTR)(lib_id + 6);

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 39);
    if (IntuitionBase != NULL && GfxBase != NULL && UtilityBase != NULL) {
        ((struct ClassLibrary *)base)->cl_Class = {$NAME}_create_class(lib_name);
    }
    if (((struct ClassLibrary *)base)->cl_Class == NULL) {
        close_libraries();
        free_base(base);
        return NULL;
    }
    AddClass(((struct ClassLibrary *)base)->cl_Class);
    return base;
}

static struct Library *LibOpen(REG(a6, struct Library *base))
{
    base->lib_OpenCnt++;
    base->lib_Flags &= (UBYTE)~LIBF_DELEXP;
    return base;
}

/* Called when the system wants the memory; 0 keeps the library loaded. */
static BPTR LibExpunge(REG(a6, struct Library *base))
{
    BPTR seg;

    if (base->lib_OpenCnt != 0) {
        base->lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    if (((struct ClassLibrary *)base)->cl_Class != NULL) {
        Class *cl = ((struct ClassLibrary *)base)->cl_Class;

        RemoveClass(cl);
        if (!FreeClass(cl)) {           /* objects are still alive */
            AddClass(cl);
            base->lib_Flags |= LIBF_DELEXP;
            return 0;
        }
        ((struct ClassLibrary *)base)->cl_Class = NULL;
    }
    seg = seg_list;
    Remove(&base->lib_Node);
    close_libraries();
    free_base(base);
    return seg;
}

static BPTR LibClose(REG(a6, struct Library *base))
{
    if (--base->lib_OpenCnt == 0 && (base->lib_Flags & LIBF_DELEXP) != 0) {
        return LibExpunge(base);
    }
    return 0;
}

/* The first function of the class: its pointer. */
static Class *LibGetClass(REG(a6, struct Library *base))
{
    return ((struct ClassLibrary *)base)->cl_Class;
}

/* The reserved vector. */
static ULONG LibNull(void)
{
    return 0;
}
