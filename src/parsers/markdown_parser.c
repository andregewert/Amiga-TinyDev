/**
 * @file markdown_parser.c
 * @brief The analysis of the Markdown parser of Scout: one pass over the
 *        lines of a file, then the check of the links it found.
 *
 * This is no full CommonMark parser; a line is, in this order: inside an
 * HTML comment or a code fence, blank, indented code (four columns, not
 * within a paragraph or a list), a code fence, a heading, a thematic
 * break, a link reference definition or text. Inline scanning skips
 * backslash escapes, code spans (on one line), HTML tags and comments.
 * Not recognized: headings and code fences within block quotes or on the
 * line of a list marker. Links are checked at the end, as a link may name
 * a heading or a reference further down.
 *
 * A heading's anchor follows make_slug() of external/mdtools/src/mdconv.c,
 * which works on the HTML of md4c; here it comes from the source text
 * without its inline markup, which gives the same text for what headings
 * usually hold. An id or name attribute of raw HTML also makes an anchor.
 */
#include "markdown_parser.h"

#include <stdlib.h>
#include <string.h>

int (*markdown_parser_should_stop)(void) = NULL;

/** Longest text of MarkdownSymbol::detail (longer ones end with "..."). */
#define DETAIL_MAX  120
/** Open brackets of one line whose place is kept. */
#define BRACKETS_MAX 8
/** Lines of a Setext heading that get a token. */
#define PARA_LINES  8
/** Levels of headings. */
#define LEVELS      6
/** Open emphasis marks of one line whose closing marks are kept. */
#define MARKS_MAX   8

/** The token kind of markers: list items, table pipes, emphasis marks. */
#define TOKEN_MARKER    SCOUT_TOKEN_FUNCTION
/** The token kind of raw HTML: tags and entities. */
#define TOKEN_HTML      SCOUT_TOKEN_PARAMETER

/** What a found link is. */
enum {
    LINK_TARGET,        /**< a link or image with a target */
    LINK_REFERENCE      /**< [text][name]: the name must be defined */
};

/** A link found in the file. */
typedef struct Link {
    char *text;         /**< the target as written, or the normalized name (allocated) */
    int kind;           /**< LINK_ value */
    long line;          /**< its line */
    long col;           /**< its column */
    long len;           /**< its length */
} Link;

/** A growing list of texts. */
typedef struct Names {
    char **items;       /**< the texts (allocated) */
    long count;         /**< number of them */
    long cap;           /**< room for them */
} Names;

/** State of one analysis. */
typedef struct Parser {
    MarkdownParseResult *res;   /**< the result being built */
    const char *path;       /**< the file's path (may be NULL) */
    const MarkdownFiles *files; /**< access to other files (may be NULL) */
    long symbol_cap;        /**< room for symbols */
    long diagnostic_cap;    /**< room for diagnostics */
    long token_cap;         /**< room for token values */
    Link *links;            /**< the links, in the order of the file */
    long link_count;        /**< number of links */
    long link_cap;          /**< room for links */
    Names anchors;          /**< the anchors of this file */
    Names references;       /**< the normalized names of the reference definitions */
    long parents[LEVELS];   /**< per level: the last heading of that level, or -1 */
    int in_comment;         /**< inside an HTML comment */
    char fence_char;        /**< the open code fence's character, or 0 */
    size_t fence_len;       /**< its length */
    long fence_line;        /**< its line */
    long fence_col;         /**< its column */
    int in_list;            /**< a list item was seen: indented text is not code */
    int para;               /**< the last line was text of a paragraph */
    int para_plain;         /**< the paragraph can become a Setext heading */
    long para_line;         /**< its first line */
    char *para_text;        /**< its lines, joined by line ends */
    size_t para_len;        /**< length of that text */
    size_t para_cap;        /**< room for that text */
    long para_cols[PARA_LINES][2];  /**< column and length of its first lines */
    int para_count;         /**< number of lines */
    int depth;              /**< open brackets in the paragraph */
    int in_table;           /**< a table's delimiter row was seen: rows follow up to the block's end */
    int row_pipes;          /**< the line is a table row: its '|' get tokens */
    long brackets[BRACKETS_MAX];    /**< per open bracket: its index in the line, or -1 */
    char *other_path;       /**< the other Markdown file read last */
    Names other_anchors;    /**< its anchors */
    int other_read;         /**< it could be read */
} Parser;

/**
 * @brief Make room for one more element of a growing array.
 * @param P The parser (out_of_memory is set on failure).
 * @param array The array.
 * @param count Its number of elements.
 * @param cap Its room (updated).
 * @param size Size of an element.
 * @return Non-zero if there is room.
 */
static int grow(Parser *P, void **array, long count, long *cap, size_t size)
{
    void *bigger;
    long n;
    if (count < *cap) return 1;
    n = *cap ? *cap * 2 : 16;
    bigger = realloc(*array, (size_t)n * size);
    if (!bigger) {
        P->res->out_of_memory = 1;
        return 0;
    }
    *array = bigger;
    *cap = n;
    return 1;
}

/**
 * @brief Copy a piece of text.
 * @param s The text.
 * @param len Its length.
 * @return The copy (terminated), or NULL if out of memory.
 */
static char *dup_text(const char *s, size_t len)
{
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    return copy;
}

/**
 * @brief Tell whether a character is a blank (space or tab).
 * @param c The character.
 * @return Non-zero if it is.
 */
static int blank(char c)
{
    return c == ' ' || c == '\t';
}

/**
 * @brief Tell whether a character belongs to a word: a letter (also of
 *        ISO-8859-1) or a digit.
 * @param c The character.
 * @return Non-zero if it does.
 */
static int word_char(char c)
{
    unsigned char u = (unsigned char)c;
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u >= 0x80;
}

/**
 * @brief Return a character in lower case (ASCII and ISO-8859-1 letters).
 * @param c The character.
 * @return The lower case one.
 */
static char lower(char c)
{
    unsigned char u = (unsigned char)c;
    if ((u >= 'A' && u <= 'Z') || (u >= 0xC0 && u <= 0xDE && u != 0xD7)) return (char)(u + 32);
    return c;
}

/**
 * @brief Add a text to a list.
 * @param P The parser.
 * @param names The list.
 * @param text The text (the list takes it; freed if there is no room, may
 *             be NULL: then memory ran out).
 */
static void add_name(Parser *P, Names *names, char *text)
{
    void *items = names->items;
    if (!text) {
        P->res->out_of_memory = 1;
        return;
    }
    if (!grow(P, &items, names->count, &names->cap, sizeof(char *))) {
        free(text);
        return;
    }
    names->items = (char **)items;
    names->items[names->count++] = text;
}

/**
 * @brief Tell whether a list holds a text.
 * @param names The list.
 * @param text The text.
 * @return Non-zero if it does.
 */
static int has_name(const Names *names, const char *text)
{
    long i;
    for (i = 0; i < names->count; i++) {
        if (strcmp(names->items[i], text) == 0) return 1;
    }
    return 0;
}

/**
 * @brief Free the texts of a list and the list.
 * @param names The list (it is empty afterwards).
 */
static void free_names(Names *names)
{
    long i;
    for (i = 0; i < names->count; i++) free(names->items[i]);
    free(names->items);
    names->items = NULL;
    names->count = names->cap = 0;
}

/**
 * @brief Add a diagnostic on one line.
 * @param P The parser.
 * @param line The line.
 * @param col Its first column.
 * @param len Its length.
 * @param prefix The start of the message.
 * @param text What follows it (may be NULL).
 */
static void add_diagnostic(Parser *P, long line, long col, long len, const char *prefix, const char *text)
{
    MarkdownParseResult *r = P->res;
    void *array = r->diagnostics;
    MarkdownDiagnostic *d;
    size_t n = strlen(prefix), m = text ? strlen(text) : 0;
    char *message = (char *)malloc(n + m + 1);
    if (!message) {
        r->out_of_memory = 1;
        return;
    }
    memcpy(message, prefix, n);
    if (m) memcpy(message + n, text, m);
    message[n + m] = '\0';
    if (!grow(P, &array, r->diagnostic_count, &P->diagnostic_cap, sizeof(MarkdownDiagnostic))) {
        free(message);
        return;
    }
    r->diagnostics = (MarkdownDiagnostic *)array;
    d = &r->diagnostics[r->diagnostic_count++];
    d->message = message;
    d->severity = SCOUT_SEVERITY_WARNING;
    d->range.start_line = d->range.end_line = line;
    d->range.start_col = col;
    d->range.end_col = col + len;
}

/**
 * @brief Add a highlighting token. The callers go through the file from
 *        front to back, so the tokens stay sorted.
 * @param P The parser.
 * @param line Its line.
 * @param col Its column.
 * @param len Its length (nothing is added if it is not positive).
 * @param kind SCOUT_TOKEN_ value.
 */
static void add_token(Parser *P, long line, long col, long len, int kind)
{
    MarkdownParseResult *r = P->res;
    void *array = r->tokens;
    long *t;
    if (len <= 0) return;
    if (!grow(P, &array, r->token_count * 4 + 3, &P->token_cap, sizeof(long))) return;
    r->tokens = (long *)array;
    t = r->tokens + r->token_count++ * 4;
    t[0] = line;
    t[1] = col;
    t[2] = len;
    t[3] = kind;
}

/**
 * @brief Remember a link.
 * @param P The parser.
 * @param kind LINK_ value.
 * @param text Its text (the parser takes it; may be NULL: memory ran out).
 * @param line Its line.
 * @param col Its column.
 * @param len Its length.
 */
static void add_link(Parser *P, int kind, char *text, long line, long col, long len)
{
    void *array = P->links;
    Link *l;
    if (!text) {
        P->res->out_of_memory = 1;
        return;
    }
    if (!grow(P, &array, P->link_count, &P->link_cap, sizeof(Link))) {
        free(text);
        return;
    }
    P->links = (Link *)array;
    l = &P->links[P->link_count++];
    l->text = text;
    l->kind = kind;
    l->line = line;
    l->col = col;
    l->len = len;
}

/**
 * @brief Make the form of a reference name that is compared: lower case,
 *        runs of blanks as one space, none at the ends.
 * @param s The name.
 * @param len Its length.
 * @return The form (allocated), or NULL if out of memory.
 */
static char *normal_label(const char *s, size_t len)
{
    char *out = (char *)malloc(len + 1);
    size_t i, n = 0;
    if (!out) return NULL;
    for (i = 0; i < len; i++) {
        if (blank(s[i])) {
            if (n > 0 && out[n - 1] != ' ') out[n++] = ' ';
        } else {
            out[n++] = lower(s[i]);
        }
    }
    if (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    return out;
}

/**
 * @brief Find a piece of text in a line.
 * @param s The line.
 * @param len Its length.
 * @param from The index to start at.
 * @param what The text.
 * @return Its index, or len if it is not there.
 */
static size_t find_text(const char *s, size_t len, size_t from, const char *what)
{
    size_t n = strlen(what), i;
    for (i = from; i + n <= len; i++) {
        if (memcmp(s + i, what, n) == 0) return i;
    }
    return len;
}

/**
 * @brief Find the end of a code span.
 * @param s The line.
 * @param len Its length.
 * @param from The index after the opening backticks.
 * @param run Their number.
 * @return The index of the closing backticks, or len if there are none.
 */
static size_t code_close(const char *s, size_t len, size_t from, size_t run)
{
    size_t close;
    for (close = from; close < len; close++) {
        size_t m = 0;
        while (close + m < len && s[close + m] == '`') m++;
        if (m == run) return close;
        if (m) close += m - 1;
    }
    return len;
}

/**
 * @brief Count the equal characters at a place.
 * @param s The line.
 * @param len Its length.
 * @param i The index.
 * @return Their number.
 */
static size_t char_run(const char *s, size_t len, size_t i)
{
    size_t run = 1;
    while (i + run < len && s[i + run] == s[i]) run++;
    return run;
}

/**
 * @brief Find the closing marks of an emphasis.
 *
 * Escapes and code spans are passed over; closing marks of another length
 * belong to another emphasis. They do not follow a blank, and '_' is not
 * followed by a word character.
 *
 * @param s The line.
 * @param len Its length.
 * @param from The index after the opening marks.
 * @param c The mark's character.
 * @param run The number of marks.
 * @return The index of the closing marks, or len if there are none.
 */
static size_t mark_close(const char *s, size_t len, size_t from, char c, size_t run)
{
    size_t j = from;
    while (j < len) {
        if (s[j] == '\\') {
            j += 2;
        } else if (s[j] == '`') {
            size_t n = char_run(s, len, j), close = code_close(s, len, j + n, n);
            j = close < len ? close + n : j + n;
        } else if (s[j] == c) {
            size_t n = char_run(s, len, j);
            if (n == run && !blank(s[j - 1]) && !(c == '_' && j + n < len && word_char(s[j + n]))) return j;
            j += n;
        } else {
            j++;
        }
    }
    return len;
}

/**
 * @brief Read the target of an inline link: "(target)", "(<target>)", each
 *        maybe with a title.
 * @param s The line.
 * @param len Its length.
 * @param open The index of the '('.
 * @param[out] start The first index of the target.
 * @param[out] end The index after it.
 * @param[out] after The index after the ')'.
 * @return Non-zero if it is such a target.
 */
static int parse_target(const char *s, size_t len, size_t open, size_t *start, size_t *end, size_t *after)
{
    size_t j = open + 1;
    while (j < len && blank(s[j])) j++;
    if (j < len && s[j] == '<') {
        *start = ++j;
        while (j < len && s[j] != '>' && s[j] != '<') j++;
        if (j >= len || s[j] != '>') return 0;
        *end = j++;
    } else {
        int depth = 0;
        *start = j;
        while (j < len && !blank(s[j])) {
            if (s[j] == '\\' && j + 1 < len) j++;
            else if (s[j] == '(') depth++;
            else if (s[j] == ')' && depth-- == 0) break;
            j++;
        }
        *end = j;
    }
    while (j < len && blank(s[j])) j++;
    if (j < len && (s[j] == '"' || s[j] == '\'' || s[j] == '(')) {
        char close = s[j] == '(' ? ')' : s[j];
        j++;
        while (j < len && s[j] != close) {
            if (s[j] == '\\') j++;
            j++;
        }
        if (j >= len) return 0;
        j++;
        while (j < len && blank(s[j])) j++;
    }
    if (j >= len || s[j] != ')') return 0;
    *after = j + 1;
    return 1;
}

/**
 * @brief Tell whether '<' starts an HTML tag or an autolink.
 * @param s The text.
 * @param len Its length.
 * @param i The index of the '<'.
 * @return The index of its '>', or len if it starts none.
 */
static size_t tag_end(const char *s, size_t len, size_t i)
{
    char c = i + 1 < len ? s[i + 1] : '\0';
    size_t j;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '/' || c == '!')) return len;
    for (j = i + 1; j < len; j++) {
        if (s[j] == '>') return j;
    }
    return len;
}

/**
 * @brief Tell whether a bracket starts a link: its closing bracket is
 *        followed by '(' or '['.
 * @param s The text.
 * @param len Its length.
 * @param i The index of the '['.
 * @return Non-zero if it does.
 */
static int starts_link(const char *s, size_t len, size_t i)
{
    int depth = 0;
    size_t j;
    for (j = i; j < len; j++) {
        if (s[j] == '\\') j++;
        else if (s[j] == '[') depth++;
        else if (s[j] == ']' && --depth == 0) return j + 1 < len && (s[j + 1] == '(' || s[j + 1] == '[');
    }
    return 0;
}

/**
 * @brief Make the plain text of a heading: without emphasis, code marks,
 *        link targets, images and tags.
 * @param s The heading as written.
 * @param len Its length.
 * @param drop_entities Also leave out character entities, like the anchors
 *                      of mdtohtml.
 * @return The text (allocated), or NULL if out of memory.
 */
static char *plain_heading(const char *s, size_t len, int drop_entities)
{
    char *out = (char *)malloc(len + 1);
    size_t i = 0, n = 0, open_marks = 0;
    if (!out) return NULL;
    while (i < len) {
        char c = s[i];
        size_t j, a, b, after;
        if (c == '\\' && i + 1 < len && !word_char(s[i + 1]) && !blank(s[i + 1])) {
            out[n++] = s[i + 1];
            i += 2;
        } else if (c == '`') {
            size_t run = char_run(s, len, i), close = code_close(s, len, i + run, run);
            if (close < len) {
                memcpy(out + n, s + i + run, close - i - run);
                n += close - i - run;
                i = close + run;
            } else {
                i += run;
            }
        } else if (c == '<' && (j = tag_end(s, len, i)) < len) {
            i = j + 1;
        } else if (c == '&' && drop_entities) {
            /* a bare '&' becomes "&amp;" in the HTML: only that goes */
            for (j = i + 1; j < len && (word_char(s[j]) || s[j] == '#') && j - i < 12; j++) {}
            i = j < len && s[j] == ';' ? j + 1 : i + 1;
        } else if (c == '!' && i + 1 < len && s[i + 1] == '[' && starts_link(s, len, i + 1)) {
            /* an image: mdtohtml's anchors know nothing of it */
            int depth = 0;
            for (j = i + 1; j < len; j++) {
                if (s[j] == '[') depth++;
                else if (s[j] == ']' && --depth == 0) break;
            }
            i = j + 1 < len && s[j + 1] == '(' && parse_target(s, len, j + 1, &a, &b, &after) ? after : j + 1;
        } else if (c == '[' && starts_link(s, len, i)) {
            i++;
        } else if (c == ']' && i + 1 < len && s[i + 1] == '(' && parse_target(s, len, i + 1, &a, &b, &after)) {
            i = after;
        } else if (c == ']' && i + 1 < len && s[i + 1] == '[') {
            for (j = i + 2; j < len && s[j] != ']'; j++) {}
            i = j < len ? j + 1 : i + 1;
        } else if (c == '*') {
            /* a mark touches a word */
            if ((i == 0 || blank(s[i - 1])) && (i + 1 >= len || blank(s[i + 1]))) out[n++] = c;
            i++;
        } else if (c == '_') {
            /* opens before a word, closes behind one what was opened;
             * otherwise (within a word, say) it is just a character */
            size_t run = char_run(s, len, i);
            int before = i > 0 && word_char(s[i - 1]), behind;
            behind = i + run < len && word_char(s[i + run]);
            if (!before && behind) {
                open_marks += run;
            } else if (before && !behind && open_marks >= run) {
                open_marks -= run;
            } else {
                memset(out + n, '_', run);
                n += run;
            }
            i += run;
        } else if (c == '~' && i + 1 < len && s[i + 1] == '~') {
            i += 2;
        } else {
            out[n++] = c;
            i++;
        }
    }
    out[n] = '\0';
    return out;
}

/**
 * @brief Make the anchor of a heading from its plain text, by the rule of
 *        make_slug() in mdconv.c.
 * @param text The plain text (changed in place).
 */
static void make_slug(char *text)
{
    char *in = text, *out = text;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == ' ' || c == '\n' || c == '-') *out++ = '-';
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80) *out++ = (char)c;
        else if (c >= 'A' && c <= 'Z') *out++ = (char)(c + 32);
    }
    *out = '\0';
}

/**
 * @brief Add a heading's anchor; a repeated one gets "-1", "-2", ...
 * @param P The parser.
 * @param slug The anchor (not taken).
 */
static void add_slug(Parser *P, const char *slug)
{
    size_t n = strlen(slug);
    char *name;
    long number = 0;
    if (!n) return;
    name = (char *)malloc(n + 12);
    if (!name) {
        P->res->out_of_memory = 1;
        return;
    }
    memcpy(name, slug, n + 1);
    while (has_name(&P->anchors, name)) {
        char digits[12];
        size_t d = 0, k = n;
        long v = ++number;
        do {
            digits[d++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        name[k++] = '-';
        while (d) name[k++] = digits[--d];
        name[k] = '\0';
    }
    add_name(P, &P->anchors, name);
}

/**
 * @brief Add a heading: its symbol and its anchor.
 * @param P The parser.
 * @param level Its level, 1 to 6.
 * @param text Its text as written (lines joined by line ends).
 * @param len Its length.
 * @param range Its lines.
 * @param selection Its text in the file.
 */
static void add_heading(Parser *P, int level, const char *text, size_t len, const MarkdownRange *range,
                        const MarkdownRange *selection)
{
    MarkdownParseResult *r = P->res;
    void *array = r->symbols;
    MarkdownSymbol *s;
    char *name, *slug, *detail;
    size_t n, i;
    int k;

    if ((slug = plain_heading(text, len, 1)) != NULL) {
        make_slug(slug);
        add_slug(P, slug);
        free(slug);
    }
    name = plain_heading(text, len, 0);
    if (!name) {
        r->out_of_memory = 1;
        return;
    }
    for (n = 0, i = 0; name[i]; i++) name[n++] = name[i] == '\n' || name[i] == '\t' ? ' ' : name[i];
    while (n > 0 && name[n - 1] == ' ') n--;
    name[n] = '\0';
    if (!n) {   /* only marks: no name to show, nothing to go to */
        free(name);
        return;
    }
    detail = (char *)malloc((size_t)level + 1 + (n > DETAIL_MAX ? DETAIL_MAX + 3 : n) + 1);
    if (!detail || !grow(P, &array, r->symbol_count, &P->symbol_cap, sizeof(MarkdownSymbol))) {
        r->out_of_memory = 1;
        free(detail);
        free(name);
        return;
    }
    memset(detail, '#', (size_t)level);
    detail[level] = ' ';
    if (n > DETAIL_MAX) {
        memcpy(detail + level + 1, name, DETAIL_MAX);
        strcpy(detail + level + 1 + DETAIL_MAX, "...");
    } else {
        strcpy(detail + level + 1, name);
    }
    r->symbols = (MarkdownSymbol *)array;
    s = &r->symbols[r->symbol_count];
    s->name = name;
    s->detail = detail;
    s->kind = SCOUT_SYMBOL_HEADING;
    s->level = level;
    s->parent = -1;
    for (k = level - 2; k >= 0 && s->parent < 0; k--) s->parent = P->parents[k];
    P->parents[level - 1] = r->symbol_count;
    for (k = level; k < LEVELS; k++) P->parents[k] = -1;
    s->range = *range;
    s->selection = *selection;
    r->symbol_count++;
}

/**
 * @brief Add the anchors an HTML tag makes with its id or name attribute.
 * @param P The parser.
 * @param s The line.
 * @param i The index of the tag's '<'.
 * @param end The index of its '>'.
 */
static void tag_anchors(Parser *P, const char *s, size_t i, size_t end)
{
    size_t j;
    for (j = i; j < end; j++) {
        size_t skip = 0, k;
        if (blank(s[j])) {
            if (j + 4 < end && lower(s[j + 1]) == 'i' && lower(s[j + 2]) == 'd' && s[j + 3] == '=') skip = 4;
            else if (j + 6 < end && lower(s[j + 1]) == 'n' && lower(s[j + 2]) == 'a' && lower(s[j + 3]) == 'm' &&
                     lower(s[j + 4]) == 'e' && s[j + 5] == '=') skip = 6;
        }
        if (!skip || (s[j + skip] != '"' && s[j + skip] != '\'')) continue;
        for (k = j + skip + 1; k < end && s[k] != s[j + skip]; k++) {}
        if (k < end) add_name(P, &P->anchors, dup_text(s + j + skip + 1, k - j - skip - 1));
        j = k;
    }
}

/**
 * @brief Tell whether what stands in angle brackets is an autolink, not a tag.
 * @param s The text after the '<'.
 * @param len Its length up to the '>'.
 * @return Non-zero if it is.
 */
static int autolink(const char *s, size_t len)
{
    return (len >= 4 && memcmp(s, "http", 4) == 0) || (len >= 7 && memcmp(s, "mailto:", 7) == 0) ||
           memchr(s, '@', len) != NULL;
}

/**
 * @brief Scan the inline parts of a line: code spans, comments, tags, links,
 *        emphasis marks, entities and the pipes of a table row.
 * @param P The parser.
 * @param s The line.
 * @param len Its length.
 * @param line Its number.
 * @param from The index to start at.
 * @param tokens Add highlighting tokens (not within a heading, which is
 *               one token).
 */
static void scan_inline(Parser *P, const char *s, size_t len, long line, size_t from, int tokens)
{
    size_t i = from, closes[MARKS_MAX];
    int k, pending = 0;
    for (k = 0; k < BRACKETS_MAX; k++) P->brackets[k] = -1;     /* of earlier lines */
    while (i < len) {
        char c = s[i];
        int closed = 0;
        /* a closing mark jumped over (in a link's target) stays uncoloured */
        for (k = 0; k < pending && !closed; ) {
            if (closes[k] < i) {
                closes[k] = closes[--pending];
            } else if (closes[k] == i) {
                size_t run = char_run(s, len, i);
                add_token(P, line, (long)i + 1, (long)run, TOKEN_MARKER);
                i += run;
                closes[k] = closes[--pending];
                closed = 1;
            } else {
                k++;
            }
        }
        if (closed) continue;
        if (c == '\\') {
            i += 2;
        } else if (c == '`') {
            size_t run = char_run(s, len, i), close = code_close(s, len, i + run, run);
            if (close < len) {
                if (tokens) add_token(P, line, (long)i + 1, (long)(close + run - i), SCOUT_TOKEN_CONSTANT);
                i = close + run;
            } else {
                i += run;
            }
        } else if (c == '<' && i + 3 < len && memcmp(s + i, "<!--", 4) == 0) {
            size_t close = find_text(s, len, i + 4, "-->");
            if (close >= len) {
                P->in_comment = 1;
                return;
            }
            i = close + 3;
        } else if (c == '<') {
            size_t end = tag_end(s, len, i);
            if (end < len) {
                tag_anchors(P, s, i, end);
                if (tokens) {
                    add_token(P, line, (long)i + 1, (long)(end + 1 - i),
                              autolink(s + i + 1, end - i - 1) ? SCOUT_TOKEN_MACRO : TOKEN_HTML);
                }
                i = end + 1;
            } else {
                i++;
            }
        } else if (tokens && (c == '*' || c == '_' || (c == '~' && i + 1 < len && s[i + 1] == '~'))) {
            size_t run = char_run(s, len, i);
            if (i + run < len && !blank(s[i + run]) && !(c == '_' && i > 0 && word_char(s[i - 1])) &&
                pending < MARKS_MAX) {
                size_t close = mark_close(s, len, i + run, c, run);
                if (close < len) {
                    add_token(P, line, (long)i + 1, (long)run, TOKEN_MARKER);
                    closes[pending++] = close;
                }
            }
            i += run;
        } else if (tokens && c == '&') {
            size_t j = i + 1;
            if (j < len && s[j] == '#') j++;
            while (j < len && word_char(s[j]) && j - i < 12) j++;
            if (j < len && s[j] == ';' && j > i + 1) {
                add_token(P, line, (long)i + 1, (long)(j + 1 - i), TOKEN_HTML);
                i = j + 1;
            } else {
                i++;
            }
        } else if (tokens && c == '|' && P->row_pipes) {
            add_token(P, line, (long)i + 1, 1, TOKEN_MARKER);
            i++;
        } else if (c == '[') {
            if (P->depth < BRACKETS_MAX) P->brackets[P->depth] = (long)i;
            P->depth++;
            i++;
        } else if (c == ']' && P->depth > 0) {
            size_t a, b, after;
            long open;
            P->depth--;
            open = P->depth < BRACKETS_MAX ? P->brackets[P->depth] : -1;
            if (i + 1 < len && s[i + 1] == '(' && parse_target(s, len, i + 1, &a, &b, &after)) {
                if (a == b) {
                    add_link(P, LINK_TARGET, dup_text("", 0), line, (long)i + 2, (long)(after - i - 1));
                } else {
                    add_link(P, LINK_TARGET, dup_text(s + a, b - a), line, (long)a + 1, (long)(b - a));
                    if (tokens) add_token(P, line, (long)a + 1, (long)(b - a), SCOUT_TOKEN_MACRO);
                }
                i = after;
            } else if (i + 1 < len && s[i + 1] == '[') {
                size_t j;
                for (j = i + 2; j < len && s[j] != ']'; j++) {}
                if (j < len) {
                    if (j > i + 2) {
                        add_link(P, LINK_REFERENCE, normal_label(s + i + 2, j - i - 2), line, (long)i + 3,
                                 (long)(j - i - 2));
                    } else if (open >= 0 && i > (size_t)open + 1) {
                        add_link(P, LINK_REFERENCE, normal_label(s + open + 1, i - (size_t)open - 1), line,
                                 open + 2, (long)(i - (size_t)open - 1));
                    }
                    i = j + 1;
                } else {
                    i++;
                }
            } else {
                i++;
            }
        } else {
            i++;
        }
    }
}

/**
 * @brief Tell whether a line starts with a list marker.
 * @param s The line from its first non-blank character.
 * @param len The length of that.
 * @return Non-zero if it does.
 */
static int list_marker(const char *s, size_t len)
{
    size_t i = 0;
    if (len == 0) return 0;
    if (s[0] == '-' || s[0] == '*' || s[0] == '+') return len == 1 || blank(s[1]);
    while (i < len && i < 9 && s[i] >= '0' && s[i] <= '9') i++;
    if (i == 0 || i >= len || (s[i] != '.' && s[i] != ')')) return 0;
    return i + 1 == len || blank(s[i + 1]);
}

/**
 * @brief Tell whether a line is a Setext underline.
 * @param s The line from its first non-blank character.
 * @param len The length of that (without trailing blanks).
 * @return Its level (1 or 2), or 0.
 */
static int setext_level(const char *s, size_t len)
{
    size_t i;
    if (len == 0 || (s[0] != '=' && s[0] != '-')) return 0;
    for (i = 1; i < len; i++) {
        if (s[i] != s[0]) return 0;
    }
    return s[0] == '=' ? 1 : 2;
}

/**
 * @brief Tell whether a line is a thematic break.
 * @param s The line from its first non-blank character.
 * @param len The length of that.
 * @return Non-zero if it is.
 */
static int thematic_break(const char *s, size_t len)
{
    size_t i, n = 0;
    if (len == 0 || (s[0] != '-' && s[0] != '*' && s[0] != '_')) return 0;
    for (i = 0; i < len; i++) {
        if (s[i] == s[0]) n++;
        else if (!blank(s[i])) return 0;
    }
    return n >= 3;
}

/**
 * @brief Tell whether a line is the delimiter row of a table.
 * @param s The line from its first non-blank character.
 * @param len The length of that.
 * @return Non-zero if it is: '|', '-', ':' and blanks, with a '|' and a '-'.
 */
static int table_delimiter(const char *s, size_t len)
{
    size_t i;
    int pipe = 0, dash = 0;
    for (i = 0; i < len; i++) {
        if (s[i] == '|') pipe = 1;
        else if (s[i] == '-') dash = 1;
        else if (s[i] != ':' && !blank(s[i])) return 0;
    }
    return pipe && dash;
}

/**
 * @brief Add the tokens of the markers a line starts with: those of block
 *        quotes, a list item and its task box.
 * @param P The parser.
 * @param s The line.
 * @param first The index of its first non-blank character.
 * @param len Its length without trailing blanks.
 * @param line Its number.
 * @return The index of the text behind the markers.
 */
static size_t block_markers(Parser *P, const char *s, size_t first, size_t len, long line)
{
    size_t i = first;
    while (i < len && s[i] == '>') {
        add_token(P, line, (long)i + 1, 1, TOKEN_MARKER);
        i++;
        while (i < len && blank(s[i])) i++;
    }
    if (i < len && list_marker(s + i, len - i) && !thematic_break(s + i, len - i)) {
        size_t end = i + 1;
        while (end < len && !blank(s[end])) end++;
        add_token(P, line, (long)i + 1, (long)(end - i), TOKEN_MARKER);
        i = end;
        while (i < len && blank(s[i])) i++;
        if (i + 2 < len && s[i] == '[' && s[i + 2] == ']' && (s[i + 1] == ' ' || lower(s[i + 1]) == 'x') &&
            (i + 3 == len || blank(s[i + 3]))) {
            add_token(P, line, (long)i + 1, 3, TOKEN_MARKER);
            i += 3;
        }
    }
    return i;
}

/**
 * @brief End the block a line leaves behind: paragraph, open brackets, table.
 * @param P The parser.
 */
static void end_block(Parser *P)
{
    P->para = 0;
    P->depth = 0;
    P->in_table = 0;
}

/**
 * @brief Handle a line that may be an ATX heading.
 * @param P The parser.
 * @param s The line.
 * @param first The index of its first non-blank character.
 * @param len Its length without trailing blanks.
 * @param line Its number.
 * @return Non-zero if it was one.
 */
static int atx_heading(Parser *P, const char *s, size_t first, size_t len, long line)
{
    size_t i = first, a, b;
    int level;
    MarkdownRange range, selection;
    while (i < len && s[i] == '#') i++;
    level = (int)(i - first);
    if (level < 1 || level > LEVELS || (i < len && !blank(s[i]))) return 0;
    a = i;
    while (a < len && blank(s[a])) a++;
    b = len;
    /* closing marks count only after a blank */
    while (b > a && s[b - 1] == '#') b--;
    if (b == a || blank(s[b - 1])) {
        while (b > a && blank(s[b - 1])) b--;
    } else {
        b = len;
    }
    range.start_line = range.end_line = selection.start_line = selection.end_line = line;
    range.start_col = (long)first + 1;
    range.end_col = (long)len + 1;
    selection.start_col = (long)a + 1;
    selection.end_col = (long)b + 1;
    add_heading(P, level, s + a, b - a, &range, &selection);
    add_token(P, line, (long)first + 1, (long)(len - first), SCOUT_TOKEN_TYPE);
    scan_inline(P, s, b, line, a, 0);
    return 1;
}

/**
 * @brief Turn the paragraph just read into a Setext heading.
 * @param P The parser.
 * @param level Its level.
 * @param first The index of the underline's first character.
 * @param len The underline's end in its line.
 * @param line The underline's line.
 */
static void setext_heading(Parser *P, int level, size_t first, size_t len, long line)
{
    MarkdownParseResult *r = P->res;
    MarkdownRange range, selection;
    int i;
    /* the paragraph's own tokens give way to the heading's */
    while (r->token_count > 0 && r->tokens[(r->token_count - 1) * 4] >= P->para_line) r->token_count--;
    range.start_line = selection.start_line = P->para_line;
    range.start_col = selection.start_col = P->para_cols[0][0];
    selection.end_line = P->para_line;
    selection.end_col = P->para_cols[0][0] + P->para_cols[0][1];
    range.end_line = line;
    range.end_col = (long)len + 1;
    add_heading(P, level, P->para_text, P->para_len, &range, &selection);
    for (i = 0; i < P->para_count && i < PARA_LINES; i++) {
        add_token(P, P->para_line + i, P->para_cols[i][0], P->para_cols[i][1], SCOUT_TOKEN_TYPE);
    }
    add_token(P, line, (long)first + 1, (long)(len - first), SCOUT_TOKEN_TYPE);
}

/**
 * @brief Remember a line of a paragraph, should it become a Setext heading.
 * @param P The parser.
 * @param s The line.
 * @param first The index of its first non-blank character.
 * @param len Its length without trailing blanks.
 */
static void para_add(Parser *P, const char *s, size_t first, size_t len)
{
    size_t n = len - first, need = P->para_len + n + 2;
    if (P->para_count < PARA_LINES) {
        P->para_cols[P->para_count][0] = (long)first + 1;
        P->para_cols[P->para_count][1] = (long)n;
    }
    P->para_count++;
    if (need > P->para_cap) {
        size_t cap = P->para_cap ? P->para_cap * 2 : 256;
        char *bigger;
        if (cap < need) cap = need;
        bigger = (char *)realloc(P->para_text, cap);
        if (!bigger) {
            P->res->out_of_memory = 1;
            P->para_plain = 0;
            return;
        }
        P->para_text = bigger;
        P->para_cap = cap;
    }
    if (P->para_len) P->para_text[P->para_len++] = '\n';
    memcpy(P->para_text + P->para_len, s + first, n);
    P->para_len += n;
}

/**
 * @brief Handle a line that may be a link reference definition.
 * @param P The parser.
 * @param s The line.
 * @param first The index of its first non-blank character.
 * @param len Its length without trailing blanks.
 * @param line Its number.
 * @return Non-zero if it was one.
 */
static int reference_definition(Parser *P, const char *s, size_t first, size_t len, long line)
{
    size_t i = first + 1, a, b;
    if (first >= len || s[first] != '[' || (i < len && s[i] == '^')) return 0;
    while (i < len && s[i] != ']') {
        if (s[i] == '\\') i++;
        i++;
    }
    if (i + 1 >= len || i == first + 1 || s[i + 1] != ':') return 0;
    add_name(P, &P->references, normal_label(s + first + 1, i - first - 1));
    a = i + 2;
    while (a < len && blank(s[a])) a++;
    if (a < len && s[a] == '<') {
        a++;
        for (b = a; b < len && s[b] != '>'; b++) {}
    } else {
        for (b = a; b < len && !blank(s[b]); b++) {}
    }
    if (a == b) {
        add_link(P, LINK_TARGET, dup_text("", 0), line, (long)first + 1, (long)(i + 2 - first));
    } else {
        add_link(P, LINK_TARGET, dup_text(s + a, b - a), line, (long)a + 1, (long)(b - a));
        add_token(P, line, (long)a + 1, (long)(b - a), SCOUT_TOKEN_MACRO);
    }
    return 1;
}

/**
 * @brief Handle a line.
 * @param P The parser.
 * @param s The line (without its end).
 * @param total Its length.
 * @param line Its number.
 */
static void process_line(Parser *P, const char *s, size_t total, long line)
{
    size_t first = 0, len = total, run, start;
    long indent = 0;
    int marker, level;

    P->row_pipes = 0;
    if (P->in_comment) {
        size_t close = find_text(s, total, 0, "-->");
        if (close < total) {
            P->in_comment = 0;
            scan_inline(P, s, total, line, close + 3, 1);
        }
        return;
    }
    while (first < total && blank(s[first])) {
        indent = s[first] == '\t' ? (indent / 4 + 1) * 4 : indent + 1;
        first++;
    }
    while (len > first && blank(s[len - 1])) len--;

    if (P->fence_char) {
        run = 0;
        while (first + run < len && s[first + run] == P->fence_char) run++;
        add_token(P, line, (long)first + 1, (long)(len - first), SCOUT_TOKEN_CONSTANT);
        if (run >= P->fence_len && first + run == len && (indent < 4 || P->in_list)) P->fence_char = '\0';
        return;
    }
    if (first == len) {
        end_block(P);
        return;
    }
    if (indent >= 4 && !P->para && !P->in_list) {
        add_token(P, line, (long)first + 1, (long)(len - first), SCOUT_TOKEN_CONSTANT);
        return;
    }
    marker = list_marker(s + first, len - first) && !thematic_break(s + first, len - first);
    if (marker) P->in_list = 1;
    else if (indent < 2 && !P->para) P->in_list = 0;

    if (indent < 4 || P->in_list) {
        run = 0;
        if (s[first] == '`' || s[first] == '~') {
            while (first + run < len && s[first + run] == s[first]) run++;
        }
        if (run >= 3 && (s[first] == '~' || memchr(s + first + run, '`', len - first - run) == NULL)) {
            P->fence_char = s[first];
            P->fence_len = run;
            P->fence_line = line;
            P->fence_col = (long)first + 1;
            end_block(P);
            add_token(P, line, (long)first + 1, (long)(len - first), SCOUT_TOKEN_CONSTANT);
            return;
        }
    }
    if (indent < 4) {
        if (atx_heading(P, s, first, len, line)) {
            end_block(P);
            return;
        }
        if (P->para && P->para_plain && (level = setext_level(s + first, len - first)) != 0) {
            setext_heading(P, level, first, len, line);
            end_block(P);
            return;
        }
        if (thematic_break(s + first, len - first)) {
            add_token(P, line, (long)first + 1, (long)(len - first), TOKEN_MARKER);
            end_block(P);
            return;
        }
        if (!P->para && reference_definition(P, s, first, len, line)) return;
    }
    if (!P->para) {
        P->para = 1;
        P->para_plain = !marker && s[first] != '>';
        P->para_line = line;
        P->para_len = 0;
        P->para_count = 0;
    }
    else if (marker || s[first] == '>') P->para_plain = 0;    /* the paragraph ended here */
    if (P->para_plain) para_add(P, s, first, len);
    if (table_delimiter(s + first, len - first)) {
        add_token(P, line, (long)first + 1, (long)(len - first), TOKEN_MARKER);
        P->in_table = 1;
        return;
    }
    start = block_markers(P, s, first, len, line);
    P->row_pipes = P->in_table || (start < len && s[start] == '|');
    scan_inline(P, s, len, line, start, 1);
}

/**
 * @brief Go through the lines of a text.
 * @param P The parser.
 * @param source The text.
 * @param length Its length.
 */
static void scan_lines(Parser *P, const char *source, size_t length)
{
    size_t pos = 0;
    long line = 0;
    int k;
    for (k = 0; k < LEVELS; k++) P->parents[k] = -1;
    while (pos < length && !P->res->out_of_memory) {
        size_t end = pos;
        line++;
        if ((line & 255) == 0 && markdown_parser_should_stop && markdown_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        while (end < length && source[end] != '\n' && source[end] != '\r') end++;
        process_line(P, source + pos, end - pos, line);
        pos = end;
        if (pos < length && source[pos] == '\r') pos++;
        if (end < length && source[end] == '\r' && pos < length && source[pos] == '\n') pos++;
        else if (end < length && source[end] == '\n') pos++;
    }
    if (P->fence_char) {
        add_diagnostic(P, P->fence_line, P->fence_col, (long)P->fence_len, "code fence is not closed", NULL);
    }
}

/**
 * @brief Decode a part of a link: backslash escapes, %XX and "&amp;".
 * @param s The part.
 * @param len Its length.
 * @return The decoded text (allocated), or NULL if out of memory.
 */
static char *decode_part(const char *s, size_t len)
{
    char *out = (char *)malloc(len + 1);
    size_t i = 0, n = 0;
    if (!out) return NULL;
    while (i < len) {
        int hi = -1, lo = -1;
        if (s[i] == '%' && i + 2 < len) {
            char a = lower(s[i + 1]), b = lower(s[i + 2]);
            hi = a >= '0' && a <= '9' ? a - '0' : a >= 'a' && a <= 'f' ? a - 'a' + 10 : -1;
            lo = b >= '0' && b <= '9' ? b - '0' : b >= 'a' && b <= 'f' ? b - 'a' + 10 : -1;
        }
        if (hi >= 0 && lo >= 0) {
            out[n++] = (char)(hi * 16 + lo);
            i += 3;
        } else if (i + 5 <= len && memcmp(s + i, "&amp;", 5) == 0) {
            out[n++] = '&';
            i += 5;
        } else if (s[i] == '\\' && i + 1 < len) {
            out[n++] = s[i + 1];
            i += 2;
        } else {
            out[n++] = s[i++];
        }
    }
    out[n] = '\0';
    return out;
}

/**
 * @brief Tell whether a link has a scheme, or names a volume: a letter, then
 *        letters, digits and "+-.", then ':'.
 * @param s The link.
 * @return Non-zero if it does.
 */
static int has_scheme(const char *s)
{
    const char *p = s;
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return 0;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
           *p == '+' || *p == '-' || *p == '.') p++;
    return *p == ':';
}

/**
 * @brief Tell whether a name ends in an extension (without regard to case).
 * @param name The name.
 * @param ext The extension, in lower case.
 * @return Non-zero if it does.
 */
static int has_extension(const char *name, const char *ext)
{
    size_t n = strlen(name), m = strlen(ext), i;
    if (n < m) return 0;
    for (i = 0; i < m; i++) {
        if (lower(name[n - m + i]) != ext[i]) return 0;
    }
    return 1;
}

/**
 * @brief Tell whether a name is that of a Markdown file.
 * @param name The name.
 * @return Non-zero if it is.
 */
static int is_markdown(const char *name)
{
    return has_extension(name, ".md") || has_extension(name, ".markdown");
}

/**
 * @brief Resolve a relative link against the path of the file holding it.
 *
 * "." and empty parts go, ".." takes away the part before it. The result
 * has no "..", so it means the same to AmigaDOS (where the parent is "/")
 * and on the host.
 *
 * @param base The file's path.
 * @param link The link's path, with '/' between its parts.
 * @return The path (allocated); NULL if it leaves the volume or memory ran
 *         out.
 */
static char *resolve_path(const char *base, const char *link)
{
    size_t dir = strlen(base), n;
    char *out;
    while (dir > 0 && base[dir - 1] != '/' && base[dir - 1] != ':') dir--;
    out = (char *)malloc(dir + strlen(link) + 2);
    if (!out) return NULL;
    memcpy(out, base, dir);
    n = dir;
    while (*link) {
        size_t m = strcspn(link, "/");
        if (m == 2 && link[0] == '.' && link[1] == '.') {
            /* out ends in '/' or ':' or is empty; take its last part away */
            if (n == 0 || out[n - 1] == ':') {
                free(out);
                return NULL;
            }
            n--;
            while (n > 0 && out[n - 1] != '/' && out[n - 1] != ':') n--;
        } else if (m > 0 && !(m == 1 && link[0] == '.')) {
            memcpy(out + n, link, m);
            n += m;
            out[n++] = '/';
        }
        link += m;
        if (*link == '/') link++;
    }
    if (n > 0 && out[n - 1] == '/') n--;
    out[n] = '\0';
    return out;
}

/**
 * @brief Read the anchors of another Markdown file; the last one is kept,
 *        as the links of a page often go to the same one.
 * @param P The parser.
 * @param path The file's path.
 * @return Non-zero if they are known (P->other_anchors).
 */
static int read_other(Parser *P, const char *path)
{
    Parser sub;
    size_t length = 0;
    char *text;
    long i;
    if (P->other_path && strcmp(P->other_path, path) == 0) return P->other_read;
    free(P->other_path);
    free_names(&P->other_anchors);
    P->other_read = 0;
    P->other_path = dup_text(path, strlen(path));
    if (!P->other_path) {
        P->res->out_of_memory = 1;
        return 0;
    }
    text = P->files->read(path, &length);
    if (!text) return 0;
    memset(&sub, 0, sizeof(sub));
    sub.res = (MarkdownParseResult *)calloc(1, sizeof(MarkdownParseResult));
    if (!sub.res) {
        P->res->out_of_memory = 1;
        free(text);
        return 0;
    }
    scan_lines(&sub, text, length);
    free(text);
    if (sub.res->out_of_memory) P->res->out_of_memory = 1;
    if (sub.res->stopped) P->res->stopped = 1;
    P->other_read = !sub.res->out_of_memory && !sub.res->stopped;
    P->other_anchors = sub.anchors;
    for (i = 0; i < sub.link_count; i++) free(sub.links[i].text);
    free(sub.links);
    free_names(&sub.references);
    free(sub.para_text);
    markdown_parse_result_free(sub.res);
    return P->other_read;
}

/**
 * @brief Check a link to another file.
 * @param P The parser.
 * @param l The link.
 * @param path Its path, decoded.
 * @param anchor Its anchor, decoded (may be NULL).
 */
static void check_file(Parser *P, const Link *l, const char *path, const char *anchor)
{
    char *full = resolve_path(P->path, path);
    int found = full && P->files->exists(full);
    if (full && !found && (has_extension(full, ".html") || has_extension(full, ".htm"))) {
        /* mdtohtml makes page.html from page.md */
        static const char *const sources[] = { ".md", ".markdown" };
        size_t i, stem = (size_t)(strrchr(full, '.') - full);
        for (i = 0; i < 2 && !found; i++) {
            char *other = (char *)malloc(stem + strlen(sources[i]) + 1);
            if (!other) {
                P->res->out_of_memory = 1;
                break;
            }
            memcpy(other, full, stem);
            strcpy(other + stem, sources[i]);
            if (P->files->exists(other)) {
                free(full);
                full = other;
                found = 1;
            } else {
                free(other);
            }
        }
    }
    if (!found) {
        add_diagnostic(P, l->line, l->col, l->len, "file not found: ", path);
    } else if (anchor && *anchor && P->files->read && is_markdown(full) && read_other(P, full) &&
               !has_name(&P->other_anchors, anchor)) {
        add_diagnostic(P, l->line, l->col, l->len, "anchor not found in that file: #", anchor);
    }
    free(full);
}

/**
 * @brief Check the links of the file, once all its headings are known.
 * @param P The parser.
 */
static void check_links(Parser *P)
{
    long i;
    for (i = 0; i < P->link_count && !P->res->out_of_memory && !P->res->stopped; i++) {
        const Link *l = &P->links[i];
        size_t plen, alen = 0;
        const char *hash;
        char *path, *anchor;
        if (l->kind == LINK_REFERENCE) {
            if (*l->text && !has_name(&P->references, l->text)) {
                add_diagnostic(P, l->line, l->col, l->len, "reference not defined: ", l->text);
            }
            continue;
        }
        if (!*l->text) {
            add_diagnostic(P, l->line, l->col, l->len, "link has no target", NULL);
            continue;
        }
        if (has_scheme(l->text) || l->text[0] == '/') continue;
        if (markdown_parser_should_stop && markdown_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        plen = strcspn(l->text, "#?");
        hash = strchr(l->text, '#');
        if (hash) alen = strlen(hash + 1);
        path = decode_part(l->text, plen);
        anchor = decode_part(hash ? hash + 1 : "", alen);
        if (!path || !anchor) {
            P->res->out_of_memory = 1;
        } else if (!*path) {
            if (*anchor && !has_name(&P->anchors, anchor)) {
                add_diagnostic(P, l->line, l->col, l->len, "anchor not found: #", anchor);
            }
        } else if (P->path && P->files && P->files->exists) {
            check_file(P, l, path, anchor);
        }
        free(path);
        free(anchor);
    }
}

/**
 * @brief qsort() order of the diagnostics: by line and column.
 * @param a A diagnostic.
 * @param b Another one.
 * @return Less than, equal to or greater than 0.
 */
static int diagnostic_order(const void *a, const void *b)
{
    const MarkdownDiagnostic *x = (const MarkdownDiagnostic *)a, *y = (const MarkdownDiagnostic *)b;
    if (x->range.start_line != y->range.start_line) return x->range.start_line < y->range.start_line ? -1 : 1;
    if (x->range.start_col != y->range.start_col) return x->range.start_col < y->range.start_col ? -1 : 1;
    return 0;
}

MarkdownParseResult *markdown_parser_analyze(const char *source, size_t length, const char *path,
                                             const MarkdownFiles *files)
{
    Parser P;
    long i;
    memset(&P, 0, sizeof(P));
    P.res = (MarkdownParseResult *)calloc(1, sizeof(MarkdownParseResult));
    if (!P.res) return NULL;
    P.path = path;
    P.files = files;
    scan_lines(&P, source, length);
    if (!P.res->stopped) check_links(&P);
    if (P.res->diagnostic_count > 1) {
        qsort(P.res->diagnostics, (size_t)P.res->diagnostic_count, sizeof(MarkdownDiagnostic), diagnostic_order);
    }
    for (i = 0; i < P.link_count; i++) free(P.links[i].text);
    free(P.links);
    free_names(&P.anchors);
    free_names(&P.references);
    free_names(&P.other_anchors);
    free(P.other_path);
    free(P.para_text);
    return P.res;
}

void markdown_parse_result_free(MarkdownParseResult *res)
{
    long i;
    if (!res) return;
    for (i = 0; i < res->symbol_count; i++) {
        free(res->symbols[i].name);
        free(res->symbols[i].detail);
    }
    free(res->symbols);
    for (i = 0; i < res->diagnostic_count; i++) free(res->diagnostics[i].message);
    free(res->diagnostics);
    free(res->tokens);
    free(res);
}
