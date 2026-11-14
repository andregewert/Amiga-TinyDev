/**
 * @file buildmsg.c
 * @brief Portable reader of the file and line in a compiler's message
 *        (see buildmsg.h).
 */
#include "buildmsg.h"

#include <string.h>

/**
 * @brief Read a decimal number.
 * @param text The text.
 * @param length Its length.
 * @param[in,out] pos Where the number starts; after it on success.
 * @param[out] value The number.
 * @return Non-zero if at least one digit was there.
 */
static int number(const char *text, size_t length, size_t *pos, long *value)
{
    size_t i = *pos;
    long v = 0;
    if (i >= length || text[i] < '0' || text[i] > '9') return 0;
    while (i < length && text[i] >= '0' && text[i] <= '9') {
        if (v < 100000000L) v = v * 10 + (text[i] - '0');
        ++i;
    }
    *pos = i;
    *value = v;
    return 1;
}

/**
 * @brief Check for a word at a position, ignoring case.
 * @param text The text.
 * @param length Its length.
 * @param pos The position.
 * @param word The word (lower case).
 * @return Non-zero if the word starts there.
 */
static int word_at(const char *text, size_t length, size_t pos, const char *word)
{
    size_t n = strlen(word), i;
    if (pos + n > length) return 0;
    for (i = 0; i < n; ++i) {
        char c = text[pos + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != word[i]) return 0;
    }
    return 1;
}

/**
 * @brief Check for one of the words that tell the kind of a message.
 * @param text The text.
 * @param length Its length.
 * @param pos The position.
 * @return Non-zero if such a word starts there.
 */
static int kind_at(const char *text, size_t length, size_t pos)
{
    static const char *const kinds[] = { "error", "warning", "fatal", "note", "hint" };
    size_t i;
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
        if (word_at(text, length, pos, kinds[i])) return 1;
    return 0;
}

/**
 * @brief Store a file name in a message.
 * @param[out] m The message.
 * @param text The name (need not be terminated).
 * @param length Its length.
 * @return Non-zero if it fits and is not empty.
 */
static int set_file(BuildMessage *m, const char *text, size_t length)
{
    if (length == 0 || length >= BUILDMSG_FILE_MAX) return 0;
    memcpy(m->file, text, length);
    m->file[length] = '\0';
    return 1;
}

/**
 * @brief vbcc and vasm: <tt>... in line 12 of "file"</tt> (or <tt>from
 *        line</tt>); BAsm: <tt>Error in Line 12 in 'file'.</tt>
 * @param text The line.
 * @param length Its length.
 * @param[out] m The place.
 * @return Non-zero if found.
 */
static int parse_vbcc(const char *text, size_t length, BuildMessage *m)
{
    size_t i, pos, start;
    char quote;
    for (i = 0; i + 8 < length; ++i) {
        if (word_at(text, length, i, " in line ")) pos = i + 9;
        else if (word_at(text, length, i, " from line ")) pos = i + 11;     /* vasm's includes */
        else continue;
        if (!number(text, length, &pos, &m->line)) continue;
        if (word_at(text, length, pos, " of \"")) quote = '"';
        else if (word_at(text, length, pos, " in '")) quote = '\'';     /* BAsm */
        else continue;
        start = pos + 5;
        for (pos = start; pos < length && text[pos] != quote; ++pos) {}
        if (pos >= length || !set_file(m, text + start, pos - start)) continue;
        m->col = 0;
        return 1;
    }
    return 0;
}

/**
 * @brief Free Pascal: <tt>file(12,5) Error: ...</tt>.
 * @param text The line.
 * @param length Its length.
 * @param[out] m The place.
 * @return Non-zero if found.
 */
static int parse_fpc(const char *text, size_t length, BuildMessage *m)
{
    size_t open, pos;
    for (open = 1; open < length; ++open) {
        if (text[open] != '(' || text[open - 1] == ' ') continue;
        pos = open + 1;
        if (!number(text, length, &pos, &m->line)) continue;
        m->col = 0;
        if (pos < length && text[pos] == ',') {
            ++pos;
            if (!number(text, length, &pos, &m->col)) continue;
        }
        if (pos + 1 >= length || text[pos] != ')' || text[pos + 1] != ' ') continue;
        if (!kind_at(text, length, pos + 2)) continue;
        return set_file(m, text, open);
    }
    return 0;
}

/**
 * @brief SAS/C: <tt>file 12 Error 3: ...</tt>.
 * @param text The line.
 * @param length Its length.
 * @param[out] m The place.
 * @return Non-zero if found.
 */
static int parse_sasc(const char *text, size_t length, BuildMessage *m)
{
    size_t end, pos;
    for (end = 0; end < length && text[end] != ' '; ++end) {}
    /* not a summary such as "Errors: 1 Warnings: 0" (BAsm) */
    if (end == 0 || end >= length || text[end - 1] == ':') return 0;
    pos = end + 1;
    if (!number(text, length, &pos, &m->line)) return 0;
    if (pos >= length || text[pos] != ' ' || !kind_at(text, length, pos + 1)) return 0;
    m->col = 0;
    return set_file(m, text, end);
}

/**
 * @brief Check whether a text holds something other than digits.
 * @param text The text.
 * @param length Its length.
 * @return Non-zero if a character is not a digit.
 */
static int has_letter(const char *text, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i)
        if (text[i] < '0' || text[i] > '9') return 1;
    return 0;
}

/**
 * @brief gcc and GNU make: <tt>file:12:5:</tt> or <tt>file:12:</tt>.
 *
 * The file is the run of characters without blanks before the first
 * colon that is followed by digits and another colon (so the colon of a
 * volume name stays part of it).
 *
 * @param text The line.
 * @param length Its length.
 * @param[out] m The place.
 * @return Non-zero if found.
 */
static int parse_gcc(const char *text, size_t length, BuildMessage *m)
{
    size_t colon, pos, start;
    for (colon = 1; colon < length; ++colon) {
        if (text[colon] != ':' || text[colon - 1] == ' ') continue;
        pos = colon + 1;
        if (!number(text, length, &pos, &m->line)) continue;
        if (pos >= length || (text[pos] != ':' && text[pos] != ',')) continue;
        m->col = 0;
        if (text[pos] == ':') {
            size_t p2 = pos + 1;
            long col;
            if (number(text, length, &p2, &col) && p2 < length && text[p2] == ':') m->col = col;
        }
        for (start = colon; start > 0 && text[start - 1] != ' ' && text[start - 1] != '\t' &&
                            text[start - 1] != '[' && text[start - 1] != '('; --start) {}
        if (!has_letter(text + start, colon - start)) continue;    /* a time such as 12:30:45 */
        return set_file(m, text + start, colon - start);
    }
    return 0;
}

/**
 * @brief E-VO and ec: <tt>LINE 12:</tt> (also after <tt>ERROR: </tt>) or
 *        <tt>(line 12)</tt> at the end of a warning; no file.
 * @param text The line.
 * @param length Its length.
 * @param[out] m The place.
 * @return Non-zero if found.
 */
static int parse_e(const char *text, size_t length, BuildMessage *m)
{
    size_t pos = 0;
    if (length >= 7 && memcmp(text, "ERROR: ", 7) == 0) pos = 7;
    if (pos + 5 <= length && memcmp(text + pos, "LINE ", 5) == 0) {
        pos += 5;
        if (number(text, length, &pos, &m->line) && pos < length && text[pos] == ':') {
            m->file[0] = '\0';
            m->col = 0;
            return 1;
        }
    }
    if (length >= 9 && memcmp(text, "WARNING: ", 9) == 0) {
        size_t end = length, open;
        while (end > 0 && text[end - 1] == ' ') --end;
        if (end == 0 || text[end - 1] != ')') return 0;
        for (open = end - 1; open > 0 && text[open] != '('; --open) {}
        if (text[open] != '(' || !word_at(text, length, open, "(line ")) return 0;
        pos = open + 6;
        if (number(text, length, &pos, &m->line) && pos == end - 1) {
            m->file[0] = '\0';
            m->col = 0;
            return 1;
        }
    }
    return 0;
}

int buildmsg_parse(const char *text, size_t length, BuildMessage *m)
{
    m->file[0] = '\0';
    m->line = 0;
    m->col = 0;
    while (length > 0 && (text[length - 1] == '\r' || text[length - 1] == '\n')) --length;
    if ((parse_vbcc(text, length, m) || parse_fpc(text, length, m) || parse_e(text, length, m) ||
         parse_sasc(text, length, m) || parse_gcc(text, length, m)) && m->line > 0)
        return 1;
    m->file[0] = '\0';
    m->line = 0;
    m->col = 0;
    return 0;
}
