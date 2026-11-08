/*
 * {$NAME} - {$PURPOSE}
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#include <exec/libraries.h>
#include "{$NAME}_private.h"

LONG {$NAME}Add(REG(d0, LONG a), REG(d1, LONG b), REG(a6, struct Library *base))
{
    (void)base;
    return a + b;
}

CONST_STRPTR {$NAME}Greeting(REG(a6, struct Library *base))
{
    (void)base;
    return (CONST_STRPTR)"Hello from {$NAME:lower}.library!";
}
