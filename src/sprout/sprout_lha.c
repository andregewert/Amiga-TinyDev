/**
 * @file sprout_lha.c
 * @brief Portable reader of LhA archives (headers of levels 0-2, methods
 *        -lh0-, -lz4-, -lh4- to -lh7-, drawers -lhd-).
 *
 * The decoder follows Haruhiko Okumura's ar002 as used by LHa: each block
 * starts with its size and three Huffman tables (the lengths of the
 * code lengths, the characters and match lengths, the match positions);
 * a match copies bytes from a ring buffer, the dictionary. Bits are read
 * through a 16-bit buffer. Broken data cannot make it write outside its
 * tables (every index is checked); what it produces then is caught by
 * the CRC. No Amiga headers (host-tested in tests/test_sprout_lha.c).
 */
#include "sprout_lha.h"

#include <stdlib.h>
#include <string.h>

/** The largest match length. */
#define MAXMATCH 256
/** Shorter matches are written as characters. */
#define THRESHOLD 3
/** Characters and match lengths. */
#define NC (255 + MAXMATCH + 2 - THRESHOLD)
/** Bits of the count of character codes. */
#define CBIT 9
/** Code lengths of the code lengths (16 + 3). */
#define NT 19
/** Bits of the count of code length codes. */
#define TBIT 5
/** Room for the position and code length tables. */
#define NPT 32
/** Nodes of the code trees (shared by all tables, as in ar002). */
#define NODES (2 * NC)
/** The largest header kept whole (level 2, or level 1 with its extensions). */
#define HEADERMAX 4096

/** CRC-16 table (polynomial 0xA001), made on first use. */
static unsigned short crc_table[256];
/** The table has been made. */
static int crc_ready = 0;

/** @brief Make the CRC-16 table. */
static void make_crc_table(void)
{
    unsigned int i, j, r;
    for (i = 0; i < 256; ++i) {
        r = i;
        for (j = 0; j < 8; ++j) r = (r & 1) ? (r >> 1) ^ 0xA001U : r >> 1;
        crc_table[i] = (unsigned short)r;
    }
    crc_ready = 1;
}

/**
 * @brief Continue a CRC-16 over some bytes.
 * @param crc The CRC so far (0 at the start).
 * @param p The bytes.
 * @param n Their number.
 * @return The new CRC.
 */
static unsigned int crc16(unsigned int crc, const unsigned char *p, long n)
{
    if (!crc_ready) make_crc_table();
    while (n-- > 0) crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc & 0xFFFF;
}

/**
 * @brief Note the reason of an error.
 * @param a The state.
 * @param text The reason.
 * @return -1, for "return fail(...)".
 */
static int fail(SproutLha *a, const char *text)
{
    strncpy(a->error, text, sizeof(a->error) - 1);
    a->error[sizeof(a->error) - 1] = '\0';
    return -1;
}

void sprout_lha_init(SproutLha *a, SproutLhaRead read, void *user)
{
    memset(a, 0, sizeof(*a));
    a->read = read;
    a->user = user;
}

/**
 * @brief Get the next byte of the archive.
 * @param a The state.
 * @return The byte, or -1 at the end or on an error (SproutLha::failed).
 */
static int get_byte(SproutLha *a)
{
    if (a->inpos >= a->inlen) {
        long n;
        if (a->eof || a->failed) return -1;
        n = a->read(a->user, a->inbuf, SPROUT_LHA_INBUF);
        if (n < 0) a->failed = 1;
        if (n <= 0) {
            a->eof = 1;
            return -1;
        }
        a->inpos = 0;
        a->inlen = n;
    }
    return a->inbuf[a->inpos++];
}

/**
 * @brief Read bytes of the archive.
 * @param a The state.
 * @param[out] buf Where they go.
 * @param n How many.
 * @return Non-zero if all were read.
 */
static int get_bytes(SproutLha *a, unsigned char *buf, long n)
{
    while (n-- > 0) {
        int c = get_byte(a);
        if (c < 0) return 0;
        *buf++ = (unsigned char)c;
    }
    return 1;
}

/**
 * @brief Skip bytes of the archive.
 * @param a The state.
 * @param n How many.
 * @return Non-zero if all were there.
 */
static int skip_bytes(SproutLha *a, unsigned long n)
{
    while (n > 0) {
        long avail = a->inlen - a->inpos;
        if (avail <= 0) {
            if (get_byte(a) < 0) return 0;
            --n;
            continue;
        }
        if ((unsigned long)avail > n) avail = (long)n;
        a->inpos += avail;
        n -= (unsigned long)avail;
    }
    return 1;
}

/**
 * @brief A 16-bit little-endian number.
 * @param p Its bytes.
 * @return The number.
 */
static unsigned int get16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

/**
 * @brief A 32-bit little-endian number.
 * @param p Its bytes.
 * @return The number.
 */
static unsigned long get32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

/**
 * @brief Append bytes of a name to a path, turning '\\' and 0xFF into '/'.
 * @param path The path (#SPROUT_LHA_PATHMAX bytes, terminated).
 * @param s The bytes.
 * @param n Their number.
 * @return Non-zero on success, zero if too long.
 */
static int append_path(char *path, const unsigned char *s, long n)
{
    size_t len = strlen(path);
    long i;
    for (i = 0; i < n; ++i) {
        unsigned char c = s[i];
        if (len + 1 >= SPROUT_LHA_PATHMAX) return 0;
        path[len++] = (char)(c == '\\' || c == 0xFF ? '/' : c);
    }
    path[len] = '\0';
    return 1;
}

/** The names found in the extended headers. */
typedef struct Names {
    char dir[SPROUT_LHA_PATHMAX];   /**< drawer (extension 2) */
    char file[SPROUT_LHA_PATHMAX];  /**< file name (extension 1) */
    int has_file;                   /**< extension 1 was there */
} Names;

/**
 * @brief Take the contents of one extended header.
 * @param a The state.
 * @param e The entry.
 * @param names The names so far.
 * @param p The extended header: type, data (without the next size).
 * @param n Its length (type and data).
 * @return 1 on success, -1 on an error.
 */
static int extension(SproutLha *a, SproutLhaEntry *e, Names *names, const unsigned char *p, long n)
{
    if (n < 1) return fail(a, "broken extended header");
    switch (p[0]) {
    case 0x01:                      /* file name */
        names->file[0] = '\0';
        names->has_file = 1;
        if (!append_path(names->file, p + 1, n - 1)) return fail(a, "name too long in archive");
        break;
    case 0x02:                      /* drawer, 0xFF separated */
        names->dir[0] = '\0';
        if (!append_path(names->dir, p + 1, n - 1)) return fail(a, "name too long in archive");
        break;
    case 0x3F:                      /* comment */
        {
            long len = n - 1 < SPROUT_LHA_COMMENTMAX - 1 ? n - 1 : SPROUT_LHA_COMMENTMAX - 1;
            memcpy(e->comment, p + 1, (size_t)len);
            e->comment[len] = '\0';
        }
        break;
    default:                        /* header CRC, attributes, times: not needed */
        break;
    }
    return 1;
}

/**
 * @brief Read a chain of extended headers into a buffer and take them.
 * @param a The state.
 * @param e The entry.
 * @param names The names so far.
 * @param hdr The header buffer (#HEADERMAX bytes).
 * @param[in,out] used The bytes of @p hdr used; the extensions are added.
 * @param next The size of the first extended header (0: none).
 * @return 1 on success, -1 on an error.
 */
static int extensions(SproutLha *a, SproutLhaEntry *e, Names *names, unsigned char *hdr, long *used,
                      unsigned int next)
{
    while (next != 0) {
        unsigned char *p = hdr + *used;
        if (next < 3) return fail(a, "broken extended header");
        if (*used + (long)next > HEADERMAX) return fail(a, "header too large");
        if (!get_bytes(a, p, (long)next)) return fail(a, "archive ends in a header");
        *used += (long)next;
        if (extension(a, e, names, p, (long)next - 2) < 0) return -1;
        next = get16(p + next - 2);
    }
    return 1;
}

/**
 * @brief Find the comment of a level 0/1 name (after a NUL, as Amiga LhA
 *        writes it) and take the name into the path.
 * @param a The state.
 * @param e The entry.
 * @param s The name bytes.
 * @param n Their number.
 * @return 1 on success, -1 on an error.
 */
static int old_name(SproutLha *a, SproutLhaEntry *e, const unsigned char *s, long n)
{
    const unsigned char *nul = (const unsigned char *)memchr(s, 0, (size_t)n);
    if (nul != NULL) {
        long len = n - (long)(nul - s) - 1;
        if (len > SPROUT_LHA_COMMENTMAX - 1) len = SPROUT_LHA_COMMENTMAX - 1;
        memcpy(e->comment, nul + 1, (size_t)len);
        e->comment[len] = '\0';
        n = (long)(nul - s);
    }
    e->path[0] = '\0';
    if (!append_path(e->path, s, n)) return fail(a, "name too long in archive");
    return 1;
}

int sprout_lha_next(SproutLha *a, SproutLhaEntry *e)
{
    unsigned char hdr[HEADERMAX];
    Names names;
    long used;
    int first;

    if (!skip_bytes(a, a->data_left)) return fail(a, "archive ends in the data of a file");
    a->data_left = 0;
    memset(e, 0, sizeof(*e));
    memset(&names, 0, sizeof(names));
    if ((first = get_byte(a)) <= 0) {
        if (a->failed) return fail(a, "cannot read the archive");
        return 0;                   /* the end: a 0 byte, or none */
    }
    hdr[0] = (unsigned char)first;
    if (!get_bytes(a, hdr + 1, 21)) return fail(a, "archive ends in a header");
    memcpy(e->method, hdr + 2, 5);
    e->method[5] = '\0';
    if (e->method[0] != '-' || e->method[1] != 'l' || e->method[4] != '-') return fail(a, "not an LhA archive");
    e->packed = get32(hdr + 7);
    e->size = get32(hdr + 11);
    e->level = hdr[20];
    e->is_dir = strcmp(e->method, "-lhd-") == 0;

    if (e->level == 0 || e->level == 1) {
        long total = (long)hdr[0] + 2, namelen = hdr[21];
        unsigned int sum = 0;
        long i;
        if (total < 22 + namelen + 2 || (e->level == 1 && total < 22 + namelen + 5))
            return fail(a, "broken header");
        if (!get_bytes(a, hdr + 22, total - 22)) return fail(a, "archive ends in a header");
        for (i = 2; i < total; ++i) sum += hdr[i];
        if ((sum & 0xFF) != hdr[1]) return fail(a, "header checksum wrong");
        e->attribute = hdr[19];
        e->crc = get16(hdr + 22 + namelen);
        if (old_name(a, e, hdr + 22, namelen) < 0) return -1;
        if (total > 22 + namelen + 2) e->os_id = hdr[22 + namelen + 2];
        used = total;
        if (e->level == 1) {
            long before = used;
            if (extensions(a, e, &names, hdr, &used, get16(hdr + total - 2)) < 0) return -1;
            /* level 1: the packed size counts the extended headers */
            if ((unsigned long)(used - before) > e->packed) return fail(a, "broken header");
            e->packed -= (unsigned long)(used - before);
        }
    } else if (e->level == 2) {
        long total = (long)get16(hdr);
        unsigned int crc_at = 0, stored;
        if (!get_bytes(a, hdr + 22, 4)) return fail(a, "archive ends in a header");
        e->crc = get16(hdr + 21);
        e->os_id = hdr[23];
        used = 26;
        if (extensions(a, e, &names, hdr, &used, get16(hdr + 24)) < 0) return -1;
        if (used > total) return fail(a, "broken header");
        /* padding up to the total size */
        if (total > HEADERMAX) return fail(a, "header too large");
        if (!get_bytes(a, hdr + used, total - used)) return fail(a, "archive ends in a header");
        /* the header CRC (extension 0), counted with its own field zeroed */
        {
            long pos = 26;
            unsigned int next = get16(hdr + 24);
            while (next != 0 && pos + (long)next <= used) {
                if (hdr[pos] == 0x00 && next >= 5) crc_at = (unsigned int)pos + 1;
                pos += (long)next;
                next = get16(hdr + pos - 2);
            }
        }
        if (crc_at != 0) {
            stored = get16(hdr + crc_at);
            hdr[crc_at] = hdr[crc_at + 1] = 0;
            if (crc16(0, hdr, total) != stored) return fail(a, "header CRC wrong");
        }
    } else {
        return fail(a, "unsupported header level");
    }

    if (names.has_file || names.dir[0] != '\0') {
        /* names from the extended headers win (level 2 has no others) */
        char file[SPROUT_LHA_PATHMAX];
        size_t n;
        strcpy(file, names.has_file ? names.file : e->path);
        strcpy(e->path, names.dir);
        n = strlen(e->path);
        if (n > 0 && e->path[n - 1] != '/' && file[0] != '\0') {
            if (n + 1 >= SPROUT_LHA_PATHMAX) return fail(a, "name too long in archive");
            e->path[n++] = '/';
            e->path[n] = '\0';
        }
        if (!append_path(e->path, (const unsigned char *)file, (long)strlen(file)))
            return fail(a, "name too long in archive");
    }
    {
        size_t n = strlen(e->path);
        while (n > 0 && e->path[n - 1] == '/') e->path[--n] = '\0';
    }
    a->data_left = e->packed;
    return 1;
}

/** The state of decoding one entry with -lh4- to -lh7-. */
typedef struct Decoder {
    SproutLha *a;                       /**< the archive (bytes from it) */
    unsigned int bitbuf;                /**< the next 16 bits */
    unsigned int subbitbuf;             /**< bits of the byte being taken in */
    int bitcount;                       /**< bits left in subbitbuf */
    unsigned int blocksize;             /**< codes left in the block */
    int np;                             /**< number of position codes */
    int pbit;                           /**< bits of the count of position codes */
    int broken;                         /**< the data is broken */
    unsigned char c_len[NC];            /**< code lengths of characters and lengths */
    unsigned char pt_len[NPT];          /**< code lengths of positions / code lengths */
    unsigned short c_table[4096];       /**< character codes by their first 12 bits */
    unsigned short pt_table[256];       /**< position codes by their first 8 bits */
    unsigned short left[NODES];         /**< left children of the tree nodes */
    unsigned short right[NODES];        /**< right children */
} Decoder;

/**
 * @brief Shift bits into the bit buffer.
 * @param d The decoder.
 * @param n How many (0-16).
 */
static void fillbuf(Decoder *d, int n)
{
    d->bitbuf = (d->bitbuf << n) & 0xFFFF;
    while (n > d->bitcount) {
        int c;
        n -= d->bitcount;
        d->bitbuf |= (d->subbitbuf << n) & 0xFFFF;
        c = 0;
        if (d->a->data_left > 0) {
            c = get_byte(d->a);
            if (c < 0) c = 0;
            --d->a->data_left;
        }
        d->subbitbuf = (unsigned int)c;
        d->bitcount = 8;
    }
    d->bitcount -= n;
    d->bitbuf |= (d->subbitbuf >> d->bitcount) & 0xFFFF;
}

/**
 * @brief Take bits from the bit buffer.
 * @param d The decoder.
 * @param n How many (0-16).
 * @return The bits.
 */
static unsigned int getbits(Decoder *d, int n)
{
    unsigned int x = d->bitbuf >> (16 - n);
    fillbuf(d, n);
    return x;
}

/**
 * @brief Make the lookup table of a Huffman code from its code lengths.
 * @param d The decoder (its tree nodes).
 * @param nchar Number of symbols.
 * @param bitlen Their code lengths.
 * @param tablebits Bits looked up directly.
 * @param table The table (1 << tablebits entries).
 */
static void make_table(Decoder *d, int nchar, const unsigned char *bitlen, int tablebits, unsigned short *table)
{
    unsigned int count[17], weight[17], start[18];
    unsigned int i, k, len, ch, jutbits, avail, nextcode, mask;
    unsigned short *p;

    for (i = 1; i <= 16; ++i) count[i] = 0;
    for (i = 0; i < (unsigned int)nchar; ++i) {
        if (bitlen[i] > 16) {
            d->broken = 1;
            return;
        }
        count[bitlen[i]]++;
    }
    start[1] = 0;
    for (i = 1; i <= 16; ++i) start[i + 1] = start[i] + (count[i] << (16 - i));
    if (start[17] != 0x10000U) {
        d->broken = 1;
        return;
    }
    jutbits = 16 - (unsigned int)tablebits;
    for (i = 1; i <= (unsigned int)tablebits; ++i) {
        start[i] >>= jutbits;
        weight[i] = 1U << ((unsigned int)tablebits - i);
    }
    while (i <= 16) {
        weight[i] = 1U << (16 - i);
        ++i;
    }
    i = start[tablebits + 1] >> jutbits;
    k = 1U << tablebits;
    while (i < k) table[i++] = 0;

    avail = (unsigned int)nchar;
    mask = 1U << (15 - tablebits);
    for (ch = 0; ch < (unsigned int)nchar; ++ch) {
        if ((len = bitlen[ch]) == 0) continue;
        nextcode = start[len] + weight[len];
        if (len <= (unsigned int)tablebits) {
            if (nextcode > (1U << tablebits)) {
                d->broken = 1;
                return;
            }
            for (i = start[len]; i < nextcode; ++i) table[i] = (unsigned short)ch;
        } else {
            k = start[len];
            p = &table[k >> jutbits];
            i = len - (unsigned int)tablebits;
            while (i != 0) {
                if (*p == 0) {
                    if (avail >= NODES) {
                        d->broken = 1;
                        return;
                    }
                    d->right[avail] = d->left[avail] = 0;
                    *p = (unsigned short)avail++;
                }
                if (*p >= NODES) {
                    d->broken = 1;
                    return;
                }
                p = (k & mask) ? &d->right[*p] : &d->left[*p];
                k <<= 1;
                --i;
            }
            *p = (unsigned short)ch;
        }
        start[len] = nextcode;
    }
}

/**
 * @brief Follow the tree below a table entry to a symbol.
 * @param d The decoder.
 * @param j The table entry.
 * @param n Number of symbols (entries from it on are nodes).
 * @param bits Bits the table looked up.
 * @return The symbol (0 if the data is broken).
 */
static unsigned int walk_tree(Decoder *d, unsigned int j, unsigned int n, int bits)
{
    unsigned int mask = 1U << (15 - bits);
    while (j >= n) {
        if (j >= NODES || mask == 0) {
            d->broken = 1;
            return 0;
        }
        j = (d->bitbuf & mask) ? d->right[j] : d->left[j];
        mask >>= 1;
    }
    return j;
}

/**
 * @brief Read the code lengths of the position code or of the code length
 *        code.
 * @param d The decoder.
 * @param nn Number of symbols.
 * @param nbit Bits of their count.
 * @param i_special After this many lengths, a 2-bit count of zero lengths
 *        follows (-1: never).
 */
static void read_pt_len(Decoder *d, int nn, int nbit, int i_special)
{
    int i, c, n = (int)getbits(d, nbit);
    if (n == 0) {
        c = (int)getbits(d, nbit);
        for (i = 0; i < nn; ++i) d->pt_len[i] = 0;
        for (i = 0; i < 256; ++i) d->pt_table[i] = (unsigned short)c;
        return;
    }
    if (n > nn) {
        d->broken = 1;
        return;
    }
    i = 0;
    while (i < n) {
        c = (int)(d->bitbuf >> 13);
        if (c == 7) {
            unsigned int mask = 1U << 12;
            while (mask & d->bitbuf) {
                mask >>= 1;
                ++c;
            }
            if (c > 16) {
                d->broken = 1;
                return;
            }
        }
        fillbuf(d, c < 7 ? 3 : c - 3);
        d->pt_len[i++] = (unsigned char)c;
        if (i == i_special) {
            c = (int)getbits(d, 2);
            while (--c >= 0 && i < nn) d->pt_len[i++] = 0;
        }
    }
    while (i < nn) d->pt_len[i++] = 0;
    make_table(d, nn, d->pt_len, 8, d->pt_table);
}

/**
 * @brief Read the code lengths of the character and length code.
 * @param d The decoder.
 */
static void read_c_len(Decoder *d)
{
    int i, c, n = (int)getbits(d, CBIT);
    if (n == 0) {
        c = (int)getbits(d, CBIT);
        for (i = 0; i < NC; ++i) d->c_len[i] = 0;
        for (i = 0; i < 4096; ++i) d->c_table[i] = (unsigned short)c;
        return;
    }
    if (n > NC) {
        d->broken = 1;
        return;
    }
    i = 0;
    while (i < n && !d->broken) {
        c = (int)walk_tree(d, d->pt_table[d->bitbuf >> 8], NT, 8);
        if (c >= NT) {
            d->broken = 1;
            return;
        }
        fillbuf(d, d->pt_len[c]);
        if (c <= 2) {
            if (c == 0) c = 1;
            else if (c == 1) c = (int)getbits(d, 4) + 3;
            else c = (int)getbits(d, CBIT) + 20;
            while (--c >= 0) {
                if (i >= NC) {
                    d->broken = 1;
                    return;
                }
                d->c_len[i++] = 0;
            }
        } else {
            d->c_len[i++] = (unsigned char)(c - 2);
        }
    }
    while (i < NC) d->c_len[i++] = 0;
    make_table(d, NC, d->c_len, 12, d->c_table);
}

/**
 * @brief Decode a character or match length (starting a block if needed).
 * @param d The decoder.
 * @return 0-255 for a character, 256 and up for a match.
 */
static unsigned int decode_c(Decoder *d)
{
    unsigned int j;
    if (d->blocksize == 0) {
        d->blocksize = getbits(d, 16);
        read_pt_len(d, NT, TBIT, 3);
        read_c_len(d);
        read_pt_len(d, d->np, d->pbit, -1);
        if (d->blocksize == 0) d->broken = 1;
        if (d->broken) return 0;
    }
    --d->blocksize;
    j = walk_tree(d, d->c_table[d->bitbuf >> 4], NC, 12);
    if (j >= NC) {
        d->broken = 1;
        return 0;
    }
    fillbuf(d, d->c_len[j]);
    return j;
}

/**
 * @brief Decode the distance of a match.
 * @param d The decoder.
 * @return The distance minus one.
 */
static unsigned int decode_p(Decoder *d)
{
    unsigned int j = walk_tree(d, d->pt_table[d->bitbuf >> 8], (unsigned int)d->np, 8);
    if (j >= (unsigned int)d->np) {
        d->broken = 1;
        return 0;
    }
    fillbuf(d, d->pt_len[j]);
    if (j != 0) j = (1U << (j - 1)) + getbits(d, (int)j - 1);
    return j;
}

/**
 * @brief Decode an entry packed with -lh4- to -lh7-.
 * @param a The state.
 * @param e The entry.
 * @param dicbit Bits of the dictionary size.
 * @param write Receives the bytes.
 * @param user Passed to @p write.
 * @param[out] crc The CRC of the bytes.
 * @return 1 on success, -1 on an error.
 */
static int decode(SproutLha *a, const SproutLhaEntry *e, int dicbit, SproutLhaWrite write, void *user,
                  unsigned int *crc)
{
    unsigned long dicsiz = 1UL << dicbit, left = e->size;
    unsigned long r = 0;
    unsigned char *dic;
    Decoder *d;

    *crc = 0;
    d = (Decoder *)malloc(sizeof(Decoder));
    dic = (unsigned char *)malloc(dicsiz);
    if (d == NULL || dic == NULL) {
        free(d);
        free(dic);
        return fail(a, "out of memory");
    }
    memset(d, 0, sizeof(*d));
    memset(dic, ' ', dicsiz);
    d->a = a;
    d->np = dicbit <= 13 ? 14 : dicbit + 1;
    d->pbit = dicbit <= 13 ? 4 : 5;
    fillbuf(d, 16);

    while (left > 0 && !d->broken) {
        unsigned int c = decode_c(d);
        if (d->broken) break;
        if (c < 256) {
            dic[r++] = (unsigned char)c;
            --left;
        } else {
            unsigned long len = c - (256 - THRESHOLD);
            unsigned long from = (r - decode_p(d) - 1) & (dicsiz - 1);
            if (d->broken) break;
            if (len > left) len = left;
            left -= len;
            while (len-- > 0) {
                dic[r++] = dic[from];
                from = (from + 1) & (dicsiz - 1);
                if (r == dicsiz) {
                    *crc = crc16(*crc, dic, (long)r);
                    if (!write(user, dic, (long)r)) {
                        free(dic);
                        free(d);
                        return fail(a, "cannot write");
                    }
                    r = 0;
                }
            }
            continue;
        }
        if (r == dicsiz) {
            *crc = crc16(*crc, dic, (long)r);
            if (!write(user, dic, (long)r)) {
                free(dic);
                free(d);
                return fail(a, "cannot write");
            }
            r = 0;
        }
    }
    if (!d->broken && r > 0) {
        *crc = crc16(*crc, dic, (long)r);
        if (!write(user, dic, (long)r)) {
            free(dic);
            free(d);
            return fail(a, "cannot write");
        }
    }
    free(dic);
    if (d->broken) {
        free(d);
        return fail(a, "broken data in archive");
    }
    free(d);
    return 1;
}

int sprout_lha_extract(SproutLha *a, const SproutLhaEntry *e, SproutLhaWrite write, void *user)
{
    unsigned int crc = 0;
    int dicbit = 0;

    if (e->is_dir) return 1;
    if (strcmp(e->method, "-lh0-") == 0 || strcmp(e->method, "-lz4-") == 0) {
        unsigned char buf[1024];
        unsigned long left = e->size;
        if (e->packed != e->size) return fail(a, "broken header");
        while (left > 0) {
            long n = left > sizeof(buf) ? (long)sizeof(buf) : (long)left;
            if (!get_bytes(a, buf, n)) return fail(a, "archive ends in the data of a file");
            a->data_left -= (unsigned long)n;
            left -= (unsigned long)n;
            crc = crc16(crc, buf, n);
            if (!write(user, buf, n)) return fail(a, "cannot write");
        }
    } else {
        if (strcmp(e->method, "-lh4-") == 0) dicbit = 12;
        else if (strcmp(e->method, "-lh5-") == 0) dicbit = 13;
        else if (strcmp(e->method, "-lh6-") == 0) dicbit = 15;
        else if (strcmp(e->method, "-lh7-") == 0) dicbit = 16;
        else {
            fail(a, "unsupported method ");
            strcat(a->error, e->method);
            return -1;
        }
        if (decode(a, e, dicbit, write, user, &crc) < 0) return -1;
    }
    if (a->failed) return fail(a, "cannot read the archive");
    if (crc != e->crc) return fail(a, "CRC wrong (archive broken)");
    return 1;
}
