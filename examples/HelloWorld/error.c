/*
 * error.c - does not compile on purpose: the command "Compile error (demo)"
 * of the project settings shows how a failed build looks.
 */
#include <stdio.h>

int main(void)
{
    printf("This line lacks its semicolon\n")
    return undefined_variable;
}
