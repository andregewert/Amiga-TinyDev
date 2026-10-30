/**
 * @file test_syntax.c
 * @brief Host-native tests of the combined scan (syntax_scan()): Scout's
 *        lexical rules, then its tokens where they may show.
 */
#include "syntax.h"
#include "rules.h"
#include "tokenmap.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Spans emitted during one test. */
typedef struct Capture {
    SyntaxSpan spans[16];   /**< the spans */
    size_t count;           /**< number of them */
} Capture;

/**
 * @brief SyntaxEmit callback that records emitted spans for inspection.
 * @param context The ::Capture buffer.
 * @param span The span.
 */
static void capture(void *context, const SyntaxSpan *span)
{
    Capture *c = context;
    assert(c->count < 16);
    c->spans[c->count++] = *span;
}

/**
 * @brief Tell whether a span was emitted.
 * @param c The capture.
 * @param a Its start.
 * @param b Its end.
 * @param s Its style.
 * @return Non-zero if it was.
 */
static int has(const Capture *c, size_t a, size_t b, SyntaxStyle s)
{
    size_t i;
    for (i = 0; i < c->count; i++)
        if (c->spans[i].start == a && c->spans[i].end == b && c->spans[i].style == s) return 1;
    return 0;
}

/**
 * @brief Scan a line into an emptied capture.
 * @param r The rules (may be NULL).
 * @param m The tokens (may be NULL).
 * @param text The line.
 * @param c The capture.
 */
static void scan(const SyntaxRules *r, const TokenMap *m, const char *text, Capture *c)
{
    c->count = 0;
    syntax_scan(r, m, text, 0, capture, c);
}

/**
 * @brief Host-native test entry point of the combined scan.
 * @return 0 when all assertions pass.
 */
int main(void)
{
    /* rules like those of an AmigaDOS script parser */
    const char *rules =
        "{\"caseSensitive\":false,\"identifier\":{\"start\":\"A-Za-z_\",\"part\":\"A-Za-z0-9_\"},"
        "\"lineComments\":[\";\"],"
        "\"strings\":[{\"open\":\"\\\"\",\"close\":\"\\\"\",\"escape\":\"*\",\"multiline\":false}],"
        "\"preprocessor\":{\"prefix\":\".\"},"
        "\"keywords\":{\"keyword\":[\"echo\"],\"builtin\":[\"copy\"]}}";
    const char *text = "Echo \"<a> fehlt\" ; <a>\n.DEF a 1\nCopy <a> $b\n";
    /* <a> in the string, in the comment, on the dot line, after Copy; $b; Copy as a function */
    const long values[] = { 1, 7, 3, 6, 1, 20, 3, 6, 2, 6, 1, 6, 3, 6, 3, 6, 3, 10, 2, 6, 3, 1, 4, 2 };
    SyntaxRules *r = rules_parse(rules);
    TokenMap *m = tokenmap_build(text, strlen(text), values, 24);
    Capture c = {{{0, 0, SYNTAX_NORMAL}}, 0};
    assert(r != NULL && m != NULL);

    /* without rules and tokens nothing is highlighted */
    scan(NULL, NULL, "Echo \"x\" ; y", &c);
    assert(c.count == 0);

    /* rules alone: the "*" escape keeps the string open */
    scan(r, NULL, "echo \"a*\"b\" ; c", &c);
    assert(c.count == 3 && has(&c, 0, 4, SYNTAX_KEYWORD) && has(&c, 5, 11, SYNTAX_STRING) && has(&c, 12, 15, SYNTAX_COMMENT));

    /* parameters show in strings and on dot lines, not in comments */
    scan(r, m, "Echo \"<a> fehlt\" ; <a>", &c);
    assert(has(&c, 5, 16, SYNTAX_STRING) && has(&c, 6, 9, SYNTAX_VARIABLE));
    assert(has(&c, 17, 22, SYNTAX_COMMENT) && !has(&c, 19, 22, SYNTAX_VARIABLE));
    scan(r, m, ".DEF a 1", &c);
    assert(has(&c, 0, 8, SYNTAX_PREPROCESSOR) && has(&c, 5, 6, SYNTAX_VARIABLE));
    /* other tokens only in normal text: Copy is a builtin already */
    scan(r, m, "Copy <a> $b", &c);
    assert(has(&c, 0, 4, SYNTAX_FUNCTION) && has(&c, 5, 8, SYNTAX_VARIABLE) && has(&c, 9, 11, SYNTAX_VARIABLE));
    assert(c.count == 3);

    tokenmap_free(m);
    rules_free(r);
    puts("syntax tests passed");
    return 0;
}
