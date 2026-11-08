/*
 * {$NAME}_private.h - What the parts of {$NAME:lower}.library share.
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#ifndef {$NAME:upper}_PRIVATE_H
#define {$NAME:upper}_PRIVATE_H

#include <exec/types.h>

/* Parameters in registers, as the library's functions get them. */
#if defined(__VBCC__)
#define REG(r, decl) __reg(#r) decl
#elif defined(__GNUC__)
#define REG(r, decl) decl __asm(#r)
#else
#error This frame knows gcc and vbcc.
#endif

#define LIBNAME     "{$NAME:lower}.library"
#define LIBVERSION  1
#define LIBREVISION 0

/* The library's functions; the base is always in a6. */
LONG {$NAME}Add(REG(d0, LONG a), REG(d1, LONG b), REG(a6, struct Library *base));
CONST_STRPTR {$NAME}Greeting(REG(a6, struct Library *base));

#endif
