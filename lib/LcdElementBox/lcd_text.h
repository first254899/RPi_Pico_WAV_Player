/*-----------------------------------------------------------/
/ lcd_text: UTF-8 to LCD font text conversion
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Placeholder shown for a character the LCD font cannot draw
#define LCD_TEXT_PLACEHOLDER '?'

// Convert UTF-8 text (file names, tags) to what the LCD font can draw (printable ASCII 0x20-0x7E).
// - printable ASCII is copied
// - each other character that takes a column (any non-ASCII code point, ASCII control code) becomes
//   one LCD_TEXT_PLACEHOLDER, so the text keeps one column per character
// - characters that don't take a column of their own (combining marks such as Thai vowel/tone marks,
//   zero-width characters, variation selectors) are dropped
// - an invalid UTF-8 byte becomes one LCD_TEXT_PLACEHOLDER
// dst always gets a terminator (if dst_size > 0); output is cut at dst_size - 1 characters.
// src may be NULL (treated as ""). Returns the length of dst.
size_t lcd_text_from_utf8(char* dst, size_t dst_size, const char* src);

// Decode one UTF-8 character at s. Returns its length in bytes (1..4) and stores the code point,
// or returns 1 and stores 0xFFFFFFFF for an invalid byte (also for overlong forms, surrogates, > U+10FFFF).
// s must point to a non-NUL byte.
size_t lcd_text_decode_utf8(const char* s, unsigned long* cp);

// Non-zero if the code point is drawn on top of the previous character (takes no column)
int lcd_text_is_zero_width(unsigned long cp);

#ifdef __cplusplus
}
#endif
