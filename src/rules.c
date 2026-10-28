/**
 * @file rules.c
 * @brief Reading the lexical rules of TinyDev-Scout's parsers and the
 *        generic scanner that highlights a line with them.
 *
 * The state carried from line to line (the texteditor's
 * StatusOfPrevBlock) is packed into an unsigned long: bits 0-1 what is
 * open (0 nothing, 1 a block comment, 2 a string), bits 2-4 which one,
 * bits 5-12 the nesting depth of a block comment, bit 13 a preprocessor
 * line continued.
 */
#include "rules.h"
#include "json.h"

#include <stdlib.h>
#include <string.h>

/** @name State bits */
/** @{ */
#define ST_NONE     0UL     /**< nothing open */
#define ST_BLOCK    1UL     /**< a block comment is open */
#define ST_STRING   2UL     /**< a string is open */
#define ST_PP       (1UL << 13) /**< a preprocessor line goes on */
/** @} */

/**
 * @brief Pack a state.
 * @param kind ST_NONE, ST_BLOCK or ST_STRING.
 * @param index Which comment or string.
 * @param depth Nesting depth of a comment.
 * @param pp A preprocessor line goes on.
 * @return The state.
 */
static unsigned long pack(unsigned long kind, int index, int depth, int pp)
{
    if (depth > 255) depth = 255;
    return kind | ((unsigned long)index << 2) | ((unsigned long)depth << 5) | (pp ? ST_PP : 0);
}

/**
 * @brief Fold a Latin-1 character to lower case.
 * @param c The character.
 * @return Its lower case form.
 */
static unsigned char fold(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (unsigned char)(c + 32);
    return c;
}

/**
 * @brief Fill a character class from the protocol's notation ("A-Za-z_").
 * @param table The table (256 entries).
 * @param cls The class.
 */
static void set_class(unsigned char *table, const char *cls)
{
    const unsigned char *p;
    memset(table, 0, 256);
    for (p = (const unsigned char *)cls; *p; p++) {
        if (p[1] == '-' && p[2]) {
            unsigned c;
            for (c = p[0]; c <= p[2]; c++) table[c] = 1;
            p += 2;
        } else {
            table[*p] = 1;
        }
    }
}

/**
 * @brief Copy a delimiter into a fixed buffer (too long ones are dropped).
 * @param dst The buffer (RULES_DELIM bytes).
 * @param src The delimiter.
 * @return Non-zero if it was taken.
 */
static int set_delim(char *dst, const char *src)
{
    size_t n = src != NULL ? strlen(src) : 0;
    if (n == 0 || n >= RULES_DELIM) return 0;
    memcpy(dst, src, n + 1);
    return 1;
}

/**
 * @brief Hash a word (folded if the rules are not case sensitive).
 * @param r The rules.
 * @param s The word.
 * @param len Its length.
 * @return The hash.
 */
static unsigned long hash_word(const SyntaxRules *r, const char *s, size_t len)
{
    unsigned long h = 5381;
    size_t i;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        h = (h * 33) ^ (r->case_sensitive ? c : fold(c));
    }
    return h;
}

/**
 * @brief Find a keyword.
 * @param r The rules.
 * @param s The word (need not be terminated).
 * @param len Its length.
 * @return Its style, SYNTAX_NORMAL if it is no keyword.
 */
static SyntaxStyle find_word(const SyntaxRules *r, const char *s, size_t len)
{
    unsigned long h;
    if (r->slots == NULL) return SYNTAX_NORMAL;
    for (h = hash_word(r, s, len) & r->mask; r->slots[h] >= 0; h = (h + 1) & r->mask) {
        const char *w = r->words[r->slots[h]];
        size_t i;
        if (strlen(w) != len) continue;
        for (i = 0; i < len; i++) {
            unsigned char c = (unsigned char)s[i];
            if ((unsigned char)w[i] != (r->case_sensitive ? c : fold(c))) break;
        }
        if (i == len) return (SyntaxStyle)r->classes[r->slots[h]];
    }
    return SYNTAX_NORMAL;
}

/**
 * @brief Add the words of a keyword class.
 * @param r The rules.
 * @param list The array of words.
 * @param style Their style.
 * @return 1 on success, 0 if out of memory.
 */
static int add_words(SyntaxRules *r, const JsonValue *list, SyntaxStyle style)
{
    size_t i, n = json_array_length(list);
    for (i = 0; i < n; i++) {
        const JsonValue *v = json_array_get(list, i);
        char **words;
        unsigned char *classes;
        char *copy;
        size_t len, k;
        if (v == NULL || v->type != JSON_STRING || (len = strlen(v->u.str_val)) == 0) continue;
        words = realloc(r->words, (size_t)(r->word_count + 1) * sizeof(char *));
        if (words == NULL) return 0;
        r->words = words;
        classes = realloc(r->classes, (size_t)(r->word_count + 1));
        if (classes == NULL) return 0;
        r->classes = classes;
        if ((copy = malloc(len + 1)) == NULL) return 0;
        for (k = 0; k <= len; k++) {
            unsigned char c = (unsigned char)v->u.str_val[k];
            copy[k] = (char)(r->case_sensitive ? c : fold(c));
        }
        r->words[r->word_count] = copy;
        r->classes[r->word_count++] = (unsigned char)style;
    }
    return 1;
}

/**
 * @brief Read the pairs of delimiters of a list (block comments, strings).
 * @param list The array.
 * @param pairs The pairs (RULES_MAX).
 * @return Their number.
 */
static int read_pairs(const JsonValue *list, RulesPair *pairs)
{
    size_t i, n = json_array_length(list);
    int count = 0;
    for (i = 0; i < n && count < RULES_MAX; i++) {
        const JsonValue *v = json_array_get(list, i);
        RulesPair *p = &pairs[count];
        const char *esc;
        memset(p, 0, sizeof(*p));
        if (!set_delim(p->open, json_obj_get_string(v, "open", NULL))) continue;
        if (!set_delim(p->close, json_obj_get_string(v, "close", NULL))) strcpy(p->close, p->open);
        esc = json_obj_get_string(v, "escape", NULL);
        p->escape = esc != NULL ? (unsigned char)esc[0] : 0;
        p->multiline = (unsigned char)json_obj_get_bool(v, "multiline", 0);
        p->nested = (unsigned char)json_obj_get_bool(v, "nested", 0);
        count++;
    }
    return count;
}

SyntaxRules *rules_parse(const char *json)
{
    static const struct { const char *key; SyntaxStyle style; } classes[] = {
        { "keyword", SYNTAX_KEYWORD }, { "type", SYNTAX_TYPE },
        { "constant", SYNTAX_CONSTANT }, { "builtin", SYNTAX_FUNCTION }
    };
    JsonValue *root = json_parse(json, NULL);
    const JsonValue *v, *kw;
    SyntaxRules *r;
    size_t i;
    unsigned long size = 32;
    int ok = 1;

    if (root == NULL || root->type != JSON_OBJECT || (r = calloc(1, sizeof(SyntaxRules))) == NULL) {
        json_value_free(root);
        return NULL;
    }
    r->case_sensitive = json_obj_get_bool(root, "caseSensitive", 1);
    v = json_obj_get_obj(root, "identifier");
    set_class(r->start, v != NULL ? json_obj_get_string(v, "start", "A-Za-z_") : "A-Za-z_");
    set_class(r->part, v != NULL ? json_obj_get_string(v, "part", "A-Za-z0-9_") : "A-Za-z0-9_");
    v = json_obj_get_array(root, "lineComments");
    for (i = 0; v != NULL && i < json_array_length(v) && r->line_comment_count < RULES_MAX; i++) {
        const JsonValue *s = json_array_get(v, i);
        if (s != NULL && s->type == JSON_STRING && set_delim(r->line_comments[r->line_comment_count], s->u.str_val))
            r->line_comment_count++;
    }
    r->block_count = read_pairs(json_obj_get_array(root, "blockComments"), r->blocks);
    r->string_count = read_pairs(json_obj_get_array(root, "strings"), r->strings);
    v = json_obj_get_obj(root, "numbers");
    v = v != NULL ? json_obj_get_array(v, "prefixes") : NULL;
    for (i = 0; v != NULL && i < json_array_length(v) && r->prefix_count < RULES_MAX; i++) {
        const JsonValue *s = json_array_get(v, i);
        if (s != NULL && s->type == JSON_STRING && set_delim(r->prefixes[r->prefix_count], s->u.str_val))
            r->prefix_count++;
    }
    v = json_obj_get_obj(root, "preprocessor");
    if (v != NULL) {
        const char *cont = json_obj_get_string(v, "continuation", NULL);
        set_delim(r->pp_prefix, json_obj_get_string(v, "prefix", NULL));
        r->pp_continuation = cont != NULL ? (unsigned char)cont[0] : 0;
    }
    kw = json_obj_get_obj(root, "keywords");
    for (i = 0; kw != NULL && ok && i < sizeof(classes) / sizeof(classes[0]); i++) {
        const JsonValue *list = json_obj_get_array(kw, classes[i].key);
        if (list != NULL) ok = add_words(r, list, classes[i].style);
    }
    json_value_free(root);
    if (ok && r->word_count > 0) {
        long k;
        while (size < (unsigned long)r->word_count * 2 + 1) size *= 2;
        r->slots = malloc(size * sizeof(long));
        if (r->slots == NULL) {
            ok = 0;
        } else {
            unsigned long h;
            r->mask = size - 1;
            for (h = 0; h < size; h++) r->slots[h] = -1;
            for (k = 0; k < r->word_count; k++) {
                for (h = hash_word(r, r->words[k], strlen(r->words[k])) & r->mask; r->slots[h] >= 0;
                     h = (h + 1) & r->mask) ;
                r->slots[h] = k;
            }
        }
    }
    if (!ok) {
        rules_free(r);
        return NULL;
    }
    return r;
}

void rules_free(SyntaxRules *r)
{
    long i;
    if (r == NULL) return;
    for (i = 0; i < r->word_count; i++) free(r->words[i]);
    free(r->words);
    free(r->classes);
    free(r->slots);
    free(r);
}

/**
 * @brief Emit a span.
 * @param emit The callback (may be NULL).
 * @param context Its context.
 * @param start First column (from 0).
 * @param end Column after the span.
 * @param style Its style.
 */
static void out(SyntaxEmit emit, void *context, size_t start, size_t end, SyntaxStyle style)
{
    SyntaxSpan span;
    if (emit == NULL || start >= end) return;
    span.start = start;
    span.end = end;
    span.style = style;
    emit(context, &span);
}

/**
 * @brief Tell whether a delimiter stands at a position.
 * @param text The line.
 * @param i The position.
 * @param delim The delimiter.
 * @return Its length if it does, 0 otherwise.
 */
static size_t at(const char *text, size_t i, const char *delim)
{
    size_t n = strlen(delim);
    return n > 0 && strncmp(text + i, delim, n) == 0 ? n : 0;
}

/**
 * @brief Find the end of a block comment.
 * @param b The comment's delimiters.
 * @param text The line.
 * @param i Where to look from.
 * @param len Length of the line.
 * @param[in,out] depth Nesting depth (1 for a comment just opened).
 * @return The column after its close, or @p len if it is not closed.
 */
static size_t block_end(const RulesPair *b, const char *text, size_t i, size_t len, int *depth)
{
    while (i < len) {
        size_t n;
        if ((n = at(text, i, b->close)) != 0) {
            i += n;
            if (--*depth == 0) return i;
            continue;
        }
        if (b->nested && (n = at(text, i, b->open)) != 0) {
            i += n;
            ++*depth;
            continue;
        }
        i++;
    }
    return len;
}

/**
 * @brief Find the end of a string.
 * @param s The string's delimiters.
 * @param text The line.
 * @param i Where to look from (after the opening delimiter).
 * @param len Length of the line.
 * @param[out] closed Set to non-zero if it is closed on this line.
 * @return The column after its close, or @p len.
 */
static size_t string_end(const RulesPair *s, const char *text, size_t i, size_t len, int *closed)
{
    *closed = 0;
    while (i < len) {
        size_t n;
        if (s->escape && (unsigned char)text[i] == s->escape && i + 1 < len) {
            i += 2;
            continue;
        }
        if ((n = at(text, i, s->close)) != 0) {
            *closed = 1;
            return i + n;
        }
        i++;
    }
    return len;
}

unsigned long rules_scan_line(const SyntaxRules *r, const char *text, unsigned long previous,
                              SyntaxEmit emit, void *context)
{
    size_t len, i = 0;
    unsigned long kind = previous & 3UL;
    int index = (int)((previous >> 2) & 7UL), depth = (int)((previous >> 5) & 255UL);
    int pp = (previous & ST_PP) != 0, closed;

    if (r == NULL || text == NULL) return 0;
    len = strlen(text);
    /* a preprocessor line: as a whole first, its comments and strings over it */
    if (!pp && r->pp_prefix[0] && kind == ST_NONE) {
        size_t p = 0;
        while (p < len && (text[p] == ' ' || text[p] == '\t')) p++;
        if (at(text, p, r->pp_prefix)) pp = 1;
    }
    if (pp) out(emit, context, 0, len, SYNTAX_PREPROCESSOR);

    if (kind == ST_BLOCK && index < r->block_count) {
        size_t end = block_end(&r->blocks[index], text, 0, len, &depth);
        out(emit, context, 0, end, SYNTAX_COMMENT);
        if (depth > 0) return pack(ST_BLOCK, index, depth, 0);
        i = end;
    } else if (kind == ST_STRING && index < r->string_count) {
        size_t end = string_end(&r->strings[index], text, 0, len, &closed);
        out(emit, context, 0, end, SYNTAX_STRING);
        if (!closed) return pack(ST_STRING, index, 0, 0);
        i = end;
    }

    while (i < len) {
        unsigned char c = (unsigned char)text[i];
        size_t best = 0, n;
        int what = 0, which = 0, k;     /* 1 line comment, 2 block, 3 string */
        for (k = 0; k < r->line_comment_count; k++)
            if ((n = at(text, i, r->line_comments[k])) > best) { best = n; what = 1; which = k; }
        for (k = 0; k < r->block_count; k++)
            if ((n = at(text, i, r->blocks[k].open)) > best) { best = n; what = 2; which = k; }
        for (k = 0; k < r->string_count; k++)
            if ((n = at(text, i, r->strings[k].open)) > best) { best = n; what = 3; which = k; }
        if (what == 1) {
            out(emit, context, i, len, SYNTAX_COMMENT);
            i = len;
            break;
        }
        if (what == 2) {
            size_t end;
            depth = 1;
            end = block_end(&r->blocks[which], text, i + best, len, &depth);
            out(emit, context, i, end, SYNTAX_COMMENT);
            if (depth > 0) return pack(ST_BLOCK, which, depth, 0);
            i = end;
            continue;
        }
        if (what == 3) {
            size_t end = string_end(&r->strings[which], text, i + best, len, &closed);
            out(emit, context, i, end, SYNTAX_STRING);
            if (!closed && r->strings[which].multiline) return pack(ST_STRING, which, 0, 0);
            i = end;
            continue;
        }
        /* a number: a digit not inside a word, or a prefix and a hex digit */
        if (i == 0 || !r->part[(unsigned char)text[i - 1]]) {
            size_t start = i;
            int number = c >= '0' && c <= '9';
            for (k = 0; !number && k < r->prefix_count; k++) {
                n = at(text, i, r->prefixes[k]);
                if (n && i + n < len && ((text[i + n] >= '0' && text[i + n] <= '9') ||
                    (fold((unsigned char)text[i + n]) >= 'a' && fold((unsigned char)text[i + n]) <= 'f'))) {
                    number = 1;
                    i += n;
                }
            }
            if (number) {
                while (i < len && (r->part[(unsigned char)text[i]] || text[i] == '.')) i++;
                if (!pp) out(emit, context, start, i, SYNTAX_CONSTANT);
                continue;
            }
        }
        if (r->start[c]) {
            size_t start = i++;
            SyntaxStyle style;
            while (i < len && r->part[(unsigned char)text[i]]) i++;
            style = find_word(r, text + start, i - start);
            if (style != SYNTAX_NORMAL && !pp) out(emit, context, start, i, style);
            continue;
        }
        i++;
    }
    /* a preprocessor line ending in the continuation goes on */
    if (pp && r->pp_continuation && len > 0 && (unsigned char)text[len - 1] == r->pp_continuation)
        return ST_PP;
    return ST_NONE;
}
