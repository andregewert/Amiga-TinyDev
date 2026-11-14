/**
 * @file comment.h
 * @brief Portable core of Edit/Comment Lines: the comment form of a
 *        language and commenting or uncommenting lines with it; no Amiga
 *        headers.
 */
#ifndef COMMENT_H
#define COMMENT_H

#include "rules.h"

#include <stddef.h>

/** How lines are commented in a language. */
typedef struct CommentStyle {
    char open[RULES_DELIM];     /**< what starts the comment ("//", ";", "REM", "/" "*") */
    char close[RULES_DELIM];    /**< what ends it, "" for a comment to the end of the line */
    int column;                 /**< it must stand in the line's first column */
} CommentStyle;

/**
 * @brief Choose the comment form of a language from its lexical rules.
 *
 * The first comment to the end of the line is taken, then a word that
 * starts one (REM), then a comment of the first column (an assembler's
 * '*'), and last the first block comment, which then encloses each line.
 *
 * @param rules The rules, NULL for none.
 * @param[out] style The form.
 * @return Non-zero if the language has comments.
 */
int comment_style(const SyntaxRules *rules, CommentStyle *style);

/**
 * @brief Comment lines, or uncomment them if all are commented.
 *
 * Lines are separated by line feeds. Blank lines stay as they are and
 * do not count. To uncomment, every other line must start (after its
 * indentation; in the first column for a column comment) with the
 * opening (and end with the closing) delimiter; it is removed with one
 * blank beside it. Otherwise each line gets the opening delimiter and a
 * blank at the smallest indentation of the lines (and a blank and the
 * closing delimiter at its end).
 *
 * @param text The lines (need not be terminated).
 * @param length Their length.
 * @param style The comment form.
 * @param[out] commented 1 if the lines were commented, 0 if uncommented.
 * @return The new lines (terminated; free() them), NULL if out of memory.
 */
char *comment_toggle(const char *text, size_t length, const CommentStyle *style, int *commented);

#endif
