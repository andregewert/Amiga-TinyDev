#include "search.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Prepare a pattern and assert that it compiled.
 * @param p The pattern.
 * @param text The text to search for.
 * @param case_sensitive Case-sensitive search.
 * @param whole_word Whole words only.
 */
static void compile(SearchPattern *p, const char *text, int case_sensitive, int whole_word)
{
    SearchOptions o;
    memset(&o, 0, sizeof(o));
    o.case_sensitive = case_sensitive;
    o.whole_word = whole_word;
    assert(search_compile(p, text, &o) == SEARCH_OK);
}

/**
 * @brief Search a text and assert the position of the hit.
 * @param p The pattern.
 * @param text The text.
 * @param line Line of the start position.
 * @param col Column of the start position.
 * @param backwards Search towards the start.
 * @param wrap Wrap around.
 * @param hit_line Expected line of the hit, -1 for none.
 * @param hit_col Expected column of the hit.
 * @param hit_wrapped Expected wrap flag.
 */
static void expect_hit(const SearchPattern *p, const char *text, long line, long col,
                       int backwards, int wrap, long hit_line, long hit_col, int hit_wrapped)
{
    SearchPos from;
    SearchHit hit;
    int wrapped = -1, found;
    from.line = line;
    from.col = col;
    found = search_text(p, text, (long)strlen(text), from, backwards, wrap, &hit, &wrapped);
    if (hit_line < 0) { assert(!found); return; }
    assert(found);
    assert(hit.line == hit_line && hit.col == hit_col && wrapped == hit_wrapped);
    if (!p->options.regex) assert(hit.length == p->length);
}

/**
 * @brief Host-native tests of the portable search engine: plain search,
 *        case folding (Latin-1), whole words, line endings, wrapping in
 *        both directions, all matches and replacement expansion.
 * @return 0 when all assertions pass.
 */
int main(void)
{
    SearchPattern p;
    SearchOptions o;
    SearchMatch m;
    SearchHit *hits;
    char out[32];
    const char *text = "int foo;\nFoo(foo);\r\nfood\rbar foo";
    const char *latin = "\xC4rger und \xE4rger";

    /* empty pattern and missing regular expressions */
    memset(&o, 0, sizeof(o));
    assert(search_compile(&p, "", &o) == SEARCH_EMPTY);
    search_free(&p);
    o.regex = 1;
    assert(search_compile(&p, "a(b", &o) == SEARCH_BADREGEX);
    assert(p.error[0] != '\0');
    search_free(&p);

    /* forward, case-insensitive, all three line endings */
    compile(&p, "foo", 0, 0);
    expect_hit(&p, text, 0, 0, 0, 0, 0, 4, 0);
    expect_hit(&p, text, 0, 5, 0, 0, 1, 0, 0);
    expect_hit(&p, text, 1, 1, 0, 0, 1, 4, 0);
    expect_hit(&p, text, 1, 5, 0, 0, 2, 0, 0);
    expect_hit(&p, text, 2, 1, 0, 0, 3, 4, 0);
    expect_hit(&p, text, 3, 5, 0, 0, -1, 0, 0);
    expect_hit(&p, text, 3, 5, 0, 1, 0, 4, 1);
    /* backwards */
    expect_hit(&p, text, 3, 4, 1, 0, 2, 0, 0);
    expect_hit(&p, text, 1, 4, 1, 0, 1, 0, 0);
    expect_hit(&p, text, 0, 4, 1, 0, -1, 0, 0);
    expect_hit(&p, text, 0, 4, 1, 1, 3, 4, 1);
    assert(search_all(&p, text, (long)strlen(text), &hits) == 5);
    assert(hits[2].line == 1 && hits[2].col == 4 && hits[2].line_length == 9);
    assert(hits[3].line == 2 && hits[3].line_offset == 20 && hits[3].line_length == 4);
    free(hits);
    search_free(&p);

    /* case-sensitive */
    compile(&p, "Foo", 1, 0);
    expect_hit(&p, text, 0, 0, 0, 0, 1, 0, 0);
    expect_hit(&p, text, 1, 1, 0, 1, 1, 0, 1);
    search_free(&p);

    /* whole words: "food" does not count */
    compile(&p, "foo", 0, 1);
    assert(search_all(&p, text, (long)strlen(text), &hits) == 4);
    free(hits);
    expect_hit(&p, text, 1, 5, 0, 0, 3, 4, 0);
    search_free(&p);

    /* Latin-1 letters fold and belong to words */
    compile(&p, "\xE4rger", 0, 0);
    assert(search_all(&p, latin, (long)strlen(latin), &hits) == 2);
    assert(hits[0].col == 0 && hits[1].col == 10);
    free(hits);
    search_free(&p);
    compile(&p, "rger", 0, 1);
    assert(search_all(&p, latin, (long)strlen(latin), &hits) == 0);
    assert(hits == NULL);
    search_free(&p);

    /* matches within a line */
    compile(&p, "aa", 1, 0);
    assert(search_line_next(&p, "aaaa", 4, 1, &m) && m.start == 1 && m.end == 3);
    assert(search_line_prev(&p, "aaaa", 4, 4, &m) && m.start == 2);
    assert(search_line_prev(&p, "aaaa", 4, 1, &m) && m.start == 0);
    assert(!search_line_prev(&p, "aaaa", 4, 0, &m));
    assert(search_all(&p, "aaaaa", 5, &hits) == 2);   /* no overlaps */
    free(hits);
    /* an empty last line and a text without lines */
    expect_hit(&p, "x\n", 1, 0, 0, 1, -1, 0, 0);
    expect_hit(&p, "", 0, 0, 1, 1, -1, 0, 0);

    /* plain replacements are taken as they are */
    assert(search_line_next(&p, "xaay", 4, 0, &m));
    assert(search_expand(&p, "\\1-\\\\", "xaay", &m, out, sizeof(out)) == 5);
    assert(strcmp(out, "\\1-\\\\") == 0);
    assert(search_expand(&p, "abcdef", "xaay", &m, out, 4) == 6 && strcmp(out, "abc") == 0);
    /* expansion of groups (as regular expressions will use it) */
    p.options.regex = 1;
    m.group_start[1] = 1;
    m.group_end[1] = 2;
    assert(search_expand(&p, "<\\0|\\1|\\2|\\\\>", "xaay", &m, out, sizeof(out)) == 9);
    assert(strcmp(out, "<aa|a||\\>") == 0);
    search_free(&p);

    /* regular expressions */
    memset(&o, 0, sizeof(o));
    o.regex = 1;
    o.case_sensitive = 1;
    assert(search_compile(&p, "f(o+)", &o) == SEARCH_OK);
    assert(search_line_next(&p, "xfoo foooy", 10, 0, &m));
    assert(m.start == 1 && m.end == 4 && m.group_start[1] == 2 && m.group_end[1] == 4);
    assert(m.group_start[2] == -1);
    assert(search_line_next(&p, "xfoo foooy", 10, 2, &m) && m.start == 5 && m.end == 9);
    assert(search_line_prev(&p, "xfoo foooy", 10, 5, &m) && m.start == 1);
    assert(search_line_prev(&p, "xfoo foooy", 10, 6, &m) && m.start == 5);
    assert(search_expand(&p, "[\\\\1=\\1]", "xfoo foooy", &m, out, sizeof(out)) == 8);
    assert(strcmp(out, "[\\1=ooo]") == 0);
    assert(search_all(&p, text, (long)strlen(text), &hits) == 4);    /* not "Foo" */
    assert(hits[3].line == 3 && hits[3].col == 4 && hits[3].length == 3);
    free(hits);
    search_free(&p);
    /* '^' and '$' at the line, not at the start column */
    assert(search_compile(&p, "^a|b$", &o) == SEARCH_OK);
    assert(search_line_next(&p, "aab", 3, 0, &m) && m.start == 0 && m.end == 1);
    assert(search_line_next(&p, "aab", 3, 1, &m) && m.start == 2);
    assert(search_all(&p, "a\nba\r\nab", 8, &hits) == 3);
    assert(hits[0].line == 0 && hits[1].line == 2 && hits[1].col == 0 && hits[2].col == 1);
    free(hits);
    search_free(&p);
    /* case-insensitive with Latin-1 letters, whole words */
    o.case_sensitive = 0;
    o.whole_word = 1;
    assert(search_compile(&p, "\xC4r[a-z]+", &o) == SEARCH_OK);
    assert(search_all(&p, latin, (long)strlen(latin), &hits) == 2);
    assert(hits[1].col == 10 && hits[1].length == 5);
    free(hits);
    assert(!search_line_next(&p, "\xE4rger2", 7, 0, &m));
    search_free(&p);
    /* the character classes know the Latin-1 letters */
    o.whole_word = 0;
    o.case_sensitive = 1;
    assert(search_compile(&p, "^[[:alpha:]]+$", &o) == SEARCH_OK);
    assert(search_line_next(&p, "Stra\xDF" "e\xC4\xFF", 8, 0, &m) && m.end == 8);
    assert(!search_line_next(&p, "a\xD7" "b", 3, 0, &m));      /* the multiplication sign */
    search_free(&p);
    assert(search_compile(&p, "[^[:lower:][:digit:]]", &o) == SEARCH_OK);
    assert(search_line_next(&p, "\xDF" "1\xE4\xC4", 4, 0, &m) && m.start == 3);
    search_free(&p);
    o.case_sensitive = 0;
    assert(search_compile(&p, "[[:upper:]]+", &o) == SEARCH_OK);   /* all letters */
    assert(search_line_next(&p, "1\xC4" "b\xE4", 4, 0, &m) && m.start == 1 && m.end == 4);
    search_free(&p);
    assert(search_compile(&p, "\\[:alpha:]", &o) == SEARCH_OK);  /* no class outside [] */
    search_free(&p);
    /* empty matches do not loop */
    o.whole_word = 0;
    assert(search_compile(&p, "x*", &o) == SEARCH_OK);
    assert(search_all(&p, "axxb", 4, &hits) == 4);
    free(hits);
    expect_hit(&p, "ab", 0, 2, 0, 1, 0, 2, 0);
    search_free(&p);

    puts("search tests passed");
    return 0;
}
