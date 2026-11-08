/**
 * @file test_markup_parser.c
 * @brief Host-native tests of the markup parser: the checks of HTML and
 *        XML, the tokens and the Scout protocol, with the samples in
 *        tests/data/markup_parser/.
 */
#include "markup_parser.h"
#include "scout_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Directory of the samples (tests run from the project root). */
#define DATA "tests/data/markup_parser/"

/** Number of checks made. */
static int g_test_count = 0;
/** Number of checks that failed. */
static int g_fail_count = 0;

/** Check a condition; on failure print its line and text. */
#define TEST_ASSERT(cond) do { \
        g_test_count++; \
        if (!(cond)) { \
            g_fail_count++; \
            printf("%s:%d: failed: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/**
 * @brief Read a whole file.
 * @param path The file.
 * @param[out] length Its length.
 * @return Its contents (free() it), or NULL.
 */
static char *read_all(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long size;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)size + 1);
    if (buf) *length = fread(buf, 1, (size_t)size, f);
    fclose(f);
    return buf;
}

/**
 * @brief Check that a text has exactly one diagnostic, the one expected.
 * @param language The language.
 * @param text The text.
 * @param severity The severity expected.
 * @param message The message expected.
 * @param line Its line.
 * @param col Its column.
 * @return Non-zero if it has.
 */
static int only(MarkupLanguage language, const char *text, int severity, const char *message, long line, long col)
{
    MarkupResult *r = markup_parser_analyze(text, strlen(text), language, NULL, NULL);
    int ok = r && r->diagnostic_count == 1 && r->diagnostics[0].severity == severity &&
             strcmp(r->diagnostics[0].message, message) == 0 &&
             r->diagnostics[0].line == line && r->diagnostics[0].col == col;
    if (!ok) {
        long i;
        printf("for: %s\n", text);
        for (i = 0; r && i < r->diagnostic_count; i++) {
            printf("  got %d %ld:%ld %s\n", r->diagnostics[i].severity, r->diagnostics[i].line,
                   r->diagnostics[i].col, r->diagnostics[i].message);
        }
    }
    markup_result_free(r);
    return ok;
}

/**
 * @brief Tell whether a text has no diagnostic at all.
 * @param language The language.
 * @param text The text.
 * @return Non-zero if it has none.
 */
static int clean(MarkupLanguage language, const char *text)
{
    MarkupResult *r = markup_parser_analyze(text, strlen(text), language, NULL, NULL);
    int ok = r && r->diagnostic_count == 0;
    if (!ok && r) printf("for: %s\n  got: %s\n", text, r->diagnostics[0].message);
    markup_result_free(r);
    return ok;
}

/**
 * @brief Tell whether a result has a token.
 * @param r The result.
 * @param line Its line.
 * @param col Its column.
 * @param len Its length.
 * @param kind Its kind.
 * @return Non-zero if it has.
 */
static int has_token(const MarkupResult *r, long line, long col, long len, int kind)
{
    long i;
    for (i = 0; i < r->token_count; i++) {
        const long *v = r->tokens + i * 4;
        if (v[0] == line && v[1] == col && v[2] == len && v[3] == kind) return 1;
    }
    return 0;
}

/** Test what XML must be. */
static void test_xml(void)
{
    TEST_ASSERT(clean(MARKUP_XML, ""));
    TEST_ASSERT(clean(MARKUP_XML, " \n "));
    TEST_ASSERT(clean(MARKUP_XML, "<a/>"));
    TEST_ASSERT(clean(MARKUP_XML, "<?xml version=\"1.0\"?>\n<a b=\"1\" c='2'><d/>text</a>\n<!-- end -->\n"));
    TEST_ASSERT(clean(MARKUP_XML, "\xEF\xBB\xBF<?xml version=\"1.0\"?><a/>"));
    TEST_ASSERT(clean(MARKUP_XML, "<a b=\"one\ntwo\"/>"));
    TEST_ASSERT(clean(MARKUP_XML, "<a b=\"x>y\"/>"));
    TEST_ASSERT(clean(MARKUP_XML, "<!DOCTYPE a [<!ENTITY x \"y>\"> <!-- ' -->]><a>&x;</a>"));

    TEST_ASSERT(only(MARKUP_XML, "<a><b></a>", SCOUT_SEVERITY_ERROR, "<b> is not closed", 1, 4));
    TEST_ASSERT(only(MARKUP_XML, "<a>\n  <b>\n</a>", SCOUT_SEVERITY_ERROR, "<b> is not closed", 2, 3));
    TEST_ASSERT(only(MARKUP_XML, "<a>", SCOUT_SEVERITY_ERROR, "<a> is not closed", 1, 1));
    TEST_ASSERT(only(MARKUP_XML, "<a></b></a>", SCOUT_SEVERITY_ERROR, "end tag </b> without a start tag", 1, 4));
    TEST_ASSERT(only(MARKUP_XML, "<a></A></a>", SCOUT_SEVERITY_ERROR, "end tag </A> without a start tag", 1, 4));
    TEST_ASSERT(only(MARKUP_XML, "<a/><b/>", SCOUT_SEVERITY_ERROR, "second root element", 1, 5));
    TEST_ASSERT(only(MARKUP_XML, "<a/>x", SCOUT_SEVERITY_ERROR, "text outside the root element", 1, 5));
    TEST_ASSERT(only(MARKUP_XML, "<!-- only -->", SCOUT_SEVERITY_ERROR, "the document has no root element", 1, 1));
    TEST_ASSERT(only(MARKUP_XML, "<a b=1/>", SCOUT_SEVERITY_ERROR, "the value of attribute b is not in quotes", 1, 6));
    TEST_ASSERT(only(MARKUP_XML, "<a b/>", SCOUT_SEVERITY_ERROR, "attribute b has no value", 1, 4));
    TEST_ASSERT(only(MARKUP_XML, "<a b=/>", SCOUT_SEVERITY_ERROR, "attribute b has no value after '='", 1, 4));
    TEST_ASSERT(only(MARKUP_XML, "<a b=\"1\" b=\"2\"/>", SCOUT_SEVERITY_ERROR, "attribute b is given twice", 1, 10));
    TEST_ASSERT(only(MARKUP_XML, "<a>x & y</a>", SCOUT_SEVERITY_ERROR, "'&' must be written as &amp;", 1, 6));
    TEST_ASSERT(only(MARKUP_XML, "<a>x < y</a>", SCOUT_SEVERITY_ERROR, "'<' must be written as &lt;", 1, 6));
    TEST_ASSERT(only(MARKUP_XML, "<a>&nope;</a>", SCOUT_SEVERITY_ERROR, "unknown entity &nope;", 1, 4));
    TEST_ASSERT(only(MARKUP_XML, "<a b=\"&\"/>", SCOUT_SEVERITY_ERROR, "'&' must be written as &amp;", 1, 7));
    TEST_ASSERT(only(MARKUP_XML, "<a b=\"x<y\"/>", SCOUT_SEVERITY_ERROR,
                     "'<' in an attribute value must be written as &lt;", 1, 8));
    TEST_ASSERT(only(MARKUP_XML, "<a><!-- x -- y --></a>", SCOUT_SEVERITY_ERROR, "\"--\" is not allowed in a comment", 1, 11));
    TEST_ASSERT(only(MARKUP_XML, "<a>]]></a>", SCOUT_SEVERITY_ERROR, "\"]]>\" is not allowed in the text", 1, 4));
    TEST_ASSERT(only(MARKUP_XML, "\n<?xml version=\"1.0\"?><a/>", SCOUT_SEVERITY_ERROR,
                     "the XML declaration must be at the very start of the file", 2, 1));
}

/** Test the checks of HTML and what it must let pass. */
static void test_html(void)
{
    TEST_ASSERT(clean(MARKUP_HTML, ""));
    TEST_ASSERT(clean(MARKUP_HTML, "just text, 1 < 2 & it's fine"));
    TEST_ASSERT(clean(MARKUP_HTML, "<p>one<p>two<div>x</div>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<ul><li>a<li><p>b<li>c</ul>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<table><tr><td>a<th>b<tr><td>c</table>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<dl><dt>a<dd>b<dt>c</dl>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<select><option>a<option>b</select>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<HTML><BODY><B>x</b></BODY></html>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<br><br/><hr /><img src=\"a\">"));
    TEST_ASSERT(clean(MARKUP_HTML, "<input disabled checked>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<svg><g><path d=\"M0\"/></g></svg><svg/>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<script>if (a<b) x='</div>';</script>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<STYLE>a > b {}</style>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<a onclick=\"if (a>b) c='<b>';\">x</a>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<a title=\"a>b\">x</a>"));
    TEST_ASSERT(clean(MARKUP_HTML, "<?php echo '<b>'; ?><![if IE]>"));

    TEST_ASSERT(only(MARKUP_HTML, "<b><i>x</b>", SCOUT_SEVERITY_ERROR, "<i> is not closed", 1, 4));
    TEST_ASSERT(only(MARKUP_HTML, "<div>", SCOUT_SEVERITY_ERROR, "<div> is not closed", 1, 1));
    TEST_ASSERT(only(MARKUP_HTML, "x</div>", SCOUT_SEVERITY_ERROR, "end tag </div> without a start tag", 1, 2));
    TEST_ASSERT(only(MARKUP_HTML, "<p>x<div>y</div></p>", SCOUT_SEVERITY_WARNING,
                     "the element before </p> has already ended the paragraph", 1, 17));
    TEST_ASSERT(only(MARKUP_HTML, "<p>x<p>y</p></p>", SCOUT_SEVERITY_ERROR, "end tag </p> without a start tag", 1, 13));
    TEST_ASSERT(only(MARKUP_HTML, "x</br>", SCOUT_SEVERITY_ERROR, "<br> has no end tag", 1, 2));
    TEST_ASSERT(only(MARKUP_HTML, "<div/>", SCOUT_SEVERITY_WARNING, "HTML has no short form of <div>", 1, 1));
    TEST_ASSERT(only(MARKUP_HTML, "<td width=50></td>", SCOUT_SEVERITY_HINT, "the value of attribute width is not in quotes", 1, 11));
    TEST_ASSERT(only(MARKUP_HTML, "<a b=\"1\" B=\"2\"></a>", SCOUT_SEVERITY_WARNING, "attribute B is given twice", 1, 10));
    TEST_ASSERT(only(MARKUP_HTML, "<a href=\"x\"\">y</a>", SCOUT_SEVERITY_WARNING, "unexpected character in a tag", 1, 12));
    TEST_ASSERT(only(MARKUP_HTML, "<script>x", SCOUT_SEVERITY_ERROR, "<script> is not closed", 1, 1));
    TEST_ASSERT(only(MARKUP_HTML, "<!-- x", SCOUT_SEVERITY_ERROR, "comment is not closed", 1, 1));
}

/** Test what both report the same way. */
static void test_both(void)
{
    /* the quote is missing: the tag still ends at its '>' */
    TEST_ASSERT(only(MARKUP_HTML, "<a href=\"x>y</a><b class=\"c\">z</b>", SCOUT_SEVERITY_ERROR,
                     "the closing quote of attribute href is missing", 1, 9));
    TEST_ASSERT(only(MARKUP_XML, "<r><a href=\"x>y</a><b class=\"c\">z</b></r>", SCOUT_SEVERITY_ERROR,
                     "the closing quote of attribute href is missing", 1, 12));
    TEST_ASSERT(only(MARKUP_HTML, "<a href=\"x>y</a>", SCOUT_SEVERITY_ERROR,
                     "the closing quote of attribute href is missing", 1, 9));
    TEST_ASSERT(only(MARKUP_HTML, "<p>x<a href=\"y\"", SCOUT_SEVERITY_ERROR, "tag <a has no closing '>'", 1, 5));
    TEST_ASSERT(only(MARKUP_XML, "<a><b c=\"d\"</b></a>", SCOUT_SEVERITY_ERROR, "tag <b has no closing '>'", 1, 4));
    TEST_ASSERT(only(MARKUP_HTML, "<div></div", SCOUT_SEVERITY_ERROR, "end tag </div has no closing '>'", 1, 6));
}

/** Test the limits of the diagnostics. */
static void test_limits(void)
{
    char *text = (char *)malloc(5000 * 5 + 16);
    MarkupResult *r;
    long i;
    size_t n = 0;
    for (i = 0; i < 5000; i++) {
        memcpy(text + n, "</a> ", 5);
        n += 5;
    }
    r = markup_parser_analyze(text, n, MARKUP_HTML, NULL, NULL);
    TEST_ASSERT(r && r->diagnostic_count == 200 && !r->out_of_memory);
    markup_result_free(r);
    /* hints leave room for the errors */
    n = 0;
    for (i = 0; i < 400; i++) {
        memcpy(text + n, "<a b=c>", 7);
        n += 7;
    }
    r = markup_parser_analyze(text, n, MARKUP_HTML, NULL, NULL);
    TEST_ASSERT(r && r->diagnostic_count == 200);
    markup_result_free(r);
    free(text);
}

/** Test the tokens. */
static void test_tokens(void)
{
    const char *text = "<a href=\"x\">&amp;</a>\n<?pi?><!DOCTYPE html>\n<b c=\"1\n2\"/>";
    MarkupResult *r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, NULL, NULL);
    long i;
    TEST_ASSERT(r != NULL);
    TEST_ASSERT(has_token(r, 1, 1, 2, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 1, 4, 4, SCOUT_TOKEN_PARAMETER));
    TEST_ASSERT(has_token(r, 1, 9, 3, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 1, 12, 1, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 1, 13, 5, SCOUT_TOKEN_MACRO));
    TEST_ASSERT(has_token(r, 1, 18, 3, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 1, 21, 1, SCOUT_TOKEN_TYPE));
    TEST_ASSERT(has_token(r, 2, 7, 9, SCOUT_TOKEN_FUNCTION));
    /* a value over two lines is two tokens */
    TEST_ASSERT(has_token(r, 3, 6, 2, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 4, 1, 2, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(has_token(r, 4, 3, 2, SCOUT_TOKEN_TYPE));
    for (i = 1; i < r->token_count; i++) {
        const long *a = r->tokens + (i - 1) * 4, *b = r->tokens + i * 4;
        TEST_ASSERT(a[0] < b[0] || (a[0] == b[0] && a[1] + a[2] <= b[1]));
    }
    markup_result_free(r);

    text = "<a><![CDATA[x\r\ny]]></a><!-- <b> -->";
    r = markup_parser_analyze(text, strlen(text), MARKUP_XML, NULL, NULL);
    TEST_ASSERT(r && r->diagnostic_count == 0);
    TEST_ASSERT(r && has_token(r, 1, 4, 10, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(r && has_token(r, 2, 1, 4, SCOUT_TOKEN_CONSTANT));
    TEST_ASSERT(r && r->token_count == 6);   /* <a, >, CDATA twice, </a, > */
    markup_result_free(r);
}

/** Test the headings of HTML as symbols. */
static void test_headings(void)
{
    const char *text = "<H1>Title</H1>\n<h2 class=\"x\">Caf&eacute; <b>bold</b>\n &amp;  more</h2>\n"
                       "<h3>A</h3><h2>B</h2><h1> <img src=\"a\"> </h1>\n<h1>&#65;&#8364;&nope;</h1><h2>open";
    MarkupResult *r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, NULL, NULL);
    TEST_ASSERT(r && r->symbol_count == 5);
    if (r && r->symbol_count == 5) {
        const MarkupSymbol *s = r->symbols;
        TEST_ASSERT(strcmp(s[0].name, "Title") == 0 && strcmp(s[0].detail, "# Title") == 0 && s[0].parent == -1);
        TEST_ASSERT(s[0].kind == SCOUT_SYMBOL_HEADING);
        TEST_ASSERT(s[0].range.start_line == 1 && s[0].range.start_col == 1 && s[0].range.end_col == 15);
        TEST_ASSERT(s[0].selection.start_col == 5 && s[0].selection.end_col == 10);
        TEST_ASSERT(strcmp(s[1].name, "Caf\xE9 bold & more") == 0 && s[1].parent == 0);
        TEST_ASSERT(strncmp(s[1].detail, "## Caf", 6) == 0);
        TEST_ASSERT(s[1].range.start_line == 2 && s[1].range.end_line == 3);
        TEST_ASSERT(s[1].selection.start_line == 2 && s[1].selection.end_line == 2);
        TEST_ASSERT(s[1].selection.start_col == 15 && s[1].selection.end_col == 34);
        TEST_ASSERT(strcmp(s[2].name, "A") == 0 && s[2].parent == 1);
        TEST_ASSERT(strcmp(s[3].name, "B") == 0 && s[3].parent == 0);
        TEST_ASSERT(strcmp(s[4].name, "A?&nope;") == 0 && s[4].parent == -1);
    }
    markup_result_free(r);

    text = "<h1>0123456789012345678901234567890123456789012345678901234567890123456789</h1>";
    r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, NULL, NULL);
    TEST_ASSERT(r && r->symbol_count == 1 && strlen(r->symbols[0].name) == 70 && strlen(r->symbols[0].detail) == 65);
    markup_result_free(r);

    text = "<r><h1>no heading in XML</h1></r>";
    r = markup_parser_analyze(text, strlen(text), MARKUP_XML, NULL, NULL);
    TEST_ASSERT(r && r->symbol_count == 0 && r->diagnostic_count == 0);
    markup_result_free(r);
}

/** The path of the page the link tests look at. */
#define PAGE "Work:site/sub/page.html"

/**
 * @brief Tell whether a file of the made-up site exists.
 * @param path Its path.
 * @return Non-zero if it does.
 */
static int site_exists(const char *path)
{
    return strcmp(path, PAGE) == 0 || strcmp(path, "Work:site/sub/a.html") == 0 ||
           strcmp(path, "Work:site/b.html") == 0 || strcmp(path, "Work:site/pic one.png") == 0 ||
           strcmp(path, "Work:site/sub") == 0;
}

/**
 * @brief Read a file of the made-up site.
 * @param path Its path.
 * @param[out] length Its length.
 * @return Its contents (the caller free()s them), or NULL.
 */
static char *site_read(const char *path, size_t *length)
{
    const char *text = strcmp(path, "Work:site/b.html") == 0 ? "<p id=\"x\"><a name=\"y\">z</a>" : "<p>";
    char *copy = (char *)malloc(strlen(text) + 1);
    if (copy) strcpy(copy, text);
    *length = strlen(text);
    return copy;
}

/** The made-up site. */
static const MarkupFiles g_site = { site_exists, site_read };

/**
 * @brief Tell whether a page of the made-up site has no diagnostic.
 * @param text The page.
 * @return Non-zero if it has none.
 */
static int site_clean(const char *text)
{
    MarkupResult *r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, PAGE, &g_site);
    int ok = r && r->diagnostic_count == 0;
    if (!ok && r) printf("for: %s\n  got: %s\n", text, r->diagnostics[0].message);
    markup_result_free(r);
    return ok;
}

/**
 * @brief Tell whether a page of the made-up site has exactly one warning.
 * @param text The page.
 * @param message The message expected.
 * @param col Its column (in line 1).
 * @param end_col The column after it.
 * @return Non-zero if it has.
 */
static int site_warns(const char *text, const char *message, long col, long end_col)
{
    MarkupResult *r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, PAGE, &g_site);
    int ok = r && r->diagnostic_count == 1 && r->diagnostics[0].severity == SCOUT_SEVERITY_WARNING &&
             strcmp(r->diagnostics[0].message, message) == 0 && r->diagnostics[0].line == 1 &&
             r->diagnostics[0].col == col && r->diagnostics[0].end_col == end_col;
    if (!ok && r && r->diagnostic_count > 0) {
        printf("for: %s\n  got %ld:%ld %s\n", text, r->diagnostics[0].col, r->diagnostics[0].end_col,
               r->diagnostics[0].message);
    }
    markup_result_free(r);
    return ok;
}

/** Test the check of links and the references. */
static void test_links(void)
{
    const char *text;
    MarkupResult *r;

    TEST_ASSERT(site_clean("<a href=\"a.html\">x</a>"));
    TEST_ASSERT(site_clean("<a href=\"./a.html\">x</a><area href=\" a.html \">"));
    TEST_ASSERT(site_clean("<a href=\"../b.html\">x</a>"));
    TEST_ASSERT(site_clean("<a href=\"../b.html#x\">x</a><a href=\"../b.html#y\">y</a>"));
    TEST_ASSERT(site_clean("<A HREF=\"a.html?q=1&amp;r=2#\">x</A>"));
    TEST_ASSERT(site_clean("<img src=\"../pic%20one.png\"><a href=\"../sub/\">x</a>"));
    TEST_ASSERT(site_clean("<a href=\"page.html#i\">x</a><p id=\"i\">"));
    /* not looked at */
    TEST_ASSERT(site_clean("<a href=\"http://x/nope\">a</a><a href=\"mailto:a@b\">b</a><a href=\"Work:nope\">c</a>"));
    TEST_ASSERT(site_clean("<a href=\"/nope\">a</a><a href=\"//x/nope\">b</a><a href=\"javascript:x()\">c</a>"));
    TEST_ASSERT(site_clean("<a href=\"\">a</a><a href=\"#\">b</a><a href=\"#TOP\">c</a><a href=\"?q\">d</a>"));
    TEST_ASSERT(site_clean("<a href=\"{$PAGE}.html\">a</a><img src=\"$x.png\">"));
    TEST_ASSERT(site_clean("<base href=\"http://x/\"><a href=\"nope.html\">a</a><a href=\"#no\">b</a>"));
    TEST_ASSERT(site_clean("<p name=\"nope.html\" src=\"nope.html\">x"));
    TEST_ASSERT(site_clean("<a href=\"#i\">x</a><p id=\"i\">"));
    TEST_ASSERT(site_clean("<a href=\"#i\">x</a><a name=\"i\">y</a>"));
    TEST_ASSERT(site_clean("<a href=\"#a%20b\">x</a><a name=\"a b\">y</a>"));

    TEST_ASSERT(site_warns("<a href=\"nope.html\">x</a>", "file not found: nope.html", 10, 19));
    TEST_ASSERT(site_warns("<img src=\"nope.png\">", "file not found: nope.png", 11, 19));
    TEST_ASSERT(site_warns("<body background=\"nope.gif\">", "file not found: nope.gif", 19, 27));
    TEST_ASSERT(site_warns("<a href=\"../../../nope\">x</a>", "file not found: ../../../nope", 10, 23));
    TEST_ASSERT(site_warns("<a href=\"#no\">x</a>", "anchor not found: #no", 10, 13));
    TEST_ASSERT(site_warns("<a href=\"#I\">x</a><p id=\"i\">", "anchor not found: #I", 10, 12));
    TEST_ASSERT(site_warns("<p name=\"i\"><a href=\"#i\">x</a>", "anchor not found: #i", 22, 24));
    TEST_ASSERT(site_warns("<a href=\"page.html#no\">x</a>", "anchor not found: page.html#no", 10, 22));
    TEST_ASSERT(site_warns("<a href=\"../b.html#no\">x</a>", "anchor not found in that file: ../b.html#no", 10, 22));

    /* a value without quotes is a link too (and a hint) */
    text = "<a href=nope.html>x</a>";
    r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, PAGE, &g_site);
    TEST_ASSERT(r && r->diagnostic_count == 2 && r->diagnostics[0].col == 9);
    TEST_ASSERT(r && r->reference_count == 1);
    markup_result_free(r);

    /* references: also to files that do not exist */
    text = "<a href=\"../b.html#x\">x</a>\n<img src=\"nope.png\"><a href=\"http://x/\">y</a><a href=\"#z\">z</a>";
    r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, PAGE, &g_site);
    TEST_ASSERT(r && r->reference_count == 2);
    if (r && r->reference_count == 2) {
        TEST_ASSERT(strcmp(r->references[0].target, "Work:site/b.html") == 0);
        TEST_ASSERT(r->references[0].range.start_line == 1 && r->references[0].range.start_col == 10);
        TEST_ASSERT(r->references[0].range.end_col == 21);
        TEST_ASSERT(strcmp(r->references[1].target, "Work:site/sub/nope.png") == 0);
        TEST_ASSERT(r->references[1].range.start_line == 2 && r->references[1].range.start_col == 11);
    }
    markup_result_free(r);

    /* without the file system only the file itself is checked */
    text = "<a href=\"nope.html\">x</a><a href=\"#no\">y</a>";
    r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, NULL, NULL);
    TEST_ASSERT(r && r->diagnostic_count == 1 && r->reference_count == 0);
    markup_result_free(r);
    r = markup_parser_analyze(text, strlen(text), MARKUP_HTML, PAGE, NULL);
    TEST_ASSERT(r && r->diagnostic_count == 1 && r->reference_count == 1);
    markup_result_free(r);

    /* XML is not looked at */
    text = "<r><a href=\"nope.html\"/></r>";
    r = markup_parser_analyze(text, strlen(text), MARKUP_XML, PAGE, &g_site);
    TEST_ASSERT(r && r->diagnostic_count == 0 && r->reference_count == 0);
    markup_result_free(r);
}

/**
 * @brief Check a sample file.
 * @param name Its name.
 * @param language Its language.
 * @param hints The number of hints expected (nothing else may be reported).
 */
static void check_sample(const char *name, MarkupLanguage language, long hints)
{
    char path[128];
    size_t length = 0;
    char *text;
    MarkupResult *r;
    snprintf(path, sizeof(path), "%s%s", DATA, name);
    text = read_all(path, &length);
    TEST_ASSERT(text != NULL);
    if (!text) return;
    r = markup_parser_analyze(text, length, language, NULL, NULL);
    TEST_ASSERT(r && r->diagnostic_count == hints);
    if (r) {
        long i;
        for (i = 0; i < r->diagnostic_count; i++) {
            TEST_ASSERT(r->diagnostics[i].severity == SCOUT_SEVERITY_HINT);
            if (r->diagnostics[i].severity != SCOUT_SEVERITY_HINT) {
                printf("  %ld:%ld %s\n", r->diagnostics[i].line, r->diagnostics[i].col, r->diagnostics[i].message);
            }
        }
    }
    markup_result_free(r);
    free(text);
}

/** Test the samples. */
static void test_samples(void)
{
    check_sample("sample.html", MARKUP_HTML, 1);
    check_sample("sample.xml", MARKUP_XML, 0);
}

/**
 * @brief Answer a request and return its result.
 * @param request The request.
 * @return The parsed answer (free it with scout_rpc_free() and free()), or
 *         NULL.
 */
static ScoutRpcMessage *ask(const char *request)
{
    ScoutRpcMessage *m = (ScoutRpcMessage *)calloc(1, sizeof(ScoutRpcMessage));
    JsonWriter w;
    json_writer_init_alloc(&w, 4096);
    TEST_ASSERT(markup_parser_handle_request(request, &w) == 1);
    TEST_ASSERT(scout_rpc_parse(json_writer_get(&w), m));
    json_writer_free(&w);
    return m;
}

/** Test the protocol. */
static void test_protocol(void)
{
    ScoutRpcMessage *m;
    const JsonValue *a, *l;

    m = ask("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"scout/describe\"}");
    TEST_ASSERT(m->result != NULL);
    a = m->result ? json_obj_get_array(m->result, "languages") : NULL;
    TEST_ASSERT(a && json_array_length(a) == 2);
    l = a ? json_array_get(a, 1) : NULL;
    TEST_ASSERT(l && strcmp(json_obj_get_string(l, "id", ""), "xml") == 0);
    scout_rpc_free(m);
    free(m);

    m = ask("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"scout/analyze\",\"params\":"
            "{\"path\":\"" DATA "sample.xml\",\"language\":\"xml\"}}");
    TEST_ASSERT(m->result && strcmp(json_obj_get_string(m->result, "language", ""), "xml") == 0);
    a = m->result ? json_obj_get_array(m->result, "diagnostics") : NULL;
    TEST_ASSERT(a && json_array_length(a) == 0);
    a = m->result ? json_obj_get_array(m->result, "tokens") : NULL;
    TEST_ASSERT(a && json_array_length(a) > 0);
    a = m->result ? json_obj_get_array(m->result, "symbols") : NULL;
    TEST_ASSERT(a && json_array_length(a) == 0);
    scout_rpc_free(m);
    free(m);

    /* without a language: by the name of the file */
    m = ask("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"scout/analyze\",\"params\":"
            "{\"path\":\"" DATA "sample.html\",\"want\":[\"diagnostics\"]}}");
    TEST_ASSERT(m->result && strcmp(json_obj_get_string(m->result, "language", ""), "html") == 0);
    a = m->result ? json_obj_get_array(m->result, "diagnostics") : NULL;
    TEST_ASSERT(a && json_array_length(a) == 1);
    TEST_ASSERT(m->result && json_obj_get_array(m->result, "tokens") == NULL);
    TEST_ASSERT(m->result && json_obj_get_array(m->result, "symbols") == NULL);
    scout_rpc_free(m);
    free(m);

    m = ask("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"scout/analyze\",\"params\":"
            "{\"path\":\"" DATA "sample.html\",\"language\":\"html\"}}");
    a = m->result ? json_obj_get_array(m->result, "symbols") : NULL;
    TEST_ASSERT(a && json_array_length(a) == 1);
    l = a ? json_array_get(a, 0) : NULL;
    TEST_ASSERT(l && strcmp(json_obj_get_string(l, "name", ""), "Caf\xE9 & Bar") == 0);
    TEST_ASSERT(l && strcmp(json_obj_get_string(l, "detail", ""), "# Caf\xE9 & Bar") == 0);
    TEST_ASSERT(l && json_obj_get_int(l, "kind", 0) == SCOUT_SYMBOL_HEADING);
    a = m->result ? json_obj_get_array(m->result, "references") : NULL;
    TEST_ASSERT(a && json_array_length(a) == 1);
    l = a ? json_array_get(a, 0) : NULL;
    TEST_ASSERT(l && strcmp(json_obj_get_string(l, "kind", ""), "link") == 0);
    TEST_ASSERT(l && strcmp(json_obj_get_string(l, "target", ""), DATA "sample.xml") == 0);
    scout_rpc_free(m);
    free(m);

    m = ask("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"scout/analyze\",\"params\":"
            "{\"path\":\"x\",\"language\":\"c\"}}");
    TEST_ASSERT(m->result == NULL && m->error_code == SCOUT_ERR_LANGUAGE);
    scout_rpc_free(m);
    free(m);
}

/**
 * @brief Run the tests of the markup parser.
 * @return 0 when all checks pass, 1 otherwise.
 */
int main(void)
{
    printf("Running markup parser tests...\n");
    test_xml();
    test_html();
    test_both();
    test_limits();
    test_tokens();
    test_headings();
    test_links();
    test_samples();
    test_protocol();
    printf("Markup parser tests completed: %d tests run, %d failures.\n", g_test_count, g_fail_count);
    return g_fail_count ? 1 : 0;
}
