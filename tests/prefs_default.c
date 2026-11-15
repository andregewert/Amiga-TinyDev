/**
 * @file prefs_default.c
 * @brief Host tool of "make dist": writes the TinyDev.json that a new
 *        installation starts with.
 *
 * prefs_default OUT writes the default settings (settings_default()) and
 * the default settings per file type into OUT, every value included, so
 * the file shows what can be set, as TinyDev itself would write it. One
 * file type differs from the defaults: Makefiles are indented with tab
 * characters, which GNU make requires in recipes. The Installer copies
 * the file only into a drawer that has none, so an update keeps the
 * user's settings.
 */
#include "appprefs.h"
#include "filetypes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Write the default settings file.
 * @param argc The argument count (2).
 * @param argv The arguments: the program and the file to write.
 * @return 0 on success, 1 on a failure.
 */
int main(int argc, char **argv)
{
    Settings s;
    FileTypes ft;
    FileTypeOptions make;
    char *settings, *all;
    FILE *f;
    int ok;

    if (argc != 2) {
        fprintf(stderr, "usage: prefs_default OUT\n");
        return 1;
    }
    settings_default(&s);
    filetypes_init(&ft);
    filetypes_defaults(&make);
    make.indent = 0;                        /* tab characters */
    if (filetypes_set(&ft, "make", &make) != 1 ||
        (settings = appprefs_format(&s, NULL)) == NULL ||
        (all = filetypes_format(&ft, settings)) == NULL) {
        fprintf(stderr, "prefs_default: out of memory\n");
        return 1;
    }
    free(settings);
    filetypes_clear(&ft);
    /* what TinyDev will read back */
    ok = filetypes_parse(&ft, all) && filetypes_find(&ft, "make") != NULL &&
         filetypes_find(&ft, "make")->indent == 0;
    filetypes_clear(&ft);
    if (!ok) {
        fprintf(stderr, "prefs_default: the file does not read back\n");
        return 1;
    }
    if ((f = fopen(argv[1], "wb")) == NULL) {
        perror(argv[1]);
        return 1;
    }
    ok = fputs(all, f) >= 0;
    ok = fclose(f) == 0 && ok;
    free(all);
    if (!ok) {
        fprintf(stderr, "prefs_default: cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
