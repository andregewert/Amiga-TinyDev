#include "syntax.h"

#include <assert.h>
#include <stdio.h>

typedef struct Capture { SyntaxSpan spans[16]; size_t count; } Capture;
/**
 * @brief SyntaxEmit callback that records emitted spans for inspection.
 *
 * Appends @p span to the ::Capture buffer, asserting that the fixed capacity
 * is not exceeded.
 *
 * @param context The ::Capture buffer (as a void pointer) to append to.
 * @param span The span emitted by the scanner.
 */
static void capture(void *context, const SyntaxSpan *span)
{
    Capture *c = context;
    assert(c->count < 16);
    c->spans[c->count++] = *span;
}
/**
 * @brief Assert that a captured span matches the expected bounds and style.
 *
 * @param c The capture buffer to check.
 * @param n Index of the span within @p c.
 * @param a Expected start offset.
 * @param b Expected end offset.
 * @param s Expected ::SyntaxStyle.
 */
static void expect(const Capture *c, size_t n, size_t a, size_t b, SyntaxStyle s)
{
    assert(n < c->count);
    assert(c->spans[n].start == a && c->spans[n].end == b && c->spans[n].style == s);
}
/**
 * @brief Host-native test entry point for the portable syntax scanner.
 *
 * Exercises language detection and the C and AmigaDOS scanners, asserting
 * that the expected keyword, string, comment and preprocessor spans (and the
 * multi-line C block-comment state) are produced.
 *
 * @return 0 when all assertions pass.
 */
int main(void)
{
    Capture c = {{{0, 0, SYNTAX_NORMAL}}, 0}; SyntaxState state;
    assert(syntax_language_for_path("foo.C") == SYNTAX_C);
    assert(syntax_language_for_path("s/startup-sequence.script") == SYNTAX_AMIGADOS);
    assert(syntax_language_for_path("README") == SYNTAX_PLAIN);
    state = syntax_scan_line(SYNTAX_C, "int printint = \"if\\\"\"; // return", 0, capture, &c);
    assert(state == 0 && c.count == 3); expect(&c,0,0,3,SYNTAX_KEYWORD); expect(&c,1,15,21,SYNTAX_STRING); expect(&c,2,23,32,SYNTAX_COMMENT);
    c.count = 0; state = syntax_scan_line(SYNTAX_C, "x /* open", 0, capture, &c);
    assert(state == SYNTAX_STATE_C_COMMENT); expect(&c,0,2,9,SYNTAX_COMMENT);
    c.count = 0; state = syntax_scan_line(SYNTAX_C, "close */ return x", state, capture, &c);
    assert(state == 0 && c.count == 2); expect(&c,0,0,8,SYNTAX_COMMENT); expect(&c,1,9,15,SYNTAX_KEYWORD);
    c.count = 0; syntax_scan_line(SYNTAX_C, "  #include <x>", 0, capture, &c);
    assert(c.count == 1); expect(&c,0,2,14,SYNTAX_PREPROCESSOR);
    c.count = 0; syntax_scan_line(SYNTAX_AMIGADOS, "ECHO hi ; comment", 0, capture, &c);
    assert(c.count == 2); expect(&c,0,0,4,SYNTAX_KEYWORD); expect(&c,1,8,17,SYNTAX_COMMENT);
    puts("syntax tests passed");
    return 0;
}
