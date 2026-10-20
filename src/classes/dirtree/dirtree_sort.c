/**
 * @file dirtree_sort.c
 * @brief Natural order of names for dirtree.gadget (private, portable).
 *
 * Only plain C without the C library, so the class (which has no full C
 * runtime) and the host tests can use it.
 */
#include "dirtree_sort.h"

/**
 * @brief Fold a character for comparing (ASCII and Latin-1 letters).
 * @param c The character.
 * @return Its lower-case form.
 */
static unsigned char fold(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return (unsigned char)(c + 32);
    return c;
}

/**
 * @brief Tell whether a character is a decimal digit.
 * @param c The character.
 * @return Non-zero for '0' to '9'.
 */
static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

int dirtree_natural_compare(const char *a, const char *b)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    int zeros = 0;      /* first difference in leading zeros, as a tie-break */

    while (*x != '\0' && *y != '\0') {
        if (is_digit(*x) && is_digit(*y)) {
            const unsigned char *sx, *sy;
            long nx = 0, ny = 0;
            int lx = 0, ly = 0, diff = 0;
            /* leading zeros do not count */
            while (*x == '0') { ++x; ++nx; }
            while (*y == '0') { ++y; ++ny; }
            if (zeros == 0 && nx != ny) zeros = nx < ny ? -1 : 1;
            sx = x;
            sy = y;
            while (is_digit(*x)) { ++x; ++lx; }
            while (is_digit(*y)) { ++y; ++ly; }
            /* the longer number is larger; otherwise the first differing digit */
            if (lx != ly) return lx < ly ? -1 : 1;
            for (; sx < x; ++sx, ++sy)
                if (*sx != *sy) { diff = *sx < *sy ? -1 : 1; break; }
            if (diff != 0) return diff;
            continue;
        }
        if (fold(*x) != fold(*y)) return fold(*x) < fold(*y) ? -1 : 1;
        ++x;
        ++y;
    }
    if (*x != '\0' || *y != '\0') return *x == '\0' ? -1 : 1;
    if (zeros != 0) return zeros;
    /* the same but for case: a total order */
    for (x = (const unsigned char *)a, y = (const unsigned char *)b; *x != '\0'; ++x, ++y)
        if (*x != *y) return *x < *y ? -1 : 1;
    return *y == '\0' ? 0 : -1;
}
