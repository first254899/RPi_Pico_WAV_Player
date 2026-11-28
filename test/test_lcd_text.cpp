/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

// Host-side unit test for lcd_text (lib/LcdElementBox/lcd_text.*):
// UTF-8 file names / tags to LCD font text

#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#include "lcd_text.h"

static int g_failed = 0;
static int g_checks = 0;

static void check_str(const char* src, const char* expected, size_t dst_size, int line)
{
    g_checks++;
    char dst[1024];
    memset(dst, 0x5a, sizeof(dst));
    size_t len = lcd_text_from_utf8(dst, dst_size, src);
    bool ok = (strcmp(dst, expected) == 0) && (len == strlen(expected)) && (dst[dst_size] == 0x5a);
    if (!ok) {
        g_failed++;
        printf("  FAIL line %d: got \"%s\" (len %zu), expected \"%s\"\n", line, dst, len, expected);
    }
}
#define CHECK_TEXT(src, expected) check_str(src, expected, 256, __LINE__)
#define CHECK_TEXT_N(src, expected, n) check_str(src, expected, n, __LINE__)

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main()
{
    // ASCII is unchanged
    CHECK_TEXT("01 Track.wav", "01 Track.wav");
    CHECK_TEXT(" !\"#$%&'()*+,-./0123456789:;<=>?@AZ[\\]^_`az{|}~", " !\"#$%&'()*+,-./0123456789:;<=>?@AZ[\\]^_`az{|}~");
    CHECK_TEXT("", "");
    CHECK_TEXT(nullptr, "");

    // ASCII control codes and DEL would index outside the font
    CHECK_TEXT("a\tb\x7f" "c\x01", "a?b?c?");

    // Thai: one column per base character, vowel/tone marks above/below take none
    // "เพลงไทย" (7 base characters)
    CHECK_TEXT("\xe0\xb9\x80\xe0\xb8\x9e\xe0\xb8\xa5\xe0\xb8\x87\xe0\xb9\x84\xe0\xb8\x97\xe0\xb8\xa2.wav", "???????.wav");
    // "เพลงที่ 1" : ท + ี (U+0E35) + ่ (U+0E48) -> one column
    CHECK_TEXT("\xe0\xb9\x80\xe0\xb8\x9e\xe0\xb8\xa5\xe0\xb8\x87\xe0\xb8\x97\xe0\xb8\xb5\xe0\xb9\x88 1", "????? 1");
    // MAI HAN-AKAT U+0E31 and SARA AM U+0E33 (SARA AM takes a column)
    CHECK_TEXT("\xe0\xb8\x81\xe0\xb8\xb1\xe0\xb8\x99\xe0\xb8\x97\xe0\xb8\xb3", "????");
    // Thai digits take a column
    CHECK_TEXT("\xe0\xb9\x91\xe0\xb9\x92", "??");

    // Japanese / CJK / emoji (2-, 3-, 4-byte)
    CHECK_TEXT("\xc3\xa9t\xc3\xa9", "?t?");                                // été
    CHECK_TEXT("\xe6\x9b\xb2 01", "? 01");                                // 曲 01
    CHECK_TEXT("\xf0\x9f\x8e\xb5 song", "? song");                         // U+1F3B5
    // decomposed: e + U+0301, NFD kana か + U+3099, emoji + VS16, ZWJ, BOM
    CHECK_TEXT("e\xcc\x81", "e");
    CHECK_TEXT("\xe3\x81\x8b\xe3\x82\x99", "?");
    CHECK_TEXT("\xe2\x9d\xa4\xef\xb8\x8f!", "?!");
    CHECK_TEXT("\xef\xbb\xbf" "abc", "abc");
    CHECK_TEXT("a\xe2\x80\x8d" "b", "ab");

    // invalid UTF-8: one placeholder per bad byte, never reads past the terminator
    CHECK_TEXT("\x80" "a", "?a");                     // stray continuation
    CHECK_TEXT("\xc3" "a", "?a");                     // missing continuation
    CHECK_TEXT("a\xe0\xb8", "a??");                   // truncated at end
    CHECK_TEXT("\xc0\xaf", "??");                     // overlong '/'
    CHECK_TEXT("\xe0\x80\xaf", "???");                // overlong
    CHECK_TEXT("\xed\xa0\x80", "???");                // surrogate U+D800
    CHECK_TEXT("\xf4\x90\x80\x80", "????");           // > U+10FFFF
    CHECK_TEXT("\xff\xfe" "x", "??x");
    // CP932 (Shift_JIS) bytes, e.g. a name written by an old tool: no crash, placeholders
    CHECK_TEXT("\x83\x65\x83\x58\x83\x67.wav", "?e?X?g.wav");

    // bounded output, always terminated, never cut inside a placeholder run
    CHECK_TEXT_N("abcdef", "abc", 4);
    CHECK_TEXT_N("\xe6\x9b\xb2\xe6\x9b\xb2\xe6\x9b\xb2", "??", 3);
    CHECK_TEXT_N("abc", "", 1);
    {
        g_checks++;
        char d = 'x';
        if (lcd_text_from_utf8(&d, 0, "abc") != 0 || d != 'x') { g_failed++; printf("  FAIL dst_size 0\n"); }
    }
    // long name (255 Thai characters = 765 bytes) fits the 256 column TextBox buffer as 255 columns
    {
        std::string s;
        for (int i = 0; i < 255; i++) { s += "\xe0\xb8\x81"; }
        std::string e(255, '?');
        CHECK_TEXT(s.c_str(), e.c_str());
    }

    // decoder
    {
        unsigned long cp;
        CHECK(lcd_text_decode_utf8("A", &cp) == 1 && cp == 'A');
        CHECK(lcd_text_decode_utf8("\xe0\xb8\x81", &cp) == 3 && cp == 0x0e01);
        CHECK(lcd_text_decode_utf8("\xf0\x9f\x8e\xb5", &cp) == 4 && cp == 0x1f3b5);
        CHECK(lcd_text_decode_utf8("\xc3", &cp) == 1 && cp == 0xFFFFFFFFUL);
        CHECK(lcd_text_is_zero_width(0x0e48) && lcd_text_is_zero_width(0x0e31) && !lcd_text_is_zero_width(0x0e33));
        CHECK(!lcd_text_is_zero_width('a') && !lcd_text_is_zero_width(0x0e01));
    }

    // fuzz: random bytes never overflow, output is printable ASCII only, length bounded by input length
    {
        std::mt19937 rng(1234);
        for (int iter = 0; iter < 20000; iter++) {
            size_t n = rng() % 40;
            std::string s;
            for (size_t i = 0; i < n; i++) { s += static_cast<char>(1 + rng() % 255); }
            size_t dst_size = 1 + rng() % 48;
            char* dst = new char[dst_size];  // exact size: ASan catches any overrun
            size_t len = lcd_text_from_utf8(dst, dst_size, s.c_str());
            bool ok = len < dst_size && len <= n && strlen(dst) == len;
            for (size_t i = 0; i < len; i++) { ok = ok && dst[i] >= 0x20 && dst[i] <= 0x7e; }
            g_checks++;
            if (!ok) { g_failed++; printf("  FAIL fuzz iter %d\n", iter); }
            delete[] dst;
        }
    }

    printf("test_lcd_text: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
