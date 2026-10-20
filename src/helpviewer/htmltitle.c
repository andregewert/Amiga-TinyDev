/**
 * @file htmltitle.c
 * @brief Title of an HTML file and URL helpers of the HelpViewer
 *        (portable, no Amiga headers).
 *
 * Plain C without the C library (no stdio, no ctype), so it runs in the
 * HelpViewer and in the host tests alike.
 */
#include "htmltitle.h"

/** A named character reference and its ISO-8859-1 code. */
typedef struct Entity {
    const char *name;   /**< the name, without '&' and ';' */
    unsigned char code; /**< the character */
} Entity;

/** The named references of ISO-8859-1 (and the four of plain XML). */
static const Entity entities[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
    { "nbsp", 160 }, { "iexcl", 161 }, { "cent", 162 }, { "pound", 163 }, { "curren", 164 },
    { "yen", 165 }, { "brvbar", 166 }, { "sect", 167 }, { "uml", 168 }, { "copy", 169 },
    { "ordf", 170 }, { "laquo", 171 }, { "not", 172 }, { "shy", 173 }, { "reg", 174 },
    { "macr", 175 }, { "deg", 176 }, { "plusmn", 177 }, { "sup2", 178 }, { "sup3", 179 },
    { "acute", 180 }, { "micro", 181 }, { "para", 182 }, { "middot", 183 }, { "cedil", 184 },
    { "sup1", 185 }, { "ordm", 186 }, { "raquo", 187 }, { "frac14", 188 }, { "frac12", 189 },
    { "frac34", 190 }, { "iquest", 191 }, { "Agrave", 192 }, { "Aacute", 193 }, { "Acirc", 194 },
    { "Atilde", 195 }, { "Auml", 196 }, { "Aring", 197 }, { "AElig", 198 }, { "Ccedil", 199 },
    { "Egrave", 200 }, { "Eacute", 201 }, { "Ecirc", 202 }, { "Euml", 203 }, { "Igrave", 204 },
    { "Iacute", 205 }, { "Icirc", 206 }, { "Iuml", 207 }, { "ETH", 208 }, { "Ntilde", 209 },
    { "Ograve", 210 }, { "Oacute", 211 }, { "Ocirc", 212 }, { "Otilde", 213 }, { "Ouml", 214 },
    { "times", 215 }, { "Oslash", 216 }, { "Ugrave", 217 }, { "Uacute", 218 }, { "Ucirc", 219 },
    { "Uuml", 220 }, { "Yacute", 221 }, { "THORN", 222 }, { "szlig", 223 }, { "agrave", 224 },
    { "aacute", 225 }, { "acirc", 226 }, { "atilde", 227 }, { "auml", 228 }, { "aring", 229 },
    { "aelig", 230 }, { "ccedil", 231 }, { "egrave", 232 }, { "eacute", 233 }, { "ecirc", 234 },
    { "euml", 235 }, { "igrave", 236 }, { "iacute", 237 }, { "icirc", 238 }, { "iuml", 239 },
    { "eth", 240 }, { "ntilde", 241 }, { "ograve", 242 }, { "oacute", 243 }, { "ocirc", 244 },
    { "otilde", 245 }, { "ouml", 246 }, { "divide", 247 }, { "oslash", 248 }, { "ugrave", 249 },
    { "uacute", 250 }, { "ucirc", 251 }, { "uuml", 252 }, { "yacute", 253 }, { "thorn", 254 },
    { "yuml", 255 }, { 0, 0 }
};

/**
 * @brief Fold an ASCII letter to lower case.
 * @param c The character.
 * @return Its lower-case form.
 */
static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

/**
 * @brief Find a string in the text, ignoring the case of ASCII letters.
 * @param text The text.
 * @param size Its length.
 * @param from Where to start.
 * @param what The string (lower case).
 * @return Its position, or -1.
 */
static long find(const char *text, long size, long from, const char *what)
{
    long i, k;
    for (i = from; i < size; ++i) {
        for (k = 0; what[k] != '\0' && i + k < size && lower(text[i + k]) == what[k]; ++k) ;
        if (what[k] == '\0') return i;
    }
    return -1;
}

/**
 * @brief Value of a hexadecimal digit.
 * @param c The character.
 * @return 0 to 15, or -1.
 */
static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/**
 * @brief Decode a character reference.
 * @param s The text after '&'.
 * @param n Number of characters left.
 * @param[out] used Characters used, including the ';'.
 * @return The character, or -1 if it is not a known reference.
 */
static int reference(const char *s, long n, long *used)
{
    long i;
    int k, value = 0;
    if (n > 1 && s[0] == '#') {
        int base = 10;
        i = 1;
        if (n > 2 && lower(s[1]) == 'x') { base = 16; i = 2; }
        for (k = 0; i < n && s[i] != ';' && k < 6; ++i, ++k) {
            int d = base == 16 ? hex(s[i]) : (s[i] >= '0' && s[i] <= '9' ? s[i] - '0' : -1);
            if (d < 0) return -1;
            value = value * base + d;
        }
        if (k == 0 || i >= n || s[i] != ';' || value <= 0 || value > 255) return -1;
        *used = i + 1;
        return value;
    }
    for (k = 0; entities[k].name != 0; ++k) {
        const char *e = entities[k].name;
        for (i = 0; e[i] != '\0' && i < n && s[i] == e[i]; ++i) ;
        if (e[i] == '\0' && i < n && s[i] == ';') {
            *used = i + 1;
            return entities[k].code;
        }
    }
    return -1;
}

int html_title(const char *text, long size, char *out, long out_size)
{
    long start, end, i, n = 0;
    int space = 0;
    if (out_size <= 0) return 0;
    out[0] = '\0';
    if ((start = find(text, size, 0, "<title")) < 0) return 0;
    while (start < size && text[start] != '>') ++start;
    if (start >= size) return 0;
    ++start;
    if ((end = find(text, size, start, "</title")) < 0) return 0;
    for (i = start; i < end && n < out_size - 1; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = n > 0;
            continue;
        }
        if (c == '&') {
            long used = 0;
            int r = reference(text + i + 1, end - i - 1, &used);
            if (r >= 0) {
                c = (unsigned char)r;
                i += used;
            }
        } else if ((c == 0xC2 || c == 0xC3) && i + 1 < end &&
                   ((unsigned char)text[i + 1] & 0xC0) == 0x80) {
            /* UTF-8 of the Latin-1 range */
            c = (unsigned char)(((c & 0x1F) << 6) | ((unsigned char)text[i + 1] & 0x3F));
            ++i;
        }
        if (c == 160) c = ' ';
        if (space && n < out_size - 2) out[n++] = ' ';
        space = 0;
        out[n++] = (char)c;
    }
    while (n > 0 && out[n - 1] == ' ') --n;
    out[n] = '\0';
    return n > 0;
}

int html_link_is_external(const char *url)
{
    long i;
    if (lower(url[0]) == 'f' && lower(url[1]) == 'i' && lower(url[2]) == 'l' &&
        lower(url[3]) == 'e' && url[4] == ':')
        return 0;
    for (i = 0; url[i] != '\0' && url[i] != '/' && url[i] != '#' && url[i] != '?'; ++i)
        if (url[i] == ':') return i > 1;    /* "x:" alone could be a volume */
    return 0;
}

int html_link_to_path(const char *url, char *out, long out_size)
{
    long n = 0;
    const char *s = url;
    if (lower(s[0]) == 'f' && lower(s[1]) == 'i' && lower(s[2]) == 'l' &&
        lower(s[3]) == 'e' && s[4] == ':') {
        s += 5;
        while (*s == '/' && s[1] == '/') s += 2;    /* file:// */
    }
    while (*s != '\0') {
        char c = *s;
        if (c == '#') {                             /* the anchor as it is */
            while (*s != '\0') {
                if (n >= out_size - 1) return 0;
                out[n++] = *s++;
            }
            break;
        }
        if (c == '.' && s[1] == '/' && (s == url || s[-1] == '/')) { s += 2; continue; }
        if (c == '.' && s[1] == '.' && s[2] == '/' && (s == url || s[-1] == '/')) {
            if (n >= out_size - 1) return 0;
            out[n++] = '/';                         /* AmigaDOS parent */
            s += 3;
            continue;
        }
        if (c == '%' && hex(s[1]) >= 0 && hex(s[2]) >= 0) {
            c = (char)(hex(s[1]) * 16 + hex(s[2]));
            s += 2;
        }
        if (n >= out_size - 1) return 0;
        out[n++] = c;
        ++s;
    }
    out[n] = '\0';
    return 1;
}
