#include "utf8.h"

/** Decodes one UTF-8 sequence; an invalid byte gives UTF8_INVALID_BASE + byte and length 1. */
uint32_t utf8_decode(const unsigned char* s, size_t left, size_t* length) {
    unsigned char b = s[0];
    uint32_t c;
    size_t need, i;

    if (b < 0x80) {
        *length = 1;
        return b;
    }
    if (b >= 0xC2 && b <= 0xDF) {
        need = 1;
        c = b & 0x1F;
    } else if (b >= 0xE0 && b <= 0xEF) {
        need = 2;
        c = b & 0x0F;
    } else if (b >= 0xF0 && b <= 0xF4) {
        need = 3;
        c = b & 0x07;
    } else {
        *length = 1;
        return UTF8_INVALID_BASE + b;
    }
    if (left < need + 1) {
        *length = 1;
        return UTF8_INVALID_BASE + b;
    }
    for (i = 1; i <= need; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *length = 1;
            return UTF8_INVALID_BASE + b;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    // overlong forms, surrogates and what lies beyond U+10FFFF are not characters
    if ((need == 2 && c < 0x800) || (need == 3 && c < 0x10000) || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) {
        *length = 1;
        return UTF8_INVALID_BASE + b;
    }
    *length = need + 1;
    return c;
}

/*
 * Case mapping: ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic, by their layout. The C library's
 * towlower knows nothing beyond ASCII in the "C" locale, which is the one this program runs in.
 */
uint32_t utf8_fold(uint32_t c) {
    if (c < 0x80)
        return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
        return c + 32;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x178) {
        // pairs of an even upper case and the odd lower case, except in 0x139-0x148 and 0x179-0x17E
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
            return (c & 1) ? c + 1 : c;
        return (c & 1) ? c : c + 1;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2)
        return c + 32;
    if (c >= 0x410 && c <= 0x42F)
        return c + 32;
    if (c >= 0x400 && c <= 0x40F)
        return c + 80;
    return c;
}

uint32_t utf8_upper(uint32_t c) {
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') ? c - 32 : c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7)
        return c - 32;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x178) {
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
            return (c & 1) ? c : c - 1;
        return (c & 1) ? c - 1 : c;
    }
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2)
        return c - 32;
    if (c >= 0x430 && c <= 0x44F)
        return c - 32;
    if (c >= 0x450 && c <= 0x45F)
        return c - 80;
    return c;
}


size_t utf8_encode(uint32_t c, char* out) {
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c >= UTF8_INVALID_BASE) {
        out[0] = (char)(c - UTF8_INVALID_BASE);     // the byte it stood for
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000) {
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}
