/**
 * @file test_htmltitle.c
 * @brief Host-native tests of the HelpViewer's title and link helpers.
 */
#include "htmltitle.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Assert the title found in a text.
 * @param text The HTML text.
 * @param expected The title, or NULL if none is expected.
 */
static void title(const char *text, const char *expected)
{
    char out[64];
    int found = html_title(text, (long)strlen(text), out, sizeof(out));
    if (expected == NULL) { assert(!found); return; }
    assert(found);
    if (strcmp(out, expected) != 0) { printf("got \"%s\"\n", out); assert(0); }
}

/**
 * @brief Assert the path made of a link.
 * @param url The link.
 * @param expected The path.
 */
static void path(const char *url, const char *expected)
{
    char out[64];
    assert(html_link_to_path(url, out, sizeof(out)));
    if (strcmp(out, expected) != 0) { printf("got \"%s\"\n", out); assert(0); }
}

/**
 * @brief Run the tests.
 * @return 0 when all assertions pass.
 */
int main(void)
{
    char small[6];
    title("<html><head><title>Einleitung</title></head>", "Einleitung");
    title("<HTML><TITLE lang=\"de\">\n  Kapitel   2:\n Bedienung </TITLE>", "Kapitel 2: Bedienung");
    title("<title>Gr&ouml;&szlig;e &amp; Farbe</title>", "Gr\xF6\xDF" "e & Farbe");
    title("<title>&#196;rger &#xE4;rger &unknown; &#999;</title>", "\xC4rger \xE4rger &unknown; &#999;");
    title("<title>Gr\xC3\xB6\xC3\x9F" "e</title>", "Gr\xF6\xDF" "e");      /* UTF-8 */
    title("<title>a&nbsp;b</title>", "a b");
    title("<title>   </title>", NULL);
    title("<html><body>no title</body>", NULL);
    title("<title>open", NULL);
    assert(html_title("<title>abcdefgh</title>", 22, small, sizeof(small)) && strcmp(small, "abcde") == 0);

    path("kapitel2.html", "kapitel2.html");
    path("./bilder/a.html", "bilder/a.html");
    path("../index.html", "/index.html");
    path("../../a/../b.html#teil%202", "//a//b.html#teil%202");
    path("file:///Work/help/x.html", "/Work/help/x.html");
    path("mit%20leer.html#oben", "mit leer.html#oben");
    assert(html_link_is_external("http://aminet.net/"));
    assert(html_link_is_external("mailto:a@b.de"));
    assert(!html_link_is_external("file:x.html"));
    assert(!html_link_is_external("kapitel.html#a:b"));
    assert(!html_link_is_external("x/y:z.html"));
    puts("htmltitle tests passed");
    return 0;
}
