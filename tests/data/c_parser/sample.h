/* sample.h: a header for the tests of the C parser */
#ifndef SAMPLE_H
#define SAMPLE_H

#include <exec/types.h>

#define MAX_ITEMS 16
#define SQUARE(x) ((x) * (x))

typedef struct Point {
    LONG x;
    LONG y;
} Point;

typedef enum { RED, GREEN = 2, BLUE } Colour;

struct List;

extern int item_count;
int add_numbers(int a, int b);

#endif
