/**
 * @file json_parser.c
 * @brief The analysis of the JSON parser of Scout: one pass over the
 *        tokens of a file with a stack of the open objects and arrays.
 *
 * A small scanner of its own finds the tokens with their lines and
 * columns (json.c knows no positions); a state machine checks their order.
 * The first syntax error ends the check: in JSON a missing comma or
 * bracket makes everything after it look wrong. Up to there, every key is
 * a token, the keys of the first two levels become symbols, and a key
 * defined twice in an object is a warning.
 */
#include "json_parser.h"

#include <stdlib.h>
#include <string.h>

int (*json_parser_should_stop)(void) = NULL;

/** Longest text of JsonDocSymbol::detail (longer ones end with "..."). */
#define DETAIL_MAX  60
/** Deepest nesting of objects and arrays that is checked. */
#define DEPTH_MAX   1000

/** What a token is. */
enum {
    T_EOF,          /**< the end of the file */
    T_LBRACE,       /**< { */
    T_RBRACE,       /**< } */
    T_LBRACKET,     /**< [ */
    T_RBRACKET,     /**< ] */
    T_COLON,        /**< : */
    T_COMMA,        /**< , */
    T_STRING,       /**< a string with its quotes */
    T_NUMBER,       /**< a number */
    T_LITERAL,      /**< true, false or null */
    T_ERROR         /**< something that is no token (reported) */
};

/** What the checker expects next. */
enum {
    S_START,        /**< the value of the file, or its end if it is empty */
    S_VALUE,        /**< a value (after a key's ':') */
    S_ARR_FIRST,    /**< a value or ']' (after '[') */
    S_ARR_COMMA,    /**< a value (after a ',' in an array) */
    S_ARR_NEXT,     /**< ',' or ']' */
    S_OBJ_FIRST,    /**< a key or '}' (after '{') */
    S_KEY,          /**< a key (after a ',' in an object) */
    S_COLON,        /**< ':' after a key */
    S_OBJ_NEXT,     /**< ',' or '}' */
    S_END           /**< the end of the file */
};

/** A token: always within one line. */
typedef struct Token {
    int type;           /**< T_ value */
    size_t start;       /**< its first byte */
    size_t end;         /**< the byte after it */
    long line;          /**< its line */
    long col;           /**< its column */
} Token;

/** An open object or array. */
typedef struct Frame {
    char open;          /**< '{' or '[' */
    long line;          /**< line of the bracket */
    long col;           /**< its column */
    int level;          /**< for an object: level of its keys as symbols (1, 2), 0 for none */
    long parent;        /**< the symbol of the key whose value it is, or -1 */
    long member;        /**< the symbol of the key whose value is being read, or -1 */
    char **keys;        /**< for an object: its keys so far (allocated) */
    long key_count;     /**< number of keys */
    long key_cap;       /**< room for keys */
} Frame;

/** State of one analysis. */
typedef struct Parser {
    JsonDocResult *res;     /**< the result being built */
    const char *s;          /**< the text */
    size_t len;             /**< its length */
    size_t i;               /**< the scanner's position */
    long line;              /**< its line */
    long col;               /**< its column */
    int failed;             /**< a syntax error was reported */
    long symbol_cap;        /**< room for symbols */
    long diagnostic_cap;    /**< room for diagnostics */
    long token_cap;         /**< room for token values */
    Frame *stack;           /**< the open objects and arrays */
    long depth;             /**< their number */
    long stack_cap;         /**< room for them */
    int state;              /**< S_ value */
    Token comma;            /**< the last comma, for "comma before ']'" */
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
 * @brief Add a diagnostic within one line.
 * @param P The parser.
 * @param severity SCOUT_SEVERITY_ value.
 * @param line Its line.
 * @param col Its first column.
 * @param len Its length (at least 1 is marked).
 * @param prefix The start of the message.
 * @param text What follows it (may be NULL).
 * @param text_len Length of @p text.
 */
static void add_diagnostic(Parser *P, int severity, long line, long col, long len,
                           const char *prefix, const char *text, size_t text_len)
{
    JsonDocResult *r = P->res;
    void *array = r->diagnostics;
    JsonDocDiagnostic *d;
    size_t n = strlen(prefix), m = text ? text_len : 0;
    char *message = (char *)malloc(n + m + 1);
    if (!message) {
        r->out_of_memory = 1;
        return;
    }
    memcpy(message, prefix, n);
    if (m) memcpy(message + n, text, m);
    message[n + m] = '\0';
    if (!grow(P, &array, r->diagnostic_count, &P->diagnostic_cap, sizeof(JsonDocDiagnostic))) {
        free(message);
        return;
    }
    r->diagnostics = (JsonDocDiagnostic *)array;
    d = &r->diagnostics[r->diagnostic_count++];
    d->message = message;
    d->severity = severity;
    d->range.start_line = d->range.end_line = line;
    d->range.start_col = col;
    d->range.end_col = col + (len > 0 ? len : 1);
}

/**
 * @brief Report the syntax error that ends the check.
 * @param P The parser.
 * @param line Its line.
 * @param col Its first column.
 * @param len Its length.
 * @param message The message.
 */
static void fail(Parser *P, long line, long col, long len, const char *message)
{
    if (P->failed) return;
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, line, col, len, message, NULL, 0);
    P->failed = 1;
}

/**
 * @brief Report the syntax error that ends the check, at a token.
 * @param P The parser.
 * @param t The token.
 * @param message The message.
 */
static void fail_at(Parser *P, const Token *t, const char *message)
{
    fail(P, t->line, t->col, (long)(t->end - t->start), message);
}

/**
 * @brief Report a syntax error naming the text of a token (shortened).
 * @param P The parser.
 * @param t The token.
 * @param prefix The start of the message.
 * @param suffix What follows the text (may be "").
 */
static void fail_with_text(Parser *P, const Token *t, const char *prefix, const char *suffix)
{
    size_t n = t->end - t->start, m = strlen(suffix);
    char text[64];
    if (P->failed) return;
    if (n > 24) n = 24;
    if (m > sizeof(text) - 1 - n) m = sizeof(text) - 1 - n;
    memcpy(text, P->s + t->start, n);
    memcpy(text + n, suffix, m);
    add_diagnostic(P, SCOUT_SEVERITY_ERROR, t->line, t->col, (long)(t->end - t->start), prefix, text, n + m);
    P->failed = 1;
}

/**
 * @brief Add a highlighting token; the file is gone through from front to
 *        back, so the tokens stay sorted.
 * @param P The parser.
 * @param t The token marked.
 * @param kind SCOUT_TOKEN_ value.
 */
static void add_token(Parser *P, const Token *t, int kind)
{
    JsonDocResult *r = P->res;
    void *array = r->tokens;
    long *v;
    if (!grow(P, &array, r->token_count * 4 + 3, &P->token_cap, sizeof(long))) return;
    r->tokens = (long *)array;
    v = r->tokens + r->token_count++ * 4;
    v[0] = t->line;
    v[1] = t->col;
    v[2] = (long)(t->end - t->start);
    v[3] = kind;
}

/**
 * @brief Tell whether a byte is a hexadecimal digit.
 * @param c The byte.
 * @return Non-zero if it is.
 */
static int hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/**
 * @brief Tell whether a byte may be part of a word (true, null, a mistake).
 * @param c The byte.
 * @return Non-zero if it may.
 */
static int word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

/**
 * @brief Move the scanner over blanks and line ends.
 * @param P The parser.
 */
static void skip_blanks(Parser *P)
{
    while (P->i < P->len) {
        char c = P->s[P->i];
        if (c == ' ' || c == '\t') {
            P->i++;
            P->col++;
        } else if (c == '\n' || c == '\r') {
            P->i++;
            if (c == '\r' && P->i < P->len && P->s[P->i] == '\n') P->i++;
            P->line++;
            P->col = 1;
        } else {
            break;
        }
    }
}

/**
 * @brief Tell whether a number is written as JSON allows:
 *        -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
 * @param s The number.
 * @param n Its length.
 * @return Non-zero if it is.
 */
static int valid_number(const char *s, size_t n)
{
    size_t i = 0, digits;
    if (i < n && s[i] == '-') i++;
    if (i < n && s[i] == '0') {
        i++;
    } else {
        for (digits = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++) digits++;
        if (digits == 0) return 0;
    }
    if (i < n && s[i] == '.') {
        for (i++, digits = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++) digits++;
        if (digits == 0) return 0;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        for (digits = 0; i < n && s[i] >= '0' && s[i] <= '9'; i++) digits++;
        if (digits == 0) return 0;
    }
    return i == n;
}

/**
 * @brief Read a string; on a mistake it is reported and the token is
 *        T_ERROR.
 * @param P The parser (at the opening quote).
 * @param[in,out] t The token (start, line and column set).
 */
static void scan_string(Parser *P, Token *t)
{
    size_t i = P->i + 1;
    t->type = T_ERROR;
    for (;;) {
        char c = i < P->len ? P->s[i] : '\n';
        if (c == '"') {
            t->type = T_STRING;
            t->end = i + 1;
            return;
        }
        if (c == '\n' || c == '\r') {
            t->end = i;
            fail_at(P, t, "string is not closed");
            return;
        }
        if (c == '\\') {
            char e = i + 1 < P->len ? P->s[i + 1] : '\n';
            int ok = e != '\0' && strchr("\"\\/bfnrt", e) != NULL;
            if (e == 'u')
                ok = i + 5 < P->len && hex_digit(P->s[i + 2]) && hex_digit(P->s[i + 3]) &&
                     hex_digit(P->s[i + 4]) && hex_digit(P->s[i + 5]);
            if (!ok) {
                Token bad;
                bad.start = i;
                bad.end = i + (e == '\n' || e == '\r' ? 1 : 2);
                bad.line = t->line;
                bad.col = t->col + (long)(i - P->i);
                fail_with_text(P, &bad, "invalid escape in a string: ", "");
                t->end = i;
                return;
            }
            i += e == 'u' ? 6 : 2;
            continue;
        }
        if ((unsigned char)c < 0x20) {
            fail(P, t->line, t->col + (long)(i - P->i), 1,
                 c == '\t' ? "tab in a string (write \\t)" : "control character in a string");
            t->end = i;
            return;
        }
        i++;
    }
}

/**
 * @brief Read the next token. A mistake is reported (P->failed) and
 *        gives T_ERROR.
 * @param P The parser.
 * @param[out] t The token.
 */
static void next_token(Parser *P, Token *t)
{
    char c;
    skip_blanks(P);
    t->start = t->end = P->i;
    t->line = P->line;
    t->col = P->col;
    if (P->i >= P->len) {
        t->type = T_EOF;
        return;
    }
    c = P->s[P->i];
    t->end = P->i + 1;
    switch (c) {
    case '{': t->type = T_LBRACE; break;
    case '}': t->type = T_RBRACE; break;
    case '[': t->type = T_LBRACKET; break;
    case ']': t->type = T_RBRACKET; break;
    case ':': t->type = T_COLON; break;
    case ',': t->type = T_COMMA; break;
    case '"': scan_string(P, t); break;
    default:
        t->type = T_ERROR;
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t e = P->i + 1;
            while (e < P->len && (word_char(P->s[e]) || P->s[e] == '.' || P->s[e] == '+' || P->s[e] == '-')) e++;
            t->end = e;
            if (valid_number(P->s + P->i, e - P->i)) t->type = T_NUMBER;
            else fail_with_text(P, t, "invalid number: ", "");
        } else if (word_char(c)) {
            size_t e = P->i + 1, n;
            while (e < P->len && word_char(P->s[e])) e++;
            t->end = e;
            n = e - P->i;
            if ((n == 4 && memcmp(P->s + P->i, "true", 4) == 0) || (n == 5 && memcmp(P->s + P->i, "false", 5) == 0) ||
                (n == 4 && memcmp(P->s + P->i, "null", 4) == 0))
                t->type = T_LITERAL;
            else
                fail_with_text(P, t, "not a JSON value: ", " (strings need double quotes)");
        } else if (c == '\'') {
            fail_at(P, t, "strings need double quotes");
        } else if (c == '/' && P->i + 1 < P->len && (P->s[P->i + 1] == '/' || P->s[P->i + 1] == '*')) {
            t->end = P->i + 2;
            fail_at(P, t, "comments are not allowed in JSON");
        } else {
            fail_with_text(P, t, "unexpected character: ", "");
        }
        break;
    }
    P->col += (long)(t->end - t->start);
    P->i = t->end;
}

/**
 * @brief Decode a string token's text (without quotes): escapes become
 *        their byte, \\uXXXX above 0xFF a '?'.
 * @param s The text between the quotes.
 * @param n Its length.
 * @return The text (allocated), or NULL if out of memory.
 */
static char *decode_string(const char *s, size_t n)
{
    char *out = (char *)malloc(n + 1);
    size_t i = 0, k = 0;
    if (!out) return NULL;
    while (i < n) {
        char c = s[i++];
        if (c == '\\' && i < n) {
            char e = s[i++];
            switch (e) {
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                unsigned long v = 0;
                int j;
                for (j = 0; j < 4 && i < n; j++, i++) {
                    char h = s[i];
                    v = v * 16 + (unsigned long)(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
                }
                c = v <= 0xFF ? (char)v : '?';
                break;
            }
            default: c = e; break;
            }
        }
        out[k++] = c;
    }
    out[k] = '\0';
    return out;
}

/**
 * @brief Free the keys of a frame.
 * @param f The frame.
 */
static void free_frame(Frame *f)
{
    long i;
    for (i = 0; i < f->key_count; i++) free(f->keys[i]);
    free(f->keys);
    f->keys = NULL;
    f->key_count = f->key_cap = 0;
}

/**
 * @brief Set the detail of the symbol whose value starts here.
 * @param P The parser.
 * @param t The value's first token.
 */
static void set_detail(Parser *P, const Token *t)
{
    Frame *f = P->depth > 0 ? &P->stack[P->depth - 1] : NULL;
    JsonDocSymbol *sym;
    size_t n;
    if (!f || f->member < 0) return;
    sym = &P->res->symbols[f->member];
    if (t->type == T_LBRACE) {
        sym->detail = dup_text("{...}", 5);
    } else if (t->type == T_LBRACKET) {
        sym->detail = dup_text("[...]", 5);
    } else {
        n = t->end - t->start;
        if (n <= DETAIL_MAX) {
            sym->detail = dup_text(P->s + t->start, n);
        } else if ((sym->detail = (char *)malloc(DETAIL_MAX + 1)) != NULL) {
            memcpy(sym->detail, P->s + t->start, DETAIL_MAX - 3);
            strcpy(sym->detail + DETAIL_MAX - 3, "...");
        }
    }
    if (!sym->detail) P->res->out_of_memory = 1;
}

/**
 * @brief A value has ended: close the range of its key's symbol and
 *        expect what follows it.
 * @param P The parser.
 * @param line The line of its end.
 * @param col The column after it.
 */
static void value_done(Parser *P, long line, long col)
{
    Frame *f;
    if (P->depth == 0) {
        P->state = S_END;
        return;
    }
    f = &P->stack[P->depth - 1];
    if (f->open == '[') {
        P->state = S_ARR_NEXT;
        return;
    }
    if (f->member >= 0) {
        P->res->symbols[f->member].range.end_line = line;
        P->res->symbols[f->member].range.end_col = col;
        f->member = -1;
    }
    P->state = S_OBJ_NEXT;
}

/**
 * @brief Take the first token of a value: open an object or array, or
 *        take a scalar.
 * @param P The parser.
 * @param t The token.
 */
static void begin_value(Parser *P, const Token *t)
{
    Frame *outer, *f;
    void *array = P->stack;
    set_detail(P, t);
    if (t->type != T_LBRACE && t->type != T_LBRACKET) {
        value_done(P, t->line, t->col + (long)(t->end - t->start));
        return;
    }
    if (P->depth >= DEPTH_MAX) {
        fail_at(P, t, "nested too deeply");
        return;
    }
    if (!grow(P, &array, P->depth, &P->stack_cap, sizeof(Frame))) return;
    P->stack = (Frame *)array;
    outer = P->depth > 0 ? &P->stack[P->depth - 1] : NULL;
    f = &P->stack[P->depth++];
    memset(f, 0, sizeof(*f));
    f->open = t->type == T_LBRACE ? '{' : '[';
    f->line = t->line;
    f->col = t->col;
    f->parent = -1;
    f->member = -1;
    if (f->open == '{') {
        if (!outer) {
            f->level = 1;
        } else if (outer->open == '{' && outer->level == 1 && outer->member >= 0) {
            f->level = 2;
            f->parent = outer->member;
        }
    }
    P->state = f->open == '{' ? S_OBJ_FIRST : S_ARR_FIRST;
}

/**
 * @brief Take a key of the innermost object: its token, a warning if it
 *        is there already, its symbol on the first two levels.
 * @param P The parser.
 * @param t The key's token.
 */
static void take_key(Parser *P, const Token *t)
{
    Frame *f = &P->stack[P->depth - 1];
    char *key = decode_string(P->s + t->start + 1, t->end - t->start - 2);
    void *array;
    long i;
    add_token(P, t, SCOUT_TOKEN_TYPE);
    P->state = S_COLON;
    if (!key) {
        P->res->out_of_memory = 1;
        return;
    }
    for (i = 0; i < f->key_count; i++) {
        if (strcmp(f->keys[i], key) == 0) {
            add_diagnostic(P, SCOUT_SEVERITY_WARNING, t->line, t->col, (long)(t->end - t->start),
                           "key defined twice in this object: ", key, strlen(key));
            break;
        }
    }
    if (f->level > 0) {
        JsonDocSymbol *sym;
        array = P->res->symbols;
        if (grow(P, &array, P->res->symbol_count, &P->symbol_cap, sizeof(JsonDocSymbol))) {
            P->res->symbols = (JsonDocSymbol *)array;
            sym = &P->res->symbols[P->res->symbol_count];
            memset(sym, 0, sizeof(*sym));
            if ((sym->name = dup_text(key, strlen(key))) == NULL) {
                P->res->out_of_memory = 1;
            } else {
                sym->kind = SCOUT_SYMBOL_KEY;
                sym->parent = f->parent;
                sym->range.start_line = sym->range.end_line = t->line;
                sym->range.start_col = t->col;
                sym->range.end_col = t->col + (long)(t->end - t->start);
                sym->selection.start_line = sym->selection.end_line = t->line;
                sym->selection.start_col = t->col + 1;
                sym->selection.end_col = t->col + (long)(t->end - t->start) - 1;
                f->member = P->res->symbol_count++;
            }
        }
    }
    array = f->keys;
    if (grow(P, &array, f->key_count, &f->key_cap, sizeof(char *))) {
        f->keys = (char **)array;
        f->keys[f->key_count++] = key;
    } else {
        free(key);
    }
}

/**
 * @brief Close the innermost object or array with its bracket.
 * @param P The parser.
 * @param t The bracket.
 */
static void close_value(Parser *P, const Token *t)
{
    free_frame(&P->stack[--P->depth]);
    value_done(P, t->line, t->col + 1);
}

/**
 * @brief Tell whether a token can start a value.
 * @param type T_ value.
 * @return Non-zero if it can.
 */
static int starts_value(int type)
{
    return type == T_LBRACE || type == T_LBRACKET || type == T_STRING || type == T_NUMBER || type == T_LITERAL;
}

/**
 * @brief Report the end of the file while objects or arrays are open:
 *        the innermost one is not closed.
 * @param P The parser.
 */
static void report_unclosed(Parser *P)
{
    const Frame *f = &P->stack[P->depth - 1];
    fail(P, f->line, f->col, 1, f->open == '{' ? "'{' is not closed" : "'[' is not closed");
}

/**
 * @brief Check a closing bracket against the innermost open one.
 * @param P The parser.
 * @param t The bracket.
 * @return Non-zero if it closes it.
 */
static int matches(Parser *P, const Token *t)
{
    char want = t->type == T_RBRACE ? '{' : '[';
    char text[64];
    const Frame *f;
    long n, k;
    if (P->depth == 0) {
        fail_at(P, t, t->type == T_RBRACE ? "'}' without an opening '{'" : "']' without an opening '['");
        return 0;
    }
    f = &P->stack[P->depth - 1];
    if (f->open == want) return 1;
    /* "'}' does not close the '[' of line 12" */
    strcpy(text, t->type == T_RBRACE ? "'}' does not close the '[' of line " : "']' does not close the '{' of line ");
    k = (long)strlen(text);
    for (n = f->line; n > 0 && k < 63; n /= 10) k++;
    text[k] = '\0';
    for (n = f->line; n > 0; n /= 10) text[--k] = (char)('0' + n % 10);
    fail_at(P, t, text);
    return 0;
}

/**
 * @brief Take one token in the current state.
 * @param P The parser.
 * @param t The token.
 */
static void step(Parser *P, const Token *t)
{
    int st = P->state;
    if (t->type == T_EOF) {
        if (st == S_START || st == S_END) return;
        if (P->depth > 0) report_unclosed(P);
        else fail_at(P, t, "value expected");
        return;
    }
    if ((t->type == T_RBRACE || t->type == T_RBRACKET) && !matches(P, t)) return;
    switch (st) {
    case S_START:
    case S_VALUE:
        if (starts_value(t->type)) begin_value(P, t);
        else fail_at(P, t, "value expected");
        break;
    case S_ARR_FIRST:
        if (t->type == T_RBRACKET) close_value(P, t);
        else if (starts_value(t->type)) begin_value(P, t);
        else fail_at(P, t, "value expected");
        break;
    case S_ARR_COMMA:
        if (starts_value(t->type)) begin_value(P, t);
        else if (t->type == T_RBRACKET) fail_at(P, &P->comma, "comma before ']'");
        else fail_at(P, t, "value expected");
        break;
    case S_ARR_NEXT:
        if (t->type == T_COMMA) {
            P->comma = *t;
            P->state = S_ARR_COMMA;
        } else if (t->type == T_RBRACKET) {
            close_value(P, t);
        } else if (starts_value(t->type)) {
            fail_at(P, t, "comma missing before this value");
        } else {
            fail_at(P, t, "',' or ']' expected");
        }
        break;
    case S_OBJ_FIRST:
        if (t->type == T_RBRACE) close_value(P, t);
        else if (t->type == T_STRING) take_key(P, t);
        else fail_at(P, t, "key expected (a string in double quotes)");
        break;
    case S_KEY:
        if (t->type == T_STRING) take_key(P, t);
        else if (t->type == T_RBRACE) fail_at(P, &P->comma, "comma before '}'");
        else fail_at(P, t, "key expected (a string in double quotes)");
        break;
    case S_COLON:
        if (t->type == T_COLON) P->state = S_VALUE;
        else fail_at(P, t, "':' expected after the key");
        break;
    case S_OBJ_NEXT:
        if (t->type == T_COMMA) {
            P->comma = *t;
            P->state = S_KEY;
        } else if (t->type == T_RBRACE) {
            close_value(P, t);
        } else if (t->type == T_STRING) {
            fail_at(P, t, "comma missing before this key");
        } else {
            fail_at(P, t, "',' or '}' expected");
        }
        break;
    default:    /* S_END */
        fail_at(P, t, "text after the end of the JSON value");
        break;
    }
}

JsonDocResult *json_parser_analyze(const char *source, size_t length)
{
    Parser P;
    Token t;
    long n = 0;
    memset(&P, 0, sizeof(P));
    P.res = (JsonDocResult *)calloc(1, sizeof(JsonDocResult));
    if (!P.res) return NULL;
    P.s = source;
    P.len = length;
    P.line = P.col = 1;
    P.state = S_START;
    while (!P.failed && !P.res->out_of_memory) {
        if ((++n & 1023) == 0 && json_parser_should_stop && json_parser_should_stop()) {
            P.res->stopped = 1;
            break;
        }
        next_token(&P, &t);
        if (P.failed) break;
        step(&P, &t);
        if (t.type == T_EOF) break;
    }
    while (P.depth > 0) free_frame(&P.stack[--P.depth]);
    free(P.stack);
    return P.res;
}

void json_doc_result_free(JsonDocResult *res)
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
