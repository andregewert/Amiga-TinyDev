/*
 * hello.c - the demo project of TinyDev's Build menu.
 *
 * Build Project (Amiga+E) runs "make", Run (Amiga+R) starts the program.
 */
#include <stdio.h>

int main(void)
{
    int i;

    printf("Hello, World!\n");
    for (i = 1; i <= 3; ++i)
        printf("Line %d of the demo\n", i);
    return 0;
}
