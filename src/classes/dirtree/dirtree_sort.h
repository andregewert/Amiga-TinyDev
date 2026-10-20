/**
 * @file dirtree_sort.h
 * @brief Natural order of names for dirtree.gadget (private, portable).
 *
 * No Amiga headers, so the host tests can use it.
 */
#ifndef DIRTREE_SORT_H
#define DIRTREE_SORT_H

/**
 * @brief Compare two names in natural order.
 *
 * Runs of digits are compared as numbers ("Kapitel 2" before "Kapitel 10",
 * leading zeros do not count), everything else ignoring case (ASCII and
 * the Latin-1 letters). Names that are equal that way are ordered by the
 * shorter run of leading zeros, then case-sensitively, so the order is
 * total.
 *
 * @param a A name.
 * @param b Another name.
 * @return <0 if a comes first, 0 if they are the same, >0 otherwise.
 */
int dirtree_natural_compare(const char *a, const char *b);

#endif
