/*
 * search.c - portable search engine of the find and replace window
 *
 * Plain text search in 8-bit text, line by line, with Latin-1 case folding
 * and whole-word matching. Used by find.c on the text exported from
 * texteditor.gadget; no Amiga headers, so the host tests can use it.
 */
#include "search.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Fold a character for case-insensitive comparison (ASCII and the
 *        Latin-1 letters; the multiplication and division signs stay).
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
 * @brief Tell whether a character belongs to a word (letters, digits, '_',
 *        the Latin-1 letters).
 * @param c The character.
 * @return Non-zero for a word character.
 */
static int is_word(unsigned char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
        return 1;
    return c >= 0xC0 && c != 0xD7 && c != 0xF7;
}

int search_compile(SearchPattern *p, const char *text, const SearchOptions *options)
{
    long i;
    memset(p, 0, sizeof(*p));
    p->options = *options;
    if (text == NULL || text[0] == '\0') return SEARCH_EMPTY;
    if (options->regex) return SEARCH_NOREGEX;
    p->length = (long)strlen(text);
    p->text = malloc((size_t)p->length + 1);
    if (p->text == NULL) return SEARCH_NOMEM;
    for (i = 0; i <= p->length; ++i)
        p->text[i] = options->case_sensitive ? text[i] : (char)fold((unsigned char)text[i]);
    return SEARCH_OK;
}

void search_free(SearchPattern *p)
{
    free(p->text);
    p->text = NULL;
    p->length = 0;
}

const char *search_error(int error)
{
    switch (error) {
        case SEARCH_OK: return "";
        case SEARCH_EMPTY: return "Nothing to search for";
        case SEARCH_NOMEM: return "Not enough memory";
        case SEARCH_NOREGEX: return "Regular expressions are not available";
    }
    return "Search failed";
}

/**
 * @brief Compare the plain pattern with the line at a column.
 * @param p The pattern.
 * @param line The line.
 * @param at The column (at + p->length must not exceed the line).
 * @return Non-zero if the pattern is there.
 */
static int plain_at(const SearchPattern *p, const char *line, long at)
{
    long i;
    if (p->options.case_sensitive) return memcmp(line + at, p->text, (size_t)p->length) == 0;
    for (i = 0; i < p->length; ++i)
        if (fold((unsigned char)line[at + i]) != (unsigned char)p->text[i]) return 0;
    return 1;
}

/**
 * @brief Tell whether a match stands as a whole word (no word character
 *        directly before or after it).
 * @param line The line.
 * @param length Its length.
 * @param start First column of the match.
 * @param end Column after the match.
 * @return Non-zero if it is a whole word.
 */
static int word_bounds(const char *line, long length, long start, long end)
{
    if (start > 0 && is_word((unsigned char)line[start - 1])) return 0;
    if (end < length && is_word((unsigned char)line[end])) return 0;
    return 1;
}

/**
 * @brief Fill in a match without capture groups other than the match.
 * @param m The match.
 * @param start First column.
 * @param end Column after the match.
 */
static void set_match(SearchMatch *m, long start, long end)
{
    int i;
    m->start = m->group_start[0] = start;
    m->end = m->group_end[0] = end;
    for (i = 1; i < SEARCH_GROUPS; ++i) m->group_start[i] = m->group_end[i] = -1;
}

int search_line_next(const SearchPattern *p, const char *line, long length, long from,
                     SearchMatch *m)
{
    long at;
    if (p->text == NULL) return 0;
    if (from < 0) from = 0;
    for (at = from; at + p->length <= length; ++at) {
        if (!plain_at(p, line, at)) continue;
        if (p->options.whole_word && !word_bounds(line, length, at, at + p->length)) continue;
        set_match(m, at, at + p->length);
        return 1;
    }
    return 0;
}

int search_line_prev(const SearchPattern *p, const char *line, long length, long before,
                     SearchMatch *m)
{
    long at;
    if (p->text == NULL) return 0;
    at = length - p->length;
    if (at > before - 1) at = before - 1;
    for (; at >= 0; --at) {
        if (!plain_at(p, line, at)) continue;
        if (p->options.whole_word && !word_bounds(line, length, at, at + p->length)) continue;
        set_match(m, at, at + p->length);
        return 1;
    }
    return 0;
}

/**
 * @brief Find the end of the line starting at an offset.
 * @param text The text.
 * @param size Its size.
 * @param offset Start of the line.
 * @return Offset of its line break (or size).
 */
static long line_end(const char *text, long size, long offset)
{
    while (offset < size && text[offset] != '\n' && text[offset] != '\r') ++offset;
    return offset;
}

/**
 * @brief Find the start of the line after a line end.
 * @param text The text.
 * @param size Its size.
 * @param end The line end from line_end().
 * @return Start of the next line, or -1 if there is none.
 */
static long next_line(const char *text, long size, long end)
{
    if (end >= size) return -1;
    if (text[end] == '\r' && end + 1 < size && text[end + 1] == '\n') return end + 2;
    return end + 1;
}

/**
 * @brief Fill in a hit.
 * @param hit The hit.
 * @param line Line number.
 * @param offset Offset of the line.
 * @param end End of the line.
 * @param m The match in the line.
 */
static void set_hit(SearchHit *hit, long line, long offset, long end, const SearchMatch *m)
{
    hit->line = line;
    hit->col = m->start;
    hit->length = m->end - m->start;
    hit->line_offset = offset;
    hit->line_length = end - offset;
}

/**
 * @brief Search forward from a position (without wrapping).
 * @param p The pattern.
 * @param text The text.
 * @param size Its size.
 * @param first_line First line to search.
 * @param from_col First column in that line.
 * @param last_line Last line to search (-1: to the end).
 * @param hit The hit.
 * @return Non-zero if found.
 */
static int forward(const SearchPattern *p, const char *text, long size, long first_line,
                   long from_col, long last_line, SearchHit *hit)
{
    long line = 0, offset = 0, end;
    SearchMatch m;
    while (offset >= 0) {
        end = line_end(text, size, offset);
        if (last_line >= 0 && line > last_line) break;
        if (line >= first_line &&
            search_line_next(p, text + offset, end - offset, line == first_line ? from_col : 0, &m)) {
            set_hit(hit, line, offset, end, &m);
            return 1;
        }
        offset = next_line(text, size, end);
        ++line;
    }
    return 0;
}

/**
 * @brief Search for the last match in a range of lines.
 * @param p The pattern.
 * @param text The text.
 * @param size Its size.
 * @param first_line First line of the range.
 * @param last_line Last line of the range (-1: to the end).
 * @param before_col In the last line, matches must start before this
 *                   column (-1: anywhere).
 * @param hit The hit.
 * @return Non-zero if found.
 */
static int last_match(const SearchPattern *p, const char *text, long size, long first_line,
                      long last_line, long before_col, SearchHit *hit)
{
    long line = 0, offset = 0, end, before;
    int found = 0;
    SearchMatch m;
    while (offset >= 0) {
        end = line_end(text, size, offset);
        if (last_line >= 0 && line > last_line) break;
        if (line >= first_line) {
            before = (line == last_line && before_col >= 0) ? before_col : end - offset + 1;
            if (search_line_prev(p, text + offset, end - offset, before, &m)) {
                set_hit(hit, line, offset, end, &m);
                found = 1;
            }
        }
        offset = next_line(text, size, end);
        ++line;
    }
    return found;
}

int search_text(const SearchPattern *p, const char *text, long size, SearchPos from,
                int backwards, int wrap, SearchHit *hit, int *wrapped)
{
    if (wrapped != NULL) *wrapped = 0;
    if (p->text == NULL || text == NULL) return 0;
    if (from.line < 0) from.line = 0;
    if (from.col < 0) from.col = 0;
    if (!backwards) {
        if (forward(p, text, size, from.line, from.col, -1, hit)) return 1;
        if (!wrap || !forward(p, text, size, 0, 0, from.line, hit)) return 0;
    } else {
        if (last_match(p, text, size, 0, from.line, from.col, hit)) return 1;
        if (!wrap || !last_match(p, text, size, from.line, -1, -1, hit)) return 0;
    }
    if (wrapped != NULL) *wrapped = 1;
    return 1;
}

long search_all(const SearchPattern *p, const char *text, long size, SearchHit **hits)
{
    long line = 0, offset = 0, end, from, count = 0, capacity = 0;
    SearchHit *list = NULL;
    SearchMatch m;
    *hits = NULL;
    if (p->text == NULL || text == NULL) return 0;
    while (offset >= 0) {
        end = line_end(text, size, offset);
        from = 0;
        while (from <= end - offset &&
               search_line_next(p, text + offset, end - offset, from, &m)) {
            if (count == capacity) {
                long n = capacity ? capacity * 2 : 64;
                SearchHit *grown = realloc(list, (size_t)n * sizeof(*list));
                if (grown == NULL) { free(list); return -1; }
                list = grown;
                capacity = n;
            }
            set_hit(&list[count++], line, offset, end, &m);
            from = m.end > m.start ? m.end : m.end + 1;
        }
        offset = next_line(text, size, end);
        ++line;
    }
    *hits = list;
    return count;
}

/**
 * @brief Append a character to the expansion buffer.
 * @param out The buffer.
 * @param size Its size.
 * @param n The length so far (counted on even when the buffer is full).
 * @param c The character.
 */
static void put(char *out, long size, long *n, char c)
{
    if (*n < size - 1) out[*n] = c;
    ++*n;
}

long search_expand(const SearchPattern *p, const char *replacement, const char *line,
                   const SearchMatch *m, char *out, long size)
{
    long n = 0, i;
    const char *r;
    for (r = replacement; *r != '\0'; ++r) {
        if (p->options.regex && r[0] == '\\' && r[1] >= '0' && r[1] <= '9') {
            int g = r[1] - '0';
            if (m->group_start[g] >= 0)
                for (i = m->group_start[g]; i < m->group_end[g]; ++i) put(out, size, &n, line[i]);
            ++r;
        } else if (p->options.regex && r[0] == '\\' && r[1] == '\\') {
            put(out, size, &n, '\\');
            ++r;
        } else put(out, size, &n, *r);
    }
    if (size > 0) out[n < size ? n : size - 1] = '\0';
    return n;
}
