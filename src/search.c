/*
 * search.c - portable search engine of the find and replace window
 *
 * Plain text and regular expressions in 8-bit text, line by line, with
 * Latin-1 case folding and whole-word matching. Used by find.c on the text
 * exported from texteditor.gadget; no Amiga headers, so the host tests can
 * use it.
 *
 * Regular expressions are POSIX extended ones from Henry Spencer's regex
 * library (submodule external/regex; its regex.h is generated into
 * build/regex). regexec() runs with REG_STARTEND on the line, so lines
 * need no terminator; REG_NOTBOL keeps '^' from matching in the middle of
 * a line. Its REG_ICASE folds with <ctype.h>, which may only know ASCII,
 * so case-insensitive searches fold pattern and line themselves. Its
 * character classes ([:alpha:] ...) only list ASCII characters, so
 * latin1_classes() adds the Latin-1 ones to them in the pattern.
 */
#include "search.h"

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include "regex.h"

/** Buffer for folded lines of case-insensitive regular expressions. */
struct SearchScratch {
    char *line;
    long size;
};

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

/**
 * @brief Set the error message of a pattern.
 * @param p The pattern.
 * @param text The message.
 */
static void set_error(SearchPattern *p, const char *text)
{
    strncpy(p->error, text, sizeof(p->error) - 1);
    p->error[sizeof(p->error) - 1] = '\0';
}

/**
 * @brief Return the Latin-1 characters (0xA0-0xFF) of a character class
 *        that Spencer's ASCII class table lacks.
 * @param name The class name (after "[:").
 * @param length Length of the name.
 * @param case_sensitive Zero if the text is folded (then upper and lower
 *                       stand for all letters, as with REG_ICASE).
 * @param out Buffer of at least 96 characters for the characters.
 * @param ascii Set to the class to use in place of the name, or NULL to
 *              keep it.
 * @return The number of characters written.
 */
static int latin1_class(const char *name, long length, int case_sensitive, char *out,
                        const char **ascii)
{
    int n = 0, c, upper = 0, lower = 0, punct = 0, graph = 0;
    *ascii = NULL;
#define IS(s) (length == (long)sizeof(s) - 1 && memcmp(name, s, sizeof(s) - 1) == 0)
    if (IS("alpha") || IS("alnum")) upper = lower = 1;
    else if (IS("upper") || IS("lower")) {
        if (!case_sensitive) {
            upper = lower = 1;
            *ascii = "alpha";
        } else if (IS("upper")) upper = 1;
        else lower = 1;
    }
    else if (IS("punct")) punct = 1;
    else if (IS("graph")) graph = 1;
    else if (IS("print")) {
        out[n++] = (char)0xA0;
        graph = 1;
    }
#undef IS
    for (c = 0xA1; c <= 0xFF; ++c) {
        int letter_upper = c >= 0xC0 && c <= 0xDE && c != 0xD7;
        int letter_lower = c >= 0xDF && c != 0xF7;
        if (graph || (upper && letter_upper) || (lower && letter_lower) ||
            (punct && !letter_upper && !letter_lower))
            out[n++] = (char)c;
    }
    return n;
}

/**
 * @brief Add the Latin-1 characters to the character classes of a
 *        regular expression (inside bracket expressions).
 * @param pattern The pattern.
 * @param case_sensitive Zero if the text is folded.
 * @param out The result, or NULL to only count.
 * @return The length of the result.
 */
static long latin1_classes(const char *pattern, int case_sensitive, char *out)
{
    long n = 0;
    int in_bracket = 0;
    const char *s = pattern, *end, *ascii;
    char extra[96];
    int k, count;

#define PUT(ch) do { char put_ = (ch); if (out != NULL) out[n] = put_; ++n; } while (0)
    while (*s != '\0') {
        if (!in_bracket) {
            if (*s == '\\' && s[1] != '\0') {
                PUT(*s++);
            } else if (*s == '[') {
                /* a leading '^' and ']' belong to the expression */
                in_bracket = 1;
                PUT(*s++);
                if (*s == '^') PUT(*s++);
                if (*s == ']') PUT(*s++);
                continue;
            }
            PUT(*s++);
            continue;
        }
        if (s[0] == '[' && (s[1] == ':' || s[1] == '=' || s[1] == '.') &&
            (end = strstr(s + 2, s[1] == ':' ? ":]" : s[1] == '=' ? "=]" : ".]")) != NULL) {
            if (s[1] == ':') {
                count = latin1_class(s + 2, (long)(end - (s + 2)), case_sensitive, extra, &ascii);
                PUT('[');
                PUT(':');
                if (ascii != NULL) while (*ascii != '\0') PUT(*ascii++);
                else for (s += 2; s < end; ++s) PUT(*s);
                PUT(':');
                PUT(']');
                for (k = 0; k < count; ++k) PUT(extra[k]);
                s = end + 2;
            } else {
                while (s < end + 2) PUT(*s++);
            }
            continue;
        }
        if (*s == ']') in_bracket = 0;
        PUT(*s++);
    }
    if (out != NULL) out[n] = '\0';
#undef PUT
    return n;
}

int search_compile(SearchPattern *p, const char *text, const SearchOptions *options)
{
    char *expanded;
    long i;
    int error;
    memset(p, 0, sizeof(*p));
    p->options = *options;
    if (text == NULL || text[0] == '\0') {
        set_error(p, "Nothing to search for");
        return SEARCH_EMPTY;
    }
    p->length = (long)strlen(text);
    p->text = malloc((size_t)p->length + 1);
    if (p->text == NULL) {
        set_error(p, "Not enough memory");
        return SEARCH_NOMEM;
    }
    for (i = 0; i <= p->length; ++i)
        p->text[i] = options->case_sensitive ? text[i] : (char)fold((unsigned char)text[i]);
    if (!options->regex) return SEARCH_OK;

    p->regex = malloc(sizeof(regex_t));
    p->scratch = calloc(1, sizeof(*p->scratch));
    if (p->regex == NULL || p->scratch == NULL) {
        set_error(p, "Not enough memory");
        return SEARCH_NOMEM;
    }
    expanded = malloc((size_t)latin1_classes(p->text, options->case_sensitive, NULL) + 1);
    if (expanded == NULL) {
        set_error(p, "Not enough memory");
        return SEARCH_NOMEM;
    }
    latin1_classes(p->text, options->case_sensitive, expanded);
    error = regcomp((regex_t *)p->regex, expanded, REG_EXTENDED);
    free(expanded);
    if (error != 0) {
        regerror(error, (regex_t *)p->regex, p->error, sizeof(p->error));
        free(p->regex);
        p->regex = NULL;
        return error == REG_ESPACE ? SEARCH_NOMEM : SEARCH_BADREGEX;
    }
    return SEARCH_OK;
}

void search_free(SearchPattern *p)
{
    if (p->regex != NULL) {
        regfree((regex_t *)p->regex);
        free(p->regex);
        p->regex = NULL;
    }
    if (p->scratch != NULL) {
        free(p->scratch->line);
        free(p->scratch);
        p->scratch = NULL;
    }
    free(p->text);
    p->text = NULL;
    p->length = 0;
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

/**
 * @brief Find the first match of the regular expression in a line that
 *        starts at or after a column.
 * @param p The pattern (with a compiled regex).
 * @param line The line.
 * @param length Its length.
 * @param from The first column a match may start at.
 * @param m The match, if one is found.
 * @return Non-zero if a match was found.
 */
static int regex_next(const SearchPattern *p, const char *line, long length, long from,
                      SearchMatch *m)
{
    regmatch_t pm[SEARCH_GROUPS];
    const regex_t *re = (const regex_t *)p->regex;
    const char *subject = line;
    long i;
    int g;

    if (!p->options.case_sensitive) {
        struct SearchScratch *s = p->scratch;
        if (s->size < length + 1) {
            char *grown = realloc(s->line, (size_t)length + 64);
            if (grown == NULL) return 0;
            s->line = grown;
            s->size = length + 64;
        }
        for (i = 0; i < length; ++i) s->line[i] = (char)fold((unsigned char)line[i]);
        s->line[length] = '\0';
        subject = s->line;
    }
    while (from <= length) {
        pm[0].rm_so = (regoff_t)from;
        pm[0].rm_eo = (regoff_t)length;
        if (regexec(re, subject, re->re_nsub > 0 ? SEARCH_GROUPS : 1, pm,
                    REG_STARTEND | (from > 0 ? REG_NOTBOL : 0)) != 0)
            return 0;
        if (p->options.whole_word &&
            !word_bounds(line, length, (long)pm[0].rm_so, (long)pm[0].rm_eo)) {
            from = (long)pm[0].rm_so + 1;
            continue;
        }
        m->start = (long)pm[0].rm_so;
        m->end = (long)pm[0].rm_eo;
        for (g = 0; g < SEARCH_GROUPS; ++g) {
            if ((g == 0 || (size_t)g <= re->re_nsub) && pm[g].rm_so >= 0) {
                m->group_start[g] = (long)pm[g].rm_so;
                m->group_end[g] = (long)pm[g].rm_eo;
            } else m->group_start[g] = m->group_end[g] = -1;
        }
        return 1;
    }
    return 0;
}

int search_line_next(const SearchPattern *p, const char *line, long length, long from,
                     SearchMatch *m)
{
    long at;
    if (p->text == NULL) return 0;
    if (from < 0) from = 0;
    if (p->regex != NULL) return regex_next(p, line, length, from, m);
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
    SearchMatch next;
    int found = 0;
    if (p->text == NULL) return 0;
    if (p->regex != NULL) {
        /* the matches from the left, as a forward search finds them */
        at = 0;
        while (at < before && regex_next(p, line, length, at, &next) && next.start < before) {
            *m = next;
            found = 1;
            at = next.end > next.start ? next.end : next.end + 1;
        }
        return found;
    }
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
