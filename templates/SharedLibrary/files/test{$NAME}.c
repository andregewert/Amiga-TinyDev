/*
 * test{$NAME}.c - Tries the functions of {$NAME:lower}.library.
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#include <stdio.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/{$NAME:lower}.h>

struct Library *{$NAME}Base = NULL;

int main(void)
{
    {$NAME}Base = OpenLibrary((STRPTR)"PROGDIR:{$NAME:lower}.library", 1);
    if ({$NAME}Base == NULL) {
        printf("Cannot open {$NAME:lower}.library.\n");
        return RETURN_FAIL;
    }
    printf("%s\n", (const char *){$NAME}Greeting());
    printf("2 + 3 = %ld\n", (long){$NAME}Add(2, 3));
    CloseLibrary({$NAME}Base);
    return RETURN_OK;
}
