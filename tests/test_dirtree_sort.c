/**
 * @file test_dirtree_sort.c
 * @brief Host-native tests of the natural order of dirtree.gadget.
 */
#include "dirtree_sort.h"

#include <assert.h>
#include <stdio.h>

/**
 * @brief Assert the order of two names, both ways round.
 * @param a The name that comes first.
 * @param b The name that comes after it.
 */
static void before(const char *a, const char *b)
{
    assert(dirtree_natural_compare(a, b) < 0);
    assert(dirtree_natural_compare(b, a) > 0);
}

/**
 * @brief Run the natural order tests.
 * @return 0 when all assertions pass.
 */
int main(void)
{
    before("Kapitel 2", "Kapitel 10");
    before("1-Einleitung.html", "2-Bedienung.html");
    before("2-Bedienung.html", "10-Anhang.html");
    before("a", "B");                       /* case does not decide first */
    before("B", "c");
    before("file", "file1");
    before("file1", "file1a");
    before("file9z", "file10a");
    before("x1y2", "x1y10");
    before("\xC4pfel", "\xE4rger");          /* Latin-1 letters fold */
    before("02", "2a");
    before("2", "02");                      /* fewer leading zeros first */
    before("A", "a");                       /* only then case */
    before("", "a");
    assert(dirtree_natural_compare("index.html", "index.html") == 0);
    assert(dirtree_natural_compare("007", "007") == 0);
    before("99", "100");
    before("img", "index.html");
    puts("dirtree sort tests passed");
    return 0;
}
