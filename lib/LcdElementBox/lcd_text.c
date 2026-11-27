/*-----------------------------------------------------------/
/ lcd_text: UTF-8 to LCD font text conversion
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/

#include "lcd_text.h"

#define INVALID_CP 0xFFFFFFFFUL

size_t lcd_text_decode_utf8(const char* s, unsigned long* cp)
{
    const unsigned char* u = (const unsigned char*) s;
    unsigned long c = u[0];
    size_t len;
    unsigned long min;
    if (c < 0x80) { *cp = c; return 1; }
    else if ((c & 0xe0) == 0xc0) { len = 2; c &= 0x1f; min = 0x80; }
    else if ((c & 0xf0) == 0xe0) { len = 3; c &= 0x0f; min = 0x800; }
    else if ((c & 0xf8) == 0xf0) { len = 4; c &= 0x07; min = 0x10000; }
    else { *cp = INVALID_CP; return 1; }  // continuation byte or 0xF8-0xFF as a lead byte
    for (size_t i = 1; i < len; i++) {
        if ((u[i] & 0xc0) != 0x80) { *cp = INVALID_CP; return 1; }  // also stops at the terminator
        c = (c << 6) | (u[i] & 0x3f);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) { *cp = INVALID_CP; return 1; }
    *cp = c;
    return len;
}

int lcd_text_is_zero_width(unsigned long cp)
{
    return (cp >= 0x0300 && cp <= 0x036f)      // Combining Diacritical Marks
        || cp == 0x0e31                        // Thai MAI HAN-AKAT
        || (cp >= 0x0e34 && cp <= 0x0e3a)      // Thai vowel signs above/below, PHINTHU
        || (cp >= 0x0e47 && cp <= 0x0e4e)      // Thai MAITAIKHU, tone marks, THANTHAKHAT, NIKHAHIT, YAMAKKAN
        || (cp >= 0x1ab0 && cp <= 0x1aff)      // Combining Diacritical Marks Extended
        || (cp >= 0x1dc0 && cp <= 0x1dff)      // Combining Diacritical Marks Supplement
        || (cp >= 0x200b && cp <= 0x200f)      // ZWSP, ZWNJ, ZWJ, LRM, RLM
        || (cp >= 0x202a && cp <= 0x202e)      // bidi embedding controls
        || (cp >= 0x2060 && cp <= 0x2064)      // word joiner, invisible operators
        || (cp >= 0x20d0 && cp <= 0x20ff)      // Combining Diacritical Marks for Symbols
        || (cp >= 0x3099 && cp <= 0x309a)      // combining (semi-)voiced sound marks (NFD kana)
        || (cp >= 0xfe00 && cp <= 0xfe0f)      // variation selectors
        || (cp >= 0xfe20 && cp <= 0xfe2f)      // Combining Half Marks
        || cp == 0xfeff                        // ZWNBSP / BOM
        || (cp >= 0xe0100 && cp <= 0xe01ef);   // variation selectors supplement
}

size_t lcd_text_from_utf8(char* dst, size_t dst_size, const char* src)
{
    size_t di = 0;
    if (dst_size == 0) { return 0; }
    if (src != NULL) {
        while (*src != '\0' && di < dst_size - 1) {
            unsigned long cp;
            size_t len = lcd_text_decode_utf8(src, &cp);
            src += len;
            if (cp >= 0x20 && cp <= 0x7e) {
                dst[di++] = (char) cp;
            } else if (cp != INVALID_CP && lcd_text_is_zero_width(cp)) {
                // no column of its own
            } else {
                dst[di++] = LCD_TEXT_PLACEHOLDER;
            }
        }
    }
    dst[di] = '\0';
    return di;
}
