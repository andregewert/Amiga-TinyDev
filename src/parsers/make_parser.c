/**
 * @file make_parser.c
 * @brief The analysis of the Makefile parser of Scout: one pass over the
 *        logical lines of a Makefile.
 *
 * A line ending in a backslash goes on in the next one; the pair becomes
 * a space, and every character of the joined line keeps its place in the
 * file for the positions of diagnostics and tokens. A line starting with
 * a tab after a rule is a recipe line: only its variable references are
 * looked at. Other lines are, in this order: blank or a comment, a
 * directive (include, conditionals, define, export, ...), an assignment
 * (the first '=' outside references comes before the first ':', or ':'
 * is part of ":=" or "::="), a rule (targets, ':' or "::",
 * prerequisites, maybe "; recipe"), or a line make would reject. Inside
 * define ... endef only the endef counts. '#' outside a recipe starts a
 * comment unless a backslash comes before it.
 */
#include "make_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int (*make_parser_should_stop)(void) = NULL;

const char *const make_parser_keywords[] = {
    "include", "sinclude", "ifdef", "ifndef", "ifeq", "ifneq", "else",
    "endif", "define", "endef", "export", "unexport", "override",
    "private", "vpath", "undefine",
    NULL
};

const char *const make_parser_functions[] = {
    "subst", "patsubst", "strip", "findstring", "filter", "filter-out",
    "sort", "word", "wordlist", "words", "firstword", "lastword", "dir",
    "notdir", "suffix", "basename", "addsuffix", "addprefix", "join",
    "wildcard", "realpath", "abspath", "error", "warning", "info",
    "shell", "origin", "flavor", "foreach", "if", "or", "and", "call",
    "eval", "file", "value", "let", "intcmp", "guile",
    NULL
};

/** Most open conditionals followed. */
#define COND_MAX    32
/** Longest text of MakeSymbol::detail (longer ones end with "..."). */
#define DETAIL_MAX  120
/** Most targets of one rule that are looked at. */
#define TARGETS_MAX 32

/** An open conditional (ifdef, ifeq, ...). */
typedef struct MakeCond {
    long line;          /**< its line */
    long col;           /**< column of its directive */
    long len;           /**< length of the directive */
    int has_else;       /**< an else without a condition was seen */
} MakeCond;

/** One logical line: the joined text and the place of each character. */
typedef struct Line {
    char *text;         /**< the text (continuations joined) */
    long *lines;        /**< per character: its line in the file */
    long *cols;         /**< per character: its column (1-based) */
    size_t len;         /**< its length */
    size_t cap;         /**< room in the arrays */
} Line;

/** State of one analysis. */
typedef struct Parser {
    MakeParseResult *res;   /**< the result being built */
    long symbol_cap;        /**< room for symbols (and has_recipe) */
    long diagnostic_cap;    /**< room for diagnostics */
    long reference_cap;     /**< room for references */
    long token_cap;         /**< room for token values */
    unsigned char *has_recipe;  /**< per symbol: 0 no recipe, 1 one outside conditionals, 2 one in a "::" rule or a conditional */
    MakeCond conds[COND_MAX];   /**< the open conditionals */
    int cond_count;         /**< number of them (beyond COND_MAX: counted only) */
    int in_define;          /**< inside define ... endef */
    long define_line;       /**< line of that define */
    long define_col;        /**< its column */
    int seen_rule;          /**< a rule was seen */
    int in_rule;            /**< tab lines are recipe lines of the last rule */
    long targets[TARGETS_MAX];  /**< symbols of the last rule's targets (-1: special or computed) */
    int target_count;       /**< number of them */
    int double_colon;       /**< the last rule has "::" */
    int recipe_seen;        /**< the last rule got a recipe */
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
 * @brief Compare a piece of text with a word.
 * @param s The text.
 * @param len Its length.
 * @param word The word (terminated).
 * @return Non-zero if they are equal.
 */
static int word_is(const char *s, size_t len, const char *word)
{
    return strlen(word) == len && memcmp(s, word, len) == 0;
}

/**
 * @brief Tell whether a piece of text is one of a list of words.
 * @param s The text.
 * @param len Its length.
 * @param list The words, ended by NULL.
 * @return Non-zero if it is.
 */
static int word_in(const char *s, size_t len, const char *const *list)
{
    size_t i;
    for (i = 0; list[i]; i++) {
        if (word_is(s, len, list[i])) return 1;
    }
    return 0;
}

/**
 * @brief Add a diagnostic between two characters of a line.
 * @param P The parser.
 * @param L The line.
 * @param severity SCOUT_SEVERITY_ value.
 * @param from Index of the first character.
 * @param to Index after the last one (at most the line's length).
 * @param format The message, with at most one "%s".
 * @param name What "%s" stands for (may be NULL).
 * @param name_len Length of @p name.
 */
static void add_diagnostic(Parser *P, const Line *L, int severity, size_t from, size_t to, const char *format,
                           const char *name, size_t name_len)
{
    MakeParseResult *r = P->res;
    MakeDiagnostic *d;
    char text[160], quoted[64];
    if (!grow(P, (void **)&r->diagnostics, r->diagnostic_count, &P->diagnostic_cap, sizeof(MakeDiagnostic)))
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
    if (L->len == 0) {
        d->range.start_line = d->range.end_line = L->lines ? L->lines[0] : 1;
        d->range.start_col = 1;
        d->range.end_col = 2;
    } else {
        if (from >= L->len) from = L->len - 1;
        if (to <= from) to = from + 1;
        if (to > L->len) to = L->len;
        d->range.start_line = L->lines[from];
        d->range.start_col = L->cols[from];
        d->range.end_line = L->lines[to - 1];
        d->range.end_col = L->cols[to - 1] + 1;
    }
    r->diagnostic_count++;
}

/**
 * @brief Add a highlighting token for characters of a line; one that
 *        runs over a continuation is left out.
 * @param P The parser.
 * @param L The line.
 * @param from Index of the first character.
 * @param to Index after the last one.
 * @param kind SCOUT_TOKEN_ value.
 */
static void add_token(Parser *P, const Line *L, size_t from, size_t to, long kind)
{
    MakeParseResult *r = P->res;
    long *t;
    if (to <= from || to > L->len || L->lines[from] != L->lines[to - 1]) return;
    if (!grow(P, (void **)&r->tokens, r->token_count * 4 + 3, &P->token_cap, sizeof(long))) return;
    t = &r->tokens[r->token_count * 4];
    t[0] = L->lines[from];
    t[1] = L->cols[from];
    t[2] = (long)(to - from);
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
        const MakeSymbol *s = &P->res->symbols[i];
        if (s->kind == kind && strlen(s->name) == len && memcmp(s->name, name, len) == 0) return i;
    }
    return -1;
}

/**
 * @brief Add a symbol; its detail is the line, cut at DETAIL_MAX.
 * @param P The parser.
 * @param L The line.
 * @param kind SCOUT_SYMBOL_ value.
 * @param from Index of the name's first character.
 * @param to Index after it.
 * @return Its index, or -1 if out of memory.
 */
static long add_symbol(Parser *P, const Line *L, int kind, size_t from, size_t to)
{
    MakeParseResult *r = P->res;
    MakeSymbol *s;
    size_t start = 0, end = L->len, n;
    long cap = P->symbol_cap;
    char detail[DETAIL_MAX + 4];
    if (!grow(P, (void **)&r->symbols, r->symbol_count, &P->symbol_cap, sizeof(MakeSymbol))) return -1;
    if (P->symbol_cap != cap) {
        unsigned char *bigger = (unsigned char *)realloc(P->has_recipe, (size_t)P->symbol_cap);
        if (!bigger) {
            r->out_of_memory = 1;
            return -1;
        }
        P->has_recipe = bigger;
    }
    while (start < end && blank(L->text[start])) start++;
    while (end > start && blank(L->text[end - 1])) end--;
    n = end - start;
    if (n > DETAIL_MAX) {
        memcpy(detail, L->text + start, DETAIL_MAX);
        memcpy(detail + DETAIL_MAX, "...", 4);
    } else {
        memcpy(detail, L->text + start, n);
        detail[n] = '\0';
    }
    s = &r->symbols[r->symbol_count];
    memset(s, 0, sizeof(*s));
    s->name = dup_text(L->text + from, to - from);
    s->detail = dup_text(detail, strlen(detail));
    if (!s->name || !s->detail) {
        free(s->name);
        free(s->detail);
        r->out_of_memory = 1;
        return -1;
    }
    s->kind = kind;
    s->range.start_line = L->lines[0];
    s->range.start_col = L->cols[0];
    s->range.end_line = L->lines[L->len - 1];
    s->range.end_col = L->cols[L->len - 1] + 1;
    s->selection.start_line = L->lines[from];
    s->selection.start_col = L->cols[from];
    s->selection.end_line = L->lines[to - 1];
    s->selection.end_col = L->cols[to - 1] + 1;
    P->has_recipe[r->symbol_count] = 0;
    return r->symbol_count++;
}

/**
 * @brief Add an included file.
 * @param P The parser.
 * @param L The line.
 * @param from Index of the name's first character.
 * @param to Index after it.
 */
static void add_reference(Parser *P, const Line *L, size_t from, size_t to)
{
    MakeParseResult *r = P->res;
    MakeReference *ref;
    if (!grow(P, (void **)&r->references, r->reference_count, &P->reference_cap, sizeof(MakeReference))) return;
    ref = &r->references[r->reference_count];
    if ((ref->target = dup_text(L->text + from, to - from)) == NULL) {
        r->out_of_memory = 1;
        return;
    }
    ref->range.start_line = L->lines[from];
    ref->range.start_col = L->cols[from];
    ref->range.end_line = L->lines[to - 1];
    ref->range.end_col = L->cols[to - 1] + 1;
    r->reference_count++;
}

/**
 * @brief Find the bracket that closes a variable reference.
 * @param s The text.
 * @param i Index of the opening bracket.
 * @param end The end of the text.
 * @return Index of the closing bracket, or @p end if there is none.
 */
static size_t close_of(const char *s, size_t i, size_t end)
{
    char open = s[i], close = open == '(' ? ')' : '}';
    int depth = 0;
    for (; i < end; i++) {
        if (s[i] == open) depth++;
        else if (s[i] == close && --depth == 0) return i;
    }
    return end;
}

/**
 * @brief Tell whether a name is an automatic variable: @, <, ^, ?, *,
 *        %, + or |, maybe followed by D or F.
 * @param s The name.
 * @param len Its length.
 * @return Non-zero if it is.
 */
static int automatic(const char *s, size_t len)
{
    if (len < 1 || len > 2 || !strchr("@<^?*%+|", s[0]) || s[0] == '\0') return 0;
    return len == 1 || s[1] == 'D' || s[1] == 'F';
}

/**
 * @brief Mark the variable references in part of a line and check them:
 *        an unterminated reference, an unknown function.
 * @param P The parser.
 * @param L The line.
 * @param i Index of the first character.
 * @param end Index after the last one.
 */
static void scan_refs(Parser *P, const Line *L, size_t i, size_t end)
{
    const char *s = L->text;
    while (i < end && !P->res->out_of_memory) {
        size_t close, name, n;
        if (s[i] != '$' || i + 1 >= end) {
            i++;
            continue;
        }
        if (s[i + 1] == '$') {
            i += 2;
            continue;
        }
        if (s[i + 1] != '(' && s[i + 1] != '{') {
            /* $@, $X */
            if (strchr("@<^?*%+|", s[i + 1])) add_token(P, L, i, i + 2, SCOUT_TOKEN_PARAMETER);
            else if (!blank(s[i + 1])) add_token(P, L, i, i + 2, SCOUT_TOKEN_VARIABLE);
            i += 2;
            continue;
        }
        close = close_of(s, i + 1, end);
        if (close >= end) {
            add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, i, end, "Unterminated variable reference", NULL, 0);
            return;
        }
        name = i + 2;
        n = name;
        while (n < close && !blank(s[n]) && s[n] != ':' && s[n] != '=' && s[n] != ',' && s[n] != '$') n++;
        if (n < close && blank(s[n]) && n > name) {
            /* $(function arguments) */
            size_t k;
            int lower = 1;
            for (k = name; k < n; k++) {
                if (!((s[k] >= 'a' && s[k] <= 'z') || s[k] == '-')) lower = 0;
            }
            if (lower && !word_in(s + name, n - name, make_parser_functions))
                add_diagnostic(P, L, SCOUT_SEVERITY_WARNING, name, n, "Unknown function %s", s + name, n - name);
            scan_refs(P, L, n, close);
        } else if (memchr(s + name, '$', close - name)) {
            /* a computed name: $($(X)_FLAGS) */
            scan_refs(P, L, name, close);
        } else {
            add_token(P, L, i, close + 1, automatic(s + name, n - name) ? SCOUT_TOKEN_PARAMETER : SCOUT_TOKEN_VARIABLE);
        }
        i = close + 1;
    }
}

/**
 * @brief Find where a comment starts: the first '#' without a backslash
 *        before it.
 * @param L The line.
 * @return Its index, or the line's length.
 */
static size_t comment_start(const Line *L)
{
    size_t i;
    for (i = 0; i < L->len; i++) {
        if (L->text[i] == '#' && (i == 0 || L->text[i - 1] != '\\')) return i;
    }
    return L->len;
}

/**
 * @brief Give the targets of the last rule their recipe: a target that
 *        had one already (and is no "::" rule) is a warning.
 * @param P The parser.
 * @param L The line of the recipe.
 */
static void got_recipe(Parser *P, const Line *L)
{
    int k;
    if (P->recipe_seen) return;
    P->recipe_seen = 1;
    for (k = 0; k < P->target_count; k++) {
        long t = P->targets[k];
        if (t < 0) continue;
        /* pattern rules with other prerequisites are other rules; the
           branches of a conditional may each give one */
        if (P->has_recipe[t] == 1 && !P->double_colon && P->cond_count == 0 &&
            !strchr(P->res->symbols[t].name, '%')) {
            const MakeSymbol *s = &P->res->symbols[t];
            size_t col = 0;
            /* the warning goes to the recipe line */
            while (col < L->len && blank(L->text[col])) col++;
            add_diagnostic(P, L, SCOUT_SEVERITY_WARNING, col, L->len, "Target %s has a recipe already",
                           s->name, strlen(s->name));
        }
        if (P->has_recipe[t] != 1) P->has_recipe[t] = P->double_colon || P->cond_count > 0 ? 2 : 1;
    }
}

/**
 * @brief Tell whether a target is special (.PHONY, .SUFFIXES, ...): a dot
 *        and upper case letters or '_'.
 * @param s The target.
 * @param len Its length.
 * @return Non-zero if it is.
 */
static int special_target(const char *s, size_t len)
{
    size_t i;
    if (len < 2 || s[0] != '.') return 0;
    for (i = 1; i < len; i++) {
        if (!((s[i] >= 'A' && s[i] <= 'Z') || s[i] == '_')) return 0;
    }
    return 1;
}

/**
 * @brief Handle a rule: its targets (symbols, tokens), prerequisites and
 *        a recipe after ';'.
 * @param P The parser.
 * @param L The line.
 * @param start Index of the first target.
 * @param colon Index of the ':'.
 * @param end The end of the line (before a comment).
 */
static void rule(Parser *P, const Line *L, size_t start, size_t colon, size_t end)
{
    const char *s = L->text;
    size_t i = start, after = colon + 1, semi;
    P->double_colon = after < end && s[after] == ':';
    if (P->double_colon) after++;
    P->seen_rule = 1;
    P->in_rule = 1;
    P->recipe_seen = 0;
    P->target_count = 0;
    while (i < colon) {
        size_t w;
        while (i < colon && blank(s[i])) i++;
        w = i;
        while (i < colon && !blank(s[i])) {
            if (s[i] == '$' && i + 1 < colon && (s[i + 1] == '(' || s[i + 1] == '{')) i = close_of(s, i + 1, colon);
            if (i < colon) i++;
        }
        if (i > w && P->target_count < TARGETS_MAX) {
            long sym = -1;
            if (!special_target(s + w, i - w) && !memchr(s + w, '$', i - w)) {
                sym = find_symbol(P, SCOUT_SYMBOL_FUNCTION, s + w, i - w);
                if (sym < 0) sym = add_symbol(P, L, SCOUT_SYMBOL_FUNCTION, w, i);
                add_token(P, L, w, i, SCOUT_TOKEN_FUNCTION);
            }
            P->targets[P->target_count++] = sym;
        }
    }
    scan_refs(P, L, start, colon);
    /* "target: prerequisites ; recipe" */
    for (semi = after; semi < end && s[semi] != ';'; semi++) {
        if (s[semi] == '$' && semi + 1 < end && (s[semi + 1] == '(' || s[semi + 1] == '{'))
            semi = close_of(s, semi + 1, end);
    }
    scan_refs(P, L, after, semi < end ? semi : end);
    if (semi < end) {
        size_t k = semi + 1;
        while (k < end && blank(s[k])) k++;
        if (k < end) got_recipe(P, L);
        scan_refs(P, L, semi + 1, L->len);
    }
}

/**
 * @brief Handle an assignment: the variable's symbol and token, the
 *        references of its value.
 * @param P The parser.
 * @param L The line.
 * @param start Index of the name (after export or override).
 * @param op Index of the operator's first character.
 * @param value Index after the operator.
 * @param end The end of the line (before a comment).
 */
static void assignment(Parser *P, const Line *L, size_t start, size_t op, size_t value, size_t end)
{
    const char *s = L->text;
    size_t e = op;
    while (e > start && blank(s[e - 1])) e--;
    P->in_rule = 0;
    if (e > start && !memchr(s + start, '$', e - start) && !memchr(s + start, ' ', e - start) &&
        !memchr(s + start, '\t', e - start)) {
        if (find_symbol(P, SCOUT_SYMBOL_VARIABLE, s + start, e - start) < 0 &&
            find_symbol(P, SCOUT_SYMBOL_MACRO, s + start, e - start) < 0)
            add_symbol(P, L, SCOUT_SYMBOL_VARIABLE, start, e);
        add_token(P, L, start, e, SCOUT_TOKEN_VARIABLE);
    } else {
        scan_refs(P, L, start, e);
    }
    scan_refs(P, L, value, end);
}

/**
 * @brief Find the operator of an assignment or the ':' of a rule outside
 *        variable references.
 * @param s The text.
 * @param i Where to start.
 * @param end The end.
 * @param[out] op Index of the operator's first character ('?', '+', '!',
 *                ':' or '='), or of the rule's ':'.
 * @param[out] value Index after an assignment's operator.
 * @return 1 for an assignment, 2 for a rule, 0 for neither.
 */
static int find_separator(const char *s, size_t i, size_t end, size_t *op, size_t *value)
{
    for (; i < end; i++) {
        char c = s[i];
        if (c == '$' && i + 1 < end && (s[i + 1] == '(' || s[i + 1] == '{')) {
            i = close_of(s, i + 1, end);
            continue;
        }
        if (c == '=') {
            *op = i;
            *value = i + 1;
            if (i > 0 && (s[i - 1] == '?' || s[i - 1] == '+' || s[i - 1] == '!')) *op = i - 1;
            return 1;
        }
        if (c == ':') {
            if (i + 1 < end && s[i + 1] == '=') {
                *op = i;
                *value = i + 2;
                return 1;
            }
            if (i + 2 < end && s[i + 1] == ':' && s[i + 2] == '=') {
                *op = i;
                *value = i + 3;
                return 1;
            }
            *op = i;
            return 2;
        }
    }
    return 0;
}

/**
 * @brief Handle a conditional directive or else/endif.
 * @param P The parser.
 * @param L The line.
 * @param w Index of the directive.
 * @param n Its length.
 * @param rest Index after it.
 * @param end The end of the line (before a comment).
 */
static void conditional(Parser *P, const Line *L, size_t w, size_t n, size_t rest, size_t end)
{
    const char *s = L->text;
    if (word_is(s + w, n, "endif")) {
        if (P->cond_count == 0) add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, w, w + n, "endif without if", NULL, 0);
        else P->cond_count--;
        return;
    }
    if (word_is(s + w, n, "else")) {
        size_t k = rest, m;
        while (k < end && blank(s[k])) k++;
        m = k;
        while (m < end && !blank(s[m]) && s[m] != '(') m++;
        if (P->cond_count == 0) {
            add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, w, w + n, "else without if", NULL, 0);
        } else if (P->cond_count <= COND_MAX) {
            MakeCond *c = &P->conds[P->cond_count - 1];
            if (c->has_else) add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, w, w + n, "else after the last else", NULL, 0);
            /* "else ifeq ...": another branch */
            if (!(m > k && (word_is(s + k, m - k, "ifeq") || word_is(s + k, m - k, "ifneq") ||
                            word_is(s + k, m - k, "ifdef") || word_is(s + k, m - k, "ifndef"))))
                c->has_else = 1;
        }
        scan_refs(P, L, rest, end);
        return;
    }
    if (P->cond_count < COND_MAX) {
        MakeCond *c = &P->conds[P->cond_count];
        c->line = L->lines[w];
        c->col = L->cols[w];
        c->len = (long)n;
        c->has_else = 0;
    }
    P->cond_count++;
    scan_refs(P, L, rest, end);
}

/**
 * @brief Tell whether part of a line holds only variable references
 *        ($(...), ${...}) and blanks, such as "$(eval $(call f,x))".
 * @param s The text.
 * @param i Index of the first character.
 * @param end Index after the last one.
 * @return Non-zero if so.
 */
static int only_refs(const char *s, size_t i, size_t end)
{
    while (i < end) {
        if (blank(s[i])) {
            i++;
        } else if (s[i] == '$' && i + 1 < end && (s[i + 1] == '(' || s[i + 1] == '{')) {
            i = close_of(s, i + 1, end);
            if (i >= end) return 1;     /* reported as unterminated */
            i++;
        } else {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Tell whether a line is a directive of BSD make (".include",
 *        ".if", ". for", ...), which is left alone.
 * @param s The text.
 * @param i Index of the line's first non-blank character.
 * @param end The end of the line.
 * @return Non-zero if it is.
 */
static int bsd_directive(const char *s, size_t i, size_t end)
{
    static const char *const words[] = {
        "include", "sinclude", "if", "ifdef", "ifndef", "ifmake", "ifnmake", "elif", "elifdef", "elifndef",
        "else", "endif", "for", "endfor", "undef", "error", "warning", "info", "export", "unexport",
        NULL
    };
    size_t w;
    if (s[i] != '.') return 0;
    i++;
    while (i < end && blank(s[i])) i++;
    w = i;
    while (i < end && s[i] >= 'a' && s[i] <= 'z') i++;
    return (i == end || blank(s[i]) || s[i] == '<' || s[i] == '(' || s[i] == '"') && word_in(s + w, i - w, words);
}

/**
 * @brief Handle a logical line outside define blocks.
 * @param P The parser.
 * @param L The line.
 */
static void logical_line(Parser *P, const Line *L)
{
    const char *s = L->text;
    size_t end, i = 0, w, n, op = 0, value = 0;
    int tab = L->len > 0 && s[0] == '\t', spaces, kind, exported = 0;

    if (tab && P->in_rule) {
        size_t k = 1;
        while (k < L->len && blank(s[k])) k++;
        if (k < L->len) got_recipe(P, L);
        scan_refs(P, L, 1, L->len);
        return;
    }
    end = comment_start(L);
    while (i < end && blank(s[i])) i++;
    spaces = i > 0 && !tab;
    while (end > i && blank(s[end - 1])) end--;
    if (i >= end) return;   /* blank or a comment: a rule goes on */

    /* the first word: a directive? */
    w = i;
    while (i < end && !blank(s[i]) && s[i] != '(' && s[i] != '=' && s[i] != ':' && s[i] != '?' && s[i] != '+' &&
           s[i] != '!')
        i++;
    n = i - w;
    if (bsd_directive(s, w, end)) return;
    /* export, override and private may stand before an assignment or define */
    while (word_is(s + w, n, "export") || word_is(s + w, n, "override") || word_is(s + w, n, "private")) {
        exported |= word_is(s + w, n, "export");
        while (i < end && blank(s[i])) i++;
        if (i >= end) {
            P->in_rule = 0;
            return;     /* "export" alone */
        }
        w = i;
        while (i < end && !blank(s[i]) && s[i] != '=' && s[i] != ':' && s[i] != '?' && s[i] != '+' && s[i] != '!')
            i++;
        n = i - w;
    }

    if (word_is(s + w, n, "ifdef") || word_is(s + w, n, "ifndef") || word_is(s + w, n, "ifeq") ||
        word_is(s + w, n, "ifneq") || word_is(s + w, n, "else") || word_is(s + w, n, "endif")) {
        conditional(P, L, w, n, i, end);
        return;
    }
    if (word_is(s + w, n, "define")) {
        size_t k = i, m;
        while (k < end && blank(s[k])) k++;
        m = k;
        while (m < end && !blank(s[m]) && s[m] != '=' && s[m] != ':' && s[m] != '?' && s[m] != '+' && s[m] != '!')
            m++;
        if (m == k) {
            add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, w, w + n, "define without a name", NULL, 0);
        } else {
            if (find_symbol(P, SCOUT_SYMBOL_MACRO, s + k, m - k) < 0 &&
                find_symbol(P, SCOUT_SYMBOL_VARIABLE, s + k, m - k) < 0)
                add_symbol(P, L, SCOUT_SYMBOL_MACRO, k, m);
            add_token(P, L, k, m, SCOUT_TOKEN_VARIABLE);
        }
        P->in_define = 1;
        P->define_line = L->lines[w];
        P->define_col = L->cols[w];
        P->in_rule = 0;
        return;
    }
    if (word_is(s + w, n, "endef")) {
        add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, w, w + n, "endef without define", NULL, 0);
        return;
    }
    if (word_is(s + w, n, "include") || word_is(s + w, n, "-include") || word_is(s + w, n, "sinclude")) {
        size_t k = i;
        while (k < end) {
            size_t m;
            while (k < end && blank(s[k])) k++;
            m = k;
            while (m < end && !blank(s[m])) {
                if (s[m] == '$' && m + 1 < end && (s[m + 1] == '(' || s[m + 1] == '{')) m = close_of(s, m + 1, end);
                if (m < end) m++;
            }
            if (m > k) add_reference(P, L, k, m);
            k = m;
        }
        scan_refs(P, L, i, end);
        P->in_rule = 0;
        return;
    }
    if (word_is(s + w, n, "vpath") || word_is(s + w, n, "unexport") || word_is(s + w, n, "undefine")) {
        scan_refs(P, L, i, end);
        P->in_rule = 0;
        return;
    }
    kind = find_separator(s, w, end, &op, &value);
    if (kind == 1) {
        assignment(P, L, w, op, value, end);
        return;
    }
    if (kind == 2) {
        size_t v_op, v_value;
        /* "target: VAR = value", a variable for a target */
        if (find_separator(s, op + 1, end, &v_op, &v_value) == 1 && memchr(s + op + 1, '=', end - op - 1)) {
            size_t t = op + 1;
            while (t < end && (blank(s[t]) || s[t] == ':')) t++;
            if (t < v_op && !memchr(s + t, ':', v_op - t)) {
                scan_refs(P, L, w, op);
                assignment(P, L, t, v_op, v_value, end);
                P->in_rule = 0;
                return;
            }
        }
        rule(P, L, w, op, end);
        return;
    }
    /* "export NAME ...", a line of references only ($(eval ...)); not
       after a rule with spaces in front, where a tab was meant */
    if (exported || (!(spaces && P->in_rule) && only_refs(s, w, end))) {
        scan_refs(P, L, w, end);
        return;
    }
    if (spaces && P->in_rule)
        add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, 0, end, "Recipe line indented with spaces instead of a tab", NULL, 0);
    else if (tab && !P->seen_rule)
        add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, 1, end, "Recipe before the first target", NULL, 0);
    else
        add_diagnostic(P, L, SCOUT_SEVERITY_ERROR, w, end, "Missing separator: neither a rule nor an assignment",
                       NULL, 0);
    P->in_rule = 0;
}

/**
 * @brief Append a character to a logical line.
 * @param P The parser.
 * @param L The line.
 * @param c The character.
 * @param line Its line in the file.
 * @param col Its column.
 * @return Non-zero on success.
 */
static int put_char(Parser *P, Line *L, char c, long line, long col)
{
    if (L->len >= L->cap) {
        size_t n = L->cap ? L->cap * 2 : 256;
        char *text = (char *)realloc(L->text, n);
        long *lines, *cols;
        if (text) L->text = text;
        lines = text ? (long *)realloc(L->lines, n * sizeof(long)) : NULL;
        if (lines) L->lines = lines;
        cols = lines ? (long *)realloc(L->cols, n * sizeof(long)) : NULL;
        if (!cols) {
            P->res->out_of_memory = 1;
            return 0;
        }
        L->cols = cols;
        L->cap = n;
    }
    L->text[L->len] = c;
    L->lines[L->len] = line;
    L->cols[L->len] = col;
    L->len++;
    return 1;
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
    const MakeRange *x = &((const MakeDiagnostic *)a)->range, *y = &((const MakeDiagnostic *)b)->range;
    if (x->start_line != y->start_line) return x->start_line < y->start_line ? -1 : 1;
    return x->start_col < y->start_col ? -1 : x->start_col > y->start_col;
}

/**
 * @brief Sort the tokens and drop those that overlap the one before.
 * @param r The result.
 */
static void finish_tokens(MakeParseResult *r)
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

/**
 * @brief Tell whether a line inside a define block is its endef.
 * @param L The line.
 * @return Non-zero if it is.
 */
static int is_endef(const Line *L)
{
    size_t i = 0, w;
    while (i < L->len && blank(L->text[i])) i++;
    w = i;
    while (i < L->len && !blank(L->text[i]) && L->text[i] != '#') i++;
    return word_is(L->text + w, i - w, "endef");
}

MakeParseResult *make_parser_analyze(const char *source, size_t length)
{
    Parser P;
    Line L;
    MakeParseResult *res = (MakeParseResult *)calloc(1, sizeof(MakeParseResult));
    size_t pos = 0;
    long line = 1, i;
    if (!res) return NULL;
    memset(&P, 0, sizeof(P));
    memset(&L, 0, sizeof(L));
    P.res = res;
    while (pos < length && !res->out_of_memory) {
        long first = line, col = 1;
        L.len = 0;
        /* one logical line: physical lines joined at a final backslash */
        for (;;) {
            size_t end = pos, len;
            int cont;
            while (end < length && source[end] != '\n' && source[end] != '\r') end++;
            len = end - pos;
            cont = len > 0 && source[end - 1] == '\\';
            for (col = 1; pos < end - (cont ? 1 : 0); pos++, col++) {
                if (!put_char(&P, &L, source[pos], line, col)) break;
            }
            if (cont && !put_char(&P, &L, ' ', line, col)) break;
            pos = end;
            if (pos < length && source[pos] == '\r') {
                pos++;
                if (pos < length && source[pos] == '\n') pos++;
            } else if (pos < length) {
                pos++;
            }
            if (!cont || pos >= length) break;
            line++;
        }
        if (res->out_of_memory) break;
        if (L.len == 0 && !put_char(&P, &L, ' ', first, 1)) break;
        if ((first & 255) == 0 && make_parser_should_stop && make_parser_should_stop()) {
            res->stopped = 1;
            break;
        }
        if (P.in_define) {
            if (is_endef(&L)) P.in_define = 0;
        } else {
            logical_line(&P, &L);
        }
        line++;
    }
    if (P.in_define) {
        L.len = 0;
        if (put_char(&P, &L, ' ', P.define_line, P.define_col)) {
            P.in_define = 0;
            add_diagnostic(&P, &L, SCOUT_SEVERITY_ERROR, 0, 1, "define without endef", NULL, 0);
            res->diagnostics[res->diagnostic_count - 1].range.end_col = P.define_col + 6;
        }
    }
    for (i = 0; i < P.cond_count && i < COND_MAX; i++) {
        L.len = 0;
        if (!put_char(&P, &L, ' ', P.conds[i].line, P.conds[i].col)) break;
        add_diagnostic(&P, &L, SCOUT_SEVERITY_ERROR, 0, 1, "Conditional without endif", NULL, 0);
        if (!res->out_of_memory) res->diagnostics[res->diagnostic_count - 1].range.end_col = P.conds[i].col + P.conds[i].len;
    }
    if (res->diagnostic_count > 1)
        qsort(res->diagnostics, (size_t)res->diagnostic_count, sizeof(MakeDiagnostic), diagnostic_order);
    finish_tokens(res);
    free(L.text);
    free(L.lines);
    free(L.cols);
    free(P.has_recipe);
    return res;
}

void make_parse_result_free(MakeParseResult *res)
{
    long i;
    if (!res) return;
    for (i = 0; i < res->symbol_count; i++) {
        free(res->symbols[i].name);
        free(res->symbols[i].detail);
    }
    for (i = 0; i < res->diagnostic_count; i++) free(res->diagnostics[i].message);
    for (i = 0; i < res->reference_count; i++) free(res->references[i].target);
    free(res->symbols);
    free(res->diagnostics);
    free(res->references);
    free(res->tokens);
    free(res);
}
