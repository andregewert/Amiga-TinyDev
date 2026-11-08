/**
 * @file markup_parser.c
 * @brief The analysis of the markup parser of Scout (HTML and XML): one
 *        pass over the bytes of a file with a stack of the open elements.
 *
 * The lexical rules cannot tell a tag from text (a word is a tag name only
 * after '<', an apostrophe in the text opens no string), so they only know
 * the comments and everything else is coloured by tokens. Names on the
 * stack are offsets into the text, which stays valid during the analysis.
 */
#include "markup_parser.h"

#include <stdlib.h>
#include <string.h>

int (*markup_parser_should_stop)(void) = NULL;

/** Most diagnostics of a file: bad old HTML could have thousands. */
#define DIAGNOSTIC_MAX  200
/** Hints stop here, so that they leave room for the errors. */
#define HINT_MAX        100
/** Most bytes of a name shown in a message. */
#define NAME_SHOWN      60

/** Longest text of a heading in MarkupSymbol::detail (longer ones end with "..."). */
#define DETAIL_MAX      60
/** Levels of headings. */
#define LEVELS          6

/** @name Token kinds of the parts of the markup */
/** @{ */
#define TOK_TAG     SCOUT_TOKEN_TYPE        /**< a tag with its brackets */
#define TOK_ATTR    SCOUT_TOKEN_PARAMETER   /**< an attribute's name */
#define TOK_VALUE   SCOUT_TOKEN_CONSTANT    /**< an attribute's value, a CDATA section */
#define TOK_ENTITY  SCOUT_TOKEN_MACRO       /**< an entity in the text */
#define TOK_DECL    SCOUT_TOKEN_FUNCTION    /**< DOCTYPE, processing instructions */
/** @} */

/** The void elements of HTML: they have no content and no end tag. */
static const char VOID_ELEMENTS[] =
    "area base basefont bgsound br col command embed frame hr image img input isindex keygen link meta "
    "param source spacer track wbr";

/** The HTML elements whose contents are text, not markup. */
static const char RAW_ELEMENTS[] = "script style textarea title xmp";

/** The HTML elements whose end tag may be left out. */
static const char OPTIONAL_END[] =
    "body colgroup dd dt head html li optgroup option p rp rt tbody td tfoot th thead tr";

/** The entities of the characters 160 to 255 of ISO-8859-1, in that order. */
static const char LATIN1_ENTITIES[] =
    "nbsp iexcl cent pound curren yen brvbar sect uml copy ordf laquo not shy reg macr deg plusmn sup2 sup3 "
    "acute micro para middot cedil sup1 ordm raquo frac14 frac12 frac34 iquest Agrave Aacute Acirc Atilde "
    "Auml Aring AElig Ccedil Egrave Eacute Ecirc Euml Igrave Iacute Icirc Iuml ETH Ntilde Ograve Oacute "
    "Ocirc Otilde Ouml times Oslash Ugrave Uacute Ucirc Uuml Yacute THORN szlig agrave aacute acirc atilde "
    "auml aring aelig ccedil egrave eacute ecirc euml igrave iacute icirc iuml eth ntilde ograve oacute "
    "ocirc otilde ouml divide oslash ugrave uacute ucirc uuml yacute thorn yuml";

/** An HTML element and the start tags that close it implicitly. */
typedef struct ImpliedClose {
    const char *name;       /**< the element */
    const char *closed_by;  /**< the start tags that close it, separated by blanks */
} ImpliedClose;

/** The elements closed by the start of another one. */
static const ImpliedClose IMPLIED[] = {
    { "colgroup", "caption tbody td tfoot th thead tr" },
    { "dd", "dd dt" },
    { "dt", "dd dt" },
    { "head", "body" },
    { "li", "li" },
    { "optgroup", "optgroup" },
    { "option", "optgroup option" },
    { "p", "address article aside blockquote caption center dd details dir div dl dt fieldset figcaption "
           "figure footer form h1 h2 h3 h4 h5 h6 header hr li main menu nav ol p pre section table tbody "
           "td tfoot th thead tr ul" },
    { "rp", "rp rt" },
    { "rt", "rp rt" },
    { "tbody", "tbody tfoot thead" },
    { "td", "tbody td tfoot th thead tr" },
    { "tfoot", "tbody thead" },
    { "th", "tbody td tfoot th thead tr" },
    { "thead", "tbody tfoot" },
    { "tr", "tbody tfoot thead tr" }
};

/** A place in the file. */
typedef struct Mark {
    size_t at;      /**< its byte */
    long line;      /**< its line */
    long col;       /**< its column */
} Mark;

/** An open element. */
typedef struct Frame {
    size_t name;        /**< offset of its name */
    size_t name_len;    /**< length of its name */
    long line;          /**< line of its start tag */
    long col;           /**< column of its '<' */
    size_t content;     /**< offset of the byte after its start tag */
    long content_line;  /**< line of that byte */
    long content_col;   /**< column of that byte */
} Frame;

/** An attribute of the tag being read. */
typedef struct Attr {
    size_t name;        /**< offset of its name */
    size_t len;         /**< length of its name */
} Attr;

/** A link found in the file (its value lies on one line). */
typedef struct Link {
    size_t value;       /**< offset of its value */
    size_t len;         /**< length of its value */
    long line;          /**< line of its value */
    long col;           /**< column of its value */
} Link;

/** A set of names, sorted when it is first searched. */
typedef struct Names {
    char **items;       /**< the names (allocated) */
    long count;         /**< number of names */
    long cap;           /**< room for names */
    int sorted;         /**< items are in strcmp() order */
} Names;

/** State of one analysis. */
typedef struct Parser {
    MarkupResult *res;      /**< the result being built */
    const char *s;          /**< the text */
    size_t len;             /**< its length */
    size_t i;               /**< the position */
    long line;              /**< its line */
    size_t line_start;      /**< the first byte of its line */
    size_t doc_start;       /**< the first byte after a byte order mark */
    int xml;                /**< checked as XML */
    int root_seen;          /**< XML: the root element has begun */
    int doctype_seen;       /**< a DOCTYPE was read */
    int stray_reported;     /**< XML: text outside the root was reported since the last markup */
    long ended_paragraphs;  /**< HTML: \<p\> elements ended by a block, whose \</p\> may still come */
    long parents[LEVELS];   /**< HTML: the last heading of each level, or -1 */
    long symbol_cap;        /**< room for symbols */
    const char *path;       /**< the file's real path, or NULL */
    const MarkupFiles *files; /**< the access to other files, or NULL */
    size_t tag;             /**< offset of the name of the tag being read */
    size_t tag_len;         /**< length of that name */
    int has_base;           /**< a base element was seen: the links are not checked */
    Names anchors;          /**< the anchors of this file */
    Link *links;            /**< the links of this file */
    long link_count;        /**< number of links */
    long link_cap;          /**< room for links */
    long reference_cap;     /**< room for references */
    char *other_path;       /**< the other file whose anchors were read last */
    Names other_anchors;    /**< its anchors */
    int other_read;         /**< they are known */
    long diagnostic_cap;    /**< room for diagnostics */
    long token_cap;         /**< room for token values */
    Frame *stack;           /**< the open elements */
    long depth;             /**< their number */
    long stack_cap;         /**< room for them */
    Attr *attrs;            /**< the attributes of the current tag */
    long attr_count;        /**< their number */
    long attr_cap;          /**< room for them */
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
    while (n <= count) n *= 2;
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
 * @brief The current place.
 * @param P The parser.
 * @return The place.
 */
static Mark here(const Parser *P)
{
    Mark m;
    m.at = P->i;
    m.line = P->line;
    m.col = (long)(P->i - P->line_start) + 1;
    return m;
}

/**
 * @brief Step over one byte, counting lines (LF, CR and CRLF end a line).
 * @param P The parser (not at the end of the text).
 */
static void advance(Parser *P)
{
    char c = P->s[P->i++];
    if (c == '\n' || (c == '\r' && !(P->i < P->len && P->s[P->i] == '\n'))) {
        P->line++;
        P->line_start = P->i;
    }
}

/**
 * @brief Move forward to a position.
 * @param P The parser.
 * @param to The position (at most the end of the text).
 */
static void skip_to(Parser *P, size_t to)
{
    while (P->i < to) advance(P);
}

/**
 * @brief Tell whether a byte is white space.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/**
 * @brief Move over white space.
 * @param P The parser.
 */
static void skip_blanks(Parser *P)
{
    while (P->i < P->len && blank(P->s[P->i])) advance(P);
}

/**
 * @brief The lower case of an ASCII letter.
 * @param c The byte.
 * @return Its lower case, or the byte itself.
 */
static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

/**
 * @brief Tell whether a byte is an ASCII letter.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/**
 * @brief Tell whether a byte is a decimal digit.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int digit(char c)
{
    return c >= '0' && c <= '9';
}

/**
 * @brief Tell whether a byte may start a name.
 * @param c The byte.
 * @return Non-zero if it may.
 */
static int name_start(char c)
{
    return letter(c) || c == '_' || c == ':' || (unsigned char)c >= 0x80;
}

/**
 * @brief Tell whether a byte may be part of a name.
 * @param c The byte.
 * @return Non-zero if it may.
 */
static int name_part(char c)
{
    return name_start(c) || digit(c) || c == '-' || c == '.';
}

/**
 * @brief Tell whether the text has a string at a position.
 * @param P The parser.
 * @param at The position.
 * @param text The string.
 * @param any_case Compare without regard to the case of ASCII letters.
 * @return Non-zero if it has.
 */
static int starts_with(const Parser *P, size_t at, const char *text, int any_case)
{
    size_t n = strlen(text), k;
    if (at + n > P->len) return 0;
    for (k = 0; k < n; k++) {
        char a = P->s[at + k], b = text[k];
        if (any_case ? lower(a) != lower(b) : a != b) return 0;
    }
    return 1;
}

/**
 * @brief Find a string.
 * @param P The parser.
 * @param from Where to start.
 * @param text The string.
 * @return Its position, or the length of the text if it is not there.
 */
static size_t find_text(const Parser *P, size_t from, const char *text)
{
    size_t at;
    for (at = from; at < P->len; at++) {
        if (P->s[at] == text[0] && starts_with(P, at, text, 0)) return at;
    }
    return P->len;
}

/**
 * @brief Tell whether a word is in a list.
 * @param list Words separated by single blanks.
 * @param word The word (need not be terminated).
 * @param n Its length.
 * @param any_case Compare without regard to the case of ASCII letters.
 * @return Non-zero if it is.
 */
static int in_list(const char *list, const char *word, size_t n, int any_case)
{
    const char *p = list;
    while (*p) {
        const char *end = strchr(p, ' ');
        size_t len = end ? (size_t)(end - p) : strlen(p), k;
        if (len == n) {
            for (k = 0; k < n; k++) {
                if (any_case ? lower(p[k]) != lower(word[k]) : p[k] != word[k]) break;
            }
            if (k == n) return 1;
        }
        p += len;
        if (*p) p++;
    }
    return 0;
}

/**
 * @brief Tell whether two names of the text are the same (in HTML without
 *        regard to case).
 * @param P The parser.
 * @param a The offset of the first name.
 * @param a_len Its length.
 * @param b The offset of the second name.
 * @param b_len Its length.
 * @return Non-zero if they are.
 */
static int same_name(const Parser *P, size_t a, size_t a_len, size_t b, size_t b_len)
{
    size_t k;
    if (a_len != b_len) return 0;
    for (k = 0; k < a_len; k++) {
        char x = P->s[a + k], y = P->s[b + k];
        if (P->xml ? x != y : lower(x) != lower(y)) return 0;
    }
    return 1;
}

/**
 * @brief Add a diagnostic naming something of the text.
 * @param P The parser.
 * @param severity SCOUT_SEVERITY_ value.
 * @param m Where it is.
 * @param len How much is marked (at least 1).
 * @param prefix The start of the message.
 * @param name The offset of the name.
 * @param name_len Its length (0: no name; a long one is shortened).
 * @param suffix The end of the message.
 */
static void report_name(Parser *P, int severity, const Mark *m, long len, const char *prefix,
                        size_t name, size_t name_len, const char *suffix)
{
    MarkupResult *r = P->res;
    void *array = r->diagnostics;
    MarkupDiagnostic *d;
    size_t a = strlen(prefix), b = name_len > NAME_SHOWN ? NAME_SHOWN : name_len, c = strlen(suffix);
    char *message;
    if (r->diagnostic_count >= (severity == SCOUT_SEVERITY_HINT ? HINT_MAX : DIAGNOSTIC_MAX)) return;
    message = (char *)malloc(a + b + c + 1);
    if (!message) {
        r->out_of_memory = 1;
        return;
    }
    memcpy(message, prefix, a);
    if (b) memcpy(message + a, P->s + name, b);
    memcpy(message + a + b, suffix, c + 1);
    if (!grow(P, &array, r->diagnostic_count, &P->diagnostic_cap, sizeof(MarkupDiagnostic))) {
        free(message);
        return;
    }
    r->diagnostics = (MarkupDiagnostic *)array;
    d = &r->diagnostics[r->diagnostic_count++];
    d->message = message;
    d->severity = severity;
    d->line = m->line;
    d->col = m->col;
    d->end_col = m->col + (len > 0 ? len : 1);
}

/**
 * @brief Add a diagnostic.
 * @param P The parser.
 * @param severity SCOUT_SEVERITY_ value.
 * @param m Where it is.
 * @param len How much is marked (at least 1).
 * @param message The message.
 */
static void report(Parser *P, int severity, const Mark *m, long len, const char *message)
{
    report_name(P, severity, m, len, message, 0, 0, "");
}

/**
 * @brief Add a highlighting token; the file is gone through from front to
 *        back, so the tokens stay sorted.
 * @param P The parser.
 * @param line Its line.
 * @param col Its column.
 * @param len Its length.
 * @param kind SCOUT_TOKEN_ value.
 */
static void add_token(Parser *P, long line, long col, long len, int kind)
{
    MarkupResult *r = P->res;
    void *array = r->tokens;
    long *v;
    if (!grow(P, &array, r->token_count * 4 + 3, &P->token_cap, sizeof(long))) return;
    r->tokens = (long *)array;
    v = r->tokens + r->token_count++ * 4;
    v[0] = line;
    v[1] = col;
    v[2] = len;
    v[3] = kind;
}

/**
 * @brief Move forward to a position, marking what is passed as tokens (one
 *        per line, as a token never spans lines).
 * @param P The parser.
 * @param to The position.
 * @param kind SCOUT_TOKEN_ value.
 */
static void emit_until(Parser *P, size_t to, int kind)
{
    Mark m = here(P);
    while (P->i < to) {
        char c = P->s[P->i];
        if (c == '\n' || c == '\r') {
            if (P->i > m.at) add_token(P, m.line, m.col, (long)(P->i - m.at), kind);
            advance(P);
            m = here(P);
        } else {
            advance(P);
        }
    }
    if (P->i > m.at) add_token(P, m.line, m.col, (long)(P->i - m.at), kind);
}

/**
 * @brief The length of a well-formed entity reference.
 * @param P The parser.
 * @param at The offset of its '&'.
 * @return Its length with '&' and ';', or 0 if there is none at @p at.
 */
static size_t entity_length(const Parser *P, size_t at)
{
    size_t p = at + 1, first;
    if (p < P->len && P->s[p] == '#') {
        p++;
        first = p;
        if (p < P->len && (P->s[p] == 'x' || P->s[p] == 'X')) {
            p++;
            first = p;
            while (p < P->len && (digit(P->s[p]) || (lower(P->s[p]) >= 'a' && lower(P->s[p]) <= 'f'))) p++;
        } else {
            while (p < P->len && digit(P->s[p])) p++;
        }
    } else {
        first = p;
        if (p < P->len && name_start(P->s[p])) {
            while (p < P->len && name_part(P->s[p])) p++;
        }
    }
    if (p == first || p >= P->len || P->s[p] != ';') return 0;
    return p - at + 1;
}

/**
 * @brief Check an entity reference of an XML file.
 * @param P The parser (at the '&').
 * @param length Its length from entity_length().
 */
static void check_entity(Parser *P, size_t length)
{
    Mark m = here(P);
    if (length == 0) {
        report(P, SCOUT_SEVERITY_ERROR, &m, 1, "'&' must be written as &amp;");
    } else if (P->s[P->i + 1] != '#' && !P->doctype_seen &&
               !in_list("amp lt gt quot apos", P->s + P->i + 1, length - 2, 0)) {
        report_name(P, SCOUT_SEVERITY_ERROR, &m, (long)length, "unknown entity &", P->i + 1, length - 2, ";");
    }
}

/**
 * @brief Tell whether an element is inside svg or math, where HTML takes
 *        short tags as XML does.
 * @param P The parser.
 * @return Non-zero if it is.
 */
static int in_foreign(const Parser *P)
{
    long k;
    for (k = 0; k < P->depth; k++) {
        if (in_list("svg math", P->s + P->stack[k].name, P->stack[k].name_len, 1)) return 1;
    }
    return 0;
}

/**
 * @brief Tell whether the start of an element closes the open one.
 * @param P The parser.
 * @param top The open element.
 * @param name The offset of the new element's name.
 * @param n Its length.
 * @return Non-zero if it does.
 */
static int closes_implicitly(const Parser *P, const Frame *top, size_t name, size_t n)
{
    size_t k;
    for (k = 0; k < sizeof(IMPLIED) / sizeof(IMPLIED[0]); k++) {
        if (in_list(IMPLIED[k].name, P->s + top->name, top->name_len, 1)) {
            return in_list(IMPLIED[k].closed_by, P->s + name, n, 1);
        }
    }
    return 0;
}

/**
 * @brief Report an element that was not closed, unless HTML allows that.
 * @param P The parser.
 * @param f The element.
 */
static void report_unclosed(Parser *P, const Frame *f)
{
    Mark m;
    if (!P->xml && in_list(OPTIONAL_END, P->s + f->name, f->name_len, 1)) return;
    m.at = 0;
    m.line = f->line;
    m.col = f->col;
    report_name(P, SCOUT_SEVERITY_ERROR, &m, (long)f->name_len + 1, "<", f->name, f->name_len, "> is not closed");
}

/**
 * @brief Find the end tag of a raw element.
 * @param P The parser (after the start tag).
 * @param name The offset of the element's name.
 * @param n Its length.
 * @return The offset of its "</", or the length of the text.
 */
static size_t find_raw_end(const Parser *P, size_t name, size_t n)
{
    size_t at, k;
    for (at = P->i; at + 2 + n <= P->len; at++) {
        if (P->s[at] != '<' || P->s[at + 1] != '/') continue;
        for (k = 0; k < n; k++) {
            if (lower(P->s[at + 2 + k]) != lower(P->s[name + k])) break;
        }
        if (k == n) {
            char c = at + 2 + n < P->len ? P->s[at + 2 + n] : '>';
            if (blank(c) || c == '/' || c == '>') return at;
        }
    }
    return P->len;
}

/**
 * @brief Add an open element to the stack.
 * @param P The parser.
 * @param open The place of its '<'.
 * @param name The offset of its name.
 * @param n Its length.
 * @return Non-zero if there was memory.
 */
static int push_element(Parser *P, const Mark *open, size_t name, size_t n)
{
    void *array = P->stack;
    Frame *f;
    if (!grow(P, &array, P->depth, &P->stack_cap, sizeof(Frame))) return 0;
    P->stack = (Frame *)array;
    f = &P->stack[P->depth++];
    f->name = name;
    f->name_len = n;
    f->line = open->line;
    f->col = open->col;
    f->content = P->i;
    f->content_line = P->line;
    f->content_col = (long)(P->i - P->line_start) + 1;
    return 1;
}

/**
 * @brief Take note of a start tag that has been read.
 * @param P The parser (after the tag).
 * @param open The place of its '<'.
 * @param name The offset of its name.
 * @param n Its length.
 * @param short_tag It ended with "/>".
 */
static void open_element(Parser *P, const Mark *open, size_t name, size_t n, int short_tag)
{
    const char *word = P->s + name;
    if (P->xml) {
        if (P->depth == 0) {
            if (P->root_seen) report(P, SCOUT_SEVERITY_ERROR, open, (long)n + 1, "second root element");
            P->root_seen = 1;
        }
        if (!short_tag) push_element(P, open, name, n);
        return;
    }
    while (P->depth > 0 && closes_implicitly(P, &P->stack[P->depth - 1], name, n)) {
        const Frame *top = &P->stack[--P->depth];
        if (in_list("p", P->s + top->name, top->name_len, 1) && !in_list("p", word, n, 1)) P->ended_paragraphs++;
    }
    if (in_list(VOID_ELEMENTS, word, n, 1)) return;
    if (short_tag) {
        /* the warning says it all: as an open element it would be reported again */
        if (!in_foreign(P) && !in_list("svg math", word, n, 1)) {
            report_name(P, SCOUT_SEVERITY_WARNING, open, (long)n + 1, "HTML has no short form of <", name, n, ">");
        }
        return;
    }
    if (!push_element(P, open, name, n)) return;
    if (in_list(RAW_ELEMENTS, word, n, 1) && !in_foreign(P)) {
        size_t end = find_raw_end(P, name, n);
        if (end >= P->len) {
            report_unclosed(P, &P->stack[--P->depth]);
        }
        skip_to(P, end);    /* the end tag is read by the main loop */
    }
}

/**
 * @brief The character an HTML entity reference stands for.
 * @param P The parser.
 * @param at The offset of its '&'.
 * @param n Its length from entity_length() (not 0).
 * @return The byte (characters beyond ISO-8859-1: '?'), or -1 if it is
 *         not known.
 */
static int entity_value(const Parser *P, size_t at, size_t n)
{
    const char *word = P->s + at + 1, *p = LATIN1_ENTITIES;
    size_t len = n - 2;
    int code = 160;
    if (word[0] == '#') {
        int hex = word[1] == 'x' || word[1] == 'X';
        size_t k;
        long value = 0;
        for (k = hex ? 2 : 1; k < len && value <= 0x10FFFFL; k++) {
            char c = lower(word[k]);
            value = value * (hex ? 16 : 10) + (digit(c) ? c - '0' : c - 'a' + 10);
        }
        if (value == 0) return -1;
        return value > 255 ? '?' : (int)value;
    }
    if (in_list("amp", word, len, 0)) return '&';
    if (in_list("lt", word, len, 0)) return '<';
    if (in_list("gt", word, len, 0)) return '>';
    if (in_list("quot", word, len, 0)) return '"';
    if (in_list("apos", word, len, 0)) return '\'';
    while (*p) {
        const char *end = strchr(p, ' ');
        size_t m = end ? (size_t)(end - p) : strlen(p);
        if (m == len && memcmp(p, word, m) == 0) return code;
        p += m;
        if (*p) p++;
        code++;
    }
    return -1;
}

/**
 * @brief Add the symbol of a heading whose end tag has been read.
 *
 * Goes over its content once more, with the position of the parser, which
 * is put back afterwards.
 *
 * @param P The parser (after the end tag).
 * @param f The heading.
 * @param end The offset of its end tag.
 */
static void add_heading(Parser *P, const Frame *f, size_t end)
{
    MarkupResult *r = P->res;
    size_t saved_i = P->i, saved_start = P->line_start, len = 0, shown;
    long saved_line = P->line, first_line = 0, first_col = 0, last_col = 0;
    int level = P->s[f->name + 1] - '0', space = 0, k;
    char *name = (char *)malloc(end - f->content + 1), *detail;
    void *array = r->symbols;
    MarkupSymbol *s;
    Mark after;

    if (!name) {
        r->out_of_memory = 1;
        return;
    }
    after = here(P);
    P->i = f->content;
    P->line = f->content_line;
    P->line_start = f->content - (size_t)(f->content_col - 1);
    while (P->i < end) {
        char c = P->s[P->i];
        size_t step = 1;
        if (c == '<') {
            size_t stop = starts_with(P, P->i, "<!--", 0) ? find_text(P, P->i + 4, "-->") + 2
                                                          : find_text(P, P->i, ">");
            skip_to(P, stop < end ? stop + 1 : end);
            continue;
        }
        if (c == '&') {
            size_t n = entity_length(P, P->i);
            int value = n ? entity_value(P, P->i, n) : -1;
            if (value >= 0) {
                c = (char)value;
                step = n;
            }
        }
        if (blank(c) || (unsigned char)c == 0xA0) {
            space = 1;
        } else {
            if (len == 0) {
                first_line = P->line;
                first_col = (long)(P->i - P->line_start) + 1;
            } else if (space) {
                name[len++] = ' ';
            }
            space = 0;
            name[len++] = c;
            if (P->line == first_line) last_col = (long)(P->i - P->line_start) + 1 + (long)step;
        }
        skip_to(P, P->i + step);
    }
    P->i = saved_i;
    P->line = saved_line;
    P->line_start = saved_start;
    if (len == 0) {     /* only an image or nothing: no name to show */
        free(name);
        return;
    }
    name[len] = '\0';
    shown = len > DETAIL_MAX ? DETAIL_MAX : len;
    detail = (char *)malloc((size_t)level + 1 + shown + 3 + 1);
    if (!detail || !grow(P, &array, r->symbol_count, &P->symbol_cap, sizeof(MarkupSymbol))) {
        r->out_of_memory = 1;
        free(detail);
        free(name);
        return;
    }
    memset(detail, '#', (size_t)level);
    detail[level] = ' ';
    memcpy(detail + level + 1, name, shown);
    strcpy(detail + (size_t)level + 1 + shown, len > DETAIL_MAX ? "..." : "");
    r->symbols = (MarkupSymbol *)array;
    s = &r->symbols[r->symbol_count];
    s->name = name;
    s->detail = detail;
    s->kind = SCOUT_SYMBOL_HEADING;
    s->parent = -1;
    for (k = level - 2; k >= 0 && s->parent < 0; k--) s->parent = P->parents[k];
    P->parents[level - 1] = r->symbol_count;
    for (k = level; k < LEVELS; k++) P->parents[k] = -1;
    s->range.start_line = f->line;
    s->range.start_col = f->col;
    s->range.end_line = after.line;
    s->range.end_col = after.col;
    s->selection.start_line = s->selection.end_line = first_line;
    s->selection.start_col = first_col;
    s->selection.end_col = last_col;
    r->symbol_count++;
}

/**
 * @brief Take note of an end tag that has been read.
 * @param P The parser.
 * @param open The place of its '<'.
 * @param name The offset of its name.
 * @param n Its length.
 */
static void close_element(Parser *P, const Mark *open, size_t name, size_t n)
{
    long k;
    if (!P->xml && in_list(VOID_ELEMENTS, P->s + name, n, 1)) {
        report_name(P, SCOUT_SEVERITY_ERROR, open, (long)n + 2, "<", name, n, "> has no end tag");
        return;
    }
    for (k = P->depth - 1; k >= 0; k--) {
        if (same_name(P, P->stack[k].name, P->stack[k].name_len, name, n)) break;
    }
    if (k < 0) {
        if (P->ended_paragraphs > 0 && in_list("p", P->s + name, n, 1)) {
            /* <p>...<ul>...</ul></p> is everywhere and does little harm */
            P->ended_paragraphs--;
            report_name(P, SCOUT_SEVERITY_WARNING, open, (long)n + 2, "the element before </", name, n,
                        "> has already ended the paragraph");
            return;
        }
        report_name(P, SCOUT_SEVERITY_ERROR, open, (long)n + 2, "end tag </", name, n, "> without a start tag");
        return;
    }
    while (P->depth > k + 1) report_unclosed(P, &P->stack[--P->depth]);
    if (!P->xml && n == 2 && lower(P->s[name]) == 'h' && P->s[name + 1] >= '1' && P->s[name + 1] <= '6') {
        add_heading(P, &P->stack[k], open->at);
    }
    P->depth = k;
}

/**
 * @brief Tell whether an attribute's name was used before in this tag.
 * @param P The parser.
 * @param name The offset of the name.
 * @param n Its length.
 * @return Non-zero if it was.
 */
static int duplicate_attribute(const Parser *P, size_t name, size_t n)
{
    long k;
    for (k = 0; k < P->attr_count; k++) {
        if (same_name(P, P->attrs[k].name, P->attrs[k].len, name, n)) return 1;
    }
    return 0;
}

/**
 * @brief Check the text of a quoted value of an XML file for '<' and for
 *        entities.
 * @param P The parser (at the first byte of the value; it stays there).
 * @param end The offset of the closing quote.
 */
static void check_xml_value(Parser *P, size_t end)
{
    size_t saved_i = P->i, saved_start = P->line_start;
    long saved_line = P->line;
    while (P->i < end) {
        char c = P->s[P->i];
        if (c == '<') {
            Mark m = here(P);
            report(P, SCOUT_SEVERITY_ERROR, &m, 1, "'<' in an attribute value must be written as &lt;");
            advance(P);
        } else if (c == '&') {
            size_t n = entity_length(P, P->i);
            check_entity(P, n);
            skip_to(P, P->i + (n ? n : 1));
        } else {
            advance(P);
        }
    }
    P->i = saved_i;
    P->line_start = saved_start;
    P->line = saved_line;
}

/**
 * @brief Decode the entities of an attribute value.
 * @param P The parser.
 * @param value The offset of the value.
 * @param len Its length.
 * @return The text (allocated), or NULL if out of memory (reported).
 */
static char *decode_value(Parser *P, size_t value, size_t len)
{
    char *out = (char *)malloc(len + 1);
    size_t at = value, end = value + len, n = 0;
    if (!out) {
        P->res->out_of_memory = 1;
        return NULL;
    }
    while (at < end) {
        size_t m = P->s[at] == '&' ? entity_length(P, at) : 0;
        int code = m && at + m <= end ? entity_value(P, at, m) : -1;
        if (code >= 0) {
            out[n++] = (char)code;
            at += m;
        } else {
            out[n++] = P->s[at++];
        }
    }
    out[n] = '\0';
    return out;
}

/**
 * @brief Add a name to a set.
 * @param P The parser.
 * @param names The set.
 * @param name The name (allocated; the set takes it, or frees it on
 *             failure).
 */
static void add_name(Parser *P, Names *names, char *name)
{
    void *array = names->items;
    if (!grow(P, &array, names->count, &names->cap, sizeof(char *))) {
        free(name);
        return;
    }
    names->items = (char **)array;
    names->items[names->count++] = name;
    names->sorted = 0;
}

/**
 * @brief Free the names of a set.
 * @param names The set.
 */
static void free_names(Names *names)
{
    long i;
    for (i = 0; i < names->count; i++) free(names->items[i]);
    free(names->items);
    memset(names, 0, sizeof(*names));
}

/**
 * @brief Compare two names, for qsort().
 * @param a The first (a char **).
 * @param b The second.
 * @return Negative, zero or positive.
 */
static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/**
 * @brief Tell whether a set has a name.
 * @param names The set (sorted if it was not yet).
 * @param name The name.
 * @return Non-zero if it has.
 */
static int has_name(Names *names, const char *name)
{
    long low = 0, high = names->count - 1;
    if (!names->sorted) {
        if (names->count > 1) qsort(names->items, (size_t)names->count, sizeof(char *), compare_names);
        names->sorted = 1;
    }
    while (low <= high) {
        long middle = low + (high - low) / 2;
        int c = strcmp(names->items[middle], name);
        if (c == 0) return 1;
        if (c < 0) low = middle + 1;
        else high = middle - 1;
    }
    return 0;
}

/**
 * @brief Take note of an attribute value of an HTML file that is an
 *        anchor or a link.
 * @param P The parser.
 * @param attr The offset of the attribute's name.
 * @param attr_len Its length.
 * @param m The place of the value.
 * @param value The offset of the value.
 * @param len Its length.
 */
static void note_value(Parser *P, size_t attr, size_t attr_len, const Mark *m, size_t value, size_t len)
{
    const char *a = P->s + attr, *tag = P->s + P->tag;
    size_t n = P->tag_len, lead = 0, k;
    int link;
    void *array = P->links;

    if (P->xml) return;
    for (k = 0; k < len; k++) {     /* a link has to be on one line to be marked */
        if (P->s[value + k] == '\n' || P->s[value + k] == '\r') return;
    }
    while (lead < len && blank(P->s[value + lead])) lead++;
    while (len > lead && blank(P->s[value + len - 1])) len--;
    if (len == lead) return;
    if (in_list("id", a, attr_len, 1) || (in_list("name", a, attr_len, 1) && in_list("a", tag, n, 1))) {
        char *name = decode_value(P, value + lead, len - lead);
        if (name) add_name(P, &P->anchors, name);
        return;
    }
    if (in_list("href", a, attr_len, 1)) {
        if (in_list("base", tag, n, 1)) P->has_base = 1;
        link = in_list("a area link", tag, n, 1);
    } else if (in_list("src", a, attr_len, 1)) {
        link = in_list("audio embed frame iframe img input script source track video", tag, n, 1);
    } else if (in_list("background", a, attr_len, 1)) {
        link = in_list("body table td th tr", tag, n, 1);
    } else {
        link = in_list("data", a, attr_len, 1) && in_list("object", tag, n, 1);
    }
    if (!link || !grow(P, &array, P->link_count, &P->link_cap, sizeof(Link))) return;
    P->links = (Link *)array;
    P->links[P->link_count].value = value + lead;
    P->links[P->link_count].len = len - lead;
    P->links[P->link_count].line = m->line;
    P->links[P->link_count++].col = m->col + (long)lead;
}

/**
 * @brief Read a quoted attribute value.
 * @param P The parser (at the opening quote).
 * @param attr The offset of the attribute's name.
 * @param attr_len Its length.
 */
static void parse_quoted_value(Parser *P, size_t attr, size_t attr_len)
{
    char quote = P->s[P->i];
    size_t p = P->i + 1, lt = P->len, gt = P->len;
    int event = !P->xml && attr_len >= 2 && lower(P->s[attr]) == 'o' && lower(P->s[attr + 1]) == 'n';
    while (p < P->len && P->s[p] != quote) {
        if (P->s[p] == '<' && lt == P->len) lt = p;
        if (P->s[p] == '>' && gt == P->len) gt = p;
        p++;
    }
    /* a '>' and then a '<' look like the end of this tag and the next one */
    if (p >= P->len || (gt < lt && lt < p && !event)) {
        Mark m = here(P);
        size_t end = P->i + 1;
        report_name(P, SCOUT_SEVERITY_ERROR, &m, 1, "the closing quote of attribute ", attr, attr_len, " is missing");
        while (end < P->len && P->s[end] != '>' && P->s[end] != '\n' && P->s[end] != '\r') end++;
        emit_until(P, end, TOK_VALUE);
        return;
    }
    if (P->xml) {
        advance(P);
        check_xml_value(P, p);
        P->i--;     /* the opening quote is on this line: no line was counted */
    }
    {
        Mark value = here(P);
        value.col++;    /* after the opening quote */
        note_value(P, attr, attr_len, &value, P->i + 1, p - P->i - 1);
    }
    emit_until(P, p + 1, TOK_VALUE);
}

/**
 * @brief Read an attribute.
 * @param P The parser (inside a tag, at something that is no blank, '>'
 *          or '<').
 */
static void parse_attribute(Parser *P)
{
    char c = P->s[P->i];
    Mark m = here(P);
    size_t name = P->i, n;

    if (c == '"' || c == '\'' || c == '=' || (P->xml && !name_start(c))) {
        report(P, P->xml ? SCOUT_SEVERITY_ERROR : SCOUT_SEVERITY_WARNING, &m, 1, "unexpected character in a tag");
        advance(P);
        return;
    }
    if (P->xml) {
        while (P->i < P->len && name_part(P->s[P->i])) advance(P);
    } else {
        while (P->i < P->len && !blank(c = P->s[P->i]) && c != '/' && c != '>' && c != '=' && c != '<' &&
               c != '"' && c != '\'') {
            advance(P);
        }
    }
    n = P->i - name;
    if (n == 0) {       /* cannot happen, but must never loop for ever */
        advance(P);
        return;
    }
    add_token(P, m.line, m.col, (long)n, TOK_ATTR);
    if (duplicate_attribute(P, name, n)) {
        report_name(P, P->xml ? SCOUT_SEVERITY_ERROR : SCOUT_SEVERITY_WARNING, &m, (long)n, "attribute ", name, n,
                    " is given twice");
    } else {
        void *array = P->attrs;
        if (grow(P, &array, P->attr_count, &P->attr_cap, sizeof(Attr))) {
            P->attrs = (Attr *)array;
            P->attrs[P->attr_count].name = name;
            P->attrs[P->attr_count++].len = n;
        }
    }
    skip_blanks(P);
    if (P->i >= P->len || P->s[P->i] != '=') {
        if (P->xml) report_name(P, SCOUT_SEVERITY_ERROR, &m, (long)n, "attribute ", name, n, " has no value");
        return;
    }
    advance(P);
    skip_blanks(P);
    if (P->i >= P->len) return;
    c = P->s[P->i];
    if (c == '"' || c == '\'') {
        parse_quoted_value(P, name, n);
    } else if (c == '>' || c == '<' || (c == '/' && P->i + 1 < P->len && P->s[P->i + 1] == '>')) {
        report_name(P, SCOUT_SEVERITY_ERROR, &m, (long)n, "attribute ", name, n, " has no value after '='");
    } else {
        size_t end = P->i;
        Mark value = here(P);
        /* in HTML a '/' belongs to such a value, even before the '>' */
        while (end < P->len && !blank(P->s[end]) && P->s[end] != '>' &&
               !(P->xml && P->s[end] == '/' && end + 1 < P->len && P->s[end + 1] == '>')) {
            end++;
        }
        report_name(P, P->xml ? SCOUT_SEVERITY_ERROR : SCOUT_SEVERITY_HINT, &value, (long)(end - P->i),
                    "the value of attribute ", name, n, " is not in quotes");
        note_value(P, name, n, &value, P->i, end - P->i);
        emit_until(P, end, TOK_VALUE);
    }
}

/**
 * @brief Read a start tag.
 * @param P The parser (at its '<', a name follows).
 */
static void parse_start_tag(Parser *P)
{
    Mark open = here(P);
    size_t name, n;
    int short_tag = 0;

    advance(P);
    name = P->i;
    while (P->i < P->len && name_part(P->s[P->i])) advance(P);
    n = P->i - name;
    add_token(P, open.line, open.col, (long)n + 1, TOK_TAG);
    P->attr_count = 0;
    P->tag = name;
    P->tag_len = n;
    for (;;) {
        char c;
        skip_blanks(P);
        if (P->i >= P->len) {
            report_name(P, SCOUT_SEVERITY_ERROR, &open, (long)n + 1, "tag <", name, n, " has no closing '>'");
            return;
        }
        c = P->s[P->i];
        if (c == '>') {
            emit_until(P, P->i + 1, TOK_TAG);
            break;
        }
        if (c == '/' && P->i + 1 < P->len && P->s[P->i + 1] == '>') {
            emit_until(P, P->i + 2, TOK_TAG);
            short_tag = 1;
            break;
        }
        if (c == '<') {     /* the next tag begins: read it as one */
            report_name(P, SCOUT_SEVERITY_ERROR, &open, (long)n + 1, "tag <", name, n, " has no closing '>'");
            break;
        }
        if (c == '/') {
            if (P->xml) {
                Mark m = here(P);
                report(P, SCOUT_SEVERITY_ERROR, &m, 1, "unexpected character in a tag");
            }
            advance(P);
            continue;
        }
        parse_attribute(P);
    }
    open_element(P, &open, name, n, short_tag);
}

/**
 * @brief Skip what HTML reads as a bogus comment.
 * @param P The parser.
 */
static void skip_to_bracket(Parser *P)
{
    size_t end = find_text(P, P->i, ">");
    skip_to(P, end < P->len ? end + 1 : P->len);
}

/**
 * @brief Read an end tag.
 * @param P The parser (at its "</").
 */
static void parse_end_tag(Parser *P)
{
    Mark open = here(P);
    size_t name, n, end;

    skip_to(P, P->i + 2);
    name = P->i;
    if (P->i >= P->len || !(P->xml ? name_start(P->s[P->i]) : letter(P->s[P->i]))) {
        if (P->xml) report(P, SCOUT_SEVERITY_ERROR, &open, 2, "name expected in the end tag");
        skip_to_bracket(P);
        return;
    }
    while (P->i < P->len && name_part(P->s[P->i])) advance(P);
    n = P->i - name;
    add_token(P, open.line, open.col, (long)n + 2, TOK_TAG);
    skip_blanks(P);
    end = P->i;
    while (end < P->len && P->s[end] != '>' && P->s[end] != '<') end++;
    if (end >= P->len || P->s[end] == '<') {
        report_name(P, SCOUT_SEVERITY_ERROR, &open, (long)n + 2, "end tag </", name, n, " has no closing '>'");
        skip_to(P, end);
    } else {
        if (end != P->i && P->xml) {
            Mark m = here(P);
            report(P, SCOUT_SEVERITY_ERROR, &m, (long)(end - P->i), "unexpected text in the end tag");
        }
        skip_to(P, end);
        emit_until(P, end + 1, TOK_TAG);
    }
    close_element(P, &open, name, n);
}

/**
 * @brief Read a comment.
 * @param P The parser (at its "<!--").
 */
static void parse_comment(Parser *P)
{
    Mark open = here(P);
    size_t end = find_text(P, P->i + 4, "-->");
    if (end >= P->len) {
        report(P, SCOUT_SEVERITY_ERROR, &open, 4, "comment is not closed");
        skip_to(P, P->len);
        return;
    }
    if (P->xml) {
        size_t dashes = find_text(P, P->i + 4, "--");
        if (dashes < end) {
            Mark m;
            skip_to(P, dashes);
            m = here(P);
            report(P, SCOUT_SEVERITY_ERROR, &m, 2, "\"--\" is not allowed in a comment");
        }
    }
    skip_to(P, end + 3);
}

/**
 * @brief Read a CDATA section of an XML file.
 * @param P The parser (at its "<![CDATA[").
 */
static void parse_cdata(Parser *P)
{
    Mark open = here(P);
    size_t end = find_text(P, P->i + 9, "]]>");
    if (P->depth == 0) report(P, SCOUT_SEVERITY_ERROR, &open, 9, "CDATA section outside the root element");
    if (end >= P->len) {
        report(P, SCOUT_SEVERITY_ERROR, &open, 9, "CDATA section is not closed");
        skip_to(P, P->len);
        return;
    }
    emit_until(P, end + 3, TOK_VALUE);
}

/**
 * @brief Read a DOCTYPE, with its internal subset if it has one.
 * @param P The parser (at its "<!").
 */
static void parse_doctype(Parser *P)
{
    Mark open = here(P);
    int depth = 0;
    char quote = 0;

    if (P->xml && P->root_seen) report(P, SCOUT_SEVERITY_ERROR, &open, 9, "DOCTYPE after the root element");
    else if (P->xml && P->doctype_seen) report(P, SCOUT_SEVERITY_ERROR, &open, 9, "second DOCTYPE");
    P->doctype_seen = 1;
    add_token(P, open.line, open.col, 9, TOK_DECL);
    skip_to(P, P->i + 9);
    while (P->i < P->len) {
        char c = P->s[P->i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (depth > 0 && starts_with(P, P->i, "<!--", 0)) {
            size_t end = find_text(P, P->i + 4, "-->");
            if (end >= P->len) {
                skip_to(P, P->len);
                break;
            }
            skip_to(P, end + 2);    /* its '>' is stepped over below */
        } else if (c == '[') {
            depth++;
        } else if (c == ']') {
            if (depth > 0) depth--;
        } else if (c == '>' && depth == 0) {
            emit_until(P, P->i + 1, TOK_DECL);
            return;
        }
        advance(P);
    }
    report(P, SCOUT_SEVERITY_ERROR, &open, 9, "DOCTYPE is not closed");
}

/**
 * @brief Read something that begins with "<?".
 * @param P The parser (at the "<?").
 */
static void parse_instruction(Parser *P)
{
    Mark open = here(P);
    size_t name = P->i + 2, p = name, n, end = find_text(P, P->i + 2, "?>");

    if (!P->xml) {      /* PHP and the like when closed, otherwise a bogus comment */
        if (end < P->len) skip_to(P, end + 2);
        else skip_to_bracket(P);
        return;
    }
    while (p < P->len && name_part(P->s[p])) p++;
    n = p - name;
    if (n == 0) report(P, SCOUT_SEVERITY_ERROR, &open, 2, "name expected after \"<?\"");
    if (n == 3 && starts_with(P, name, "xml", 1) && open.at != P->doc_start) {
        report(P, SCOUT_SEVERITY_ERROR, &open, 5, "the XML declaration must be at the very start of the file");
    }
    if (end >= P->len) {
        report(P, SCOUT_SEVERITY_ERROR, &open, (long)n + 2, "processing instruction is not closed");
        skip_to(P, P->len);
        return;
    }
    add_token(P, open.line, open.col, (long)n + 2, TOK_DECL);
    skip_to(P, end);
    emit_until(P, end + 2, TOK_DECL);
}

/**
 * @brief Read something that begins with '<'.
 * @param P The parser (at the '<').
 */
static void parse_markup(Parser *P)
{
    char next = P->i + 1 < P->len ? P->s[P->i + 1] : '\0';

    P->stray_reported = 0;
    if (starts_with(P, P->i, "<!--", 0)) {
        parse_comment(P);
    } else if (next == '!') {
        if (P->xml && starts_with(P, P->i, "<![CDATA[", 0)) {
            parse_cdata(P);
        } else if (starts_with(P, P->i, "<!DOCTYPE", !P->xml)) {
            parse_doctype(P);
        } else {
            if (P->xml) {
                Mark m = here(P);
                report(P, SCOUT_SEVERITY_ERROR, &m, 2, "unknown declaration");
            }
            skip_to_bracket(P);
        }
    } else if (next == '?') {
        parse_instruction(P);
    } else if (next == '/') {
        parse_end_tag(P);
    } else if (P->xml ? name_start(next) : letter(next)) {
        parse_start_tag(P);
    } else {
        if (P->xml) {
            Mark m = here(P);
            report(P, SCOUT_SEVERITY_ERROR, &m, 1, "'<' must be written as &lt;");
        }
        advance(P);
    }
}

/**
 * @brief Read a byte of the text between the tags.
 * @param P The parser (not at a '<').
 */
static void parse_text(Parser *P)
{
    char c = P->s[P->i];
    if (c == '&') {
        size_t n = entity_length(P, P->i);
        Mark m = here(P);
        if (P->xml) check_entity(P, n);
        if (n > 0) {
            add_token(P, m.line, m.col, (long)n, TOK_ENTITY);
            skip_to(P, P->i + n);
            return;
        }
    } else if (P->xml && c == ']' && starts_with(P, P->i, "]]>", 0)) {
        Mark m = here(P);
        report(P, SCOUT_SEVERITY_ERROR, &m, 3, "\"]]>\" is not allowed in the text");
    }
    if (P->xml && P->depth == 0 && !blank(c) && !P->stray_reported) {
        Mark m = here(P);
        report(P, SCOUT_SEVERITY_ERROR, &m, 1, "text outside the root element");
        P->stray_reported = 1;
    }
    advance(P);
}

/**
 * @brief Set up a parser for a text.
 * @param P The parser (its result is already made).
 * @param source The text.
 * @param length Its length.
 * @param xml Check it as XML.
 */
static void begin(Parser *P, const char *source, size_t length, int xml)
{
    int i;
    P->s = source;
    P->len = length;
    P->line = 1;
    P->xml = xml;
    for (i = 0; i < LEVELS; i++) P->parents[i] = -1;
    if (length >= 3 && memcmp(source, "\xEF\xBB\xBF", 3) == 0) P->i = P->doc_start = 3;
}

/**
 * @brief Go through the text.
 * @param P The parser.
 */
static void scan(Parser *P)
{
    long n = 0;
    while (P->i < P->len && !P->res->out_of_memory) {
        if ((++n & 4095) == 0 && markup_parser_should_stop && markup_parser_should_stop()) {
            P->res->stopped = 1;
            break;
        }
        if (P->s[P->i] == '<') parse_markup(P);
        else parse_text(P);
    }
}

/**
 * @brief Tell whether a link starts with a scheme or a volume.
 * @param s The link.
 * @return Non-zero if it does.
 */
static int has_scheme(const char *s)
{
    if (!letter(*s)) return 0;
    while (letter(*s) || digit(*s) || *s == '+' || *s == '-' || *s == '.') s++;
    return *s == ':';
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
 * @brief The value of a hexadecimal digit.
 * @param c The byte.
 * @return Its value, or -1 if it is no such digit.
 */
static int hex_value(char c)
{
    if (digit(c)) return c - '0';
    c = lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/**
 * @brief Decode the %xx of a part of a link, in place.
 * @param s The part.
 */
static void decode_percent(char *s)
{
    char *out = s;
    while (*s) {
        if (s[0] == '%' && hex_value(s[1]) >= 0 && hex_value(s[2]) >= 0 && (s[1] != '0' || s[2] != '0')) {
            *out++ = (char)(hex_value(s[1]) * 16 + hex_value(s[2]));
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
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
 * @brief Read the anchors of another HTML file; the last one is kept, as
 *        the links of a page often go to the same one.
 * @param P The parser.
 * @param path The file's path.
 * @return Non-zero if they are known (P->other_anchors).
 */
static int read_other(Parser *P, const char *path)
{
    Parser sub;
    size_t length = 0;
    char *text;
    if (P->other_path && strcmp(P->other_path, path) == 0) return P->other_read;
    free(P->other_path);
    free_names(&P->other_anchors);
    P->other_read = 0;
    P->other_path = (char *)malloc(strlen(path) + 1);
    if (!P->other_path) {
        P->res->out_of_memory = 1;
        return 0;
    }
    strcpy(P->other_path, path);
    text = P->files->read(path, &length);
    if (!text) return 0;
    memset(&sub, 0, sizeof(sub));
    sub.res = (MarkupResult *)calloc(1, sizeof(MarkupResult));
    if (!sub.res) {
        P->res->out_of_memory = 1;
        free(text);
        return 0;
    }
    begin(&sub, text, length, 0);
    scan(&sub);
    free(text);
    if (sub.res->out_of_memory) P->res->out_of_memory = 1;
    if (sub.res->stopped) P->res->stopped = 1;
    P->other_read = !sub.res->out_of_memory && !sub.res->stopped;
    P->other_anchors = sub.anchors;
    free(sub.stack);
    free(sub.attrs);
    free(sub.links);
    markup_result_free(sub.res);
    return P->other_read;
}

/**
 * @brief Add a reference to another file.
 * @param P The parser.
 * @param l The link.
 * @param full The file's path (the reference takes it, or frees it on
 *             failure).
 */
static void add_reference(Parser *P, const Link *l, char *full)
{
    void *array = P->res->references;
    MarkupReference *r;
    if (!grow(P, &array, P->res->reference_count, &P->reference_cap, sizeof(MarkupReference))) {
        free(full);
        return;
    }
    P->res->references = (MarkupReference *)array;
    r = &P->res->references[P->res->reference_count++];
    r->target = full;
    r->range.start_line = r->range.end_line = l->line;
    r->range.start_col = l->col;
    r->range.end_col = l->col + (long)l->len;
}

/**
 * @brief Report a problem with a link, naming it as written.
 * @param P The parser.
 * @param l The link.
 * @param prefix The start of the message.
 */
static void report_link(Parser *P, const Link *l, const char *prefix)
{
    Mark m;
    m.at = l->value;
    m.line = l->line;
    m.col = l->col;
    report_name(P, SCOUT_SEVERITY_WARNING, &m, (long)l->len, prefix, l->value, l->len, "");
}

/**
 * @brief Report a link to another file and check it.
 * @param P The parser.
 * @param l The link.
 * @param path Its path, decoded.
 * @param anchor Its anchor, decoded ("" for none).
 */
static void check_file(Parser *P, const Link *l, const char *path, const char *anchor)
{
    char *full = resolve_path(P->path, path);
    int checking = P->files && P->files->exists;
    int found = full && checking && P->files->exists(full);

    if (checking && !found) {
        report_link(P, l, "file not found: ");
    } else if (found && *anchor && strcmp(full, P->path) == 0) {
        if (!has_name(&P->anchors, anchor)) report_link(P, l, "anchor not found: ");
    } else if (found && *anchor && P->files->read && (has_extension(full, ".html") || has_extension(full, ".htm")) &&
               read_other(P, full) && !has_name(&P->other_anchors, anchor)) {
        report_link(P, l, "anchor not found in that file: ");
    }
    if (full) add_reference(P, l, full);
}

/**
 * @brief Check the links of an HTML file, once all its anchors are known.
 * @param P The parser.
 */
static void check_links(Parser *P)
{
    long i;
    if (P->has_base) return;
    for (i = 0; i < P->link_count && !P->res->out_of_memory && !P->res->stopped; i++) {
        const Link *l = &P->links[i];
        char *text, *end, *anchor = NULL;
        if (markup_parser_should_stop && markup_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        text = decode_value(P, l->value, l->len);
        if (!text) return;
        if (!has_scheme(text) && text[0] != '/' && !strpbrk(text, "{$<")) {
            if ((end = strchr(text, '#')) != NULL) {
                *end = '\0';
                anchor = end + 1;
                decode_percent(anchor);
            }
            if ((end = strchr(text, '?')) != NULL) *end = '\0';
            decode_percent(text);
            if (*text) {
                if (P->path) check_file(P, l, text, anchor ? anchor : "");
            } else if (anchor && *anchor && !has_name(&P->anchors, anchor) && !in_list("top", anchor, strlen(anchor), 1)) {
                report_link(P, l, "anchor not found: ");
            }
        }
        free(text);
    }
}

/**
 * @brief Compare two diagnostics by their place, for qsort().
 * @param a The first.
 * @param b The second.
 * @return Negative, zero or positive.
 */
static int compare_diagnostics(const void *a, const void *b)
{
    const MarkupDiagnostic *x = (const MarkupDiagnostic *)a, *y = (const MarkupDiagnostic *)b;
    if (x->line != y->line) return x->line < y->line ? -1 : 1;
    if (x->col != y->col) return x->col < y->col ? -1 : 1;
    return 0;
}

/**
 * @brief Report what is left at the end of the file.
 * @param P The parser.
 */
static void finish(Parser *P)
{
    while (P->depth > 0) report_unclosed(P, &P->stack[--P->depth]);
    if (P->xml && !P->root_seen) {
        size_t k;
        for (k = P->doc_start; k < P->len && blank(P->s[k]); k++) {
            /* only looking for the first thing that is not blank */
        }
        if (k < P->len) {
            Mark m;
            m.at = 0;
            m.line = m.col = 1;
            report(P, SCOUT_SEVERITY_ERROR, &m, 1, "the document has no root element");
        }
    }
}

MarkupResult *markup_parser_analyze(const char *source, size_t length, MarkupLanguage language, const char *path,
                                    const MarkupFiles *files)
{
    Parser P;

    memset(&P, 0, sizeof(P));
    P.res = (MarkupResult *)calloc(1, sizeof(MarkupResult));
    if (!P.res) return NULL;
    P.path = path;
    P.files = files;
    begin(&P, source, length, language == MARKUP_XML);
    scan(&P);
    if (!P.res->stopped && !P.res->out_of_memory) {
        finish(&P);
        if (!P.xml) check_links(&P);
    }
    if (P.res->diagnostic_count > 1) {
        qsort(P.res->diagnostics, (size_t)P.res->diagnostic_count, sizeof(MarkupDiagnostic), compare_diagnostics);
    }
    free(P.stack);
    free(P.attrs);
    free(P.links);
    free(P.other_path);
    free_names(&P.anchors);
    free_names(&P.other_anchors);
    return P.res;
}

void markup_result_free(MarkupResult *res)
{
    long i;
    if (!res) return;
    for (i = 0; i < res->symbol_count; i++) {
        free(res->symbols[i].name);
        free(res->symbols[i].detail);
    }
    free(res->symbols);
    for (i = 0; i < res->reference_count; i++) free(res->references[i].target);
    free(res->references);
    for (i = 0; i < res->diagnostic_count; i++) free(res->diagnostics[i].message);
    free(res->diagnostics);
    free(res->tokens);
    free(res);
}
