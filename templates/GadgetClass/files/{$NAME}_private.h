/*
 * {$NAME}_private.h - What the parts of {$NAME:lower}.gadget share.
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

#include <intuition/classes.h>

#define LIBNAME     "{$NAME:lower}.gadget"
#define LIBVERSION  1
#define LIBREVISION 0

/* Make the class (not yet public); NULL on failure. */
Class *{$NAME}_create_class(const char *name);

#endif
