/*
 * {$NAME} - {$PURPOSE}
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#include <exec/libraries.h>
#include "{$NAME}_private.h"

/*
 * Every function gets the library base in a6, even if it does not use it.
 * "(void)base;" says that this is deliberate: it generates no code and
 * keeps gcc quiet when warnings for unused parameters are on (-Wextra).
 * vbcc reports the same line as "statement has no effect" (warning 153),
 * which is harmless.
 */

LONG {$NAME}Add(REG(d0, LONG a), REG(d1, LONG b), REG(a6, struct Library *base))
{
    (void)base;
    return a + b;
}

CONST_STRPTR {$NAME}Greeting(REG(a6, struct Library *base))
{
    (void)base;
    return (CONST_STRPTR)"Hello from {$NAME.lower}.library!";
}
