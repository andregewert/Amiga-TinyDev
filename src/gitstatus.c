/**
 * @file gitstatus.c
 * @brief Portable reading of am-git's status (see gitstatus.h).
 */
#include "gitstatus.h"

#include <stdlib.h>
#include <string.h>

void gitstatus_init(GitStatus *st)
{
    st->entries = NULL;
    st->count = 0;
}

void gitstatus_clear(GitStatus *st)
{
    long i;
    for (i = 0; i < st->count; ++i) free(st->entries[i].path);
    free(st->entries);
    gitstatus_init(st);
}

/**
 * @brief A character in lower case (ISO-8859-1).
 * @param c The character.
 * @return It in lower case.
 */
static int lower(int c)
{
    c &= 0xFF;
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) c += 32;
    return c;
}

/**
 * @brief Compare at most n characters of two strings, ignoring case.
 * @param a The first.
 * @param b The second.
 * @param n The most to compare.
 * @return Less than, equal to or greater than 0.
 */
static int compare_n(const char *a, const char *b, size_t n)
{
    for (; n > 0; --n, ++a, ++b) {
        int d = lower((unsigned char)*a) - lower((unsigned char)*b);
        if (d != 0 || *a == '\0') return d;
    }
    return 0;
}

/**
 * @brief qsort() comparison of two entries by path, ignoring case.
 * @param a The first entry.
 * @param b The second.
 * @return Less than, equal to or greater than 0.
 */
static int by_path(const void *a, const void *b)
{
    return compare_n(((const GitStatusEntry *)a)->path, ((const GitStatusEntry *)b)->path, (size_t)-1);
}

/**
 * @brief The state of a status line's two letters.
 * @param x The index's letter.
 * @param y The work tree's letter.
 * @return A GitState; GIT_ST_CLEAN for a line to leave out.
 */
static int state_of(char x, char y)
{
    if (x == '?' && y == '?') return GIT_ST_UNTRACKED;
    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) return GIT_ST_CONFLICT;
    if (y == 'D') return GIT_ST_CLEAN;              /* not on disk */
    if (y == 'M' || y == 'T' || y == 'A') return GIT_ST_MODIFIED;
    if (x == 'D') return GIT_ST_CLEAN;
    if (strchr("MADRCT", x) != NULL && x != '\0') return GIT_ST_STAGED;
    return GIT_ST_CLEAN;
}

/**
 * @brief Copy a path, taking off git's double quotes and C escapes.
 * @param s The path as printed.
 * @param n Its length.
 * @return The path (free() it), or NULL if out of memory.
 */
static char *copy_path(const char *s, size_t n)
{
    char *out = malloc(n + 1), *o = out;
    size_t i;
    if (out == NULL) return NULL;
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        for (i = 1; i + 1 < n; ++i) {
            char c = s[i];
            if (c == '\\' && i + 2 < n) {
                c = s[++i];
                if (c >= '0' && c <= '7') {
                    int v = 0, k;
                    for (k = 0; k < 3 && i + 1 < n && s[i] >= '0' && s[i] <= '7'; ++k) v = v * 8 + (s[i++] - '0');
                    --i;
                    c = (char)v;
                } else if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
            }
            *o++ = c;
        }
    } else {
        memcpy(out, s, n);
        o = out + n;
    }
    *o = '\0';
    return out;
}

int gitstatus_parse(GitStatus *st, const char *text)
{
    long cap = 0;
    const char *line = text;

    gitstatus_clear(st);
    while (line != NULL && *line != '\0') {
        const char *end = strchr(line, '\n'), *path, *arrow;
        size_t len = end != NULL ? (size_t)(end - line) : strlen(line);
        int state;
        if (len > 0 && line[len - 1] == '\r') --len;
        /* "XY path": two letters, a space, the path */
        if (len >= 4 && line[2] == ' ' && strchr(" MADRCTU?!", line[0]) != NULL &&
            strchr(" MADRCTU?!", line[1]) != NULL && (state = state_of(line[0], line[1])) != GIT_ST_CLEAN) {
            size_t plen = len - 3;
            char *p;
            path = line + 3;
            /* a rename: the new name counts */
            for (arrow = path; arrow + 4 <= path + plen; ++arrow)
                if (memcmp(arrow, " -> ", 4) == 0) {
                    plen -= (size_t)(arrow + 4 - path);
                    path = arrow + 4;
                    break;
                }
            if (st->count == cap) {
                long ncap = cap > 0 ? cap * 2 : 16;
                GitStatusEntry *e = realloc(st->entries, (size_t)ncap * sizeof(*e));
                if (e == NULL) {
                    gitstatus_clear(st);
                    return 0;
                }
                st->entries = e;
                cap = ncap;
            }
            if ((p = copy_path(path, plen)) == NULL) {
                gitstatus_clear(st);
                return 0;
            }
            st->entries[st->count].path = p;
            st->entries[st->count].state = state;
            ++st->count;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    if (st->count > 1) qsort(st->entries, (size_t)st->count, sizeof(st->entries[0]), by_path);
    return 1;
}

/**
 * @brief The first entry whose path is not before a text (binary search).
 * @param st The status.
 * @param key The text.
 * @param n How much of the paths to compare (the length of a prefix).
 * @return Its index (st->count if none).
 */
static long lower_bound(const GitStatus *st, const char *key, size_t n)
{
    long lo = 0, hi = st->count;
    while (lo < hi) {
        long mid = (lo + hi) / 2;
        if (compare_n(st->entries[mid].path, key, n) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

int gitstatus_state(const GitStatus *st, const char *path, int directory)
{
    size_t n = strlen(path), k;
    long i;
    int best = GIT_ST_CLEAN;

    if (st->count == 0) return GIT_ST_CLEAN;
    if (!directory) {
        i = lower_bound(st, path, (size_t)-1);
        if (i < st->count && compare_n(st->entries[i].path, path, (size_t)-1) == 0) return st->entries[i].state;
    } else if (n == 0) {
        /* the root: everything is below it */
        for (i = 0; i < st->count; ++i)
            if (st->entries[i].state > best) best = st->entries[i].state;
        return best;
    } else {
        /* the entries below "path/" (an untracked "path/" among them) */
        char *prefix = malloc(n + 2);
        if (prefix == NULL) return GIT_ST_CLEAN;
        memcpy(prefix, path, n);
        prefix[n] = '/';
        prefix[n + 1] = '\0';
        for (i = lower_bound(st, prefix, n + 1);
             i < st->count && compare_n(st->entries[i].path, prefix, n + 1) == 0; ++i)
            if (st->entries[i].state > best) best = st->entries[i].state;
        free(prefix);
        if (best != GIT_ST_CLEAN) return best;
    }
    /* an untracked drawer above it ("dir/") */
    for (k = n; k > 0; --k) {
        if (path[k - 1] != '/') continue;
        i = lower_bound(st, path, k);
        if (i < st->count && strlen(st->entries[i].path) == k &&
            compare_n(st->entries[i].path, path, k) == 0)
            return st->entries[i].state;
    }
    return best;
}

const char *gitstatus_relative(const char *root, const char *path)
{
    size_t n = strlen(root);
    if (n == 0 || compare_n(root, path, n) != 0) return NULL;
    path += n;
    if (*path == '\0') return path;
    if (root[n - 1] == ':' || root[n - 1] == '/') return path;
    return *path == '/' ? path + 1 : NULL;
}

int gitstatus_head(const char *text, char *out, size_t size)
{
    size_t n;
    if (size == 0) return 0;
    out[0] = '\0';
    if (strncmp(text, "ref:", 4) == 0) {
        const char *ref = text + 4;
        while (*ref == ' ' || *ref == '\t') ++ref;
        if (strncmp(ref, "refs/heads/", 11) == 0) ref += 11;
        n = strcspn(ref, "\r\n \t");
        if (n == 0) return 0;
        if (n >= size) n = size - 1;
        memcpy(out, ref, n);
        out[n] = '\0';
        return 1;
    }
    for (n = 0; text[n] != '\0' && strchr("0123456789abcdefABCDEF", text[n]) != NULL; ++n) ;
    if (n < 7) return 0;
    n = 7 < size - 1 ? 7 : size - 1;
    memcpy(out, text, n);
    out[n] = '\0';
    return 1;
}
