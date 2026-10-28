/**
 * @file c_parser.c
 * @brief The analysis of the C parser of Scout: lexer, preprocessor
 *        lines, declarations and highlighting tokens of one C source.
 *
 * The analysis runs in passes over the whole text:
 * 1. the lexer splits it into tokens (comments dropped) and marks the
 *    tokens of preprocessor lines;
 * 2. the directives give macros, includes and the \#if nesting; tokens in
 *    \#elif and \#else branches are left out of the code, so a function
 *    written twice for two compilers is read once;
 * 3. the brackets of the code are matched (unbalanced ones are
 *    diagnostics);
 * 4. a heuristic declaration parser walks the code: file scope, struct
 *    and enum bodies, parameters, and the statements of function bodies
 *    (local declarations, labels);
 * 5. every identifier is classified for highlighting: by its declaration,
 *    by what precedes it ("." or "->", "struct", "goto"), by the locals of
 *    the function it is in, by the symbols of the file, or as a call.
 *
 * Without the included headers the parser cannot know library types, so
 * it takes "name name" and "name *name" at the start of a statement for a
 * declaration with a type name.
 */
#include "c_parser.h"

#include <stdlib.h>
#include <string.h>

int (*c_parser_should_stop)(void) = NULL;

const char *const c_parser_keywords[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "sizeof", "static", "struct", "switch", "typedef", "union",
    "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Generic",
    "_Noreturn", "_Static_assert", "_Thread_local",
    NULL
};

const char *const c_parser_types[] = {
    "void", "char", "short", "int", "long", "float", "double", "signed",
    "unsigned", "_Bool", "_Complex", "_Imaginary",
    NULL
};

const char *const c_parser_constants[] = {
    "NULL", "TRUE", "FALSE", "true", "false",
    NULL
};

/**
 * Words that may stand among the specifiers and declarators without being
 * names: qualifiers, storage classes and compiler extensions (SAS/C, vbcc,
 * gcc). Ends with NULL.
 */
static const char *const QUALIFIERS[] = {
    "const", "volatile", "restrict", "register", "auto", "inline",
    "__inline", "__inline__", "__const", "__volatile__", "__restrict",
    "__chip", "__far", "__near", "__saveds", "__stdargs", "__regargs",
    "__interrupt", "__aligned", "__extension__", "_Noreturn",
    "_Thread_local", "_Atomic",
    NULL
};

/** Extension words followed by a parenthesised argument. Ends with NULL. */
static const char *const ATTRIBUTES[] = {
    "__attribute__", "__attribute", "__declspec", "__reg", "__asm",
    "__asm__", "_Alignas",
    NULL
};

/** @name Token types */
/** @{ */
#define TK_IDENT    1   /**< identifier or keyword */
#define TK_NUMBER   2   /**< number */
#define TK_STRING   3   /**< string literal */
#define TK_CHAR     4   /**< character literal */
#define TK_PUNCT    5   /**< operator or punctuation */
/** @} */

/** @name Token flags */
/** @{ */
#define TF_PP       1   /**< part of a preprocessor line */
#define TF_SKIP     2   /**< in an \#elif or \#else branch */
#define TF_HASH     4   /**< the '#' that starts a directive */
/** @} */

/** @name Contexts of declarations */
/** @{ */
#define CTX_FILE    0   /**< file scope */
#define CTX_STRUCT  1   /**< members of a struct or union */
#define CTX_BLOCK   2   /**< statements of a function body */
/** @} */

/** Longest text of CSymbol::detail (longer ones end with "..."). */
#define DETAIL_MAX  160

/** Deepest \#if nesting followed. */
#define COND_MAX    64

/** A token: a piece of the source. */
typedef struct CTok {
    const char *s;          /**< its first character in the source */
    long len;               /**< its length */
    long line;              /**< its line */
    long col;               /**< its column */
    unsigned char type;     /**< TK_ value */
    unsigned char flags;    /**< TF_ flags */
} CTok;

/** The tokens of a function definition, for the lookup of its locals. */
typedef struct FuncRange {
    long sym;               /**< the function's symbol */
    long first_local;       /**< first symbol after its parameters */
    long last;              /**< symbol after its last local */
    long tok_first;         /**< first token (the parameter list's '(') */
    long tok_last;          /**< last token (the body's '}') */
} FuncRange;

/** The state of one analysis. */
typedef struct Parser {
    const char *src;        /**< the source */
    const char *end;        /**< its end */
    int is_header;          /**< it is a header */
    CParseResult *res;      /**< the result being built */
    CTok *tok;              /**< all tokens */
    long ntok;              /**< number of tokens */
    long tokcap;            /**< size of tok */
    long *code;             /**< indices in tok of the code's tokens (no preprocessor lines, no skipped branches) */
    long ncode;             /**< number of them */
    long *match;            /**< per code token: code index of the matching bracket, -1 */
    unsigned char *mark;    /**< per token: SCOUT_TOKEN_ kind given by its declaration, 0 */
    FuncRange *funcs;       /**< the function definitions, in source order */
    long nfuncs;            /**< number of them */
    long funccap;           /**< size of funcs */
    long symcap;            /**< size of CParseResult::symbols */
    long diagcap;           /**< size of CParseResult::diagnostics */
    long refcap;            /**< size of CParseResult::references */
    long tokvalcap;         /**< size of CParseResult::tokens (values) */
    unsigned long steps;    /**< counter for c_parser_should_stop() */
} Parser;

/** Text being joined from tokens, at most DETAIL_MAX characters. */
typedef struct TextBuf {
    char buf[DETAIL_MAX + 4];   /**< the text */
    size_t len;                 /**< its length */
    int full;                   /**< it was cut */
} TextBuf;

/**
 * @brief Tell whether a character may start an identifier.
 * @param c The character.
 * @return Non-zero if it may.
 */
static int is_ident_start(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/**
 * @brief Tell whether a character is a decimal digit.
 * @param c The character.
 * @return Non-zero if it is.
 */
static int is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

/**
 * @brief Tell whether a character may be part of an identifier.
 * @param c The character.
 * @return Non-zero if it may.
 */
static int is_ident_part(unsigned char c)
{
    return is_ident_start(c) || is_digit(c);
}

/**
 * @brief Copy a piece of text into an allocated, terminated string.
 * @param s The text.
 * @param len Its length.
 * @return The copy, or NULL if out of memory.
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
 * @brief Ask c_parser_should_stop() now and then.
 * @param P The parser.
 * @return Non-zero if the analysis is to stop (or has stopped).
 */
static int check_stop(Parser *P)
{
    if ((++P->steps & 1023) == 0 && c_parser_should_stop && c_parser_should_stop()) {
        P->res->stopped = 1;
    }
    return P->res->stopped || P->res->out_of_memory;
}

/**
 * @brief Tell whether a token is a given word or punctuation.
 * @param t The token.
 * @param type TK_IDENT or TK_PUNCT.
 * @param text The text.
 * @return Non-zero if it is.
 */
static int tok_is(const CTok *t, int type, const char *text)
{
    size_t n = strlen(text);
    return t->type == type && (size_t)t->len == n && memcmp(t->s, text, n) == 0;
}

/**
 * @brief Tell whether a token is one of a list of words.
 * @param t The token.
 * @param list The words, ended by NULL.
 * @return Non-zero if it is.
 */
static int tok_in(const CTok *t, const char *const *list)
{
    size_t i;
    if (t->type != TK_IDENT) return 0;
    for (i = 0; list[i]; i++) {
        if (list[i][0] == t->s[0] && strlen(list[i]) == (size_t)t->len &&
            memcmp(list[i], t->s, (size_t)t->len) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Tell whether a token is a word of the language (keyword, type
 *        keyword or constant), so that it cannot be a name.
 * @param t The token.
 * @return Non-zero if it is.
 */
static int tok_reserved(const CTok *t)
{
    return tok_in(t, c_parser_keywords) || tok_in(t, c_parser_types) ||
           tok_in(t, c_parser_constants) || tok_in(t, QUALIFIERS) || tok_in(t, ATTRIBUTES);
}

/* ---- Results ---- */

/**
 * @brief Grow an array of the result when it is full.
 * @param P The parser.
 * @param array The array (pointer to its pointer).
 * @param cap Its size, updated.
 * @param count The number of elements in use.
 * @param size The size of an element.
 * @return Non-zero if there is room for one more.
 */
static int grow(Parser *P, void **array, long *cap, long count, size_t size)
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
 * @brief Add a diagnostic.
 * @param P The parser.
 * @param message The text (copied).
 * @param severity SCOUT_SEVERITY_ value.
 * @param line First line.
 * @param col First column.
 * @param end_line Line of the end.
 * @param end_col Column after the end (at least @p col + 1 on one line).
 */
static void add_diag(Parser *P, const char *message, int severity,
                     long line, long col, long end_line, long end_col)
{
    CParseResult *res = P->res;
    CDiagnostic *d;
    if (!grow(P, (void **)&res->diagnostics, &P->diagcap, res->diagnostic_count, sizeof(CDiagnostic))) return;
    d = &res->diagnostics[res->diagnostic_count];
    d->message = dup_text(message, strlen(message));
    if (!d->message) {
        res->out_of_memory = 1;
        return;
    }
    if (end_line < line || (end_line == line && end_col <= col)) {
        end_line = line;
        end_col = col + 1;
    }
    d->severity = severity;
    d->range.start_line = line;
    d->range.start_col = col;
    d->range.end_line = end_line;
    d->range.end_col = end_col;
    res->diagnostic_count++;
}

/**
 * @brief Add a diagnostic covering tokens.
 * @param P The parser.
 * @param message The text (copied).
 * @param severity SCOUT_SEVERITY_ value.
 * @param from The first token.
 * @param to The last token.
 */
static void add_diag_toks(Parser *P, const char *message, int severity,
                          const CTok *from, const CTok *to)
{
    add_diag(P, message, severity, from->line, from->col, to->line, to->col + to->len);
}

/**
 * @brief Set a range from a first and a last token.
 * @param[out] r The range.
 * @param from The first token.
 * @param to The last token.
 */
static void set_range(CRange *r, const CTok *from, const CTok *to)
{
    r->start_line = from->line;
    r->start_col = from->col;
    r->end_line = to->line;
    r->end_col = to->col + to->len;
}

/**
 * @brief Add a symbol.
 * @param P The parser.
 * @param name Its name's token.
 * @param kind SCOUT_SYMBOL_ value.
 * @param scope Its visibility.
 * @param parent Index of the symbol it belongs to, -1 for none.
 * @param from First token of its definition.
 * @param to Last token of its definition.
 * @param detail Its declaration as text (taken over; may be NULL).
 * @return Its index, or -1 if out of memory.
 */
static long add_symbol(Parser *P, const CTok *name, int kind, CScope scope, long parent,
                       const CTok *from, const CTok *to, char *detail)
{
    CParseResult *res = P->res;
    CSymbol *s;
    if (!grow(P, (void **)&res->symbols, &P->symcap, res->symbol_count, sizeof(CSymbol))) {
        free(detail);
        return -1;
    }
    s = &res->symbols[res->symbol_count];
    s->name = dup_text(name->s, (size_t)name->len);
    if (!s->name) {
        free(detail);
        res->out_of_memory = 1;
        return -1;
    }
    s->detail = detail;
    s->kind = kind;
    s->scope = scope;
    s->parent = parent;
    s->declaration = 0;
    set_range(&s->range, from, to);
    set_range(&s->selection, name, name);
    return res->symbol_count++;
}

/**
 * @brief Add an include.
 * @param P The parser.
 * @param target The name as written (copied).
 * @param len Its length.
 * @param system Written with <...>.
 * @param from First token (the quote or '<').
 * @param to Last token (the same, or '>').
 */
static void add_reference(Parser *P, const char *target, size_t len, int system,
                          const CTok *from, const CTok *to)
{
    CParseResult *res = P->res;
    CReference *r;
    if (!grow(P, (void **)&res->references, &P->refcap, res->reference_count, sizeof(CReference))) return;
    r = &res->references[res->reference_count];
    r->target = dup_text(target, len);
    if (!r->target) {
        res->out_of_memory = 1;
        return;
    }
    r->system = system;
    set_range(&r->range, from, to);
    res->reference_count++;
}

/* ---- Text of declarations ---- */

/**
 * @brief Append text to a TextBuf, cutting it at DETAIL_MAX.
 * @param b The buffer.
 * @param space Put a space before it (if the buffer is not empty).
 * @param s The text.
 * @param len Its length.
 */
static void tb_add(TextBuf *b, int space, const char *s, size_t len)
{
    if (b->full) return;
    if (space && b->len > 0) {
        b->buf[b->len++] = ' ';
    }
    if (b->len + len > DETAIL_MAX) {
        len = b->len < DETAIL_MAX ? DETAIL_MAX - b->len : 0;
        memcpy(b->buf + b->len, s, len);
        b->len += len;
        memcpy(b->buf + b->len, "...", 3);
        b->len += 3;
        b->full = 1;
        return;
    }
    memcpy(b->buf + b->len, s, len);
    b->len += len;
}

/**
 * @brief Return the code index of the bracket matching an opening one.
 * @param P The parser.
 * @param i The opening bracket.
 * @param end The end of the area searched (exclusive).
 * @return The closing bracket, or @p end if there is none before it.
 */
static long close_of(Parser *P, long i, long end)
{
    long m = P->match[i];
    return (m < 0 || m >= end) ? end : m;
}

/**
 * @brief Append the text of code tokens to a TextBuf; a brace body is
 *        written as "{...}".
 *
 * Tokens that stand apart in the source are joined with one space.
 *
 * @param P The parser.
 * @param b The buffer.
 * @param from The first code token.
 * @param to The last code token.
 * @param space Put a space before the first token.
 */
static void tb_add_code(Parser *P, TextBuf *b, long from, long to, int space)
{
    const CTok *prev = NULL;
    long i;
    for (i = from; i <= to && i < P->ncode; i++) {
        const CTok *t = &P->tok[P->code[i]];
        int gap = prev ? prev->s + prev->len < t->s : space;
        if (tok_is(t, TK_PUNCT, "{")) {
            long close = close_of(P, i, P->ncode);
            tb_add(b, gap, "{...}", 5);
            if (close >= P->ncode) break;
            prev = &P->tok[P->code[close]];
            i = close;
            continue;
        }
        tb_add(b, gap, t->s, (size_t)t->len);
        prev = t;
    }
}

/**
 * @brief Return the text of a TextBuf as an allocated string.
 * @param b The buffer.
 * @return The text, or NULL if it is empty or memory ran out.
 */
static char *tb_string(TextBuf *b)
{
    return b->len ? dup_text(b->buf, b->len) : NULL;
}

/* ---- Lexer ---- */

/**
 * @brief Append a token.
 * @param P The parser.
 * @param s Its first character.
 * @param len Its length.
 * @param line Its line.
 * @param col Its column.
 * @param type TK_ value.
 * @param flags TF_ flags.
 * @return Non-zero on success, 0 if out of memory or stopped.
 */
static int add_tok(Parser *P, const char *s, long len, long line, long col, int type, int flags)
{
    CTok *t;
    if (check_stop(P)) return 0;
    if (!grow(P, (void **)&P->tok, &P->tokcap, P->ntok, sizeof(CTok))) return 0;
    t = &P->tok[P->ntok++];
    t->s = s;
    t->len = len;
    t->line = line;
    t->col = col;
    t->type = (unsigned char)type;
    t->flags = (unsigned char)flags;
    return 1;
}

/**
 * @brief Return the length of the operator or punctuation at a position.
 * @param p The position.
 * @param end The end of the source.
 * @return 1 to 3.
 */
static long punct_length(const char *p, const char *end)
{
    static const char *const three[] = { "...", "<<=", ">>=", NULL };
    static const char *const two[] = {
        "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##", "::", NULL
    };
    size_t i;
    if (end - p >= 3) {
        for (i = 0; three[i]; i++) {
            if (memcmp(p, three[i], 3) == 0) return 3;
        }
    }
    if (end - p >= 2) {
        for (i = 0; two[i]; i++) {
            if (p[0] == two[i][0] && p[1] == two[i][1]) return 2;
        }
    }
    return 1;
}

/**
 * @brief Split the source into tokens; comments are dropped, unclosed
 *        comments and literals become diagnostics.
 * @param P The parser.
 */
static void lex(Parser *P)
{
    const char *p = P->src;
    const char *end = P->end;
    long line = 1, col = 1;
    int line_start = 1;     /* only blanks so far on this line */
    int in_pp = 0;          /* in a preprocessor line */

    while (p < end) {
        unsigned char c = (unsigned char)*p;
        const char *start = p;
        long sline = line, scol = col;
        int flags;

        if (c == '\n' || c == '\r') {
            p++;
            if (c == '\r' && p < end && *p == '\n') p++;
            line++;
            col = 1;
            line_start = 1;
            in_pp = 0;
            continue;
        }
        if (c == '\\' && p + 1 < end && (p[1] == '\n' || p[1] == '\r')) {
            /* a spliced line: a preprocessor line goes on */
            p++;
            c = (unsigned char)*p++;
            if (c == '\r' && p < end && *p == '\n') p++;
            line++;
            col = 1;
            line_start = 0;
            continue;
        }
        if (c <= ' ' || c == 0x7f) {
            p++;
            col++;
            continue;
        }
        if (c == '/' && p + 1 < end && p[1] == '/') {
            while (p < end && *p != '\n' && *p != '\r') {
                p++;
                col++;
            }
            continue;
        }
        if (c == '/' && p + 1 < end && p[1] == '*') {
            int closed = 0;
            p += 2;
            col += 2;
            while (p < end) {
                if (*p == '*' && p + 1 < end && p[1] == '/') {
                    p += 2;
                    col += 2;
                    closed = 1;
                    break;
                }
                if (*p == '\n' || *p == '\r') {
                    char nl = *p++;
                    if (nl == '\r' && p < end && *p == '\n') p++;
                    line++;
                    col = 1;
                } else {
                    p++;
                    col++;
                }
            }
            if (!closed) {
                add_diag(P, "Unclosed block comment", SCOUT_SEVERITY_ERROR, sline, scol, line, col);
            }
            continue;
        }

        flags = in_pp ? TF_PP : 0;
        if (c == '#' && line_start) {
            in_pp = 1;
            line_start = 0;
            if (!add_tok(P, p, 1, line, col, TK_PUNCT, TF_PP | TF_HASH)) return;
            p++;
            col++;
            continue;
        }
        line_start = 0;

        if (c == '"' || c == '\'') {
            int closed = 0;
            p++;
            col++;
            while (p < end && *p != '\n' && *p != '\r') {
                if ((unsigned char)*p == c) {
                    p++;
                    col++;
                    closed = 1;
                    break;
                }
                if (*p == '\\' && p + 1 < end && p[1] != '\n' && p[1] != '\r') {
                    p += 2;
                    col += 2;
                } else {
                    p++;
                    col++;
                }
            }
            /* an apostrophe in "#error don't" is no literal */
            if (!closed && !in_pp) {
                add_diag(P, c == '"' ? "Unterminated string literal" : "Unterminated character literal",
                         SCOUT_SEVERITY_ERROR, sline, scol, line, col);
            }
            if (!add_tok(P, start, (long)(p - start), sline, scol,
                         c == '"' ? TK_STRING : TK_CHAR, flags)) return;
            continue;
        }
        if (is_digit(c) || (c == '.' && p + 1 < end && is_digit((unsigned char)p[1]))) {
            p++;
            while (p < end) {
                unsigned char d = (unsigned char)*p;
                unsigned char before = (unsigned char)p[-1];
                if (is_ident_part(d) || d == '.') {
                    p++;
                } else if ((d == '+' || d == '-') &&
                           (before == 'e' || before == 'E' || before == 'p' || before == 'P')) {
                    p++;
                } else {
                    break;
                }
            }
            col += (long)(p - start);
            if (!add_tok(P, start, (long)(p - start), sline, scol, TK_NUMBER, flags)) return;
            continue;
        }
        if (is_ident_start(c)) {
            while (p < end && is_ident_part((unsigned char)*p)) p++;
            col += (long)(p - start);
            if (!add_tok(P, start, (long)(p - start), sline, scol, TK_IDENT, flags)) return;
            continue;
        }
        {
            long n = punct_length(p, end);
            p += n;
            col += n;
            if (!add_tok(P, start, n, sline, scol, TK_PUNCT, flags)) return;
        }
    }
}

/* ---- Preprocessor lines ---- */

/**
 * @brief Tell whether a macro of a name is known already.
 * @param P The parser.
 * @param name Its name's token.
 * @return Non-zero if it is.
 */
static int macro_known(Parser *P, const CTok *name)
{
    long i;
    for (i = 0; i < P->res->symbol_count; i++) {
        const CSymbol *s = &P->res->symbols[i];
        if (s->kind == SCOUT_SYMBOL_MACRO && strlen(s->name) == (size_t)name->len &&
            memcmp(s->name, name->s, (size_t)name->len) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Handle \#define: a macro symbol.
 * @param P The parser.
 * @param hash The directive's '#'.
 * @param m The token after "define".
 * @param k The token after the directive line.
 * @param skipped The directive is in a skipped branch (an existing macro
 *                of the name is kept then).
 */
static void do_define(Parser *P, long hash, long m, long k, int skipped)
{
    const CTok *name = &P->tok[m];
    TextBuf b;
    long i;
    long sym;
    if (m >= k || name->type != TK_IDENT) {
        add_diag_toks(P, "#define without a name", SCOUT_SEVERITY_ERROR, &P->tok[hash], &P->tok[k - 1]);
        return;
    }
    if (skipped && macro_known(P, name)) return;
    memset(&b, 0, sizeof(b));
    for (i = m; i < k; i++) {
        const CTok *t = &P->tok[i];
        tb_add(&b, i > m && P->tok[i - 1].s + P->tok[i - 1].len < t->s, t->s, (size_t)t->len);
    }
    sym = add_symbol(P, name, SCOUT_SYMBOL_MACRO, P->is_header ? C_SCOPE_GLOBAL : C_SCOPE_FILE, -1,
                     &P->tok[hash], &P->tok[k - 1], tb_string(&b));
    if (sym >= 0) P->mark[m] = SCOUT_TOKEN_MACRO;
}

/**
 * @brief Handle \#include: a reference.
 * @param P The parser.
 * @param hash The directive's '#'.
 * @param m The token after "include".
 * @param k The token after the directive line.
 */
static void do_include(Parser *P, long hash, long m, long k)
{
    const CTok *t = &P->tok[m];
    long n;
    if (m < k && t->type == TK_STRING) {
        long len = t->len >= 2 && t->s[t->len - 1] == '"' ? t->len - 2 : t->len - 1;
        add_reference(P, t->s + 1, (size_t)len, 0, t, t);
        return;
    }
    if (m < k && tok_is(t, TK_PUNCT, "<")) {
        for (n = m + 1; n < k; n++) {
            if (tok_is(&P->tok[n], TK_PUNCT, ">")) {
                const char *from = t->s + 1;
                add_reference(P, from, (size_t)(P->tok[n].s - from), 1, t, &P->tok[n]);
                return;
            }
        }
    }
    if (m < k && t->type == TK_IDENT) return;   /* #include MACRO */
    add_diag_toks(P, "#include without a file name", SCOUT_SEVERITY_ERROR, &P->tok[hash], &P->tok[k - 1]);
}

/**
 * @brief Handle the directives: macros, includes and the \#if nesting;
 *        tokens in \#elif and \#else branches get TF_SKIP.
 * @param P The parser.
 */
static void directives(Parser *P)
{
    static const char *const known[] = {
        "undef", "line", "error", "warning", "pragma", "ident", "sccs",
        "assert", "unassert", "include_next", "import", NULL
    };
    long cond[COND_MAX];        /* the '#' of each open #if */
    int in_else[COND_MAX];      /* whether it is past its #elif or #else */
    int depth = 0;              /* open #ifs */
    int deep = 0;               /* open #ifs beyond COND_MAX */
    int else_count = 0;         /* open #ifs past their #elif or #else */
    long i = 0;

    while (i < P->ntok) {
        CTok *t = &P->tok[i];
        long j, k;
        if (!(t->flags & TF_HASH)) {
            if (else_count > 0) t->flags |= TF_SKIP;
            i++;
            continue;
        }
        if (check_stop(P)) return;
        for (k = i + 1; k < P->ntok && (P->tok[k].flags & TF_PP) && !(P->tok[k].flags & TF_HASH); k++);
        j = i + 1;
        if (j < k && P->tok[j].type == TK_IDENT) {
            const CTok *d = &P->tok[j];
            if (tok_is(d, TK_IDENT, "if") || tok_is(d, TK_IDENT, "ifdef") || tok_is(d, TK_IDENT, "ifndef")) {
                if (depth < COND_MAX) {
                    cond[depth] = i;
                    in_else[depth++] = 0;
                } else {
                    deep++;
                }
            } else if (tok_is(d, TK_IDENT, "elif") || tok_is(d, TK_IDENT, "else")) {
                if (deep == 0 && depth == 0) {
                    add_diag_toks(P, tok_is(d, TK_IDENT, "else") ? "#else without #if" : "#elif without #if",
                                  SCOUT_SEVERITY_ERROR, t, d);
                } else if (deep == 0 && !in_else[depth - 1]) {
                    in_else[depth - 1] = 1;
                    else_count++;
                }
            } else if (tok_is(d, TK_IDENT, "endif")) {
                if (deep > 0) {
                    deep--;
                } else if (depth == 0) {
                    add_diag_toks(P, "#endif without #if", SCOUT_SEVERITY_ERROR, t, d);
                } else {
                    if (in_else[--depth]) else_count--;
                }
            } else if (tok_is(d, TK_IDENT, "define")) {
                do_define(P, i, j + 1, k, else_count > 0);
            } else if (tok_is(d, TK_IDENT, "include")) {
                do_include(P, i, j + 1, k);
            } else if (!tok_in(d, known)) {
                char msg[80];
                long n = d->len < 40 ? d->len : 40;
                memcpy(msg, "Unknown preprocessor directive #", 32);
                memcpy(msg + 32, d->s, (size_t)n);
                msg[32 + n] = '\0';
                add_diag_toks(P, msg, SCOUT_SEVERITY_WARNING, t, d);
            }
        }
        i = k;
    }
    while (depth > 0) {
        long h = cond[--depth];
        add_diag_toks(P, "#if without #endif", SCOUT_SEVERITY_ERROR, &P->tok[h], &P->tok[h + 1]);
    }
}

/* ---- Brackets ---- */

/**
 * @brief Collect the code's tokens and match its brackets; unbalanced
 *        ones become diagnostics.
 * @param P The parser.
 */
static void match_brackets(Parser *P)
{
    long *stack;
    long sp = 0, i;
    P->code = (long *)malloc((size_t)(P->ntok + 1) * sizeof(long));
    P->match = (long *)malloc((size_t)(P->ntok + 1) * sizeof(long));
    stack = (long *)malloc((size_t)(P->ntok + 1) * sizeof(long));
    if (!P->code || !P->match || !stack) {
        free(stack);
        P->res->out_of_memory = 1;
        return;
    }
    for (i = 0; i < P->ntok; i++) {
        if (!(P->tok[i].flags & (TF_PP | TF_SKIP))) P->code[P->ncode++] = i;
    }
    for (i = 0; i < P->ncode; i++) {
        const CTok *t = &P->tok[P->code[i]];
        char c = t->s[0];
        P->match[i] = -1;
        if (t->type != TK_PUNCT || t->len != 1) continue;
        if (c == '(' || c == '[' || c == '{') {
            stack[sp++] = i;
        } else if (c == ')' || c == ']' || c == '}') {
            char open = c == ')' ? '(' : c == ']' ? '[' : '{';
            long k = sp;
            while (k > 0 && P->tok[P->code[stack[k - 1]]].s[0] != open) k--;
            if (k > 0) {
                /* brackets opened inside and not closed are reported, the
                   rest matches again */
                while (sp > k) {
                    const CTok *o = &P->tok[P->code[stack[--sp]]];
                    char msg[] = "Unclosed bracket 'x'";
                    msg[sizeof(msg) - 3] = o->s[0];
                    add_diag_toks(P, msg, SCOUT_SEVERITY_ERROR, o, o);
                }
                sp--;
                P->match[i] = stack[sp];
                P->match[stack[sp]] = i;
            } else {
                char msg[] = "Mismatched bracket 'x'";
                msg[sizeof(msg) - 3] = c;
                add_diag_toks(P, msg, SCOUT_SEVERITY_ERROR, t, t);
            }
        }
    }
    while (sp > 0) {
        const CTok *t = &P->tok[P->code[stack[--sp]]];
        const CTok *last = &P->tok[P->ntok - 1];
        char msg[] = "Unclosed bracket 'x'";
        msg[sizeof(msg) - 3] = t->s[0];
        add_diag(P, msg, SCOUT_SEVERITY_ERROR, t->line, t->col, last->line, last->col + last->len);
    }
    free(stack);
}

/* ---- Declarations ---- */

/**
 * @brief Return a code token.
 * @param P The parser.
 * @param i Its code index.
 * @return The token.
 */
static const CTok *CT(Parser *P, long i)
{
    return &P->tok[P->code[i]];
}

/**
 * @brief Tell whether a code token (before an end) is a punctuation.
 * @param P The parser.
 * @param i Its code index.
 * @param end The end (exclusive).
 * @param text The punctuation.
 * @return Non-zero if it is.
 */
static int is_punct(Parser *P, long i, long end, const char *text)
{
    return i < end && tok_is(CT(P, i), TK_PUNCT, text);
}

/**
 * @brief Tell whether a code token (before an end) is a word.
 * @param P The parser.
 * @param i Its code index.
 * @param end The end (exclusive).
 * @param text The word.
 * @return Non-zero if it is.
 */
static int is_word(Parser *P, long i, long end, const char *text)
{
    return i < end && tok_is(CT(P, i), TK_IDENT, text);
}

/**
 * @brief Tell whether a code token (before an end) is a name: an
 *        identifier that is no word of the language.
 * @param P The parser.
 * @param i Its code index.
 * @param end The end (exclusive).
 * @return Non-zero if it is.
 */
static int is_name(Parser *P, long i, long end)
{
    return i < end && CT(P, i)->type == TK_IDENT && !tok_reserved(CT(P, i));
}

/**
 * @brief Tell whether a code token is "struct", "union" or "enum".
 * @param P The parser.
 * @param i Its code index.
 * @param end The end (exclusive).
 * @return Non-zero if it is.
 */
static int is_tag_word(Parser *P, long i, long end)
{
    return is_word(P, i, end, "struct") || is_word(P, i, end, "union") || is_word(P, i, end, "enum");
}

/**
 * @brief Give a code token a highlighting kind.
 * @param P The parser.
 * @param i Its code index.
 * @param kind SCOUT_TOKEN_ value.
 */
static void mark(Parser *P, long i, int kind)
{
    P->mark[P->code[i]] = (unsigned char)kind;
}

/**
 * @brief Step over one code token, or a bracketed group.
 * @param P The parser.
 * @param i The token.
 * @param end The end (exclusive).
 * @return The index after it.
 */
static long skip_one(Parser *P, long i, long end)
{
    if (is_punct(P, i, end, "(") || is_punct(P, i, end, "[") || is_punct(P, i, end, "{")) {
        return close_of(P, i, end) + 1;
    }
    return i + 1;
}

/**
 * @brief Return the scope of a type, macro or enumeration at file scope.
 * @param P The parser.
 * @return C_SCOPE_GLOBAL in a header, otherwise C_SCOPE_FILE.
 */
static CScope type_scope(Parser *P)
{
    return P->is_header ? C_SCOPE_GLOBAL : C_SCOPE_FILE;
}

/**
 * @brief Find the name of the first declarator after a struct body
 *        (the name of "typedef struct { ... } Name;").
 * @param P The parser.
 * @param i The token after the body.
 * @param end The end (exclusive).
 * @return Its code index, or -1.
 */
static long first_declarator_name(Parser *P, long i, long end)
{
    while (i < end) {
        if (is_punct(P, i, end, ";") || is_punct(P, i, end, ",") || is_punct(P, i, end, "=") ||
            is_punct(P, i, end, "[") || is_punct(P, i, end, "{")) {
            return -1;
        }
        if (is_punct(P, i, end, "(")) {
            long close = close_of(P, i, end), k;
            for (k = i + 1; k < close; k++) {
                if (is_name(P, k, end)) return k;
            }
            i = close + 1;
            continue;
        }
        if (is_name(P, i, end)) return i;
        i++;
    }
    return -1;
}

static long parse_declaration(Parser *P, long i, long end, int ctx, long parent);

/**
 * @brief Parse the members of a struct or union body.
 * @param P The parser.
 * @param i The token after the '{'.
 * @param end The '}' (exclusive end).
 * @param parent Index of the struct's symbol (or of the symbol the
 *               members belong to), -1 for none.
 */
static void parse_members(Parser *P, long i, long end, long parent)
{
    while (i < end && !check_stop(P)) {
        long n;
        if (is_punct(P, i, end, ";")) {
            i++;
            continue;
        }
        n = parse_declaration(P, i, end, CTX_STRUCT, parent);
        i = n > i ? n : i + 1;
    }
}

/**
 * @brief Parse the members of an enum body.
 * @param P The parser.
 * @param i The token after the '{'.
 * @param end The '}' (exclusive end).
 * @param parent Index of the enum's symbol, -1 for none.
 * @param scope The scope of the members.
 */
static void parse_enum(Parser *P, long i, long end, long parent, CScope scope)
{
    while (i < end && !check_stop(P)) {
        if (is_name(P, i, end)) {
            long last = i, k = i;
            while (k < end && !is_punct(P, k, end, ",")) {
                last = k;
                k = skip_one(P, k, end);
            }
            if (add_symbol(P, CT(P, i), SCOUT_SYMBOL_ENUM_MEMBER, scope, parent,
                           CT(P, i), CT(P, last < end ? last : i), NULL) >= 0) {
                mark(P, i, SCOUT_TOKEN_CONSTANT);
            }
            i = k;
        }
        while (i < end && !is_punct(P, i, end, ",")) i = skip_one(P, i, end);
        i++;
    }
}

/**
 * @brief Parse the parameters of a function definition into local
 *        symbols of the function.
 * @param P The parser.
 * @param lp The parameter list's '('.
 * @param func Index of the function's symbol.
 */
static void parse_params(Parser *P, long lp, long func)
{
    long rp = close_of(P, lp, P->ncode);
    long a = lp + 1;
    while (a < rp) {
        long b = a, name = -1, k;
        while (b < rp && !is_punct(P, b, rp, ",")) b = skip_one(P, b, rp);
        for (k = a; k < b; k++) {
            /* a function pointer: void (*fn)(int) */
            if (is_punct(P, k, b, "(") && is_punct(P, k + 1, b, "*")) {
                long close = close_of(P, k, b), m;
                for (m = k + 1; m < close; m++) {
                    if (is_name(P, m, b)) {
                        name = m;
                        break;
                    }
                }
                break;
            }
        }
        if (name < 0 && b - a >= 2) {
            /* otherwise the last name that is no struct tag */
            for (k = a; k < b; k++) {
                if (is_punct(P, k, b, "(") || is_punct(P, k, b, "[")) {
                    k = close_of(P, k, b);
                    continue;
                }
                if (is_name(P, k, b) && !(k > a && is_tag_word(P, k - 1, b))) name = k;
            }
        }
        if (name >= 0) {
            TextBuf t;
            memset(&t, 0, sizeof(t));
            tb_add_code(P, &t, a, b - 1, 0);
            for (k = a; k < name; k++) {
                if (is_name(P, k, b)) mark(P, k, SCOUT_TOKEN_TYPE);
            }
            if (add_symbol(P, CT(P, name), SCOUT_SYMBOL_VARIABLE, C_SCOPE_LOCAL, func,
                           CT(P, a), CT(P, b - 1), tb_string(&t)) >= 0) {
                mark(P, name, SCOUT_TOKEN_PARAMETER);
            }
        }
        a = b + 1;
    }
}

/**
 * @brief Tell whether a statement in a function body starts a declaration.
 *
 * Without the headers, a type name is not known: "name name", "name
 * *name" and "name name[" count as declarations, as does anything
 * starting with a type keyword, a qualifier or struct/union/enum.
 *
 * @param P The parser.
 * @param i The statement's first token.
 * @param end The end of the block (exclusive).
 * @return Non-zero if it does.
 */
static int looks_like_decl(Parser *P, long i, long end)
{
    const CTok *t;
    long k;
    if (i >= end) return 0;
    t = CT(P, i);
    if (tok_in(t, c_parser_types) || tok_in(t, QUALIFIERS) || is_tag_word(P, i, end) ||
        is_word(P, i, end, "static") || is_word(P, i, end, "extern") || is_word(P, i, end, "typedef")) {
        return 1;
    }
    if (!is_name(P, i, end)) return 0;
    k = i + 1;
    while (k < end && (is_punct(P, k, end, "*") || tok_in(CT(P, k), QUALIFIERS))) k++;
    if (!is_name(P, k, end)) return 0;
    return is_punct(P, k + 1, end, "=") || is_punct(P, k + 1, end, ";") ||
           is_punct(P, k + 1, end, ",") || is_punct(P, k + 1, end, "[");
}

/**
 * @brief Add a function definition to the ranges searched for locals.
 * @param P The parser.
 * @param sym The function's symbol.
 * @param first_local The first symbol after its parameters.
 * @param lp The parameter list's '('.
 * @param close The body's '}'.
 */
static void add_func(Parser *P, long sym, long first_local, long lp, long close)
{
    FuncRange *f;
    if (!grow(P, (void **)&P->funcs, &P->funccap, P->nfuncs, sizeof(FuncRange))) return;
    f = &P->funcs[P->nfuncs++];
    f->sym = sym;
    f->first_local = first_local;
    f->last = first_local;
    f->tok_first = P->code[lp];
    f->tok_last = close < P->ncode ? P->code[close] : P->ntok - 1;
}

/**
 * @brief Parse the statements of a block: local declarations, labels and
 *        nested blocks; other statements are skipped.
 * @param P The parser.
 * @param i The token after the '{'.
 * @param end The '}' (exclusive end).
 * @param func Index of the function's symbol.
 */
static void parse_block(Parser *P, long i, long end, long func)
{
    while (i < end && !check_stop(P)) {
        if (is_punct(P, i, end, "{")) {
            long close = close_of(P, i, end);
            parse_block(P, i + 1, close, func);
            i = close + 1;
            continue;
        }
        if (is_punct(P, i, end, ";") || is_punct(P, i, end, "}")) {
            i++;
            continue;
        }
        if (is_name(P, i, end) && is_punct(P, i + 1, end, ":")) {
            if (add_symbol(P, CT(P, i), SCOUT_SYMBOL_LABEL, C_SCOPE_LOCAL, func,
                           CT(P, i), CT(P, i + 1), NULL) >= 0) {
                mark(P, i, SCOUT_TOKEN_LABEL);
            }
            i += 2;
            continue;
        }
        if (is_word(P, i, end, "for") && is_punct(P, i + 1, end, "(")) {
            long close = close_of(P, i + 1, end);
            if (looks_like_decl(P, i + 2, close)) parse_declaration(P, i + 2, close, CTX_BLOCK, func);
            i = close + 1;
            continue;
        }
        if (is_word(P, i, end, "if") || is_word(P, i, end, "while") || is_word(P, i, end, "switch")) {
            i++;
            if (is_punct(P, i, end, "(")) i = close_of(P, i, end) + 1;
            continue;
        }
        if (is_word(P, i, end, "else") || is_word(P, i, end, "do")) {
            i++;
            continue;
        }
        if (looks_like_decl(P, i, end)) {
            long n = parse_declaration(P, i, end, CTX_BLOCK, func);
            i = n > i ? n : i + 1;
            continue;
        }
        while (i < end && !is_punct(P, i, end, ";") && !is_punct(P, i, end, "{") &&
               !is_punct(P, i, end, "}")) {
            i = (is_punct(P, i, end, "(") || is_punct(P, i, end, "[")) ? close_of(P, i, end) + 1 : i + 1;
        }
        if (is_punct(P, i, end, ";")) i++;
    }
}

/**
 * @brief Parse one declaration: its specifiers (with a struct, union or
 *        enum defined there) and its declarators, up to the ';' or the
 *        end of a function body.
 * @param P The parser.
 * @param i Its first token.
 * @param end The end of the area (exclusive).
 * @param ctx CTX_FILE, CTX_STRUCT or CTX_BLOCK.
 * @param parent The symbol members and locals belong to (-1 at file scope).
 * @return The index after the declaration.
 */
static long parse_declaration(Parser *P, long i, long end, int ctx, long parent)
{
    long start = i, spec_end;
    int is_typedef = 0, is_static = 0, is_extern = 0, saw_type = 0;
    long pre_alias = -1;    /* typedef name added before its struct's members */

    /* specifiers */
    while (i < end) {
        const CTok *t = CT(P, i);
        if (t->type != TK_IDENT) break;
        if (is_word(P, i, end, "typedef")) {
            is_typedef = 1;
            i++;
        } else if (is_word(P, i, end, "static")) {
            is_static = 1;
            i++;
        } else if (is_word(P, i, end, "extern")) {
            is_extern = 1;
            i++;
        } else if (tok_in(t, ATTRIBUTES) && is_punct(P, i + 1, end, "(")) {
            i = close_of(P, i + 1, end) + 1;
        } else if (tok_in(t, QUALIFIERS)) {
            i++;
        } else if (tok_in(t, c_parser_types)) {
            saw_type = 1;
            i++;
        } else if (is_tag_word(P, i, end)) {
            long kw = i, tag = -1;
            int is_enum = is_word(P, i, end, "enum");
            i++;
            if (is_name(P, i, end)) {
                tag = i;
                mark(P, i, SCOUT_TOKEN_TYPE);
                i++;
            }
            if (is_punct(P, i, end, "{")) {
                long close = close_of(P, i, end);
                long sym = -1;
                const CTok *last = CT(P, close < end ? close : end - 1);
                CScope scope = ctx == CTX_FILE ? type_scope(P) : C_SCOPE_LOCAL;
                long par = ctx == CTX_FILE ? -1 : parent;
                TextBuf b;
                memset(&b, 0, sizeof(b));
                if (tag >= 0) {
                    tb_add_code(P, &b, kw, tag, 0);
                    sym = add_symbol(P, CT(P, tag), is_enum ? SCOUT_SYMBOL_ENUM : SCOUT_SYMBOL_STRUCT,
                                     scope, par, CT(P, kw), last, tb_string(&b));
                } else if (is_typedef) {
                    long n = first_declarator_name(P, close + 1, end);
                    if (n >= 0) {
                        tb_add_code(P, &b, start, n, 0);
                        sym = add_symbol(P, CT(P, n), SCOUT_SYMBOL_TYPE_ALIAS, scope, par,
                                         CT(P, start), CT(P, n), tb_string(&b));
                        if (sym >= 0) {
                            mark(P, n, SCOUT_TOKEN_TYPE);
                            pre_alias = n;
                        }
                    }
                }
                if (is_enum) {
                    parse_enum(P, i + 1, close, sym >= 0 ? sym : par, scope);
                } else {
                    parse_members(P, i + 1, close, sym >= 0 ? sym : parent);
                }
                i = close + 1;
            }
            saw_type = 1;
        } else if (is_name(P, i, end) && !saw_type) {
            /* a type name, unless it is the declarator itself */
            if (is_punct(P, i + 1, end, "(") || is_punct(P, i + 1, end, ";") ||
                is_punct(P, i + 1, end, ",") || is_punct(P, i + 1, end, "=") ||
                is_punct(P, i + 1, end, "[") || is_punct(P, i + 1, end, ":")) {
                break;
            }
            mark(P, i, SCOUT_TOKEN_TYPE);
            saw_type = 1;
            i++;
        } else {
            break;
        }
    }
    spec_end = i;

    /* declarators */
    while (i < end) {
        long j = i, name = -1, params = -1, sym = -1;
        int is_func = 0, fptr = 0, has_body;

        if (is_punct(P, i, end, ";")) return i + 1;
        while (j < end) {
            const CTok *t = CT(P, j);
            if (t->type == TK_PUNCT) {
                if (tok_is(t, TK_PUNCT, ",") || tok_is(t, TK_PUNCT, ";") || tok_is(t, TK_PUNCT, "{") ||
                    tok_is(t, TK_PUNCT, "=") || tok_is(t, TK_PUNCT, "}")) {
                    break;
                }
                if (tok_is(t, TK_PUNCT, "(")) {
                    long close = close_of(P, j, end);
                    if (name < 0 && (is_punct(P, j + 1, end, "*") || is_punct(P, j + 1, end, "^"))) {
                        long k;
                        for (k = j + 1; k < close; k++) {
                            if (is_name(P, k, end)) {
                                name = k;
                                break;
                            }
                        }
                        fptr = 1;
                    } else if (name >= 0 && !is_func && !fptr) {
                        is_func = 1;
                        params = j;
                    }
                    j = close + 1;
                    continue;
                }
                if (tok_is(t, TK_PUNCT, "[")) {
                    j = close_of(P, j, end) + 1;
                    continue;
                }
                if (tok_is(t, TK_PUNCT, ":") && ctx == CTX_STRUCT) {
                    /* a bit field */
                    j++;
                    while (j < end && !is_punct(P, j, end, ",") && !is_punct(P, j, end, ";")) {
                        j = skip_one(P, j, end);
                    }
                    continue;
                }
                j++;
                continue;
            }
            if (t->type == TK_IDENT && tok_in(t, ATTRIBUTES) && is_punct(P, j + 1, end, "(")) {
                j = close_of(P, j + 1, end) + 1;
                continue;
            }
            if (name < 0 && is_name(P, j, end)) name = j;
            j++;
        }
        if (j > end) j = end;
        has_body = is_punct(P, j, end, "{");

        if (name >= 0 && name != pre_alias) {
            int kind, tkind, decl = 0;
            CScope scope;
            long par = ctx == CTX_FILE ? -1 : parent;
            long last = has_body ? close_of(P, j, end) : j - 1;
            TextBuf b;
            if (is_typedef) {
                kind = SCOUT_SYMBOL_TYPE_ALIAS;
                tkind = SCOUT_TOKEN_TYPE;
                scope = ctx == CTX_FILE ? type_scope(P) : C_SCOPE_LOCAL;
            } else if (ctx == CTX_STRUCT) {
                kind = SCOUT_SYMBOL_FIELD;
                tkind = SCOUT_TOKEN_FIELD;
                scope = C_SCOPE_LOCAL;
            } else if (is_func) {
                kind = SCOUT_SYMBOL_FUNCTION;
                tkind = SCOUT_TOKEN_FUNCTION;
                decl = !has_body;
                scope = ctx == CTX_FILE ? (is_static ? C_SCOPE_FILE : C_SCOPE_GLOBAL) : C_SCOPE_LOCAL;
            } else {
                kind = SCOUT_SYMBOL_VARIABLE;
                tkind = SCOUT_TOKEN_VARIABLE;
                if (ctx == CTX_FILE) {
                    scope = is_static ? C_SCOPE_FILE : C_SCOPE_GLOBAL;
                    decl = is_extern;
                } else {
                    scope = C_SCOPE_LOCAL;
                }
            }
            if (last >= end) last = end - 1;
            memset(&b, 0, sizeof(b));
            if (i == spec_end) {
                tb_add_code(P, &b, start, j - 1, 0);
            } else {
                tb_add_code(P, &b, start, spec_end - 1, 0);
                tb_add_code(P, &b, i, j - 1, 1);
            }
            sym = add_symbol(P, CT(P, name), kind, scope, par, CT(P, start), CT(P, last), tb_string(&b));
            if (sym >= 0) {
                P->res->symbols[sym].declaration = decl;
                mark(P, name, tkind);
            }
        }

        if (is_punct(P, j, end, "=")) {
            j++;
            while (j < end && !is_punct(P, j, end, ",") && !is_punct(P, j, end, ";")) j = skip_one(P, j, end);
        }
        if (is_punct(P, j, end, "{")) {
            long close = close_of(P, j, end);
            if (is_func && sym >= 0 && ctx == CTX_FILE) {
                long first_local;
                parse_params(P, params, sym);
                first_local = P->res->symbol_count;
                add_func(P, sym, first_local, params, close);
                parse_block(P, j + 1, close, sym);
                if (P->nfuncs > 0 && P->funcs[P->nfuncs - 1].sym == sym) {
                    P->funcs[P->nfuncs - 1].last = P->res->symbol_count;
                }
            }
            return close + 1;
        }
        if (is_punct(P, j, end, ",")) {
            i = j + 1;
            continue;
        }
        if (is_punct(P, j, end, ";")) return j + 1;
        return j > i ? j : i + 1;
    }
    return i;
}

/* ---- Highlighting tokens ---- */

/**
 * @brief Return the highlighting kind of a symbol kind.
 * @param kind SCOUT_SYMBOL_ value.
 * @return SCOUT_TOKEN_ value, 0 for none.
 */
static int token_kind_of(int kind)
{
    switch (kind) {
    case SCOUT_SYMBOL_STRUCT:
    case SCOUT_SYMBOL_ENUM:
    case SCOUT_SYMBOL_TYPE_ALIAS:   return SCOUT_TOKEN_TYPE;
    case SCOUT_SYMBOL_FUNCTION:     return SCOUT_TOKEN_FUNCTION;
    case SCOUT_SYMBOL_MACRO:        return SCOUT_TOKEN_MACRO;
    case SCOUT_SYMBOL_ENUM_MEMBER:
    case SCOUT_SYMBOL_CONSTANT:     return SCOUT_TOKEN_CONSTANT;
    case SCOUT_SYMBOL_VARIABLE:     return SCOUT_TOKEN_VARIABLE;
    case SCOUT_SYMBOL_LABEL:        return SCOUT_TOKEN_LABEL;
    default:                        return 0;
    }
}

/**
 * @brief Hash a name.
 * @param s The name.
 * @param len Its length.
 * @return The hash.
 */
static unsigned long hash_name(const char *s, long len)
{
    unsigned long h = 5381;
    long i;
    for (i = 0; i < len; i++) h = (h * 33) ^ (unsigned char)s[i];
    return h;
}

/**
 * @brief Tell whether a symbol has a name.
 * @param s The symbol.
 * @param name The name.
 * @param len Its length.
 * @return Non-zero if it has.
 */
static int sym_is(const CSymbol *s, const char *name, long len)
{
    return strncmp(s->name, name, (size_t)len) == 0 && s->name[len] == '\0';
}

/**
 * @brief Find the highlighting kind of a name among the locals of a
 *        function.
 * @param P The parser.
 * @param f The function.
 * @param t The name's token.
 * @return SCOUT_TOKEN_ value, 0 if it is no local.
 */
static int local_kind(Parser *P, const FuncRange *f, const CTok *t)
{
    long k;
    for (k = f->sym + 1; k < f->last; k++) {
        const CSymbol *s = &P->res->symbols[k];
        if (s->kind == SCOUT_SYMBOL_FIELD || !sym_is(s, t->s, t->len)) continue;
        if (s->kind == SCOUT_SYMBOL_VARIABLE && k < f->first_local) return SCOUT_TOKEN_PARAMETER;
        return token_kind_of(s->kind);
    }
    return 0;
}

/**
 * @brief Classify every identifier and build CParseResult::tokens.
 * @param P The parser.
 */
static void make_tokens(Parser *P)
{
    CParseResult *res = P->res;
    unsigned long size = 64, h;
    long *slots;
    long i, fi = 0;

    while (size < (unsigned long)res->symbol_count * 2 + 1) size *= 2;
    slots = (long *)malloc(size * sizeof(long));
    if (!slots) {
        res->out_of_memory = 1;
        return;
    }
    for (h = 0; h < size; h++) slots[h] = -1;
    for (i = 0; i < res->symbol_count; i++) {
        const CSymbol *s = &res->symbols[i];
        long len = (long)strlen(s->name);
        if (s->scope == C_SCOPE_LOCAL || !token_kind_of(s->kind)) continue;
        for (h = hash_name(s->name, len) & (size - 1); slots[h] >= 0; h = (h + 1) & (size - 1)) {
            if (sym_is(&res->symbols[slots[h]], s->name, len)) break;
        }
        if (slots[h] < 0) slots[h] = i;
    }

    for (i = 0; i < P->ntok && !check_stop(P); i++) {
        const CTok *t = &P->tok[i];
        const CTok *prev = i > 0 ? &P->tok[i - 1] : NULL;
        int kind;
        if (t->type != TK_IDENT || tok_reserved(t)) continue;
        if (prev && (prev->flags & TF_HASH)) continue;     /* the directive's name */
        kind = P->mark[i];
        if (!kind && prev && (tok_is(prev, TK_PUNCT, ".") || tok_is(prev, TK_PUNCT, "->"))) {
            kind = SCOUT_TOKEN_FIELD;
        } else if (!kind && prev && tok_is(prev, TK_IDENT, "goto")) {
            kind = SCOUT_TOKEN_LABEL;
        } else if (!kind && prev && (tok_is(prev, TK_IDENT, "struct") || tok_is(prev, TK_IDENT, "union") ||
                                     tok_is(prev, TK_IDENT, "enum"))) {
            kind = SCOUT_TOKEN_TYPE;
        } else if (!kind) {
            while (fi < P->nfuncs && P->funcs[fi].tok_last < i) fi++;
            if (fi < P->nfuncs && i >= P->funcs[fi].tok_first) kind = local_kind(P, &P->funcs[fi], t);
            if (!kind) {
                for (h = hash_name(t->s, t->len) & (size - 1); slots[h] >= 0; h = (h + 1) & (size - 1)) {
                    if (sym_is(&res->symbols[slots[h]], t->s, t->len)) {
                        kind = token_kind_of(res->symbols[slots[h]].kind);
                        break;
                    }
                }
            }
            if (!kind && i + 1 < P->ntok && tok_is(&P->tok[i + 1], TK_PUNCT, "(") &&
                !((t->flags & TF_PP) && tok_is(t, TK_IDENT, "defined"))) {
                kind = SCOUT_TOKEN_FUNCTION;
            }
        }
        if (!kind) continue;
        if (!grow(P, (void **)&res->tokens, &P->tokvalcap, res->token_count * 4 + 3, sizeof(long))) break;
        res->tokens[res->token_count * 4] = t->line;
        res->tokens[res->token_count * 4 + 1] = t->col;
        res->tokens[res->token_count * 4 + 2] = t->len;
        res->tokens[res->token_count * 4 + 3] = kind;
        res->token_count++;
    }
    free(slots);
}

/* ---- Entry points ---- */

CParseResult *c_parser_analyze(const char *source, size_t length, int is_header)
{
    Parser P;
    CParseResult *res = (CParseResult *)calloc(1, sizeof(CParseResult));
    if (!res) return NULL;
    memset(&P, 0, sizeof(P));
    P.src = source;
    P.end = source + length;
    P.is_header = is_header;
    P.res = res;

    lex(&P);
    if (!res->stopped && !res->out_of_memory) {
        P.mark = (unsigned char *)calloc((size_t)P.ntok + 1, 1);
        if (!P.mark) res->out_of_memory = 1;
    }
    if (!res->stopped && !res->out_of_memory) directives(&P);
    if (!res->stopped && !res->out_of_memory) match_brackets(&P);
    if (!res->stopped && !res->out_of_memory) {
        long i = 0;
        while (i < P.ncode && !check_stop(&P)) {
            long n = parse_declaration(&P, i, P.ncode, CTX_FILE, -1);
            i = n > i ? n : i + 1;
        }
    }
    if (!res->stopped && !res->out_of_memory) make_tokens(&P);

    free(P.tok);
    free(P.code);
    free(P.match);
    free(P.mark);
    free(P.funcs);
    return res;
}

void c_parse_result_free(CParseResult *res)
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
