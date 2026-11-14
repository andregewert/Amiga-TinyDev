/**
 * @file comment.c
 * @brief Portable core of Edit/Comment Lines (see comment.h).
 */
#include "comment.h"

#include <stdlib.h>
#include <string.h>

int comment_style(const SyntaxRules *rules, CommentStyle *style)
{
    memset(style, 0, sizeof(*style));
    if (rules == NULL) return 0;
    if (rules->line_comment_count > 0) {
        strcpy(style->open, rules->line_comments[0]);
    } else if (rules->comment_word_count > 0) {
        strcpy(style->open, rules->comment_words[0]);
    } else if (rules->column_comment_count > 0) {
        strcpy(style->open, rules->column_comments[0]);
        style->column = 1;
    } else if (rules->block_count > 0) {
        strcpy(style->open, rules->blocks[0].open);
        strcpy(style->close, rules->blocks[0].close);
    }
    return style->open[0] != '\0';
}

/** A line of the text. */
typedef struct Line {
    const char *text;   /**< its start */
    size_t length;      /**< its length (without the line feed) */
    size_t indent;      /**< blanks and tabs at its start */
    size_t end;         /**< its length without blanks and tabs at the end */
} Line;

/**
 * @brief Tell whether a line is commented.
 * @param l The line (not blank).
 * @param style The comment form.
 * @return Non-zero if it is.
 */
static int is_commented(const Line *l, const CommentStyle *style)
{
    size_t at = style->column ? 0 : l->indent, o = strlen(style->open), c = strlen(style->close);
    if (at + o > l->end || memcmp(l->text + at, style->open, o) != 0) return 0;
    if (c == 0) return 1;
    return l->end >= at + o + c && memcmp(l->text + l->end - c, style->close, c) == 0;
}

char *comment_toggle(const char *text, size_t length, const CommentStyle *style, int *commented)
{
    size_t count = 1, i, n = 0, min_indent = (size_t)-1, o = strlen(style->open),
           c = strlen(style->close), size, start = 0;
    Line *lines;
    char *out, *w;
    int all = 1, any = 0;

    for (i = 0; i < length; ++i)
        if (text[i] == '\n') ++count;
    if ((lines = malloc(count * sizeof(Line))) == NULL) return NULL;
    for (i = 0; i <= length; ++i) {
        if (i == length || text[i] == '\n') {
            Line *l = &lines[n++];
            l->text = text + start;
            l->length = i - start;
            start = i + 1;
            for (l->indent = 0; l->indent < l->length &&
                 (l->text[l->indent] == ' ' || l->text[l->indent] == '\t'); ++l->indent) {}
            for (l->end = l->length; l->end > l->indent &&
                 (l->text[l->end - 1] == ' ' || l->text[l->end - 1] == '\t'); --l->end) {}
            if (l->end > l->indent) {
                any = 1;
                if (!is_commented(l, style)) all = 0;
                if (l->indent < min_indent) min_indent = l->indent;
            }
        }
    }
    if (!any) all = 0;
    if (style->column) min_indent = 0;
    *commented = !all;
    size = length + 1 + (all ? 0 : count * (o + c + 2));
    if ((out = malloc(size)) == NULL) {
        free(lines);
        return NULL;
    }
    w = out;
    for (i = 0; i < n; ++i) {
        const Line *l = &lines[i];
        if (l->end <= l->indent) {
            memcpy(w, l->text, l->length);
            w += l->length;
        } else if (all) {
            size_t at = style->column ? 0 : l->indent, from = at + o, to = l->end - c;
            if (from < to && l->text[from] == ' ') ++from;
            if (c > 0 && to > from && l->text[to - 1] == ' ') --to;
            memcpy(w, l->text, at);
            w += at;
            memcpy(w, l->text + from, to - from);
            w += to - from;
            memcpy(w, l->text + l->end, l->length - l->end);
            w += l->length - l->end;
        } else {
            memcpy(w, l->text, min_indent);
            w += min_indent;
            memcpy(w, style->open, o);
            w += o;
            *w++ = ' ';
            if (c == 0) {
                memcpy(w, l->text + min_indent, l->length - min_indent);
                w += l->length - min_indent;
            } else {
                memcpy(w, l->text + min_indent, l->end - min_indent);
                w += l->end - min_indent;
                *w++ = ' ';
                memcpy(w, style->close, c);
                w += c;
                memcpy(w, l->text + l->end, l->length - l->end);
                w += l->length - l->end;
            }
        }
        if (i + 1 < n) *w++ = '\n';
    }
    *w = '\0';
    free(lines);
    return out;
}
