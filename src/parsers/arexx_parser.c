/**
 * @file arexx_parser.c
 * @brief The analysis of the ARexx parser of Scout: one pass over the
 *        clauses of a program.
 *
 * A clause is read as a short list of tokens (read_clause()), then looked
 * at from its first token (process_clause()). One line can hold several
 * clauses without a ';': after a label, THEN, ELSE and OTHERWISE another
 * one begins. Expressions are not parsed; ARexx evaluates them at run
 * time and all that a static look can tell is where they call something.
 *
 * Whether an ELSE has an IF is found without a tree: chain counts the IFs
 * whose THEN branch is still to come, avail those that could take an ELSE
 * now. A DO or SELECT saves both and starts afresh; its END restores them
 * and counts as one instruction.
 */
#include "arexx_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int (*arexx_parser_should_stop)(void) = NULL;

/** The instructions, in the order of the I_ values from I_ADDRESS on. */
const char *const arexx_parser_keywords[] = {
    "address", "arg", "break", "call", "do", "drop", "echo", "else", "end",
    "exit", "if", "interpret", "iterate", "leave", "nop", "numeric",
    "options", "otherwise", "parse", "procedure", "pull", "push", "queue",
    "return", "say", "select", "shell", "signal", "then", "trace", "upper",
    "when",
    NULL
};

const char *const arexx_parser_constants[] = {
    "rc", "result", "sigl",
    NULL
};

const char *const arexx_parser_functions[] = {
    "abbrev", "abs", "addlib", "address", "arg", "b2c", "bitand", "bitchg",
    "bitclr", "bitcomp", "bitor", "bitset", "bittst", "bitxor", "c2b",
    "c2d", "c2x", "center", "centre", "close", "compare", "compress",
    "copies", "d2c", "d2x", "datatype", "date", "delstr", "delword",
    "digits", "eof", "errortext", "exists", "export", "find", "form",
    "freespace", "fuzz", "getclip", "getspace", "hash", "import", "index",
    "insert", "lastpos", "left", "length", "lines", "max", "min", "open",
    "overlay", "pos", "pragma", "random", "randu", "readch", "readln",
    "remlib", "reverse", "right", "seek", "setclip", "show", "sign",
    "sourceline", "space", "storage", "strip", "substr", "subword",
    "symbol", "time", "trace", "translate", "trim", "trunc", "upper",
    "value", "verify", "word", "wordindex", "wordlength", "words",
    "writech", "writeln", "x2c", "x2d", "xrange",
    /* rexxsupport.library */
    "allocmem", "baddr", "closeport", "delay", "delete", "forbid",
    "freemem", "getarg", "getpkt", "makedir", "next", "null", "offset",
    "openport", "permit", "rename", "reply", "showdir", "showlist",
    "statef", "typepkt", "waitpkt",
    NULL
};

const char *const arexx_parser_subkeywords[] = {
    "by", "command", "digits", "engineering", "expose", "external",
    "failat", "for", "forever", "form", "fuzz", "no", "numeric", "off",
    "on", "prompt", "results", "scientific", "source", "to", "until",
    "value", "var", "version", "while", "with",
    NULL
};

/** Most diagnostics reported for one program. */
#define MAX_DIAGNOSTICS 200

/** Most names remembered per procedure as arguments or as exposed. */
#define MAX_NAMES 32

/** Clauses between two calls of arexx_parser_should_stop(). */
#define STOP_INTERVAL 64

/** The kinds of token of a clause. */
typedef enum TokenType {
    T_WORD,     /**< a symbol that can name a variable */
    T_NUMBER,   /**< a symbol starting with a digit or a dot */
    T_STRING,   /**< a string with its quotes and suffix */
    T_OP        /**< one other character */
} TokenType;

/** The first word of a clause, as far as it matters. */
typedef enum Instruction {
    I_NONE,         /**< not an instruction: a command */
    I_ADDRESS,      /**< ADDRESS */
    I_ARG,          /**< ARG */
    I_BREAK,        /**< BREAK */
    I_CALL,         /**< CALL */
    I_DO,           /**< DO */
    I_DROP,         /**< DROP */
    I_ECHO,         /**< ECHO */
    I_ELSE,         /**< ELSE */
    I_END,          /**< END */
    I_EXIT,         /**< EXIT */
    I_IF,           /**< IF */
    I_INTERPRET,    /**< INTERPRET */
    I_ITERATE,      /**< ITERATE */
    I_LEAVE,        /**< LEAVE */
    I_NOP,          /**< NOP */
    I_NUMERIC,      /**< NUMERIC */
    I_OPTIONS,      /**< OPTIONS */
    I_OTHERWISE,    /**< OTHERWISE */
    I_PARSE,        /**< PARSE */
    I_PROCEDURE,    /**< PROCEDURE */
    I_PULL,         /**< PULL */
    I_PUSH,         /**< PUSH */
    I_QUEUE,        /**< QUEUE */
    I_RETURN,       /**< RETURN */
    I_SAY,          /**< SAY */
    I_SELECT,       /**< SELECT */
    I_SHELL,        /**< SHELL */
    I_SIGNAL,       /**< SIGNAL */
    I_THEN,         /**< THEN */
    I_TRACE,        /**< TRACE */
    I_UPPER,        /**< UPPER */
    I_WHEN          /**< WHEN */
} Instruction;

/** The kinds of name checked at the end of the program. */
typedef enum JumpKind {
    JUMP_CALL,      /**< a call: may be external */
    JUMP_SIGNAL,    /**< SIGNAL name: has to be a label */
    JUMP_SIGNAL_ON  /**< SIGNAL ON name: needs a label when it happens */
} JumpKind;

/** A token of the current clause; its text stays in the source. */
typedef struct Tok {
    const char *text;   /**< its first character */
    long len;           /**< its length */
    long line;          /**< its line */
    long col;           /**< its first column */
    TokenType type;     /**< what it is */
} Tok;

/** A name in the source. */
typedef struct Name {
    const char *text;   /**< its first character */
    long len;           /**< its length */
} Name;

/** A use of a name that can only be checked at the end. */
typedef struct Jump {
    const char *text;   /**< the name */
    long len;           /**< its length */
    long line;          /**< its line */
    long col;           /**< its first column */
    JumpKind kind;      /**< what kind of use */
} Jump;

/** A label. */
typedef struct Label {
    const char *text;   /**< its name */
    long len;           /**< its length */
} Label;

/** An open DO or SELECT. */
typedef struct Block {
    long line;          /**< its line */
    long col;           /**< its column */
    int is_select;      /**< SELECT, not DO */
    int is_loop;        /**< a DO with something to repeat */
    Name var;           /**< its control variable (text NULL if none) */
    long saved_avail;   /**< avail outside the block */
    long saved_chain;   /**< chain outside the block */
    int saved_branch;   /**< branch outside the block */
} Block;

/** State of one analysis. */
typedef struct Parser {
    ArexxResult *res;       /**< the result being built */
    long symbol_cap;        /**< room for symbols */
    long diagnostic_cap;    /**< room for diagnostics */
    long token_cap;         /**< room for token values */
    const char *src;        /**< the text */
    size_t length;          /**< its length */
    size_t pos;             /**< the next character */
    size_t line_start;      /**< offset of the current line */
    long line;              /**< the current line */
    Tok *toks;              /**< the tokens of the clause */
    long tok_count;         /**< number of them */
    long tok_cap;           /**< room for them */
    Label *labels;          /**< the labels */
    long label_count;       /**< number of them */
    long label_cap;         /**< room for them */
    Jump *jumps;            /**< names to check at the end */
    long jump_count;        /**< number of them */
    long jump_cap;          /**< room for them */
    Block *blocks;          /**< the open blocks */
    long block_count;       /**< number of them */
    long block_cap;         /**< room for them */
    long avail;             /**< IFs that could take an ELSE now */
    long chain;             /**< IFs whose THEN branch is still to come */
    int branch;             /**< the next instruction is the branch of a THEN or ELSE */
    Tok need_then;          /**< the IF or WHEN still waiting for THEN (text NULL if none) */
    int need_then_when;     /**< need_then is a WHEN */
    int after_label;        /**< the previous clause was a label */
    long label_symbol;      /**< the symbol of that label, or -1 */
    long proc;              /**< the symbol of the current procedure, or -1 */
    Name params[MAX_NAMES];     /**< the arguments known here */
    long param_count;           /**< number of them */
    Name exposed[MAX_NAMES];    /**< the names the procedure exposes */
    long exposed_count;         /**< number of them */
    int has_addlib;         /**< the program calls ADDLIB */
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
static int word_is(const char *s, long len, const char *word)
{
    long i;
    for (i = 0; i < len; i++) {
        if (!word[i] || lower((unsigned char)s[i]) != word[i]) return 0;
    }
    return word[len] == '\0';
}

/**
 * @brief Compare two names without regard to case.
 * @param a A name.
 * @param alen Its length.
 * @param b Another name.
 * @param blen Its length.
 * @return Non-zero if they are equal.
 */
static int names_equal(const char *a, long alen, const char *b, long blen)
{
    long i;
    if (alen != blen) return 0;
    for (i = 0; i < alen; i++) {
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return 0;
    }
    return 1;
}

/**
 * @brief Tell whether a word is in a list.
 * @param list The list (lower case, NULL-terminated).
 * @param s The word.
 * @param len Its length.
 * @return Its index, or -1.
 */
static long find_in_list(const char *const *list, const char *s, long len)
{
    long i;
    for (i = 0; list[i]; i++) {
        if (word_is(s, len, list[i])) return i;
    }
    return -1;
}

/**
 * @brief Tell whether a character can be part of a symbol.
 * @param c The character.
 * @return Non-zero if it can.
 */
static int symbol_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '.' || c == '!' || c == '?' || c == '$' || c >= 0xc0;
}

/**
 * @brief Add a diagnostic on one line.
 * @param P The parser.
 * @param severity SCOUT_SEVERITY_ value.
 * @param line The line.
 * @param col Its first column.
 * @param end The column after it.
 * @param format The message, with at most one "%s".
 * @param name What "%s" stands for (may be NULL).
 * @param name_len Length of @p name.
 */
static void add_diagnostic(Parser *P, int severity, long line, long col, long end, const char *format,
                           const char *name, long name_len)
{
    ArexxResult *r = P->res;
    ArexxDiagnostic *d;
    char text[160], quoted[64];
    if (r->diagnostic_count >= MAX_DIAGNOSTICS) return;
    if (!grow(P, (void **)&r->diagnostics, r->diagnostic_count, &P->diagnostic_cap, sizeof(ArexxDiagnostic)))
        return;
    if (name_len > (long)sizeof(quoted) - 1) name_len = (long)sizeof(quoted) - 1;
    if (name) memcpy(quoted, name, (size_t)name_len);
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
 * @brief Add a diagnostic on a token.
 * @param P The parser.
 * @param severity SCOUT_SEVERITY_ value.
 * @param t The token (its text is what "%s" stands for).
 * @param format The message, with at most one "%s".
 */
static void diag_tok(Parser *P, int severity, const Tok *t, const char *format)
{
    add_diagnostic(P, severity, t->line, t->col, t->col + t->len, format, t->text, t->len);
}

/**
 * @brief Add a highlighting token.
 * @param P The parser.
 * @param t The token of the clause it covers.
 * @param kind SCOUT_TOKEN_ value.
 */
static void add_token(Parser *P, const Tok *t, long kind)
{
    ArexxResult *r = P->res;
    long *v;
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
    v = &r->tokens[r->token_count * 4];
    v[0] = t->line;
    v[1] = t->col;
    v[2] = t->len;
    v[3] = kind;
    r->token_count++;
}

/**
 * @brief Add a symbol.
 * @param P The parser.
 * @param kind SCOUT_SYMBOL_ value.
 * @param t The token naming it.
 * @param name_len Length of its name in the token.
 * @param end_col The column after its definition.
 * @param parent The symbol it belongs to, or -1.
 * @param detail Text for the lists (copied; may be NULL).
 * @return Its index, or -1 if memory ran out.
 */
static long add_symbol(Parser *P, int kind, const Tok *t, long name_len, long end_col, long parent,
                       const char *detail)
{
    ArexxResult *r = P->res;
    ArexxSymbol *s;
    if (!grow(P, (void **)&r->symbols, r->symbol_count, &P->symbol_cap, sizeof(ArexxSymbol))) return -1;
    s = &r->symbols[r->symbol_count];
    memset(s, 0, sizeof(*s));
    s->name = dup_text(t->text, (size_t)name_len);
    if (detail) s->detail = dup_text(detail, strlen(detail));
    if (!s->name || (detail && !s->detail)) {
        free(s->name);
        free(s->detail);
        r->out_of_memory = 1;
        return -1;
    }
    s->kind = kind;
    s->parent = parent;
    s->range.start_line = s->range.end_line = t->line;
    s->selection = s->range;
    s->range.start_col = t->col;
    s->range.end_col = end_col;
    s->selection.start_col = t->col;
    s->selection.end_col = t->col + t->len;
    return r->symbol_count++;
}

/**
 * @brief Go to the next line; the position is at a line end.
 * @param P The parser.
 */
static void next_line(Parser *P)
{
    if (P->src[P->pos] == '\r' && P->pos + 1 < P->length && P->src[P->pos + 1] == '\n') P->pos++;
    P->pos++;
    P->line++;
    P->line_start = P->pos;
}

/**
 * @brief The column of the current position.
 * @param P The parser.
 * @return The column (from 1).
 */
static long column(const Parser *P)
{
    return (long)(P->pos - P->line_start) + 1;
}

/**
 * @brief Skip a comment, which may hold others; the position is at its
 *        start.
 * @param P The parser.
 */
static void skip_comment(Parser *P)
{
    long line = P->line, col = column(P), depth = 1;
    P->pos += 2;
    while (P->pos < P->length) {
        char c = P->src[P->pos];
        char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
        if (c == '/' && d == '*') {
            depth++;
            P->pos += 2;
        } else if (c == '*' && d == '/') {
            P->pos += 2;
            if (--depth == 0) return;
        } else if (c == '\n' || c == '\r') {
            next_line(P);
        } else {
            P->pos++;
        }
    }
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, col, col + 2, "comment is not closed", NULL, 0);
}

/**
 * @brief Add a token of the current clause and step over it.
 * @param P The parser.
 * @param type What it is.
 * @param len Its length.
 */
static void push_tok(Parser *P, TokenType type, size_t len)
{
    Tok *t;
    if (grow(P, (void **)&P->toks, P->tok_count, &P->tok_cap, sizeof(Tok))) {
        t = &P->toks[P->tok_count++];
        t->text = P->src + P->pos;
        t->len = (long)len;
        t->line = P->line;
        t->col = column(P);
        t->type = type;
    }
    P->pos += len;
}

/**
 * @brief Check what a hexadecimal or binary string holds.
 * @param s Its contents.
 * @param len Their length.
 * @param binary Binary, not hexadecimal.
 * @return Non-zero if only digits of that kind and blanks are there.
 */
static int digits_valid(const char *s, size_t len, int binary)
{
    size_t i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t') continue;
        if (binary ? c != '0' && c != '1'
                   : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

/**
 * @brief Read a string, with the x or b that may follow it; the position
 *        is at its opening quote. A doubled quote stays inside.
 * @param P The parser.
 */
static void scan_string(Parser *P)
{
    char quote = P->src[P->pos];
    size_t i = P->pos + 1;
    int closed = 0;
    while (i < P->length && P->src[i] != '\n' && P->src[i] != '\r') {
        if (P->src[i] == quote) {
            if (i + 1 < P->length && P->src[i + 1] == quote) {
                i += 2;
                continue;
            }
            closed = 1;
            i++;
            break;
        }
        i++;
    }
    if (!closed) {
        add_diagnostic(P, SCOUT_SEVERITY_WARNING, P->line, column(P), column(P) + (long)(i - P->pos),
                       "string is not closed on this line", NULL, 0);
    } else if (i < P->length && (lower((unsigned char)P->src[i]) == 'x' || lower((unsigned char)P->src[i]) == 'b') &&
               (i + 1 >= P->length || !symbol_char((unsigned char)P->src[i + 1]))) {
        int binary = lower((unsigned char)P->src[i]) == 'b';
        if (!digits_valid(P->src + P->pos + 1, i - P->pos - 2, binary)) {
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, P->line, column(P), column(P) + (long)(i + 1 - P->pos),
                           binary ? "invalid binary string" : "invalid hexadecimal string", NULL, 0);
        }
        i++;
    }
    push_tok(P, T_STRING, i - P->pos);
}

/**
 * @brief Read the next clause into the list of tokens.
 * @param P The parser.
 * @return Non-zero if there is a clause with at least one token.
 */
static int read_clause(Parser *P)
{
    P->tok_count = 0;
    while (P->pos < P->length && !P->res->out_of_memory) {
        char c = P->src[P->pos];
        char d = P->pos + 1 < P->length ? P->src[P->pos + 1] : '\0';
        if (c == '\n' || c == '\r') {
            next_line(P);
            if (P->tok_count > 0) {
                const Tok *last = &P->toks[P->tok_count - 1];
                if (last->type != T_OP || last->text[0] != ',') return 1;
                P->tok_count--;     /* a comma at the line end: the clause goes on */
            }
        } else if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
            P->pos++;
        } else if (c == '/' && d == '*') {
            skip_comment(P);
        } else if (c == ';') {
            P->pos++;
            if (P->tok_count > 0) return 1;
        } else if (c == '\'' || c == '"') {
            scan_string(P);
        } else if (symbol_char((unsigned char)c)) {
            size_t i = P->pos + 1;
            while (i < P->length && symbol_char((unsigned char)P->src[i])) i++;
            push_tok(P, (c >= '0' && c <= '9') || c == '.' ? T_NUMBER : T_WORD, i - P->pos);
        } else {
            push_tok(P, T_OP, 1);
        }
    }
    return P->tok_count > 0;
}

/**
 * @brief Tell whether a token is a given operator character.
 * @param t The token.
 * @param c The character.
 * @return Non-zero if it is.
 */
static int is_op(const Tok *t, char c)
{
    return t->type == T_OP && t->text[0] == c;
}

/**
 * @brief Tell whether a token follows another without anything between.
 * @param a The first token.
 * @param b The token after it.
 * @return Non-zero if it does.
 */
static int glued(const Tok *a, const Tok *b)
{
    return a->text + a->len == b->text;
}

/**
 * @brief Tell whether the clause's parentheses are balanced, reporting the
 *        first one that is not.
 * @param P The parser.
 */
static void check_parens(Parser *P)
{
    long i, depth = 0;
    const Tok *open = NULL;
    for (i = 0; i < P->tok_count; i++) {
        const Tok *t = &P->toks[i];
        if (is_op(t, '(')) {
            if (depth++ == 0) open = t;
        } else if (is_op(t, ')')) {
            if (depth == 0) {
                diag_tok(P, SCOUT_SEVERITY_ERROR, t, "')' without '('");
                return;
            }
            depth--;
        }
    }
    if (depth > 0) diag_tok(P, SCOUT_SEVERITY_ERROR, open, "'(' is not closed in this clause");
}

/**
 * @brief Find a word outside parentheses.
 * @param P The parser.
 * @param i The first token to look at.
 * @param n The token after the last.
 * @param word The word (lower case).
 * @return Its index, or -1.
 */
static long find_word(const Parser *P, long i, long n, const char *word)
{
    long depth = 0;
    for (; i < n; i++) {
        const Tok *t = &P->toks[i];
        if (is_op(t, '(')) depth++;
        else if (is_op(t, ')')) depth--;
        else if (depth <= 0 && t->type == T_WORD && word_is(t->text, t->len, word)) return i;
    }
    return -1;
}

/**
 * @brief Note a name for a check at the end of the program.
 * @param P The parser.
 * @param t Its token.
 * @param kind What kind of use.
 */
static void add_jump(Parser *P, const Tok *t, JumpKind kind)
{
    Jump *j;
    if (!grow(P, (void **)&P->jumps, P->jump_count, &P->jump_cap, sizeof(Jump))) return;
    j = &P->jumps[P->jump_count++];
    j->text = t->text;
    j->len = t->len;
    j->line = t->line;
    j->col = t->col;
    j->kind = kind;
}

/**
 * @brief Note a call of a function or subroutine.
 * @param P The parser.
 * @param t The token naming it.
 */
static void note_call(Parser *P, const Tok *t)
{
    add_token(P, t, SCOUT_TOKEN_FUNCTION);
    if (word_is(t->text, t->len, "addlib")) P->has_addlib = 1;
    if (find_in_list(arexx_parser_functions, t->text, t->len) < 0) add_jump(P, t, JUMP_CALL);
}

/**
 * @brief Tell whether a name is an argument known here.
 * @param P The parser.
 * @param t The token.
 * @return Non-zero if it is.
 */
static int is_param(const Parser *P, const Tok *t)
{
    long i;
    for (i = 0; i < P->param_count; i++) {
        if (names_equal(P->params[i].text, P->params[i].len, t->text, t->len)) return 1;
    }
    return 0;
}

/**
 * @brief Look at one word of an expression.
 * @param P The parser.
 * @param i Its index.
 * @param n The token after the clause's or expression's last.
 */
static void scan_word(Parser *P, long i, long n)
{
    const Tok *t = &P->toks[i];
    if (i + 1 < n && is_op(&P->toks[i + 1], '(') && glued(t, &P->toks[i + 1])) note_call(P, t);
    else if (is_param(P, t)) add_token(P, t, SCOUT_TOKEN_PARAMETER);
}

/**
 * @brief Look at an expression: its function calls and arguments.
 * @param P The parser.
 * @param i The first token.
 * @param n The token after the last.
 */
static void scan_expr(Parser *P, long i, long n)
{
    for (; i < n; i++) {
        if (P->toks[i].type == T_WORD) scan_word(P, i, n);
    }
}

/**
 * @brief Add a variable at its first assignment.
 * @param P The parser.
 * @param t The token naming it (of a compound symbol, only the stem counts).
 * @param detail What assigns it, for the lists (may be NULL).
 */
static void define_variable(Parser *P, const Tok *t, const char *detail)
{
    const ArexxResult *r = P->res;
    const char *dot = (const char *)memchr(t->text, '.', (size_t)t->len);
    long len = dot ? (long)(dot - t->text) + 1 : t->len;
    long i, parent = P->proc;
    for (i = 0; i < P->exposed_count; i++) {
        if (names_equal(P->exposed[i].text, P->exposed[i].len, t->text, len)) {
            parent = -1;
            break;
        }
    }
    for (i = 0; i < r->symbol_count; i++) {
        const ArexxSymbol *s = &r->symbols[i];
        if (s->kind == SCOUT_SYMBOL_VARIABLE && s->parent == parent &&
            names_equal(s->name, (long)strlen(s->name), t->text, len))
            return;
    }
    add_symbol(P, SCOUT_SYMBOL_VARIABLE, t, len, t->col + t->len, parent, detail);
}

/**
 * @brief Look at a parsing template: its symbols are assigned.
 * @param P The parser.
 * @param i The first token.
 * @param n The token after the last.
 * @param params Its symbols are the arguments of the program or procedure.
 * @param detail What assigns them, for the lists.
 */
static void scan_template(Parser *P, long i, long n, int params, const char *detail)
{
    for (; i < n; i++) {
        const Tok *t = &P->toks[i];
        if (is_op(t, '(')) {
            long depth = 1, start = ++i;
            while (i < n && depth > 0) {
                if (is_op(&P->toks[i], '(')) depth++;
                else if (is_op(&P->toks[i], ')')) depth--;
                i++;
            }
            scan_expr(P, start, depth == 0 ? i - 1 : i);
            i--;
        } else if (t->type == T_WORD) {
            if (params) {
                if (P->param_count < MAX_NAMES && !is_param(P, t)) {
                    P->params[P->param_count].text = t->text;
                    P->params[P->param_count].len = t->len;
                    P->param_count++;
                }
                add_token(P, t, SCOUT_TOKEN_PARAMETER);
            }
            define_variable(P, t, detail);
        }
    }
}

/**
 * @brief Mark a word as having a meaning only in its instruction, if the
 *        token at that index is a word.
 * @param P The parser.
 * @param i The token's index.
 * @param n The token after the last of the clause.
 * @return Non-zero if a word was marked.
 */
static int mark_subkeyword(Parser *P, long i, long n)
{
    if (i >= n || P->toks[i].type != T_WORD) return 0;
    add_token(P, &P->toks[i], SCOUT_TOKEN_CONSTANT);
    return 1;
}

/**
 * @brief Count an instruction as done, for what ELSE may follow it.
 * @param P The parser.
 */
static void finish_instruction(Parser *P)
{
    if (!P->branch) P->avail = 0;
    P->avail += P->chain;
    P->chain = 0;
    P->branch = 0;
}

/**
 * @brief Take a THEN of an IF or WHEN.
 * @param P The parser.
 * @param is_when It belongs to a WHEN.
 */
static void take_then(Parser *P, int is_when)
{
    if (is_when) {
        P->avail = 0;
        P->chain = 0;
    } else {
        P->chain++;
    }
    P->branch = 1;
}

/**
 * @brief Open a DO or SELECT.
 * @param P The parser.
 * @param t The token of the instruction.
 * @param is_select SELECT, not DO.
 * @param is_loop A DO with something to repeat.
 * @param var Its control variable (NULL if none).
 */
static void open_block(Parser *P, const Tok *t, int is_select, int is_loop, const Tok *var)
{
    Block *b;
    if (!grow(P, (void **)&P->blocks, P->block_count, &P->block_cap, sizeof(Block))) return;
    b = &P->blocks[P->block_count++];
    b->line = t->line;
    b->col = t->col;
    b->is_select = is_select;
    b->is_loop = is_loop;
    b->var.text = var ? var->text : NULL;
    b->var.len = var ? var->len : 0;
    b->saved_avail = P->avail;
    b->saved_chain = P->chain;
    b->saved_branch = P->branch;
    P->avail = 0;
    P->chain = 0;
    P->branch = 0;
}

/**
 * @brief Forget what belongs to the routine that ended.
 * @param P The parser.
 */
static void close_routine(Parser *P)
{
    P->proc = -1;
    P->param_count = 0;
    P->exposed_count = 0;
}

/**
 * @brief Handle a label.
 * @param P The parser.
 * @param i The index of its name; the ':' follows.
 */
static void define_label(Parser *P, long i)
{
    const Tok *t = &P->toks[i];
    long k;
    P->after_label = 1;
    P->label_symbol = -1;
    add_token(P, t, SCOUT_TOKEN_FUNCTION);
    for (k = 0; k < P->label_count; k++) {
        if (names_equal(P->labels[k].text, P->labels[k].len, t->text, t->len)) {
            diag_tok(P, SCOUT_SEVERITY_WARNING, t, "label '%s' is defined twice; the first one is used");
            return;
        }
    }
    if (grow(P, (void **)&P->labels, P->label_count, &P->label_cap, sizeof(Label))) {
        P->labels[P->label_count].text = t->text;
        P->labels[P->label_count].len = t->len;
        P->label_count++;
    }
    P->label_symbol = add_symbol(P, SCOUT_SYMBOL_LABEL, t, t->len, P->toks[i + 1].col + 1, -1, NULL);
}

/**
 * @brief Handle a PROCEDURE instruction.
 * @param P The parser.
 * @param i The index of the instruction.
 * @param n The token after the last of the clause.
 * @param was_label The clause before it was a label.
 */
static void do_procedure(Parser *P, long i, long n, int was_label)
{
    const Tok *t = &P->toks[i];
    if (!was_label) {
        diag_tok(P, SCOUT_SEVERITY_ERROR, t, "PROCEDURE must follow a label");
        return;
    }
    close_routine(P);
    if (P->label_symbol >= 0) {
        ArexxSymbol *s = &P->res->symbols[P->label_symbol];
        const Tok *last = &P->toks[n - 1];
        s->kind = SCOUT_SYMBOL_PROCEDURE;
        P->proc = P->label_symbol;
        if (last->line == t->line && last->text + last->len - t->text < 80 && !s->detail) {
            s->detail = dup_text(t->text, (size_t)(last->text + last->len - t->text));
            if (!s->detail) P->res->out_of_memory = 1;
        }
    }
    if (i + 1 < n && P->toks[i + 1].type == T_WORD && word_is(P->toks[i + 1].text, P->toks[i + 1].len, "expose")) {
        mark_subkeyword(P, i + 1, n);
        for (i += 2; i < n; i++) {
            const Tok *e = &P->toks[i];
            if (e->type == T_WORD && P->exposed_count < MAX_NAMES) {
                P->exposed[P->exposed_count].text = e->text;
                P->exposed[P->exposed_count].len = e->len;
                P->exposed_count++;
            }
        }
    }
}

/**
 * @brief Handle a DO instruction.
 * @param P The parser.
 * @param i The index of the instruction.
 * @param n The token after the last of the clause.
 */
static void do_do(Parser *P, long i, long n)
{
    static const char *const words[] = { "to", "by", "for", "while", "until", "forever", NULL };
    const Tok *t = &P->toks[i];
    const Tok *var = NULL;
    int is_loop = i + 1 < n;
    long depth = 0;
    i++;
    if (i + 1 < n && P->toks[i].type == T_WORD && is_op(&P->toks[i + 1], '=')) {
        var = &P->toks[i];
        if (is_param(P, var)) add_token(P, var, SCOUT_TOKEN_PARAMETER);
        define_variable(P, var, "DO");
        i += 2;
    }
    for (; i < n; i++) {
        const Tok *w = &P->toks[i];
        if (is_op(w, '(')) depth++;
        else if (is_op(w, ')')) depth--;
        else if (w->type == T_WORD) {
            if (depth <= 0 && find_in_list(words, w->text, w->len) >= 0) mark_subkeyword(P, i, n);
            else scan_word(P, i, n);
        }
    }
    open_block(P, t, 0, is_loop, var);
}

/**
 * @brief Handle an END instruction.
 * @param P The parser.
 * @param i The index of the instruction.
 * @param n The token after the last of the clause.
 */
static void do_end(Parser *P, long i, long n)
{
    const Tok *t = &P->toks[i];
    const Tok *name = i + 1 < n && P->toks[i + 1].type == T_WORD ? &P->toks[i + 1] : NULL;
    const Block *b;
    if (P->block_count == 0) {
        diag_tok(P, SCOUT_SEVERITY_ERROR, t, "END without DO or SELECT");
        finish_instruction(P);
        return;
    }
    b = &P->blocks[--P->block_count];
    if (name && b->var.text && !names_equal(name->text, name->len, b->var.text, b->var.len))
        diag_tok(P, SCOUT_SEVERITY_ERROR, name, "END %s does not match the loop variable");
    P->avail = b->saved_avail;
    P->chain = b->saved_chain;
    P->branch = b->saved_branch;
    finish_instruction(P);
}

/**
 * @brief Handle a PARSE instruction.
 * @param P The parser.
 * @param i The index of the instruction.
 * @param n The token after the last of the clause.
 */
static void do_parse(Parser *P, long i, long n)
{
    const Tok *w;
    i++;
    if (i < n && P->toks[i].type == T_WORD && word_is(P->toks[i].text, P->toks[i].len, "upper")) i++;
    if (i >= n || P->toks[i].type != T_WORD) return;
    w = &P->toks[i];
    mark_subkeyword(P, i, n);
    i++;
    if (word_is(w->text, w->len, "arg")) {
        scan_template(P, i, n, 1, "ARG");
    } else if (word_is(w->text, w->len, "value")) {
        long with = find_word(P, i, n, "with");
        scan_expr(P, i, with < 0 ? n : with);
        if (with >= 0) {
            mark_subkeyword(P, with, n);
            scan_template(P, with + 1, n, 0, "PARSE");
        }
    } else if (word_is(w->text, w->len, "var")) {
        if (i < n && P->toks[i].type == T_WORD) scan_word(P, i, n);
        scan_template(P, i + 1, n, 0, "PARSE");
    } else {
        scan_template(P, i, n, 0, "PARSE");
    }
}

/**
 * @brief Handle a SIGNAL instruction.
 * @param P The parser.
 * @param i The index of the instruction.
 * @param n The token after the last of the clause.
 */
static void do_signal(Parser *P, long i, long n)
{
    const Tok *t;
    i++;
    if (i >= n) return;
    t = &P->toks[i];
    if (t->type != T_WORD) {
        scan_expr(P, i, n);
    } else if (word_is(t->text, t->len, "on") || word_is(t->text, t->len, "off")) {
        int on = word_is(t->text, t->len, "on");
        mark_subkeyword(P, i, n);
        if (mark_subkeyword(P, i + 1, n) && on) add_jump(P, &P->toks[i + 1], JUMP_SIGNAL_ON);
    } else if (word_is(t->text, t->len, "value")) {
        mark_subkeyword(P, i, n);
        scan_expr(P, i + 1, n);
    } else {
        add_token(P, t, SCOUT_TOKEN_FUNCTION);
        add_jump(P, t, JUMP_SIGNAL);
    }
}

/**
 * @brief Handle an instruction.
 * @param P The parser.
 * @param instr Which one.
 * @param i The index of its word.
 * @param n The token after the last of the clause.
 * @param was_label The clause before it was a label.
 * @return The index where the next clause begins (@p n if none on this
 *         line).
 */
static long do_instruction(Parser *P, Instruction instr, long i, long n, int was_label)
{
    const Tok *t = &P->toks[i];
    long k;
    switch (instr) {
    case I_THEN:
        if (P->need_then.text) {
            take_then(P, P->need_then_when);
            P->need_then.text = NULL;
        } else {
            diag_tok(P, SCOUT_SEVERITY_ERROR, t, "THEN without IF or WHEN");
            P->branch = 1;
        }
        return i + 1;
    case I_IF:
    case I_WHEN:
        if (instr == I_WHEN && (P->block_count == 0 || !P->blocks[P->block_count - 1].is_select))
            diag_tok(P, SCOUT_SEVERITY_ERROR, t, "WHEN outside SELECT");
        if (instr == I_IF && !P->branch) P->avail = 0;
        k = find_word(P, i + 1, n, "then");
        scan_expr(P, i + 1, k < 0 ? n : k);
        if (k < 0) {
            P->need_then = *t;
            P->need_then_when = instr == I_WHEN;
            return n;
        }
        take_then(P, instr == I_WHEN);
        return k + 1;
    case I_ELSE:
        if (P->avail == 0) diag_tok(P, SCOUT_SEVERITY_ERROR, t, "ELSE without IF");
        else P->avail--;
        P->branch = 1;
        return i + 1;
    case I_OTHERWISE:
        if (P->block_count == 0 || !P->blocks[P->block_count - 1].is_select)
            diag_tok(P, SCOUT_SEVERITY_ERROR, t, "OTHERWISE outside SELECT");
        P->avail = 0;
        P->chain = 0;
        P->branch = 0;
        return i + 1;
    case I_DO:
        do_do(P, i, n);
        return n;
    case I_SELECT:
        open_block(P, t, 1, 0, NULL);
        return n;
    case I_END:
        do_end(P, i, n);
        return n;
    default:
        break;
    }
    switch (instr) {
    case I_PROCEDURE:
        do_procedure(P, i, n, was_label);
        break;
    case I_ARG:
        scan_template(P, i + 1, n, 1, "ARG");
        break;
    case I_PULL:
        scan_template(P, i + 1, n, 0, "PULL");
        break;
    case I_PARSE:
        do_parse(P, i, n);
        break;
    case I_SIGNAL:
        do_signal(P, i, n);
        break;
    case I_CALL:
        if (i + 1 < n && P->toks[i + 1].type == T_WORD) {
            note_call(P, &P->toks[i + 1]);
            scan_expr(P, i + 2, n);
        } else {
            scan_expr(P, i + 1, n);
        }
        break;
    case I_ADDRESS:
    case I_SHELL:
    case I_TRACE:
    case I_NUMERIC:
    case I_OPTIONS:
        /* the first word names the host or the option; TRACE's letters
         * and OPTIONS' words are no expressions */
        if (i + 1 < n && P->toks[i + 1].type == T_WORD) {
            const Tok *w = &P->toks[i + 1];
            if (instr != I_TRACE || word_is(w->text, w->len, "value")) mark_subkeyword(P, i + 1, n);
            k = i + 2;
            if ((word_is(w->text, w->len, "form") && instr == I_NUMERIC) ||
                (word_is(w->text, w->len, "no") && instr == I_OPTIONS)) {
                if (mark_subkeyword(P, k, n)) k++;
            }
            if (instr != I_TRACE || word_is(w->text, w->len, "value")) scan_expr(P, k, n);
        } else if (instr != I_TRACE) {
            scan_expr(P, i + 1, n);
        }
        break;
    case I_LEAVE:
    case I_ITERATE:
        for (k = 0; k < P->block_count && !P->blocks[k].is_loop; k++) ;
        if (k == P->block_count) diag_tok(P, SCOUT_SEVERITY_ERROR, t, "%s outside a loop");
        break;
    case I_RETURN:
    case I_EXIT:
        scan_expr(P, i + 1, n);
        if (!P->branch && P->block_count == 0) close_routine(P);
        break;
    default:
        scan_expr(P, i + 1, n);
        break;
    }
    finish_instruction(P);
    return n;
}

/**
 * @brief Report an IF or WHEN that never got its THEN.
 * @param P The parser.
 */
static void report_missing_then(Parser *P)
{
    if (!P->need_then.text) return;
    diag_tok(P, SCOUT_SEVERITY_ERROR, &P->need_then, "%s without THEN");
    P->need_then.text = NULL;
}

/**
 * @brief Look at the clause just read.
 * @param P The parser.
 */
static void process_clause(Parser *P)
{
    long i = 0, n = P->tok_count;
    check_parens(P);
    while (i < n) {
        const Tok *t = &P->toks[i];
        Instruction instr = I_NONE;
        int was_label = P->after_label;
        int label = 0, assign = 0;
        P->after_label = 0;
        if (t->type == T_WORD && i + 1 < n) {
            label = is_op(&P->toks[i + 1], ':');
            assign = is_op(&P->toks[i + 1], '=') &&
                     !(i + 2 < n && is_op(&P->toks[i + 2], '=') && glued(&P->toks[i + 1], &P->toks[i + 2]));
        }
        if (t->type == T_WORD && !label && !assign) {
            long k = find_in_list(arexx_parser_keywords, t->text, t->len);
            if (k >= 0) instr = (Instruction)(I_ADDRESS + k);
        }
        if (instr != I_THEN) report_missing_then(P);
        if (label) {
            define_label(P, i);
            i += 2;
        } else if (assign) {
            if (is_param(P, t)) add_token(P, t, SCOUT_TOKEN_PARAMETER);
            define_variable(P, t, NULL);
            scan_expr(P, i + 2, n);
            finish_instruction(P);
            return;
        } else if (instr != I_NONE) {
            i = do_instruction(P, instr, i, n, was_label);
        } else {
            scan_expr(P, i, n);     /* a command for the host */
            finish_instruction(P);
            return;
        }
    }
}

/**
 * @brief Check what can only be checked at the end of the program.
 * @param P The parser.
 */
static void finish_program(Parser *P)
{
    long i, k;
    report_missing_then(P);
    for (i = 0; i < P->block_count; i++) {
        const Block *b = &P->blocks[i];
        add_diagnostic(P, SCOUT_SEVERITY_ERROR, b->line, b->col, b->col + (b->is_select ? 6 : 2),
                       b->is_select ? "SELECT without END" : "DO without END", NULL, 0);
    }
    for (i = 0; i < P->jump_count; i++) {
        const Jump *j = &P->jumps[i];
        for (k = 0; k < P->label_count; k++) {
            if (names_equal(P->labels[k].text, P->labels[k].len, j->text, j->len)) break;
        }
        if (k < P->label_count) continue;
        if (j->kind == JUMP_SIGNAL) {
            add_diagnostic(P, SCOUT_SEVERITY_ERROR, j->line, j->col, j->col + j->len,
                           "SIGNAL to a label that does not exist: %s", j->text, j->len);
        } else if (j->kind == JUMP_SIGNAL_ON) {
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, j->line, j->col, j->col + j->len,
                           "SIGNAL ON %s has no label of that name", j->text, j->len);
        } else if (!P->has_addlib) {
            add_diagnostic(P, SCOUT_SEVERITY_HINT, j->line, j->col, j->col + j->len,
                           "%s is neither a label of this program nor a built-in function", j->text, j->len);
        }
    }
}

/**
 * @brief Compare two diagnostics by place, for qsort().
 * @param a A diagnostic.
 * @param b Another.
 * @return Negative, zero or positive.
 */
static int compare_diagnostics(const void *a, const void *b)
{
    const ArexxDiagnostic *x = (const ArexxDiagnostic *)a;
    const ArexxDiagnostic *y = (const ArexxDiagnostic *)b;
    if (x->range.start_line != y->range.start_line) return x->range.start_line < y->range.start_line ? -1 : 1;
    if (x->range.start_col != y->range.start_col) return x->range.start_col < y->range.start_col ? -1 : 1;
    return strcmp(x->message, y->message);
}

/**
 * @brief Warn if the program does not begin with a comment, which ARexx
 *        takes as the mark of a program.
 * @param P The parser.
 */
static void check_start(Parser *P)
{
    size_t i = 0;
    while (i < P->length && strchr(" \t\r\n\f\v", P->src[i]) && P->src[i] != '\0') i++;
    if (i >= P->length) return;
    if (P->src[i] == '/' && i + 1 < P->length && P->src[i + 1] == '*') return;
    add_diagnostic(P, SCOUT_SEVERITY_WARNING, 1, 1, 2, "an ARexx program must begin with a comment", NULL, 0);
}

ArexxResult *arexx_parser_analyze(const char *source, size_t length)
{
    Parser P;
    long clauses = 0;
    memset(&P, 0, sizeof(P));
    P.res = (ArexxResult *)calloc(1, sizeof(ArexxResult));
    if (!P.res) return NULL;
    P.src = source;
    P.length = length;
    P.line = 1;
    P.proc = -1;
    P.label_symbol = -1;
    check_start(&P);
    while (read_clause(&P)) {
        if ((++clauses % STOP_INTERVAL) == 0 && arexx_parser_should_stop && arexx_parser_should_stop()) {
            P.res->stopped = 1;
            break;
        }
        process_clause(&P);
    }
    if (!P.res->stopped) finish_program(&P);
    free(P.toks);
    free(P.labels);
    free(P.jumps);
    free(P.blocks);
    if (P.res->diagnostic_count > 1) {
        qsort(P.res->diagnostics, (size_t)P.res->diagnostic_count, sizeof(ArexxDiagnostic), compare_diagnostics);
    }
    return P.res;
}

void arexx_result_free(ArexxResult *res)
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
