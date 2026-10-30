/**
 * @file dos_parser.c
 * @brief The analysis of the AmigaDOS script parser of Scout: one pass over
 *        the lines of a script.
 *
 * A line starting with the dot character (".", changed with .DOT) is a
 * dot command: .KEY gives the template whose parameters are substituted
 * in angle brackets (characters changed with .BRA and .KET, the default
 * separator "$" with .DOLLAR), .DEFAULT a parameter's default; "." alone
 * or followed by a blank is a comment. Other lines are commands: ";"
 * outside quotes starts a comment, '*' escapes the next character in a
 * quoted string. The first word names the command (after its path, e.g.
 * "C:Copy"); IF, ELSE, ENDIF, LAB, SKIP, SET, SETENV and ALIAS are looked
 * at. Parameters are only substituted when there is a .KEY line, as
 * Execute does, so "<" is a redirection otherwise. Shell variables ($name,
 * ${name}) are marked anywhere outside comments.
 */
#include "dos_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int (*dos_parser_should_stop)(void) = NULL;

const char *const dos_parser_keywords[] = {
    "if", "else", "endif", "skip", "lab", "quit", "failat", "execute",
    "run", "set", "unset", "setenv", "unsetenv", "getenv", "alias",
    "unalias", "cd", "path", "resident", "stack", "ask", "echo", "wait",
    "why", "fault", "prompt", "endcli", "endshell", "newcli", "newshell",
    "break",
    NULL
};

const char *const dos_parser_commands[] = {
    "addbuffers", "assign", "avail", "binddrivers", "changetaskpri",
    "conclip", "copy", "cpu", "date", "delete", "dir", "diskchange", "ed",
    "edit", "eval", "filenote", "format", "iconx", "info", "install",
    "join", "list", "loadresource", "loadwb", "lock", "makedir",
    "makelink", "mount", "protect", "relabel", "rename", "requestchoice",
    "requestfile", "rx", "search", "setclock", "setdate", "setfont",
    "setkeyboard", "setpatch", "sort", "status", "type", "version",
    "wbrun", "which",
    NULL
};

const char *const dos_parser_constants[] = {
    "warn", "error", "fail", "eq", "gt", "ge", "not", "exists", "val",
    "back",
    NULL
};

/** Most words of a command line that are looked at. */
#define MAX_WORDS 8

/** An open IF. */
typedef struct DosIf {
    long line;          /**< its line */
    long col;           /**< column of the IF */
    int has_else;       /**< an ELSE was seen */
} DosIf;

/** A SKIP to a label, checked at the end. */
typedef struct DosSkip {
    char *name;         /**< the label (allocated) */
    long line;          /**< its line */
    long col;           /**< column of the label's name */
    long len;           /**< length of the name */
    int back;           /**< with BACK (searches upwards) */
} DosSkip;

/** A word of a command line (columns from 0, end exclusive). */
typedef struct DosWord {
    size_t start;       /**< first column */
    size_t end;         /**< column after it */
    int quoted;         /**< written in quotes */
} DosWord;

/** State of one analysis. */
typedef struct Parser {
    DosParseResult *res;    /**< the result being built */
    long symbol_cap;        /**< room for symbols */
    long diagnostic_cap;    /**< room for diagnostics */
    long token_cap;         /**< room for tokens */
    char bra;               /**< opens a parameter (.BRA) */
    char ket;               /**< closes it (.KET) */
    char dot;               /**< starts a dot command (.DOT) */
    char dollar;            /**< separates a default (.DOLLAR) */
    char **keys;            /**< parameters of the template (allocated) */
    long key_count;         /**< number of them */
    int has_key;            /**< a .KEY line was seen */
    DosIf *ifs;             /**< the open IFs */
    long if_count;          /**< number of them */
    long if_cap;            /**< room for them */
    DosSkip *skips;         /**< the SKIPs to labels */
    long skip_count;        /**< number of them */
    long skip_cap;          /**< room for them */
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
 * @brief Fold an ASCII letter to lower case.
 * @param c The character.
 * @return The lower case letter, or @p c.
 */
static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/**
 * @brief Compare a piece of text with a lower case word, without regard to
 *        case.
 * @param s The text.
 * @param len Its length.
 * @param word The word (lower case, terminated).
 * @return Non-zero if they are equal.
 */
static int word_is(const char *s, size_t len, const char *word)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (!word[i] || lower((unsigned char)s[i]) != word[i]) return 0;
    }
    return word[len] == '\0';
}

/**
 * @brief Compare two names without regard to case.
 * @param a A name (terminated).
 * @param b Another name.
 * @param blen Length of @p b.
 * @return Non-zero if they are equal.
 */
static int name_equal(const char *a, const char *b, size_t blen)
{
    size_t i;
    for (i = 0; i < blen; i++) {
        if (!a[i] || lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return 0;
    }
    return a[blen] == '\0';
}

/**
 * @brief Tell whether a character may be part of a name (of a parameter,
 *        variable or label).
 * @param c The character.
 * @return Non-zero if it may.
 */
static int name_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '.' || c == '-' || c >= 0xc0;
}

/**
 * @brief Add a diagnostic on one line.
 * @param P The parser.
 * @param severity SCOUT_SEVERITY_ value.
 * @param line The line.
 * @param col Its first column (1-based).
 * @param end The column after it.
 * @param format The message, with at most one "%s".
 * @param name What "%s" stands for (may be NULL).
 * @param name_len Length of @p name.
 */
static void add_diagnostic(Parser *P, int severity, long line, long col, long end, const char *format,
                           const char *name, size_t name_len)
{
    DosParseResult *r = P->res;
    DosDiagnostic *d;
    char text[160], quoted[64];
    if (!grow(P, (void **)&r->diagnostics, r->diagnostic_count, &P->diagnostic_cap, sizeof(DosDiagnostic)))
        return;
    if (name_len > sizeof(quoted) - 1) name_len = sizeof(quoted) - 1;
    if (name) memcpy(quoted, name, name_len);
    quoted[name ? name_len : 0] = '\0';
    snprintf(text, sizeof(text), format, quoted);
    d = &r->diagnostics[r->diagnostic_count];
    if ((d->message = dup_text(text, strlen(text))) == NULL) {
        r->out_of_memory = 1;
        return;
    }
    d->severity = severity;
    d->range.start_line = d->range.end_line = line;
    d->range.start_col = col;
    d->range.end_col = end > col ? end : col + 1;
    r->diagnostic_count++;
}

/**
 * @brief Add a highlighting token.
 * @param P The parser.
 * @param line Its line.
 * @param col Its first column (1-based).
 * @param len Its length.
 * @param kind SCOUT_TOKEN_ value.
 */
static void add_token(Parser *P, long line, long col, long len, long kind)
{
    DosParseResult *r = P->res;
    long *t;
    if (len <= 0) return;
    /* token_cap counts longs, four per token */
    if (r->token_count * 4 + 4 > P->token_cap) {
        long n = P->token_cap ? P->token_cap * 2 : 256;
        long *bigger = (long *)realloc(r->tokens, (size_t)n * sizeof(long));
        if (!bigger) {
            r->out_of_memory = 1;
            return;
        }
        r->tokens = bigger;
        P->token_cap = n;
    }
    t = &r->tokens[r->token_count * 4];
    t[0] = line;
    t[1] = col;
    t[2] = len;
    t[3] = kind;
    r->token_count++;
}

/**
 * @brief Find a symbol by its kind and name.
 * @param P The parser.
 * @param kind SCOUT_SYMBOL_ value.
 * @param name The name.
 * @param len Its length.
 * @return Its index, or -1.
 */
static long find_symbol(const Parser *P, int kind, const char *name, size_t len)
{
    long i;
    for (i = 0; i < P->res->symbol_count; i++) {
        const DosSymbol *s = &P->res->symbols[i];
        if (s->kind == kind && name_equal(s->name, name, len)) return i;
    }
    return -1;
}

/**
 * @brief Add a symbol.
 * @param P The parser.
 * @param kind SCOUT_SYMBOL_ value.
 * @param global Visible to other scripts.
 * @param text The line.
 * @param line_len Its length.
 * @param line Its number.
 * @param start First column of the name (from 0).
 * @param end Column after the name.
 * @param detail What defines it (may be NULL).
 * @param detail_len Length of @p detail.
 */
static void add_symbol(Parser *P, int kind, int global, const char *text, size_t line_len, long line,
                       size_t start, size_t end, const char *detail, size_t detail_len)
{
    DosParseResult *r = P->res;
    DosSymbol *s;
    if (!grow(P, (void **)&r->symbols, r->symbol_count, &P->symbol_cap, sizeof(DosSymbol))) return;
    s = &r->symbols[r->symbol_count];
    memset(s, 0, sizeof(*s));
    s->name = dup_text(text + start, end - start);
    s->detail = detail ? dup_text(detail, detail_len) : NULL;
    if (!s->name || (detail && !s->detail)) {
        free(s->name);
        free(s->detail);
        r->out_of_memory = 1;
        return;
    }
    s->kind = kind;
    s->global = global;
    s->range.start_line = s->range.end_line = line;
    s->range.start_col = 1;
    s->range.end_col = (long)line_len + 1;
    s->selection.start_line = s->selection.end_line = line;
    s->selection.start_col = (long)start + 1;
    s->selection.end_col = (long)end + 1;
    r->symbol_count++;
}

/**
 * @brief Tell whether a name is a parameter of the template.
 * @param P The parser.
 * @param name The name.
 * @param len Its length.
 * @return Non-zero if it is.
 */
static int is_key(const Parser *P, const char *name, size_t len)
{
    long i;
    for (i = 0; i < P->key_count; i++) {
        if (name_equal(P->keys[i], name, len)) return 1;
    }
    return 0;
}

/**
 * @brief Skip blanks.
 * @param s The line.
 * @param i The position.
 * @param len The line's length.
 * @return The first position that is not a blank.
 */
static size_t skip_blanks(const char *s, size_t i, size_t len)
{
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    return i;
}

/**
 * @brief Read the .KEY template: each parameter becomes a symbol and a
 *        token.
 * @param P The parser.
 * @param s The line.
 * @param len Its length.
 * @param line Its number.
 * @param i Where the template starts.
 */
static void read_template(Parser *P, const char *s, size_t len, long line, size_t i)
{
    while (i < len) {
        size_t start, end, item_end;
        i = skip_blanks(s, i, len);
        start = i;
        while (i < len && s[i] != ',' && s[i] != '/' && s[i] != ' ' && s[i] != '\t') i++;
        end = i;
        while (i < len && s[i] != ',') i++;
        item_end = i;
        while (item_end > start && (s[item_end - 1] == ' ' || s[item_end - 1] == '\t')) item_end--;
        if (end > start) {
            char **bigger = (char **)realloc(P->keys, (size_t)(P->key_count + 1) * sizeof(char *));
            char *name = dup_text(s + start, end - start);
            if (!bigger || !name) {
                if (bigger) P->keys = bigger;
                free(name);
                P->res->out_of_memory = 1;
                return;
            }
            P->keys = bigger;
            P->keys[P->key_count++] = name;
            add_symbol(P, SCOUT_SYMBOL_VARIABLE, 0, s, len, line, start, end, s + start, item_end - start);
            add_token(P, line, (long)start + 1, (long)(end - start), SCOUT_TOKEN_PARAMETER);
        }
        if (i < len) i++;   /* the comma */
    }
}

/**
 * @brief Handle a dot command.
 * @param P The parser.
 * @param s The line (starting with the dot character).
 * @param len Its length.
 * @param line Its number.
 */
static void dot_command(Parser *P, const char *s, size_t len, long line)
{
    size_t i = 1, start, end;
    while (i < len && s[i] != ' ' && s[i] != '\t') i++;
    end = i;
    i = skip_blanks(s, i, len);
    if (end == 1) return;   /* "." alone or ". text": a comment */
    if (word_is(s + 1, end - 1, "key") || word_is(s + 1, end - 1, "k")) {
        if (P->has_key)
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, 1, (long)end + 1, "second .KEY line", NULL, 0);
        P->has_key = 1;
        read_template(P, s, len, line, i);
    } else if (word_is(s + 1, end - 1, "default") || word_is(s + 1, end - 1, "def")) {
        start = i;
        while (i < len && s[i] != ' ' && s[i] != '\t') i++;
        if (i == start) {
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, 1, (long)end + 1, ".DEFAULT needs a parameter", NULL, 0);
        } else if (!is_key(P, s + start, i - start)) {
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, (long)start + 1, (long)i + 1,
                           "%s is not a parameter of the template", s + start, i - start);
        } else {
            add_token(P, line, (long)start + 1, (long)(i - start), SCOUT_TOKEN_PARAMETER);
        }
    } else if (word_is(s + 1, end - 1, "bra") || word_is(s + 1, end - 1, "ket") ||
               word_is(s + 1, end - 1, "dot") || word_is(s + 1, end - 1, "dollar") ||
               word_is(s + 1, end - 1, "dol")) {
        char c;
        if (i >= len) {
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, 1, (long)end + 1, "this dot command needs a character",
                           NULL, 0);
            return;
        }
        c = s[i];
        if (word_is(s + 1, end - 1, "bra")) P->bra = c;
        else if (word_is(s + 1, end - 1, "ket")) P->ket = c;
        else if (word_is(s + 1, end - 1, "dot")) P->dot = c;
        else P->dollar = c;
    } else {
        add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, 1, (long)end + 1, "unknown dot command %s", s, end);
    }
}

/**
 * @brief Mark the parameters and Shell variables of a command line and
 *        find where its comment starts.
 * @param P The parser.
 * @param s The line.
 * @param len Its length.
 * @param line Its number.
 * @return The column where the comment starts (@p len for none).
 */
static size_t scan_line(Parser *P, const char *s, size_t len, long line)
{
    size_t i = 0, quote = 0;
    int in_quote = 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (!in_quote && c == ';') return i;
        if (c == '"') {
            in_quote = !in_quote;
            quote = i++;
            continue;
        }
        if (in_quote && c == '*' && i + 1 < len) {
            i += 2;
            continue;
        }
        if (P->has_key && c == (unsigned char)P->bra) {
            /* <name> or <name$default>: a parameter */
            size_t n = i + 1, close;
            while (n < len && name_char((unsigned char)s[n])) n++;
            if (n > i + 1 && n < len && (s[n] == P->ket || s[n] == P->dollar)) {
                close = n;
                while (close < len && s[close] != P->ket) close++;
                if (close < len) {
                    if (is_key(P, s + i + 1, n - i - 1))
                        add_token(P, line, (long)i + 1, (long)(close - i + 1), SCOUT_TOKEN_PARAMETER);
                    else
                        add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, (long)i + 1, (long)close + 2,
                                       "%s is not a parameter of the template", s + i + 1, n - i - 1);
                    i = close + 1;
                    continue;
                }
            }
        }
        if (c == '$' && i + 1 < len) {
            size_t n = i + 1;
            if (s[n] == '{') {
                while (n < len && s[n] != '}') n++;
                if (n < len && n > i + 2) {
                    add_token(P, line, (long)i + 1, (long)(n - i + 1), SCOUT_TOKEN_PARAMETER);
                    i = n + 1;
                    continue;
                }
            } else {
                while (n < len && name_char((unsigned char)s[n]) && s[n] != '.') n++;
                if (n > i + 1) {
                    add_token(P, line, (long)i + 1, (long)(n - i), SCOUT_TOKEN_PARAMETER);
                    i = n;
                    continue;
                }
            }
        }
        i++;
    }
    if (in_quote)
        add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, (long)quote + 1, (long)len + 1, "the string is not closed",
                       NULL, 0);
    return len;
}

/**
 * @brief Split a command line into words (blanks separate them, quotes
 *        keep them together).
 * @param s The line.
 * @param len Its length (up to the comment).
 * @param[out] words The words.
 * @return The number of words (at most MAX_WORDS).
 */
static int split_words(const char *s, size_t len, DosWord *words)
{
    size_t i = 0;
    int n = 0;
    while (n < MAX_WORDS) {
        i = skip_blanks(s, i, len);
        if (i >= len) break;
        words[n].start = i;
        words[n].quoted = s[i] == '"';
        if (words[n].quoted) {
            i++;
            while (i < len && s[i] != '"') i += (s[i] == '*' && i + 1 < len) ? 2 : 1;
            if (i < len) i++;
        } else {
            while (i < len && s[i] != ' ' && s[i] != '\t') i++;
        }
        words[n++].end = i;
    }
    return n;
}

/**
 * @brief Remember a SKIP to a label.
 * @param P The parser.
 * @param s The line.
 * @param w The label's word.
 * @param line The line's number.
 * @param back With BACK.
 */
static void add_skip(Parser *P, const char *s, const DosWord *w, long line, int back)
{
    DosSkip *k;
    if (!grow(P, (void **)&P->skips, P->skip_count, &P->skip_cap, sizeof(DosSkip))) return;
    k = &P->skips[P->skip_count];
    if ((k->name = dup_text(s + w->start, w->end - w->start)) == NULL) {
        P->res->out_of_memory = 1;
        return;
    }
    k->line = line;
    k->col = (long)w->start + 1;
    k->len = (long)(w->end - w->start);
    k->back = back;
    P->skip_count++;
}

/**
 * @brief Handle a command line.
 * @param P The parser.
 * @param s The line.
 * @param len Its length.
 * @param line Its number.
 */
static void command_line(Parser *P, const char *s, size_t len, long line)
{
    DosWord words[MAX_WORDS];
    size_t code = scan_line(P, s, len, line), cmd, cmd_len;
    int n = split_words(s, code, words), k;
    const char *name;
    if (n == 0 || words[0].quoted) return;
    /* the command's name, after its path */
    cmd = words[0].start;
    for (k = (int)words[0].start; k < (int)words[0].end; k++) {
        if (s[k] == ':' || s[k] == '/') cmd = (size_t)k + 1;
    }
    name = s + cmd;
    cmd_len = words[0].end - cmd;
    if (word_is(name, cmd_len, "if")) {
        DosIf *f;
        if (!grow(P, (void **)&P->ifs, P->if_count, &P->if_cap, sizeof(DosIf))) return;
        f = &P->ifs[P->if_count++];
        f->line = line;
        f->col = (long)cmd + 1;
        f->has_else = 0;
    } else if (word_is(name, cmd_len, "else")) {
        if (P->if_count == 0)
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, (long)cmd + 1, (long)words[0].end + 1, "ELSE without IF",
                           NULL, 0);
        else if (P->ifs[P->if_count - 1].has_else)
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, (long)cmd + 1, (long)words[0].end + 1,
                           "second ELSE of the same IF", NULL, 0);
        else
            P->ifs[P->if_count - 1].has_else = 1;
    } else if (word_is(name, cmd_len, "endif")) {
        if (P->if_count == 0)
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, (long)cmd + 1, (long)words[0].end + 1, "ENDIF without IF",
                           NULL, 0);
        else
            P->if_count--;
    } else if (word_is(name, cmd_len, "lab")) {
        if (n < 2 || words[1].quoted) {
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, (long)cmd + 1, (long)words[0].end + 1,
                           "LAB without a name", NULL, 0);
            return;
        }
        if (find_symbol(P, SCOUT_SYMBOL_LABEL, s + words[1].start, words[1].end - words[1].start) >= 0)
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, line, (long)words[1].start + 1, (long)words[1].end + 1,
                           "label %s is defined twice", s + words[1].start, words[1].end - words[1].start);
        else
            add_symbol(P, SCOUT_SYMBOL_LABEL, 0, s, len, line, words[1].start, words[1].end, NULL, 0);
        add_token(P, line, (long)words[1].start + 1, (long)(words[1].end - words[1].start), SCOUT_TOKEN_LABEL);
    } else if (word_is(name, cmd_len, "skip")) {
        int back = 0, label = -1;
        for (k = 1; k < n; k++) {
            if (!words[k].quoted && word_is(s + words[k].start, words[k].end - words[k].start, "back")) back = 1;
            else if (label < 0) label = k;
        }
        if (label > 0) {
            add_skip(P, s, &words[label], line, back);
            add_token(P, line, (long)words[label].start + 1, (long)(words[label].end - words[label].start),
                      SCOUT_TOKEN_LABEL);
        }
    } else if (word_is(name, cmd_len, "set") || word_is(name, cmd_len, "setenv")) {
        int env = cmd_len == 6;
        if (n < 2 || words[1].quoted) return;
        if (find_symbol(P, SCOUT_SYMBOL_VARIABLE, s + words[1].start, words[1].end - words[1].start) < 0)
            add_symbol(P, SCOUT_SYMBOL_VARIABLE, env, s, len, line, words[1].start, words[1].end,
                       env ? "environment variable" : "local variable", env ? 20 : 14);
    } else if (word_is(name, cmd_len, "alias")) {
        if (n < 2 || words[1].quoted) return;
        if (find_symbol(P, SCOUT_SYMBOL_FUNCTION, s + words[1].start, words[1].end - words[1].start) < 0)
            add_symbol(P, SCOUT_SYMBOL_FUNCTION, 0, s, len, line, words[1].start, words[1].end,
                       s + words[0].start, code - words[0].start);
    }
}

/**
 * @brief Check the SKIPs against the labels.
 * @param P The parser.
 */
static void check_skips(Parser *P)
{
    long i, j;
    for (i = 0; i < P->skip_count; i++) {
        const DosSkip *k = &P->skips[i];
        int before = 0, after = 0;
        for (j = 0; j < P->res->symbol_count; j++) {
            const DosSymbol *s = &P->res->symbols[j];
            if (s->kind != SCOUT_SYMBOL_LABEL || !name_equal(s->name, k->name, strlen(k->name))) continue;
            if (s->range.start_line < k->line) before = 1;
            else if (s->range.start_line > k->line) after = 1;
        }
        if (!before && !after)
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, k->line, k->col, k->col + k->len, "no label %s",
                           k->name, strlen(k->name));
        else if (!k->back && !after)
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, k->line, k->col, k->col + k->len,
                           "label %s is above: SKIP needs BACK", k->name, strlen(k->name));
        else if (k->back && !before)
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, k->line, k->col, k->col + k->len,
                           "label %s is below: SKIP BACK searches upwards", k->name, strlen(k->name));
    }
}

/**
 * @brief qsort() order of tokens: by line, then column.
 * @param a A token (four longs).
 * @param b Another token.
 * @return Their order.
 */
static int token_order(const void *a, const void *b)
{
    const long *x = (const long *)a, *y = (const long *)b;
    if (x[0] != y[0]) return x[0] < y[0] ? -1 : 1;
    return x[1] < y[1] ? -1 : x[1] > y[1];
}

/**
 * @brief qsort() order of diagnostics: by line, then column.
 * @param a A diagnostic.
 * @param b Another diagnostic.
 * @return Their order.
 */
static int diagnostic_order(const void *a, const void *b)
{
    const DosRange *x = &((const DosDiagnostic *)a)->range, *y = &((const DosDiagnostic *)b)->range;
    if (x->start_line != y->start_line) return x->start_line < y->start_line ? -1 : 1;
    return x->start_col < y->start_col ? -1 : x->start_col > y->start_col;
}

/**
 * @brief Sort the tokens and drop those that overlap the one before.
 * @param r The result.
 */
static void finish_tokens(DosParseResult *r)
{
    long i, n = 0;
    if (r->token_count == 0) return;
    qsort(r->tokens, (size_t)r->token_count, 4 * sizeof(long), token_order);
    for (i = 0; i < r->token_count; i++) {
        long *t = &r->tokens[i * 4];
        if (n > 0) {
            const long *p = &r->tokens[(n - 1) * 4];
            if (p[0] == t[0] && t[1] < p[1] + p[2]) continue;
        }
        if (n != i) memmove(&r->tokens[n * 4], t, 4 * sizeof(long));
        n++;
    }
    r->token_count = n;
}

DosParseResult *dos_parser_analyze(const char *source, size_t length)
{
    Parser P;
    DosParseResult *res = (DosParseResult *)calloc(1, sizeof(DosParseResult));
    size_t pos = 0;
    long line = 0, i;
    if (!res) return NULL;
    memset(&P, 0, sizeof(P));
    P.res = res;
    P.bra = '<';
    P.ket = '>';
    P.dot = '.';
    P.dollar = '$';
    while (pos < length && !res->out_of_memory) {
        size_t end = pos, len;
        while (end < length && source[end] != '\n') end++;
        len = end - pos;
        if (len > 0 && source[pos + len - 1] == '\r') len--;
        line++;
        if ((line & 255) == 0 && dos_parser_should_stop && dos_parser_should_stop()) {
            res->stopped = 1;
            break;
        }
        if (len > 0 && source[pos] == P.dot) dot_command(&P, source + pos, len, line);
        else command_line(&P, source + pos, len, line);
        pos = end + 1;
    }
    for (i = 0; i < P.if_count; i++)
        add_diagnostic(&P, SCOUT_SEVERITY_ERROR, P.ifs[i].line, P.ifs[i].col, P.ifs[i].col + 2,
                       "IF without ENDIF", NULL, 0);
    check_skips(&P);
    if (res->diagnostic_count > 1)
        qsort(res->diagnostics, (size_t)res->diagnostic_count, sizeof(DosDiagnostic), diagnostic_order);
    finish_tokens(res);
    for (i = 0; i < P.key_count; i++) free(P.keys[i]);
    for (i = 0; i < P.skip_count; i++) free(P.skips[i].name);
    free(P.keys);
    free(P.skips);
    free(P.ifs);
    return res;
}

void dos_parse_result_free(DosParseResult *res)
{
    long i;
    if (!res) return;
    for (i = 0; i < res->symbol_count; i++) {
        free(res->symbols[i].name);
        free(res->symbols[i].detail);
    }
    for (i = 0; i < res->diagnostic_count; i++) free(res->diagnostics[i].message);
    free(res->symbols);
    free(res->diagnostics);
    free(res->tokens);
    free(res);
}
