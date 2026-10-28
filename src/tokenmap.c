/**
 * @file tokenmap.c
 * @brief The highlighting tokens of a document, found again by the text
 *        of a line (see tokenmap.h).
 */
#include "tokenmap.h"
#include "scout_protocol.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Checksum of a line's text.
 * @param text The text.
 * @param length Its length.
 * @return The checksum.
 */
static unsigned long line_hash(const char *text, size_t length)
{
    unsigned long h = 5381;
    size_t i;
    for (i = 0; i < length; i++) h = ((h << 5) + h) ^ (unsigned char)text[i];
    return h & 0xffffffffUL;
}

/**
 * @brief Return the style of a token kind.
 * @param kind SCOUT_TOKEN_ value.
 * @return The style, SYNTAX_NORMAL for kinds without a colour.
 */
static SyntaxStyle style_of(long kind)
{
    switch (kind) {
    case SCOUT_TOKEN_TYPE:      return SYNTAX_TYPE;
    case SCOUT_TOKEN_FUNCTION:  return SYNTAX_FUNCTION;
    case SCOUT_TOKEN_MACRO:     return SYNTAX_MACRO;
    case SCOUT_TOKEN_CONSTANT:  return SYNTAX_CONSTANT;
    default:                    return SYNTAX_NORMAL;
    }
}

long *tokenmap_read(const char *json, long *count)
{
    const char *p = json;
    long *values = NULL, n = 0, cap = 0;
    *count = 0;
    for (;;) {
        p = strstr(p, "\"tokens\"");
        if (p == NULL) return NULL;
        p += 8;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p != ':') continue;
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '[') break;
    }
    p++;
    for (;;) {
        char *end;
        long v;
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == ']' || *p == '\0') break;
        v = strtol(p, &end, 10);
        if (end == p) break;
        p = end;
        if (n == cap) {
            long *bigger = realloc(values, (size_t)(cap ? cap * 2 : 256) * sizeof(long));
            if (bigger == NULL) {
                free(values);
                return NULL;
            }
            values = bigger;
            cap = cap ? cap * 2 : 256;
        }
        values[n++] = v;
    }
    if (n == 0) {
        free(values);
        return NULL;
    }
    *count = n;
    return values;
}

TokenMap *tokenmap_build(const char *text, size_t length, const long *values, long count)
{
    TokenMap *map;
    const char *line = text, *end = text + length;
    long number = 1, t = 0, i;
    unsigned long size = 64, h;

    if (values == NULL || count < 4 || (map = calloc(1, sizeof(TokenMap))) == NULL) return NULL;
    map->spans = malloc((size_t)(count / 4) * sizeof(TokenSpan));
    map->lines = malloc((size_t)(count / 4) * sizeof(TokenLine));
    if (map->spans == NULL || map->lines == NULL) {
        tokenmap_free(map);
        return NULL;
    }
    /* walk the lines and the (sorted) tokens together */
    while (line <= end && t + 3 < count) {
        const char *nl = line;
        size_t len;
        TokenLine *tl = NULL;
        while (nl < end && *nl != '\n') nl++;
        len = (size_t)(nl - line);
        if (len > 0 && line[len - 1] == '\r') len--;
        while (t + 3 < count && values[t] < number) t += 4;
        while (t + 3 < count && values[t] == number) {
            SyntaxStyle style = style_of(values[t + 3]);
            long col = values[t + 1] - 1, tlen = values[t + 2];
            if (style != SYNTAX_NORMAL && col >= 0 && tlen > 0 && (size_t)(col + tlen) <= len && col < 65535) {
                if (tl == NULL) {
                    tl = &map->lines[map->line_count++];
                    tl->hash = line_hash(line, len);
                    tl->length = (unsigned long)len;
                    tl->first = map->span_count;
                    tl->count = 0;
                }
                map->spans[map->span_count].col = (unsigned short)col;
                map->spans[map->span_count].len = (unsigned short)(tlen < 65535 ? tlen : 65535);
                map->spans[map->span_count++].style = (unsigned char)style;
                tl->count++;
            }
            t += 4;
        }
        if (nl >= end) break;
        line = nl + 1;
        number++;
    }
    if (map->line_count == 0) {
        tokenmap_free(map);
        return NULL;
    }
    while (size < (unsigned long)map->line_count * 2 + 1) size *= 2;
    if ((map->slots = malloc(size * sizeof(long))) == NULL) {
        tokenmap_free(map);
        return NULL;
    }
    map->mask = size - 1;
    for (h = 0; h < size; h++) map->slots[h] = -1;
    for (i = 0; i < map->line_count; i++) {
        const TokenLine *tl = &map->lines[i];
        int known = 0;
        for (h = tl->hash & map->mask; map->slots[h] >= 0; h = (h + 1) & map->mask) {
            const TokenLine *o = &map->lines[map->slots[h]];
            if (o->hash == tl->hash && o->length == tl->length) { known = 1; break; }
        }
        if (!known) map->slots[h] = i;     /* the same text again: its first tokens */
    }
    return map;
}

void tokenmap_free(TokenMap *map)
{
    if (map == NULL) return;
    free(map->spans);
    free(map->lines);
    free(map->slots);
    free(map);
}

const TokenSpan *tokenmap_find(const TokenMap *map, const char *text, size_t length, long *count)
{
    unsigned long h, hash;
    *count = 0;
    if (map == NULL || map->slots == NULL) return NULL;
    hash = line_hash(text, length);
    for (h = hash & map->mask; map->slots[h] >= 0; h = (h + 1) & map->mask) {
        const TokenLine *tl = &map->lines[map->slots[h]];
        if (tl->hash == hash && tl->length == (unsigned long)length) {
            *count = tl->count;
            return &map->spans[tl->first];
        }
    }
    return NULL;
}
