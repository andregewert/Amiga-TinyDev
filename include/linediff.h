/**
 * @file linediff.h
 * @brief Portable line diff of two texts as unified diff hunks (Myers'
 *        algorithm in linear space); no Amiga headers.
 *
 * TinyDev makes the diffs of the git window itself: am-git's diff reads
 * the work tree under "./path", which AmigaOS does not know, and shows
 * every working file as empty (am-git 0.12.3, DiffCommand.renderUnified).
 */
#ifndef LINEDIFF_H
#define LINEDIFF_H

#include <stddef.h>

/**
 * @brief Compare two texts line by line and write the changes as the
 *        hunks of a unified diff.
 *
 * Lines end with a line feed (a carriage return before it is part of
 * the line); a last line without one counts too. The result has a line
 * "@@ -a,b +c,d @@" per hunk followed by its lines, each starting with
 * ' ' (context), '-' (only in @p old) or '+' (only in @p new_text) and
 * ending with a line feed; there is no file header. Equal texts give "".
 *
 * @param old The old text (need not be terminated).
 * @param oldlen Its length.
 * @param new_text The new text (need not be terminated).
 * @param newlen Its length.
 * @param context Lines of context around a change (git uses 3).
 * @return The hunks (free() them), or NULL if out of memory.
 */
char *linediff_unified(const char *old, size_t oldlen, const char *new_text, size_t newlen,
                       int context);

#endif
