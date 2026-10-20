/**
 * @file locale.c
 * @brief Translations through locale.library.
 *
 * The built-in strings are English (include/tinydev_strings.h, made from
 * catalogs/TinyDev.cd with amiga-devkit/tools/catcomp.py). OpenCatalog()
 * looks for TinyDev.catalog in PROGDIR:Catalogs/\<language\>/ and
 * LOCALE:Catalogs/\<language\>/ for the languages the user prefers; for
 * English (or without a catalog or locale.library) the built-in strings
 * are used. Built like Kickdown's.
 *
 * The strings stay valid until locale_close(); menus and gadgets may keep
 * pointers to them.
 */
#include <exec/types.h>
#include <libraries/locale.h>

#include <proto/exec.h>
#include <proto/locale.h>

#define TINYDEV_STRINGS
#include "editor.h"

/* No OC_Version until the first release: the catalogs are built together
 * with the program, and the strings may still be reordered. With the
 * release, a catalog version is set here and in the .ct files. */

/**
 * locale.library, NULL if it is not available.
 */
struct LocaleBase *LocaleBase = NULL;
/**
 * TinyDev's catalog, NULL for the built-in English texts.
 */
static struct Catalog *catalog = NULL;

void locale_open(void)
{
    if ((LocaleBase = (struct LocaleBase *)OpenLibrary((STRPTR)"locale.library", 38)) == NULL) return;
    catalog = OpenCatalog(NULL, (STRPTR)"TinyDev.catalog",
                          OC_BuiltInLanguage, (ULONG)"english",
                          TAG_END);
}

void locale_close(void)
{
    if (LocaleBase != NULL) {
        CloseCatalog(catalog);              /* NULL is fine */
        CloseLibrary((struct Library *)LocaleBase);
    }
    catalog = NULL;
    LocaleBase = NULL;
}

const char *S(LONG id)
{
    const char *builtin = id >= 0 && id < MSG_COUNT ? msg_builtin[id] : "";
    if (catalog == NULL) return builtin;
    return (const char *)GetCatalogStr(catalog, id, (STRPTR)builtin);
}
