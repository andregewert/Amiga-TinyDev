/**
 * @file sprout_lha.h
 * @brief Portable reader of LhA archives for Sprout: headers of levels 0,
 *        1 and 2, the methods -lh0-, -lz4-, -lh4- to -lh7- and drawer
 *        entries (-lhd-), with CRC-16 checks.
 *
 * No Amiga headers, so the host tests can use it. The archive is read
 * sequentially through a callback (no seeking): sprout_lha_next() gives
 * the next entry, sprout_lha_extract() decodes its data to a write
 * callback; data not extracted is skipped by the next sprout_lha_next().
 *
 * Paths are '/' separated: '\\' and 0xFF of the headers become '/', a
 * trailing '/' of a drawer is dropped. Amiga LhA keeps a file note after
 * a NUL in the name of level 0/1 headers; it is returned as the comment.
 */
#ifndef SPROUT_LHA_H
#define SPROUT_LHA_H

/** The size of an entry's path buffer. */
#define SPROUT_LHA_PATHMAX 512
/** The size of an entry's comment buffer. */
#define SPROUT_LHA_COMMENTMAX 80
/** The size of the input buffer. */
#define SPROUT_LHA_INBUF 4096

/**
 * @brief Reads bytes of the archive.
 * @param user The pointer given to sprout_lha_init().
 * @param buf Where the bytes go.
 * @param len How many are wanted.
 * @return The number read, 0 at the end, negative on an error.
 */
typedef long (*SproutLhaRead)(void *user, void *buf, long len);

/**
 * @brief Receives decoded bytes of an entry.
 * @param user The pointer given to sprout_lha_extract().
 * @param buf The bytes.
 * @param len Their number.
 * @return Non-zero on success, zero to stop with an error.
 */
typedef int (*SproutLhaWrite)(void *user, const void *buf, long len);

/** An entry of an archive. */
typedef struct SproutLhaEntry {
    char method[6];                         /**< e.g. "-lh5-" */
    unsigned long packed;                   /**< size of the packed data */
    unsigned long size;                     /**< size of the file */
    unsigned int crc;                       /**< CRC-16 of the file */
    int level;                              /**< header level, 0-2 */
    int os_id;                              /**< the OS id ('A' for Amiga, 0 if none) */
    int is_dir;                             /**< a drawer entry (-lhd-) */
    unsigned int attribute;                 /**< attribute byte (levels 0/1); Amiga LhA: the protection bits */
    char path[SPROUT_LHA_PATHMAX];          /**< the path, '/' separated */
    char comment[SPROUT_LHA_COMMENTMAX];    /**< the file note, "" if none */
} SproutLhaEntry;

/** The state of reading an archive. */
typedef struct SproutLha {
    SproutLhaRead read;                     /**< the read callback */
    void *user;                             /**< passed to it */
    unsigned char inbuf[SPROUT_LHA_INBUF];  /**< bytes read ahead */
    long inpos;                             /**< next byte in inbuf */
    long inlen;                             /**< bytes in inbuf */
    int eof;                                /**< the callback reported the end */
    int failed;                             /**< the callback reported an error */
    unsigned long data_left;                /**< packed bytes of the current entry not read yet */
    char error[96];                         /**< the reason of the last error */
} SproutLha;

/**
 * @brief Start reading an archive.
 * @param[out] a The state.
 * @param read The read callback.
 * @param user Passed to it.
 */
void sprout_lha_init(SproutLha *a, SproutLhaRead read, void *user);

/**
 * @brief Read the header of the next entry (skipping the data of the
 *        current one).
 * @param a The state.
 * @param[out] e The entry.
 * @return 1 for an entry; 0 at the end of the archive; -1 on an error
 *         (the reason in SproutLha::error).
 */
int sprout_lha_next(SproutLha *a, SproutLhaEntry *e);

/**
 * @brief Decode the data of the entry sprout_lha_next() returned and
 *        check its CRC.
 *
 * Needs memory for the dictionary (up to 64 KB for -lh7-) and the tables,
 * allocated with malloc() and freed before it returns.
 *
 * @param a The state.
 * @param e The entry.
 * @param write Receives the bytes.
 * @param user Passed to @p write.
 * @return 1 on success; -1 on an error (the reason in SproutLha::error):
 *         unknown method, broken data, wrong CRC, out of memory, or
 *         @p write failed.
 */
int sprout_lha_extract(SproutLha *a, const SproutLhaEntry *e, SproutLhaWrite write, void *user);

#endif /* SPROUT_LHA_H */
