/**
 * @file guide_parser.c
 * @brief The analysis of the AmigaGuide parser of Scout: two passes over
 *        the lines of a file.
 *
 * AmigaGuide is line based: a line starting with '@' (but not "@{") is a
 * line command, in other lines "@{...}" is a text command, which has to
 * end on its line. A backslash makes the next character plain text. The
 * first pass collects the nodes and macros, as links may lead forward;
 * the second makes the checks, the symbols and the tokens.
 *
 * The command tables follow the AmigaGuide reference and AGuideCheck of
 * the NDK; IMG came with amigaguide.datatype 47 (AmigaOS 3.2).
 */
#include "guide_parser.h"

#include <stdlib.h>
#include <string.h>

int (*guide_parser_should_stop)(void) = NULL;

#define NODE_BUCKETS    509     /**< hash chains of the node names */
#define STOP_INTERVAL   256     /**< lines between two guide_parser_should_stop() */
#define MAX_ARGS        16      /**< arguments of a command that are looked at */

/** @name Where a line command may stand */
/** @{ */
#define W_GLOBAL    1   /**< outside a node */
#define W_NODE      2   /**< inside a node */
#define W_BOTH      3   /**< in both places */
/** @} */

/** @name What is known about a command's arguments */
/** @{ */
#define F_FREE      1   /**< free text: quotes mean nothing */
#define F_NODE      2   /**< the first one is a node or file/node */
#define F_NUMBER    4   /**< the first one is a number */
#define F_COLOUR    8   /**< the first one is a colour */
#define F_ACTION    16  /**< a text command that needs a label */
#define F_OBSOLETE  32  /**< has no effect any more */
#define F_MACRO     64  /**< \@macro: nothing else to check */
/** @} */

/** A command of the language. */
typedef struct Command {
    const char *name;   /**< its name, in lower case */
    int where;          /**< W_ value; unused for text commands */
    int min_args;       /**< arguments it cannot do without */
    int flags;          /**< F_ flags */
} Command;

/** The line commands. */
static const Command line_commands[] = {
    { "$ver:",        W_GLOBAL, 0, F_FREE },
    { "(c)",          W_GLOBAL, 0, F_FREE },
    { "amigaguide",   W_BOTH,   0, 0 },
    { "author",       W_GLOBAL, 0, F_FREE },
    { "database",     W_GLOBAL, 0, F_FREE },
    { "dnode",        W_GLOBAL, 0, F_FREE | F_OBSOLETE },
    { "embed",        W_NODE,   1, 0 },
    { "endnode",      W_BOTH,   0, F_FREE },
    { "font",         W_BOTH,   2, 0 },
    { "height",       W_GLOBAL, 1, F_NUMBER },
    { "help",         W_BOTH,   1, F_NODE },
    { "index",        W_BOTH,   1, F_NODE },
    { "keywords",     W_NODE,   0, F_FREE },
    { "macro",        W_BOTH,   1, F_MACRO },
    { "master",       W_GLOBAL, 1, 0 },
    { "next",         W_NODE,   1, F_NODE },
    { "node",         W_BOTH,   1, 0 },
    { "onclose",      W_BOTH,   1, 0 },
    { "onopen",       W_BOTH,   1, 0 },
    { "prev",         W_NODE,   1, F_NODE },
    { "proportional", W_NODE,   0, 0 },
    { "rem",          W_BOTH,   0, F_FREE },
    { "remark",       W_BOTH,   0, F_FREE },
    { "smartwrap",    W_BOTH,   0, 0 },
    { "tab",          W_BOTH,   1, F_NUMBER },
    { "title",        W_NODE,   1, 0 },
    { "toc",          W_NODE,   1, F_NODE },
    { "width",        W_GLOBAL, 1, F_NUMBER },
    { "wordwrap",     W_BOTH,   0, 0 },
    { "xref",         W_GLOBAL, 1, 0 }
};

/** The text commands. */
static const Command text_commands[] = {
    { "alink",      0, 1, F_ACTION | F_NODE },
    { "amigaguide", 0, 0, 0 },
    { "apen",       0, 1, F_NUMBER },
    { "b",          0, 0, 0 },
    { "beep",       0, 0, F_ACTION },
    { "bg",         0, 1, F_COLOUR },
    { "body",       0, 0, 0 },
    { "bpen",       0, 1, F_NUMBER },
    { "cleartabs",  0, 0, 0 },
    { "close",      0, 0, F_ACTION | F_OBSOLETE },
    { "code",       0, 0, 0 },
    { "fg",         0, 1, F_COLOUR },
    { "guide",      0, 1, F_ACTION },
    { "i",          0, 0, 0 },
    { "img",        0, 1, 0 },
    { "jcenter",    0, 0, 0 },
    { "jleft",      0, 0, 0 },
    { "jright",     0, 0, 0 },
    { "lindent",    0, 1, F_NUMBER },
    { "line",       0, 0, 0 },
    { "link",       0, 1, F_ACTION | F_NODE },
    { "par",        0, 0, 0 },
    { "pard",       0, 0, 0 },
    { "pari",       0, 1, F_NUMBER },
    { "plain",      0, 0, 0 },
    { "quit",       0, 0, F_ACTION | F_OBSOLETE },
    { "rx",         0, 1, F_ACTION },
    { "rxs",        0, 1, F_ACTION },
    { "settabs",    0, 1, F_NUMBER },
    { "system",     0, 1, F_ACTION },
    { "tab",        0, 0, 0 },
    { "u",          0, 0, 0 },
    { "ub",         0, 0, 0 },
    { "ui",         0, 0, 0 },
    { "uu",         0, 0, 0 }
};

/** The colours of \@{fg} and \@{bg}, in lower case. */
static const char *const colours[] = {
    "back", "background", "fill", "filltext", "highlight", "highlighttext", "shadow", "shine", "text"
};

/** A name found in the file: a piece of the text, not terminated. */
typedef struct Name {
    const char *text;   /**< its first character */
    size_t length;      /**< its length */
    long line;          /**< the line defining it */
    long next;          /**< next node in the hash chain, or -1 */
} Name;

/** An argument of a command. */
typedef struct Arg {
    const char *start;  /**< its first character, after a quote */
    const char *end;    /**< the character after it, before a quote */
    const char *outer;  /**< its first character, with a quote */
    const char *after;  /**< the character after it, with a quote */
    int quoted;         /**< it was written in quotes */
} Arg;

/** The arguments of a command. */
typedef struct Args {
    Arg arg[MAX_ARGS];  /**< the first ones */
    int count;          /**< how many of them there are (at most MAX_ARGS) */
    int open_quote;     /**< a quote was not closed */
    const char *quote;  /**< where that quote is */
} Args;

/** State of one analysis. */
typedef struct Parser {
    GuideResult *res;       /**< the result being built */
    const char *s;          /**< the text */
    size_t len;             /**< its length */
    long symbol_cap;        /**< room for symbols */
    long open_symbol;       /**< index of the symbol of the open node, or -1 */
    long last_end_col;      /**< column after the last character of the line before */
    long diagnostic_cap;    /**< room for diagnostics */
    long token_cap;         /**< room for token values */
    Name *nodes;            /**< the nodes */
    long node_count;        /**< their number */
    long node_cap;          /**< room for them */
    long buckets[NODE_BUCKETS]; /**< first node of each hash chain, or -1 */
    Name *macros;           /**< the macros */
    long macro_count;       /**< their number */
    long macro_cap;         /**< room for them */
    long line;              /**< the current line */
    const char *line_start; /**< its first character */
    const char *line_end;   /**< the character after it, without the line end */
    int in_node;            /**< between \@node and \@endnode */
    int saw_node;           /**< there was a \@node */
    long node_line;         /**< the line of the open \@node */
    long node_col;          /**< column after that \@node's name */
} Parser;

/**
 * @brief Fold a character to lower case (ASCII and Latin-1 letters).
 * @param c The character.
 * @return Its lower case form.
 */
static unsigned char fold(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (unsigned char)(c + 32);
    return c;
}

/**
 * @brief Compare two names without regard to case.
 * @param a The first name.
 * @param la Its length.
 * @param b The second name.
 * @param lb Its length.
 * @return Non-zero if they are the same.
 */
static int same_name(const char *a, size_t la, const char *b, size_t lb)
{
    size_t i;
    if (la != lb) return 0;
    for (i = 0; i < la; i++)
        if (fold((unsigned char)a[i]) != fold((unsigned char)b[i])) return 0;
    return 1;
}

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
    n = *cap ? *cap * 2 : 32;
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
 * @brief Add a diagnostic within the current line.
 * @param P The parser.
 * @param severity SCOUT_SEVERITY_ value.
 * @param from Its first character.
 * @param to The character after it.
 * @param before The text before the name.
 * @param name The name in the message, or NULL for none.
 * @param name_length Its length.
 * @param after The text after the name.
 */
static void report(Parser *P, int severity, const char *from, const char *to, const char *before,
                   const char *name, size_t name_length, const char *after)
{
    GuideResult *r = P->res;
    size_t lb = strlen(before), la = strlen(after);
    char *text;
    GuideDiagnostic *d;
    if (!name) name_length = 0;
    if (!grow(P, (void **)&r->diagnostics, r->diagnostic_count, &P->diagnostic_cap, sizeof(GuideDiagnostic))) return;
    text = (char *)malloc(lb + name_length + la + 1);
    if (!text) {
        r->out_of_memory = 1;
        return;
    }
    memcpy(text, before, lb);
    if (name_length) memcpy(text + lb, name, name_length);
    memcpy(text + lb + name_length, after, la + 1);
    d = &r->diagnostics[r->diagnostic_count++];
    d->message = text;
    d->severity = severity;
    d->range.start_line = d->range.end_line = P->line;
    d->range.start_col = (long)(from - P->line_start) + 1;
    d->range.end_col = (long)(to - P->line_start) + 1;
}

/**
 * @brief Copy a piece of the text as a terminated string.
 * @param P The parser (out_of_memory is set on failure).
 * @param from Its first character.
 * @param to The character after it.
 * @return The copy (allocated), or NULL.
 */
static char *copy_text(Parser *P, const char *from, const char *to)
{
    size_t length = (size_t)(to - from);
    char *text = (char *)malloc(length + 1);
    if (!text) {
        P->res->out_of_memory = 1;
        return NULL;
    }
    memcpy(text, from, length);
    text[length] = '\0';
    return text;
}

/**
 * @brief Add a symbol that begins and, for now, ends in the current line.
 * @param P The parser.
 * @param name Its name.
 * @param detail The start of its detail, or NULL for none.
 * @param detail_end The end of its detail.
 * @param kind SCOUT_SYMBOL_ kind.
 * @return Its index, or -1 if out of memory.
 */
static long add_symbol(Parser *P, const Arg *name, const char *detail, const char *detail_end, int kind)
{
    GuideResult *r = P->res;
    GuideSymbol *s;
    if (!grow(P, (void **)&r->symbols, r->symbol_count, &P->symbol_cap, sizeof(GuideSymbol))) return -1;
    s = &r->symbols[r->symbol_count];
    memset(s, 0, sizeof(*s));
    s->name = copy_text(P, name->start, name->end);
    if (!s->name) return -1;
    if (detail) s->detail = copy_text(P, detail, detail_end);
    s->kind = kind;
    s->range.start_line = s->range.end_line = P->line;
    s->range.start_col = 1;
    s->range.end_col = (long)(P->line_end - P->line_start) + 1;
    s->selection.start_line = s->selection.end_line = P->line;
    s->selection.start_col = (long)(name->start - P->line_start) + 1;
    s->selection.end_col = (long)(name->end - P->line_start) + 1;
    return r->symbol_count++;
}

/**
 * @brief End the symbol of the open node, if there is one.
 * @param P The parser.
 * @param line The last line of the node.
 * @param col The column after its last character.
 */
static void close_symbol(Parser *P, long line, long col)
{
    if (P->open_symbol >= 0) {
        GuideRange *r = &P->res->symbols[P->open_symbol].range;
        r->end_line = line;
        r->end_col = col;
        P->open_symbol = -1;
    }
}

/**
 * @brief Add a highlighting token within the current line.
 * @param P The parser.
 * @param from Its first character.
 * @param to The character after it.
 * @param kind SCOUT_TOKEN_ kind.
 */
static void add_token(Parser *P, const char *from, const char *to, int kind)
{
    GuideResult *r = P->res;
    long *t;
    if (to <= from) return;
    if (!grow(P, (void **)&r->tokens, r->token_count * 4 + 3, &P->token_cap, sizeof(long))) return;
    t = r->tokens + r->token_count * 4;
    t[0] = P->line;
    t[1] = (long)(from - P->line_start) + 1;
    t[2] = (long)(to - from);
    t[3] = kind;
    r->token_count++;
}

/**
 * @brief Move on to the next line of the text.
 * @param P The parser.
 * @param[in,out] pos The offset of the next line.
 * @return Non-zero if there is a line (P->line and its bounds are set).
 */
static int next_line(Parser *P, size_t *pos)
{
    size_t i = *pos, n;
    if (i >= P->len) return 0;
    P->line_start = P->s + i;
    while (i < P->len && P->s[i] != '\n' && P->s[i] != '\r') i++;
    P->line_end = P->s + i;
    n = i;
    if (n < P->len && P->s[n] == '\r') n++;
    if (n < P->len && P->s[n] == '\n') n++;
    *pos = n;
    P->line++;
    return 1;
}

/**
 * @brief Read the arguments of a command.
 * @param p Where they begin.
 * @param end The end of the line.
 * @param in_text Non-zero in a text command: '}' ends it.
 * @param[out] a The arguments.
 * @return Where reading stopped: at the '}', a '{' or the end of the line.
 */
static const char *read_args(const char *p, const char *end, int in_text, Args *a)
{
    a->count = 0;
    a->open_quote = 0;
    a->quote = NULL;
    for (;;) {
        Arg arg;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end || (in_text && (*p == '}' || *p == '{'))) return p;
        arg.outer = p;
        arg.quoted = *p == '"';
        if (arg.quoted) {
            arg.start = ++p;
            while (p < end && *p != '"') p += (*p == '\\' && p + 1 < end) ? 2 : 1;
            arg.end = p;
            if (p >= end) {
                a->open_quote = 1;
                a->quote = arg.outer;
                return end;
            }
            arg.after = ++p;
        } else {
            arg.start = p;
            while (p < end && *p != ' ' && *p != '\t' && !(in_text && (*p == '}' || *p == '{')))
                p += (*p == '\\' && p + 1 < end) ? 2 : 1;
            if (p > end) p = end;
            arg.end = arg.after = p;
        }
        if (a->count < MAX_ARGS) a->arg[a->count++] = arg;
    }
}

/**
 * @brief The hash chain of a node name.
 * @param text The name.
 * @param length Its length.
 * @return The index of its chain.
 */
static unsigned int name_hash(const char *text, size_t length)
{
    unsigned long h = 0;
    size_t i;
    for (i = 0; i < length; i++) h = h * 31 + fold((unsigned char)text[i]);
    return (unsigned int)(h % NODE_BUCKETS);
}

/**
 * @brief Find a node by its name.
 * @param P The parser.
 * @param text The name.
 * @param length Its length.
 * @return The first node of that name, or NULL.
 */
static const Name *find_node(const Parser *P, const char *text, size_t length)
{
    long i;
    const Name *found = NULL;
    /* the chain holds the latest first: the last one met is the first defined */
    for (i = P->buckets[name_hash(text, length)]; i >= 0; i = P->nodes[i].next)
        if (same_name(P->nodes[i].text, P->nodes[i].length, text, length)) found = &P->nodes[i];
    return found;
}

/**
 * @brief Find a command in a table.
 * @param table The table.
 * @param count Its size.
 * @param name The name.
 * @param length Its length.
 * @return The command, or NULL.
 */
static const Command *find_command(const Command *table, size_t count, const char *name, size_t length)
{
    size_t i;
    for (i = 0; i < count; i++)
        if (same_name(table[i].name, strlen(table[i].name), name, length)) return &table[i];
    return NULL;
}

/**
 * @brief Tell whether a line starts a line command.
 * @param s The line.
 * @param end Its end.
 * @return Non-zero if it does.
 */
static int is_line_command(const char *s, const char *end)
{
    unsigned char c;
    if (end - s < 2 || s[0] != '@') return 0;
    c = (unsigned char)s[1];
    return c == '$' || c == '(' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * @brief The end of a line command's name.
 * @param s Its first character, after the '@'.
 * @param end The end of the line.
 * @return The character after the name.
 */
static const char *name_end(const char *s, const char *end)
{
    while (s < end && *s != ' ' && *s != '\t') s++;
    return s;
}

/**
 * @brief Tell whether a text is a number.
 * @param s The text.
 * @param end Its end.
 * @return Non-zero if it is digits (at least one) after an optional sign.
 */
static int is_number(const char *s, const char *end)
{
    if (s < end && *s == '-') s++;
    if (s >= end) return 0;
    for (; s < end; s++)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

/**
 * @brief Add a name to a list while the first pass collects them.
 * @param P The parser.
 * @param list The list.
 * @param count Its number of names.
 * @param cap Its room.
 * @param a The argument holding the name.
 * @return The new entry, or NULL if out of memory.
 */
static Name *add_name(Parser *P, Name **list, long *count, long *cap, const Arg *a)
{
    Name *n;
    if (!grow(P, (void **)list, *count, cap, sizeof(Name))) return NULL;
    n = &(*list)[(*count)++];
    n->text = a->start;
    n->length = (size_t)(a->end - a->start);
    n->line = P->line;
    n->next = -1;
    return n;
}

/**
 * @brief The first pass: collect the nodes and macros.
 * @param P The parser.
 */
static void collect(Parser *P)
{
    size_t pos = 0;
    long i;
    for (i = 0; i < NODE_BUCKETS; i++) P->buckets[i] = -1;
    P->line = 0;
    while (next_line(P, &pos) && !P->res->out_of_memory) {
        const char *name, *stop;
        Args a;
        if (!is_line_command(P->line_start, P->line_end)) continue;
        name = P->line_start + 1;
        stop = name_end(name, P->line_end);
        if (same_name(name, (size_t)(stop - name), "node", 4)) {
            read_args(stop, P->line_end, 0, &a);
            if (a.count > 0) {
                Name *n = add_name(P, &P->nodes, &P->node_count, &P->node_cap, &a.arg[0]);
                if (n) {
                    unsigned int h = name_hash(n->text, n->length);
                    n->next = P->buckets[h];
                    P->buckets[h] = P->node_count - 1;
                }
            }
        } else if (same_name(name, (size_t)(stop - name), "macro", 5)) {
            read_args(stop, P->line_end, 0, &a);
            if (a.count > 0) add_name(P, &P->macros, &P->macro_count, &P->macro_cap, &a.arg[0]);
        }
    }
}

/**
 * @brief Tell whether a macro of that name is defined.
 * @param P The parser.
 * @param name The name.
 * @param length Its length.
 * @return Non-zero if it is.
 */
static int is_macro(const Parser *P, const char *name, size_t length)
{
    long i;
    for (i = 0; i < P->macro_count; i++)
        if (same_name(P->macros[i].text, P->macros[i].length, name, length)) return 1;
    return 0;
}

/**
 * @brief Report a target that is a node of this file and not defined.
 * @param P The parser.
 * @param a The argument holding the target.
 */
static void check_target(Parser *P, const Arg *a)
{
    const char *p;
    size_t length = (size_t)(a->end - a->start);
    for (p = a->start; p < a->end; p++)
        if (*p == '/' || *p == ':') return;     /* in another file */
    if (length > 0 && !find_node(P, a->start, length))
        report(P, SCOUT_SEVERITY_WARNING, a->outer, a->after, "this file has no node '", a->start, length, "'");
}

/**
 * @brief Check the arguments of a command against what is known about it.
 * @param P The parser.
 * @param c The command.
 * @param a The arguments, the command's own ones from @p first on.
 * @param first The index of the first one.
 * @param name The command's name in messages.
 * @param name_length Its length.
 * @param at Where to report a missing argument: its start.
 * @param at_end Its end.
 */
static void check_args(Parser *P, const Command *c, const Args *a, int first, const char *name, size_t name_length,
                       const char *at, const char *at_end)
{
    const Arg *arg = a->count > first ? &a->arg[first] : NULL;
    int have = a->count - first;
    if (have < c->min_args) {
        report(P, SCOUT_SEVERITY_ERROR, at, at_end, "'", name, name_length, "' needs more arguments");
        return;
    }
    if (!arg) return;
    if (c->flags & F_NODE) check_target(P, arg);
    if ((c->flags & F_NUMBER) && !is_number(arg->start, arg->end))
        report(P, SCOUT_SEVERITY_WARNING, arg->outer, arg->after, "'", name, name_length, "' expects a number");
    if (c->flags & F_COLOUR) {
        size_t i, length = (size_t)(arg->end - arg->start);
        for (i = 0; i < sizeof(colours) / sizeof(colours[0]); i++)
            if (same_name(colours[i], strlen(colours[i]), arg->start, length)) return;
        report(P, SCOUT_SEVERITY_WARNING, arg->outer, arg->after, "unknown colour '", arg->start, length, "'");
    }
}

/**
 * @brief Make the tokens of a command's arguments.
 * @param P The parser.
 * @param c The command, or NULL if it is not known.
 * @param a The arguments.
 * @param first The index of the first one.
 */
static void token_args(Parser *P, const Command *c, const Args *a, int first)
{
    int i;
    for (i = first; i < a->count; i++) {
        int node = i == first && c && (c->flags & F_NODE);
        add_token(P, a->arg[i].outer, a->arg[i].after, node ? SCOUT_TOKEN_LABEL : SCOUT_TOKEN_PARAMETER);
    }
}

/**
 * @brief Handle \@node.
 * @param P The parser.
 * @param a The arguments.
 * @param at The command.
 * @param stop The end of its name.
 */
static void node_command(Parser *P, const Args *a, const char *at, const char *stop)
{
    if (P->in_node)
        report(P, SCOUT_SEVERITY_WARNING, at, stop, "the node before this one has no @endnode", NULL, 0, "");
    close_symbol(P, P->line - 1, P->last_end_col);
    P->in_node = 1;
    P->saw_node = 1;
    P->node_line = P->line;
    P->node_col = (long)(stop - P->line_start) + 1;
    if (a->count == 0) {
        report(P, SCOUT_SEVERITY_ERROR, at, stop, "@node needs a name", NULL, 0, "");
        return;
    }
    {
        const Arg *name = &a->arg[0];
        size_t length = (size_t)(name->end - name->start);
        const Name *first = find_node(P, name->start, length);
        if (first && first->line != P->line)
            report(P, SCOUT_SEVERITY_WARNING, name->outer, name->after, "node '", name->start, length, "' is defined twice");
        P->open_symbol = add_symbol(P, name, a->count > 1 ? a->arg[1].start : NULL, a->count > 1 ? a->arg[1].end : NULL,
                                    SCOUT_SYMBOL_HEADING);
        add_token(P, name->outer, name->after, SCOUT_TOKEN_LABEL);
        if (a->count > 1) add_token(P, a->arg[1].outer, a->arg[1].after, SCOUT_TOKEN_CONSTANT);
    }
}

/**
 * @brief Handle a line command.
 * @param P The parser.
 */
static void line_command(Parser *P)
{
    const char *at = P->line_start, *name = at + 1;
    const char *stop = name_end(name, P->line_end);
    size_t length = (size_t)(stop - name);
    const Command *c = find_command(line_commands, sizeof(line_commands) / sizeof(line_commands[0]), name, length);
    Args a;

    if (c && (same_name(name, length, "rem", 3) || same_name(name, length, "remark", 6)))
        return;     /* a comment: the lexical rules colour it */
    add_token(P, at, stop, SCOUT_TOKEN_FUNCTION);
    if (!c) {
        report(P, SCOUT_SEVERITY_WARNING, at, stop, "unknown command @", name, length, "");
        return;
    }
    a.count = 0;
    a.open_quote = 0;
    if (!(c->flags & F_FREE)) {
        read_args(stop, P->line_end, 0, &a);
        if (a.open_quote) {
            report(P, SCOUT_SEVERITY_ERROR, a.quote, P->line_end, "a quote is not closed", NULL, 0, "");
            return;
        }
    }
    if (same_name(name, length, "node", 4)) {
        node_command(P, &a, at, stop);
        return;
    }
    if (same_name(name, length, "database", 8)) {
        if (P->line != 1)
            report(P, SCOUT_SEVERITY_WARNING, at, stop, "@database belongs in the first line", NULL, 0, "");
        return;
    }
    if (same_name(name, length, "endnode", 7)) {
        if (!P->in_node)
            report(P, SCOUT_SEVERITY_WARNING, at, stop, "@endnode without a node", NULL, 0, "");
        P->in_node = 0;
        close_symbol(P, P->line, (long)(P->line_end - P->line_start) + 1);
        return;
    }
    if (c->flags & F_OBSOLETE)
        report(P, SCOUT_SEVERITY_HINT, at, stop, "@", name, length, " is obsolete");
    if (!(c->where & W_NODE) && P->in_node)
        report(P, SCOUT_SEVERITY_WARNING, at, stop, "@", name, length, " belongs outside a node");
    else if (!(c->where & W_GLOBAL) && !P->in_node)
        report(P, SCOUT_SEVERITY_WARNING, at, stop, "@", name, length, " belongs inside a node");
    if (c->flags & F_FREE) return;
    token_args(P, c, &a, 0);
    if ((c->flags & F_MACRO) && a.count > 0) {
        /* the text of a macro may be written without quotes, with blanks */
        const char *text = a.count > 1 ? a.arg[1].start : NULL;
        add_symbol(P, &a.arg[0], text, a.count > 1 && a.arg[1].quoted ? a.arg[1].end : P->line_end, SCOUT_SYMBOL_MACRO);
    }
    if (same_name(name, length, "title", 5) && a.count > 0 && P->open_symbol >= 0
        && !P->res->symbols[P->open_symbol].detail)
        P->res->symbols[P->open_symbol].detail = copy_text(P, a.arg[0].start, a.arg[0].end);
    if (!(c->flags & F_MACRO) || a.count == 0) check_args(P, c, &a, 0, name, length, at, stop);
}

/**
 * @brief Tell whether a word in quotes is a command rather than a label.
 *
 * Some guide generators write \@{"b"}, which the datatype takes as \@{b}.
 *
 * @param P The parser.
 * @param a The word.
 * @return Non-zero if it is a macro or a text command that takes no label.
 */
static int is_plain_command(const Parser *P, const Arg *a)
{
    size_t length = (size_t)(a->end - a->start);
    const Command *c = find_command(text_commands, sizeof(text_commands) / sizeof(text_commands[0]), a->start, length);
    return c ? !(c->flags & F_ACTION) : is_macro(P, a->start, length);
}

/**
 * @brief Handle a text command.
 * @param P The parser.
 * @param at Its '@'.
 * @return The character after the command.
 */
static const char *text_command(Parser *P, const char *at)
{
    const char *end = P->line_end, *p, *name, *close;
    const Command *c;
    int first = 0;
    size_t length;
    Args a;

    p = read_args(at + 2, end, 1, &a);
    if (a.open_quote) {
        report(P, SCOUT_SEVERITY_ERROR, a.quote, end, "a quote is not closed", NULL, 0, "");
        return end;
    }
    if (p >= end) {
        report(P, SCOUT_SEVERITY_ERROR, at, end, "@{ is not closed on its line", NULL, 0, "");
        return end;
    }
    if (*p == '{') {
        report(P, SCOUT_SEVERITY_ERROR, p, p + 1, "'{' inside a text command", NULL, 0, "");
        return p + 1;
    }
    close = p + 1;
    add_token(P, at, at + 2, SCOUT_TOKEN_MACRO);
    if (a.count > 0 && a.arg[0].quoted && !(a.count == 1 && is_plain_command(P, &a.arg[0]))) {
        add_token(P, a.arg[0].outer, a.arg[0].after, SCOUT_TOKEN_CONSTANT);
        first = 1;
    }
    if (a.count <= first) {
        if (first) report(P, SCOUT_SEVERITY_ERROR, at, close, "a label needs a command", NULL, 0, "");
        else report(P, SCOUT_SEVERITY_ERROR, at, close, "empty text command", NULL, 0, "");
        add_token(P, p, close, SCOUT_TOKEN_MACRO);
        return close;
    }
    if (!P->in_node)
        report(P, SCOUT_SEVERITY_WARNING, at, close, "a text command outside a node", NULL, 0, "");
    name = a.arg[first].start;
    length = (size_t)(a.arg[first].end - name);
    c = find_command(text_commands, sizeof(text_commands) / sizeof(text_commands[0]), name, length);
    add_token(P, a.arg[first].outer, a.arg[first].after, SCOUT_TOKEN_FUNCTION);
    token_args(P, c, &a, first + 1);
    add_token(P, p, close, SCOUT_TOKEN_MACRO);
    if (!c) {
        if (!is_macro(P, name, length))
            report(P, SCOUT_SEVERITY_WARNING, a.arg[first].outer, a.arg[first].after, "unknown text command '", name, length, "'");
        return close;
    }
    if ((c->flags & F_ACTION) && !first) {
        report(P, SCOUT_SEVERITY_ERROR, at, close, "'", name, length, "' needs a label");
        return close;
    }
    if (!(c->flags & F_ACTION) && first) {
        report(P, SCOUT_SEVERITY_WARNING, a.arg[0].outer, a.arg[0].after, "'", name, length, "' takes no label");
        return close;
    }
    if (c->flags & F_OBSOLETE)
        report(P, SCOUT_SEVERITY_HINT, a.arg[first].outer, a.arg[first].after, "'", name, length, "' has no effect any more");
    check_args(P, c, &a, first + 1, name, length, a.arg[first].outer, a.arg[first].after);
    return close;
}

/**
 * @brief Look for text commands in a line of text.
 * @param P The parser.
 */
static void text_line(Parser *P)
{
    const char *p = P->line_start, *end = P->line_end;
    while (p < end) {
        if (*p == '\\') p += 2;
        else if (*p == '@' && p + 1 < end && p[1] == '{') p = text_command(P, p);
        else p++;
    }
}

/**
 * @brief The second pass: the checks and tokens.
 * @param P The parser.
 */
static void check(Parser *P)
{
    size_t pos = 0;
    size_t i = 0;
    while (i < P->len && (P->s[i] == ' ' || P->s[i] == '\t' || P->s[i] == '\r' || P->s[i] == '\n')) i++;
    if (i >= P->len) return;    /* nothing written yet */
    P->line = 0;
    while (next_line(P, &pos) && !P->res->out_of_memory) {
        if ((P->line % STOP_INTERVAL) == 0 && guide_parser_should_stop && guide_parser_should_stop()) {
            P->res->stopped = 1;
            return;
        }
        if (P->line == 1) {
            const char *stop = name_end(P->line_start, P->line_end);
            if (!same_name(P->line_start, (size_t)(stop - P->line_start), "@database", 9))
                report(P, SCOUT_SEVERITY_ERROR, P->line_start, stop > P->line_start ? stop : stop + 1,
                       "the first line must be @database", NULL, 0, "");
        }
        if (is_line_command(P->line_start, P->line_end)) line_command(P);
        else text_line(P);
        P->last_end_col = (long)(P->line_end - P->line_start) + 1;
    }
    close_symbol(P, P->line, P->last_end_col);
    if (P->in_node) {
        /* reported at the @node that stays open; only its columns matter */
        P->line = P->node_line;
        P->line_start = P->s;
        report(P, SCOUT_SEVERITY_WARNING, P->s, P->s + P->node_col - 1, "this node has no @endnode", NULL, 0, "");
    }
    P->line = 1;
    P->line_start = P->s;
    if (!P->saw_node) {
        report(P, SCOUT_SEVERITY_WARNING, P->s, P->s + 1, "this file has no @node", NULL, 0, "");
    }
}

GuideResult *guide_parser_analyze(const char *source, size_t length)
{
    Parser *P = (Parser *)calloc(1, sizeof(Parser));
    GuideResult *res = (GuideResult *)calloc(1, sizeof(GuideResult));
    if (!P || !res) {
        free(P);
        free(res);
        return NULL;
    }
    P->res = res;
    P->open_symbol = -1;
    P->s = source;
    P->len = length;
    collect(P);
    if (!res->out_of_memory) check(P);
    free(P->nodes);
    free(P->macros);
    free(P);
    return res;
}

void guide_result_free(GuideResult *res)
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
