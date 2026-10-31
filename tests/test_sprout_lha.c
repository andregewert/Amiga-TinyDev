/**
 * @file test_sprout_lha.c
 * @brief Host-native tests of Sprout's LhA reader with the archives of
 *        tests/data/sprout/ (made by tools/mksprouttests.py).
 */
#include "sprout_lha.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** An archive in memory, read through mem_read(). */
typedef struct Mem {
    unsigned char *data;    /**< the bytes */
    long len;               /**< their number */
    long pos;               /**< the next byte */
    long chunk;             /**< the most bytes one read gives */
} Mem;

/** Decoded bytes, collected by mem_write(). */
typedef struct Out {
    unsigned char *data;    /**< the bytes (malloc()ed) */
    long len;               /**< their number */
    long fail_after;        /**< fail when more than this would be written (-1: never) */
} Out;

/**
 * @brief Read callback over a Mem.
 * @param user The Mem.
 * @param buf Where the bytes go.
 * @param len How many are wanted.
 * @return The number given.
 */
static long mem_read(void *user, void *buf, long len)
{
    Mem *m = (Mem *)user;
    long n = m->len - m->pos;
    if (n > len) n = len;
    if (n > m->chunk) n = m->chunk;
    memcpy(buf, m->data + m->pos, (size_t)n);
    m->pos += n;
    return n;
}

/**
 * @brief Write callback collecting into an Out.
 * @param user The Out.
 * @param buf The bytes.
 * @param len Their number.
 * @return Non-zero on success.
 */
static int mem_write(void *user, const void *buf, long len)
{
    Out *o = (Out *)user;
    unsigned char *bigger;
    if (o->fail_after >= 0 && o->len + len > o->fail_after) return 0;
    bigger = (unsigned char *)realloc(o->data, (size_t)(o->len + len + 1));
    assert(bigger != NULL);
    o->data = bigger;
    memcpy(o->data + o->len, buf, (size_t)len);
    o->len += len;
    return 1;
}

/**
 * @brief Load a file of tests/data/sprout/.
 * @param name Its name.
 * @param[out] m The archive (chunk: everything).
 */
static void load(const char *name, Mem *m)
{
    char path[256];
    FILE *f;
    snprintf(path, sizeof(path), "tests/data/sprout/%s", name);
    f = fopen(path, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    m->len = ftell(f);
    fseek(f, 0, SEEK_SET);
    m->data = (unsigned char *)malloc((size_t)m->len);
    assert(m->data != NULL);
    assert(fread(m->data, 1, (size_t)m->len, f) == (size_t)m->len);
    fclose(f);
    m->pos = 0;
    m->chunk = m->len;
}

/**
 * @brief The expected contents of a file, as tools/mksprouttests.py makes it.
 * @param path Its path.
 * @param[out] len Its length.
 * @return The bytes (malloc()ed), or NULL for a path that is not a file.
 */
static unsigned char *expected(const char *path, long *len)
{
    unsigned char *d;
    long i;
    if (strcmp(path, "hello.txt") == 0) {
        static const char hello[] = "Hello {$NAME}\r\nzweite Zeile \xe4\n";
        *len = (long)sizeof(hello) - 1;
        d = (unsigned char *)malloc(sizeof(hello));
        memcpy(d, hello, sizeof(hello));
        return d;
    }
    if (strcmp(path, "bin.dat") == 0) {
        *len = 1024;
        d = (unsigned char *)malloc(1024);
        for (i = 0; i < 1024; ++i) d[i] = (unsigned char)((i * 7 + 3) & 255);
        return d;
    }
    if (strcmp(path, "empty.txt") == 0) {
        *len = 0;
        return (unsigned char *)malloc(1);
    }
    if (strcmp(path, "dir/sub/big.txt") == 0) {
        static const char alpha[] = "abcdefghijklmnopqrstuvwxyz";
        unsigned long x = 12345;
        char line[64];
        d = (unsigned char *)malloc(100000);
        *len = 0;
        for (i = 0; i < 3000; ++i) {
            int n;
            x = (x * 1103515245UL + 12345UL) & 0x7fffffffUL;
            n = snprintf(line, sizeof(line), "%04ld %05lu %.*s\n", i, x % 100000UL, (int)((x >> 8) % 27), alpha);
            memcpy(d + *len, line, (size_t)n);
            *len += n;
        }
        return d;
    }
    return NULL;
}

/**
 * @brief Read a whole archive and compare every entry.
 * @param name The archive.
 * @param level Its header level.
 * @param method The method of big.txt.
 * @param chunk The most bytes one read gives (small values test the
 *        input buffer).
 * @param extract_dirs Also extract the drawer entries (they give nothing).
 */
static void check_archive(const char *name, int level, const char *method, long chunk, int extract_dirs)
{
    Mem m;
    SproutLha a;
    SproutLhaEntry e;
    int files = 0, dirs = 0, r;

    load(name, &m);
    m.chunk = chunk;
    sprout_lha_init(&a, mem_read, &m);
    while ((r = sprout_lha_next(&a, &e)) == 1) {
        assert(e.level == level);
        if (e.is_dir) {
            assert(strcmp(e.path, "dir") == 0 || strcmp(e.path, "dir/sub") == 0 ||
                   strcmp(e.path, "dir/empty") == 0);
            ++dirs;
            if (extract_dirs) {
                Out o = { NULL, 0, -1 };
                assert(sprout_lha_extract(&a, &e, mem_write, &o) == 1 && o.len == 0);
            }
        } else {
            long len;
            unsigned char *want = expected(e.path, &len);
            Out o = { NULL, 0, -1 };
            if (want == NULL) printf("%s: unexpected entry %s\n", name, e.path);
            assert(want != NULL);
            assert(e.size == (unsigned long)len);
            if (strcmp(e.path, "dir/sub/big.txt") == 0) assert(strcmp(e.method, method) == 0);
            if (sprout_lha_extract(&a, &e, mem_write, &o) != 1) printf("%s: %s: %s\n", name, e.path, a.error);
            assert(o.len == len);
            assert(len == 0 || memcmp(o.data, want, (size_t)len) == 0);
            assert(e.comment[0] == '\0');
            free(o.data);
            free(want);
            ++files;
        }
    }
    if (r < 0) printf("%s: %s\n", name, a.error);
    assert(r == 0);
    assert(files == 4 && dirs == 3);
    assert(sprout_lha_next(&a, &e) == 0);   /* stays at the end */
    free(m.data);
}

/** @brief Entries that are not extracted are skipped. */
static void test_skip(void)
{
    Mem m;
    SproutLha a;
    SproutLhaEntry e;
    int n = 0, found = 0;
    load("h2lh7.lha", &m);
    sprout_lha_init(&a, mem_read, &m);
    while (sprout_lha_next(&a, &e) == 1) {
        ++n;
        if (strcmp(e.path, "empty.txt") == 0) {
            Out o = { NULL, 0, -1 };
            assert(sprout_lha_extract(&a, &e, mem_write, &o) == 1 && o.len == 0);
            free(o.data);
            found = 1;
        }
    }
    assert(n == 7 && found);
    free(m.data);
}

/** @brief Broken archives: a changed data byte, a cut archive, a write that fails. */
static void test_broken(void)
{
    Mem m;
    SproutLha a;
    SproutLhaEntry e;
    int r, failed = 0;
    long i;

    /* a byte of big.txt's data changed: its CRC (or the decoder) says so */
    load("h1lh5.lha", &m);
    m.data[m.len - 100] ^= 0x55;
    sprout_lha_init(&a, mem_read, &m);
    while ((r = sprout_lha_next(&a, &e)) == 1) {
        Out o = { NULL, 0, -1 };
        if (sprout_lha_extract(&a, &e, mem_write, &o) < 0) {
            assert(strcmp(e.path, "dir/sub/big.txt") == 0);
            assert(a.error[0] != '\0');
            failed = 1;
        }
        free(o.data);
    }
    assert(failed);
    free(m.data);

    /* the same in a stored file */
    failed = 0;
    load("h1lh0.lha", &m);
    m.data[m.len - 100] ^= 0x55;
    sprout_lha_init(&a, mem_read, &m);
    while (sprout_lha_next(&a, &e) == 1) {
        Out o = { NULL, 0, -1 };
        if (sprout_lha_extract(&a, &e, mem_write, &o) < 0) failed = 1;
        free(o.data);
    }
    assert(failed);
    free(m.data);

    /* a changed header byte: the header checksum (level 1) or CRC (level 2) */
    load("h1lh5.lha", &m);
    m.data[5] ^= 0x01;              /* in the method */
    sprout_lha_init(&a, mem_read, &m);
    assert(sprout_lha_next(&a, &e) == -1);
    free(m.data);
    load("h2lh7.lha", &m);
    m.data[12] ^= 0x01;             /* in the size */
    sprout_lha_init(&a, mem_read, &m);
    assert(sprout_lha_next(&a, &e) == -1);
    free(m.data);

    /* cut at every length up to a few headers: no crash, an error or fewer entries */
    load("h2lh5.lha", &m);
    for (i = 0; i < 400; ++i) {
        Mem cut = m;
        cut.len = i;
        cut.pos = 0;
        sprout_lha_init(&a, mem_read, &cut);
        while ((r = sprout_lha_next(&a, &e)) == 1) {
            Out o = { NULL, 0, -1 };
            r = sprout_lha_extract(&a, &e, mem_write, &o);
            free(o.data);
            if (r < 0) break;
        }
    }
    /* random bytes in big.txt's data: the decoder stays in its tables */
    for (i = 0; i < 200; ++i) {
        Mem bad;
        long k;
        bad.len = m.len;
        bad.data = (unsigned char *)malloc((size_t)m.len);
        memcpy(bad.data, m.data, (size_t)m.len);
        for (k = 0; k < 20; ++k) bad.data[m.len - 1 - (long)((unsigned long)rand() % 20000UL)] = (unsigned char)rand();
        bad.pos = 0;
        bad.chunk = bad.len;
        sprout_lha_init(&a, mem_read, &bad);
        while (sprout_lha_next(&a, &e) == 1) {
            Out o = { NULL, 0, -1 };
            r = sprout_lha_extract(&a, &e, mem_write, &o);
            free(o.data);
            if (r < 0) break;
        }
        free(bad.data);
    }
    free(m.data);

    /* the write callback fails */
    load("h2lh7.lha", &m);
    sprout_lha_init(&a, mem_read, &m);
    failed = 0;
    while (sprout_lha_next(&a, &e) == 1) {
        if (strcmp(e.path, "dir/sub/big.txt") == 0) {
            Out o = { NULL, 0, 1000 };
            assert(sprout_lha_extract(&a, &e, mem_write, &o) == -1);
            assert(strcmp(a.error, "cannot write") == 0);
            free(o.data);
            failed = 1;
        }
    }
    assert(failed);
    free(m.data);

    /* not an archive */
    {
        Mem t;
        static unsigned char text[] = "This is just a text file, not an LhA archive at all.\n";
        t.data = text;
        t.len = (long)sizeof(text) - 1;
        t.pos = 0;
        t.chunk = t.len;
        sprout_lha_init(&a, mem_read, &t);
        assert(sprout_lha_next(&a, &e) == -1);
        t.len = 0;
        t.pos = 0;
        sprout_lha_init(&a, mem_read, &t);
        assert(sprout_lha_next(&a, &e) == 0);   /* empty: no entries */
    }
}

/**
 * @brief Run the tests.
 * @return 0 (a failed test aborts).
 */
int main(void)
{
    check_archive("h0lh5.lha", 0, "-lh5-", 1L << 30, 0);
    check_archive("h1lh5.lha", 1, "-lh5-", 1L << 30, 1);
    check_archive("h2lh5.lha", 2, "-lh5-", 1L << 30, 0);
    check_archive("h1lh6.lha", 1, "-lh6-", 1L << 30, 0);
    check_archive("h2lh7.lha", 2, "-lh7-", 1L << 30, 0);
    check_archive("h1lh0.lha", 1, "-lh0-", 1L << 30, 0);
    check_archive("h2lh7.lha", 2, "-lh7-", 7, 0);
    check_archive("h0lh5.lha", 0, "-lh5-", 1, 0);
    test_skip();
    test_broken();
    puts("sprout LhA tests passed");
    return 0;
}
