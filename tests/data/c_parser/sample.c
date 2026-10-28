/* sample.c: a source for the tests of the C parser (Grüße) */
#include "sample.h"
#include <stdio.h>

static int counter = 0;
int item_count, other_count = 3;

static void (*handler)(int code);

static int helper(Point *p, ULONG flags)
{
    ULONG mask = flags & 3;
    struct List *list = NULL;
    int i;

    for (int k = 0; k < 2; k++) counter += k;
    if (p->x > MAX_ITEMS) goto done;
    for (i = 0; i < MAX_ITEMS; i++) {
        Colour c = RED;
        mask += SQUARE(i) + c;
    }
done:
    return (int)mask + p->y;
}

int add_numbers(int a, int b)
{
    printf("%d\n", helper(NULL, 0));
    return a + b;
}

#ifdef __SASC
int compiler(void) { return 1; }
#else
int compiler(void) { return 2; }
#endif
