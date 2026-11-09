/*
 * proto/{$NAME.lower}.h - Calling the functions of {$NAME.lower}.library.
 * The caller defines {$NAME}Base and opens the library.
 */
#ifndef PROTO_{$NAME.upper}_H
#define PROTO_{$NAME.upper}_H

#include <exec/types.h>
#include <exec/libraries.h>

extern struct Library *{$NAME}Base;

#if defined(__VBCC__)

LONG __{$NAME}Add(__reg("a6") struct Library *, __reg("d0") LONG a, __reg("d1") LONG b) = "\tjsr\t-30(a6)";
#define {$NAME}Add(a, b) __{$NAME}Add({$NAME}Base, (a), (b))

CONST_STRPTR __{$NAME}Greeting(__reg("a6") struct Library *) = "\tjsr\t-36(a6)";
#define {$NAME}Greeting() __{$NAME}Greeting({$NAME}Base)

#elif defined(__GNUC__)

#include <inline/macros.h>

#define {$NAME}Add(a, b) \
    LP2(0x1e, LONG, {$NAME}Add, LONG, a, d0, LONG, b, d1, , {$NAME}Base)

#define {$NAME}Greeting() \
    LP0(0x24, CONST_STRPTR, {$NAME}Greeting, , {$NAME}Base)

#else
#error This header knows gcc and vbcc.
#endif

#endif
