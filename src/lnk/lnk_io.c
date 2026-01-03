/*
 *  LNK I/O helpers
 *
 * Low-level binary reading helpers used by the .lnk parser:
 *  - UTF-16LE -> UTF-8 conversion
 *  - reading StringData entries (Shell Link format)
 *  - reading NUL-terminated strings (ANSI / UTF-16LE)
 */

#include "open_lnk/lnk_io.h"

#include <stdlib.h>
#include <string.h>

char *lnk_utf16le_to_utf8(const uint16_t *wstr, size_t max_chars) {
    if (!wstr) return NULL;

    /*
     * Find the length (in UTF-16 code units) up to:
     *   - the first NUL (0x0000), or
     *   - max_chars (safety bound)
     */
    size_t len = 0;
    while (len < max_chars && wstr[len] != 0) len++;

    /*
     * UTF-8 worst case is 4 bytes per Unicode codepoint, plus a final '\0'.
     * Allocating len*4 is safe because each UTF-16 code unit maps to <= 4 bytes
     * (even surrogate pairs consume 2 code units but still only 4 bytes).
     */
    char *out = (char*)malloc(len * 4 + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t wc = wstr[i];

        /*
         * UTF-16 surrogate handling:
         *   - High surrogate: 0xD800..0xDBFF
         *   - Low surrogate:  0xDC00..0xDFFF
         *
         * A valid non-BMP codepoint is encoded as a pair:
         *   high + low -> codepoint in U+10000..U+10FFFF
         */
        if (wc >= 0xD800 && wc <= 0xDBFF) {
            if (i + 1 < len) {
                uint32_t wc2 = wstr[i + 1];
                if (wc2 >= 0xDC00 && wc2 <= 0xDFFF) {
                    uint32_t high = wc - 0xD800;
                    uint32_t low  = wc2 - 0xDC00;
                    uint32_t cp = 0x10000 + ((high << 10) | low);

                    /*
                     * UTF-8 encoding for codepoints >= U+10000 uses 4 bytes:
                     *   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                     *
                     * The shifts extract the right groups of bits from `cp`.
                     */
                    out[j++] = (char)(0xF0 | (cp >> 18));
                    out[j++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    out[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    out[j++] = (char)(0x80 | (cp & 0x3F));
                    i++;
                    continue;
                }
            }
            /* Invalid/missing low surrogate -> emit U+FFFD (replacement char). */
            out[j++] = (char)0xEF; out[j++] = (char)0xBF; out[j++] = (char)0xBD;
            continue;
        } else if (wc >= 0xDC00 && wc <= 0xDFFF) {
            /* Low surrogate without a preceding high surrogate -> U+FFFD. */
            out[j++] = (char)0xEF; out[j++] = (char)0xBF; out[j++] = (char)0xBD;
            continue;
        }

        /* Encode a single BMP codepoint (<= U+FFFF) as UTF-8. */
        if (wc < 0x80) {
            /* 1-byte UTF-8: 0xxxxxxx */
            out[j++] = (char)wc;
        } else if (wc < 0x800) {
            /* 2-byte UTF-8: 110xxxxx 10xxxxxx */
            out[j++] = (char)(0xC0 | (wc >> 6));
            out[j++] = (char)(0x80 | (wc & 0x3F));
        } else {
            /* 3-byte UTF-8: 1110xxxx 10xxxxxx 10xxxxxx */
            out[j++] = (char)(0xE0 | (wc >> 12));
            out[j++] = (char)(0x80 | ((wc >> 6) & 0x3F));
            out[j++] = (char)(0x80 | (wc & 0x3F));
        }
    }

    out[j] = 0;
    return out;
}

char *lnk_read_string_data(FILE *f, int unicode) {
    /*
     * LNK "StringData" starts with a 16-bit count.
     * - If unicode != 0: count is number of UTF-16 code units.
     * - Else:           count is number of bytes.
     */
    uint16_t count = 0;
    if (fread(&count, sizeof(count), 1, f) != 1) return NULL;
    if (count == 0) return strdup("");

    if (unicode) {
        /* Read `count` UTF-16 code units, then convert to UTF-8. */
        uint16_t *buf = (uint16_t*)malloc((size_t)count * sizeof(uint16_t));
        if (!buf) return NULL;
        if (fread(buf, sizeof(uint16_t), count, f) != count) { free(buf); return NULL; }
        char *out = lnk_utf16le_to_utf8(buf, count);
        free(buf);
        return out;
    }

    /* ANSI/bytes path: read raw bytes and NUL-terminate. */
    char *buf = (char*)malloc((size_t)count + 1);
    if (!buf) return NULL;
    if (fread(buf, 1, count, f) != count) { free(buf); return NULL; }
    buf[count] = 0;
    return buf;
}

char *lnk_read_c_string(FILE *f, size_t cap) {
    /*
     * Read bytes until we hit a NUL byte.
     * We grow the buffer as needed, but never beyond `cap`.
     */
    size_t alloc = 256, j = 0;
    char *s = (char*)malloc(alloc);
    if (!s) return NULL;

    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == 0) break;
        if (j + 1 >= alloc) {
            size_t newa = alloc * 2;
            if (newa > cap) newa = cap;
            char *tmp = (char*)realloc(s, newa);
            if (!tmp) { free(s); return NULL; }
            s = tmp; alloc = newa;
        }
        s[j++] = (char)c;
        if (j + 1 >= cap) break;
    }
    s[j] = 0;
    return s;
}

char *lnk_read_w_string(FILE *f, size_t max_chars) {
    /*
     * Read UTF-16LE code units until we hit a 0x0000 terminator.
     * We keep a safety upper bound (`max_chars`) to avoid runaway reads.
     */
    size_t alloc = 256, j = 0;
    uint16_t *w = (uint16_t*)malloc(alloc * sizeof(uint16_t));
    if (!w) return NULL;

    uint16_t ch;
    while (fread(&ch, sizeof(ch), 1, f) == 1) {
        if (ch == 0) break;
        if (j + 1 >= alloc) {
            size_t newa = alloc * 2;
            if (newa > max_chars) newa = max_chars;
            uint16_t *tmp = (uint16_t*)realloc(w, newa * sizeof(uint16_t));
            if (!tmp) { free(w); return NULL; }
            w = tmp; alloc = newa;
        }
        w[j++] = ch;
        if (j + 1 >= max_chars) break;
    }
    w[j] = 0;

    char *out = lnk_utf16le_to_utf8(w, j + 1);
    free(w);
    return out;
}
