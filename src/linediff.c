/**
 * @file linediff.c
 * @brief Portable line diff (Myers' O(ND) algorithm with the middle snake,
 *        as in GNU diff, without its heuristics) written as unified hunks.
 */
#include "linediff.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/** The lines of one text. */
typedef struct LdText {
    const char **line;          /**< start of each line */
    long *len;                  /**< length of each line, without '\n' */
    unsigned long *hash;        /**< hash of each line */
    char *changed;              /**< non-zero: deleted (old) or inserted (new) */
    long count;                 /**< number of lines */
} LdText;

/** The state of a comparison. */
typedef struct LdDiff {
    LdText a;                   /**< the old text */
    LdText b;                   /**< the new text */
    long *fd;                   /**< forward furthest x per diagonal */
    long *bd;                   /**< backward furthest x per diagonal */
} LdDiff;

/** A growing output buffer. */
typedef struct LdOut {
    char *text;                 /**< the text, NUL-terminated */
    size_t len;                 /**< its length */
    size_t size;                /**< allocated */
    int failed;                 /**< out of memory */
} LdOut;

/**
 * @brief Split a text into lines.
 * @param[out] t The lines (the arrays allocated).
 * @param text The text.
 * @param length Its length.
 * @return Non-zero on success.
 */
static int split(LdText *t, const char *text, size_t length)
{
    size_t i, start;
    long n = 0;
    for (i = 0; i < length; ++i)
        if (text[i] == '\n') ++n;
    if (length > 0 && text[length - 1] != '\n') ++n;
    t->count = n;
    t->line = malloc(sizeof(*t->line) * (size_t)(n + 1));
    t->len = malloc(sizeof(*t->len) * (size_t)(n + 1));
    t->hash = malloc(sizeof(*t->hash) * (size_t)(n + 1));
    t->changed = calloc((size_t)(n + 1), 1);
    if (t->line == NULL || t->len == NULL || t->hash == NULL || t->changed == NULL) return 0;
    n = 0;
    for (start = 0; start < length; ) {
        unsigned long h = 5381;
        for (i = start; i < length && text[i] != '\n'; ++i) h = h * 33 + (unsigned char)text[i];
        t->line[n] = text + start;
        t->len[n] = (long)(i - start);
        t->hash[n] = h;
        ++n;
        start = i + 1;
    }
    return 1;
}

/**
 * @brief Free the arrays of a text's lines.
 * @param t The lines.
 */
static void free_text(LdText *t)
{
    free(t->line);
    free(t->len);
    free(t->hash);
    free(t->changed);
}

/**
 * @brief Whether a line of the old text equals one of the new.
 * @param d The comparison.
 * @param x The old line.
 * @param y The new line.
 * @return Non-zero if equal.
 */
static int equal(const LdDiff *d, long x, long y)
{
    return d->a.hash[x] == d->b.hash[y] && d->a.len[x] == d->b.len[y] &&
           memcmp(d->a.line[x], d->b.line[y], (size_t)d->a.len[x]) == 0;
}

/**
 * @brief Find the middle snake of a part where both sides are non-empty
 *        and differ at both ends.
 *
 * fd and bd are indexed by diagonal (x - y); the caller offsets them.
 *
 * @param d The comparison.
 * @param xoff First old line.
 * @param xlim Old line after the part.
 * @param yoff First new line.
 * @param ylim New line after the part.
 * @param[out] xmid Old line of the split.
 * @param[out] ymid New line of the split.
 */
static void middle(LdDiff *d, long xoff, long xlim, long yoff, long ylim, long *xmid, long *ymid)
{
    long *fd = d->fd, *bd = d->bd;
    long dmin = xoff - ylim, dmax = xlim - yoff;
    long fmid = xoff - yoff, bmid = xlim - ylim;
    long fmin = fmid, fmax = fmid, bmin = bmid, bmax = bmid;
    int odd = ((fmid - bmid) & 1) != 0;
    fd[fmid] = xoff;
    bd[bmid] = xlim;
    for (;;) {
        long k;
        /* forward: extend each diagonal by one edit, then follow the snake */
        if (fmin > dmin) fd[--fmin - 1] = -1; else ++fmin;
        if (fmax < dmax) fd[++fmax + 1] = -1; else --fmax;
        for (k = fmax; k >= fmin; k -= 2) {
            long lo = fd[k - 1], hi = fd[k + 1];
            long x = lo >= hi ? lo + 1 : hi, y = x - k;
            while (x < xlim && y < ylim && equal(d, x, y)) { ++x; ++y; }
            fd[k] = x;
            if (odd && bmin <= k && k <= bmax && bd[k] <= x) { *xmid = x; *ymid = y; return; }
        }
        /* backward */
        if (bmin > dmin) bd[--bmin - 1] = LONG_MAX; else ++bmin;
        if (bmax < dmax) bd[++bmax + 1] = LONG_MAX; else --bmax;
        for (k = bmax; k >= bmin; k -= 2) {
            long lo = bd[k - 1], hi = bd[k + 1];
            long x = lo < hi ? lo : hi - 1, y = x - k;
            while (x > xoff && y > yoff && equal(d, x - 1, y - 1)) { --x; --y; }
            bd[k] = x;
            if (!odd && fmin <= k && k <= fmax && x <= fd[k]) { *xmid = x; *ymid = y; return; }
        }
    }
}

/**
 * @brief Mark the changed lines of a part (recursively, halving the edits).
 * @param d The comparison.
 * @param xoff First old line.
 * @param xlim Old line after the part.
 * @param yoff First new line.
 * @param ylim New line after the part.
 */
static void compare(LdDiff *d, long xoff, long xlim, long yoff, long ylim)
{
    while (xoff < xlim && yoff < ylim && equal(d, xoff, yoff)) { ++xoff; ++yoff; }
    while (xoff < xlim && yoff < ylim && equal(d, xlim - 1, ylim - 1)) { --xlim; --ylim; }
    if (xoff == xlim) {
        while (yoff < ylim) d->b.changed[yoff++] = 1;
    } else if (yoff == ylim) {
        while (xoff < xlim) d->a.changed[xoff++] = 1;
    } else {
        long xmid, ymid;
        middle(d, xoff, xlim, yoff, ylim, &xmid, &ymid);
        compare(d, xoff, xmid, yoff, ymid);
        compare(d, xmid, xlim, ymid, ylim);
    }
}

/**
 * @brief Append text to the output.
 * @param o The output.
 * @param s The text.
 * @param n Its length.
 */
static void put(LdOut *o, const char *s, size_t n)
{
    if (o->failed) return;
    if (o->len + n + 1 > o->size) {
        size_t size = o->size * 2 + n + 256;
        char *t = realloc(o->text, size);
        if (t == NULL) { o->failed = 1; return; }
        o->text = t;
        o->size = size;
    }
    memcpy(o->text + o->len, s, n);
    o->len += n;
    o->text[o->len] = '\0';
}

/**
 * @brief Append a number to the output.
 * @param o The output.
 * @param v The number (not negative).
 */
static void put_number(LdOut *o, long v)
{
    char buf[16];
    int n = (int)sizeof(buf);
    do { buf[--n] = (char)('0' + v % 10); v /= 10; } while (v > 0);
    put(o, buf + n, sizeof(buf) - (size_t)n);
}

/**
 * @brief Append a line with its prefix.
 * @param o The output.
 * @param prefix ' ', '-' or '+'.
 * @param t The text.
 * @param i The line.
 */
static void put_line(LdOut *o, char prefix, const LdText *t, long i)
{
    put(o, &prefix, 1);
    put(o, t->line[i], (size_t)t->len[i]);
    put(o, "\n", 1);
}

/**
 * @brief Append a hunk's header ("@@ -a,b +c,d @@").
 * @param o The output.
 * @param x First old line of the hunk (0-based).
 * @param xn Its old lines.
 * @param y First new line (0-based).
 * @param yn Its new lines.
 */
static void put_header(LdOut *o, long x, long xn, long y, long yn)
{
    /* unified diffs count from 1; an empty side names the line before */
    put(o, "@@ -", 4);
    put_number(o, xn > 0 ? x + 1 : x);
    put(o, ",", 1);
    put_number(o, xn);
    put(o, " +", 2);
    put_number(o, yn > 0 ? y + 1 : y);
    put(o, ",", 1);
    put_number(o, yn);
    put(o, " @@\n", 4);
}

/**
 * @brief Write the marked changes as hunks.
 * @param d The comparison (lines marked).
 * @param o The output.
 * @param context Lines of context.
 */
static void write_hunks(const LdDiff *d, LdOut *o, long context)
{
    const LdText *a = &d->a, *b = &d->b;
    long x = 0, y = 0;
    while (x < a->count || y < b->count) {
        long hx, hy, ex, ey, cx, cy, gap;
        /* skip equal lines up to the next change */
        while (x < a->count && y < b->count && !a->changed[x] && !b->changed[y]) { ++x; ++y; }
        if (x >= a->count && y >= b->count) break;
        hx = x - context < 0 ? 0 : x - context;
        hy = y - (x - hx);
        /* the hunk's end: the last change followed by more than 2 * context
           equal lines (or the end) */
        ex = x;
        ey = y;
        for (;;) {
            while (ex < a->count && a->changed[ex]) ++ex;
            while (ey < b->count && b->changed[ey]) ++ey;
            cx = ex;
            cy = ey;
            gap = 0;
            while (cx < a->count && cy < b->count && !a->changed[cx] && !b->changed[cy] &&
                   gap <= 2 * context) { ++cx; ++cy; ++gap; }
            if (gap > 2 * context || (cx >= a->count && cy >= b->count)) break;
            ex = cx;
            ey = cy;
        }
        cx = ex + context > a->count ? a->count : ex + context;
        cy = ey + (cx - ex);
        put_header(o, hx, cx - hx, hy, cy - hy);
        x = hx;
        y = hy;
        while (x < cx || y < cy) {
            if (x < cx && a->changed[x]) put_line(o, '-', a, x++);
            else if (y < cy && b->changed[y]) put_line(o, '+', b, y++);
            else { put_line(o, ' ', a, x++); ++y; }
        }
    }
}

char *linediff_unified(const char *old, size_t oldlen, const char *new_text, size_t newlen,
                       int context)
{
    LdDiff d;
    LdOut o;
    long diagonals;
    memset(&d, 0, sizeof(d));
    memset(&o, 0, sizeof(o));
    put(&o, "", 0);
    if (split(&d.a, old, oldlen) && split(&d.b, new_text, newlen)) {
        diagonals = d.a.count + d.b.count + 3;
        d.fd = malloc(sizeof(long) * (size_t)diagonals);
        d.bd = malloc(sizeof(long) * (size_t)diagonals);
        if (d.fd != NULL && d.bd != NULL) {
            /* diagonals run from -count(b) - 1 to count(a) + 1 */
            long *fd = d.fd, *bd = d.bd;
            d.fd += d.b.count + 1;
            d.bd += d.b.count + 1;
            compare(&d, 0, d.a.count, 0, d.b.count);
            write_hunks(&d, &o, context < 0 ? 0 : context);
            d.fd = fd;
            d.bd = bd;
        } else {
            o.failed = 1;
        }
    } else {
        o.failed = 1;
    }
    free(d.fd);
    free(d.bd);
    free_text(&d.a);
    free_text(&d.b);
    if (o.failed) {
        free(o.text);
        return NULL;
    }
    return o.text;
}
