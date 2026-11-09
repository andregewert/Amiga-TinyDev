/**
 * @file tmpl_generate.c
 * @brief Host tool of "make check-templates": makes a project from a
 *        template with the portable core of Sprout.
 *
 * tmpl_generate TEMPLATE OUT [KEY=value ...] reads TEMPLATE/template.json
 * and writes the files below TEMPLATE/files to the drawer OUT, with their
 * names and contents expanded as Sprout does it, so that the check builds
 * what a user would get. The date is fixed, the project's name is "Foo".
 * The patterns of "verbatim" are not looked at: the templates that are
 * built use none.
 */
/** Makes the POSIX functions visible with -std=c99. */
#define _POSIX_C_SOURCE 200809L

#include "sprout_tmpl.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/** The project's drawer and so its default name. */
#define PROJECT_NAME "Foo"
/** The day used for DATE and YEAR, in days since 1 January 1978. */
#define FIXED_DAYS 17800L

/** Directory of the generated project. */
static const char *out_dir;
/** The values of the template's placeholders. */
static SproutValues values;

/**
 * @brief Print a message of the core.
 * @param user Not used.
 * @param error Non-zero for an error.
 * @param text The message.
 */
static void report(void *user, int error, const char *text)
{
    (void)user;
    fprintf(stderr, "%s: %s\n", error ? "error" : "warning", text);
}

/**
 * @brief Join two parts of a path.
 * @param a The first part; "" for none.
 * @param b The second part.
 * @return The path (free() it); the program ends if out of memory.
 */
static char *join(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = malloc(la + lb + 2);
    if (p == NULL) {
        fputs("error: out of memory\n", stderr);
        exit(1);
    }
    memcpy(p, a, la);
    if (la > 0) p[la++] = '/';
    memcpy(p + la, b, lb + 1);
    return p;
}

/**
 * @brief Read a whole file.
 * @param path The file.
 * @param[out] length Its length.
 * @return Its contents, NUL-terminated (free() it); NULL on an error.
 */
static char *read_file(const char *path, size_t *length)
{
    struct stat st;
    FILE *f;
    char *data;
    if (stat(path, &st) != 0 || (f = fopen(path, "rb")) == NULL) return NULL;
    data = malloc((size_t)st.st_size + 1);
    if (data != NULL) {
        *length = fread(data, 1, (size_t)st.st_size, f);
        data[*length] = '\0';
    }
    fclose(f);
    return data;
}

/**
 * @brief Write a file, making the drawers above it.
 * @param path The file.
 * @param data Its contents.
 * @param length Their length.
 * @return Non-zero on success.
 */
static int write_file(char *path, const char *data, size_t length)
{
    char *p;
    FILE *f;
    int ok;
    for (p = strchr(path + 1, '/'); p != NULL; p = strchr(p + 1, '/')) {
        *p = '\0';
        mkdir(path, 0777);
        *p = '/';
    }
    if ((f = fopen(path, "wb")) == NULL) return 0;
    ok = fwrite(data, 1, length, f) == length;
    return fclose(f) == 0 && ok;
}

/**
 * @brief Generate one file of the template.
 * @param source The file in the template.
 * @param rel Its path below files/.
 * @return Non-zero on success (also if a condition leaves it out).
 */
static int generate_file(const char *source, const char *rel)
{
    int bad = SPROUT_PATH_OK, ok = 0;
    char *name = sprout_expand_path(rel, &values, &bad);
    char *data, *target, *text = NULL;
    size_t length = 0;
    if (name == NULL) {
        if (bad == SPROUT_PATH_LEFTOUT) return 1;
        fprintf(stderr, "error: bad path in the template: %s\n", rel);
        return 0;
    }
    target = join(out_dir, name);
    if ((data = read_file(source, &length)) == NULL) {
        fprintf(stderr, "error: cannot read %s\n", source);
    } else {
        if (sprout_is_text(data, length)) text = sprout_expand(data, length, &values, &length);
        ok = write_file(target, text != NULL ? text : data, length);
        if (!ok) fprintf(stderr, "error: cannot write %s\n", target);
    }
    free(text);
    free(data);
    free(target);
    free(name);
    return ok;
}

/**
 * @brief Generate the files of a drawer of the template.
 * @param dir The drawer.
 * @param rel Its path below files/; "" for files/ itself.
 * @return Non-zero on success.
 */
static int generate_dir(const char *dir, const char *rel)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int ok = 1;
    if (d == NULL) {
        fprintf(stderr, "error: cannot read %s\n", dir);
        return 0;
    }
    while (ok && (e = readdir(d)) != NULL) {
        struct stat st;
        char *path, *child;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        path = join(dir, e->d_name);
        child = join(rel, e->d_name);
        if (stat(path, &st) != 0) ok = 0;
        else if (S_ISDIR(st.st_mode)) ok = generate_dir(path, child);
        else ok = generate_file(path, child);
        free(child);
        free(path);
    }
    closedir(d);
    return ok;
}

/**
 * @brief Make a project from a template.
 * @param argc Number of arguments.
 * @param argv The template, the output drawer and KEY=value pairs.
 * @return 0 on success, 1 otherwise.
 */
int main(int argc, char **argv)
{
    SproutTemplate t;
    SproutValues given;
    char *path, *json, *files;
    size_t length = 0;
    int i, ok = 1;

    if (argc < 3) {
        fputs("usage: tmpl_generate TEMPLATE OUT [KEY=value ...]\n", stderr);
        return 1;
    }
    path = join(argv[1], "template.json");
    if ((json = read_file(path, &length)) == NULL) {
        fprintf(stderr, "error: cannot read %s\n", path);
        return 1;
    }
    free(path);
    if (sprout_template_parse(&t, json, NULL, report, NULL) != 1) return 1;
    free(json);
    sprout_values_init(&given);
    for (i = 3; i < argc; ++i) {
        if (sprout_values_parse_assign(&given, argv[i]) != 1) {
            fprintf(stderr, "error: bad value: %s\n", argv[i]);
            return 1;
        }
    }
    if (sprout_resolve(&t, &given, PROJECT_NAME, FIXED_DAYS, &values, report, NULL) != 1) return 1;
    out_dir = argv[2];
    mkdir(out_dir, 0777);
    files = join(argv[1], "files");
    ok = generate_dir(files, "");
    free(files);
    sprout_values_free(&values);
    sprout_values_free(&given);
    sprout_template_free(&t);
    return ok ? 0 : 1;
}
