/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

// Host-side test of lib/file_menu/file_menu_FatFs.c (built as C, unmodified) against an in-memory
// directory tree with fault injection: UTF-8 names and their order, extension match, and the
// error paths (f_opendir/f_readdir errors, out of memory). ASan/LSan catch use-after-free and leaks,
// an alarm turns an endless loop into a failure.

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

#include "file_menu_FatFs.h"

static int g_failed = 0;
static int g_checks = 0;
static const char* g_stage = "";
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL line %d: %s (%s)\n", __LINE__, #cond, g_stage); } } while (0)

static int g_sectionFailed = 0;
static void section(const char* name)
{
    printf("%s %s\n", (g_failed == g_sectionFailed) ? "[ OK ]" : "[FAIL]", name);
    g_sectionFailed = g_failed;
}

//=================================
// in-memory FatFs
//=================================
struct Entry { std::string name; BYTE attr; };
static std::map<std::string, std::vector<Entry>> g_dirs;  // "/", "/a/", ...
static std::vector<std::string> g_dirIds;
static std::string g_cwd = "/";
static bool g_failOpendir = false;
static int g_failReaddirAt = -1;     // f_readdir of entry index >= this fails (fno untouched)
static bool g_failRewind = false;
static long g_readdirCalls = 0;
static int g_allocCount = 0;
static int g_failAllocAt = -1;       // n-th allocation by file_menu fails

extern "C" {
void* test_malloc(size_t n) { return (g_allocCount++ == g_failAllocAt) ? nullptr : malloc(n); }
void* test_calloc(size_t n, size_t s) { return (g_allocCount++ == g_failAllocAt) ? nullptr : calloc(n, s); }

FRESULT f_mount(FATFS* fs, const TCHAR*, BYTE) { fs->fs_type = 4; return FR_OK; }
FRESULT f_unmount(const TCHAR*) { return FR_OK; }
FRESULT f_chdir(const TCHAR* path)
{
    std::string p(path);
    std::string next;
    if (p == "/") { next = "/"; }
    else if (p == "..") {
        if (g_cwd == "/") { return FR_OK; }
        next = g_cwd.substr(0, g_cwd.rfind('/', g_cwd.size() - 2) + 1);
    } else { next = g_cwd + p + "/"; }
    if (!g_dirs.count(next)) { return FR_NO_PATH; }
    g_cwd = next;
    return FR_OK;
}
FRESULT f_opendir(DIR* dp, const TCHAR*)
{
    if (g_failOpendir) { dp->valid = 0; return FR_DISK_ERR; }
    g_dirIds.push_back(g_cwd);
    dp->valid = 1;
    dp->dirId = static_cast<int>(g_dirIds.size() - 1);
    dp->index = 0;
    return FR_OK;
}
FRESULT f_closedir(DIR* dp)
{
    if (!dp->valid) { return FR_INVALID_OBJECT; }
    dp->valid = 0;
    return FR_OK;
}
FRESULT f_readdir(DIR* dp, FILINFO* fno)
{
    g_readdirCalls++;
    if (!dp->valid) { return FR_INVALID_OBJECT; }
    if (fno == nullptr) {
        if (g_failRewind) { return FR_DISK_ERR; }
        dp->index = 0;
        return FR_OK;
    }
    if (g_failReaddirAt >= 0 && dp->index >= g_failReaddirAt) { return FR_DISK_ERR; }
    const auto& v = g_dirs[g_dirIds[dp->dirId]];
    if (dp->index >= static_cast<int>(v.size())) { fno->fname[0] = '\0'; return FR_OK; }
    const Entry& e = v[dp->index++];
    snprintf(fno->fname, sizeof(fno->fname), "%s", e.name.c_str());
    fno->fattrib = e.attr;
    return FR_OK;
}
}  // extern "C"

// order the file menu must produce: directories first, then byte order (unsigned, i.e. code point order
// for UTF-8) ignoring a leading "The "
static std::vector<std::string> expectedOrder(const std::vector<Entry>& dir)
{
    std::vector<Entry> v{{"..", AM_DIR}};
    for (auto& e : dir) {
        if (e.name[0] == '.' || (e.attr & AM_HID)) { continue; }
        v.push_back(e);
    }
    auto key = [](const std::string& s) { return s.compare(0, 4, "The ") == 0 ? s.substr(4) : s; };
    std::sort(v.begin(), v.end(), [&](const Entry& a, const Entry& b) {
        bool fa = !(a.attr & AM_DIR), fb = !(b.attr & AM_DIR);
        if (fa != fb) { return fb; }
        return key(a.name) < key(b.name);  // std::string compares as unsigned char
    });
    std::vector<std::string> names;
    for (auto& e : v) { names.push_back(e.name); }
    return names;
}

static std::vector<std::string> listed()
{
    std::vector<std::string> v;
    for (uint16_t i = 0; i < file_menu_get_num(); i++) {
        char s[FF_LFN_BUF + 1];
        file_menu_get_fname(i, s, sizeof(s));
        v.push_back(s);
    }
    return v;
}

static const char* TH1 = "\xe0\xb9\x80\xe0\xb8\x9e\xe0\xb8\xa5\xe0\xb8\x87\xe0\xb9\x84\xe0\xb8\x97\xe0\xb8\xa2.wav";  // เพลงไทย.wav
static const char* TH2 = "\xe0\xb8\x81\xe0\xb8\xb2\xe0\xb8\xa3\xe0\xb9\x8c.WAV";                                    // การ์.WAV
static const char* JP1 = "\xe6\x9b\xb2 01.wav";                                                                    // 曲 01.wav

int main()
{
    alarm(60);  // an endless loop (f_readdir error ignored) kills the test

    std::string longThai;
    for (int i = 0; i < 233; i++) { longThai += "\xe0\xb8\x81"; }  // 699 bytes
    longThai += ".wav";
    g_dirs["/"] = {
        {".hidden", 0}, {"zeta.wav", 0}, {"Alpha.wav", 0}, {"alpha.wav", 0}, {TH1, 0}, {TH2, 0}, {JP1, 0},
        {"The Band.wav", 0}, {"cover.JPG", 0}, {"notes.wave", 0}, {"x.wav.txt", 0}, {"Mixed.WaV", 0},
        {"hidden.wav", AM_HID}, {"sub", AM_DIR}, {"Album", AM_DIR}, {"\xe0\xb8\x82 dir", AM_DIR}, {longThai, 0},
        {"noext", 0}, {"a.jpeg", 0},
    };
    g_dirs["/sub/"] = {{".", AM_DIR}, {"..", AM_DIR}, {"s1.wav", 0}, {"s2.wav", 0}};
    g_dirs["/Album/"] = {{".", AM_DIR}, {"..", AM_DIR}};
    for (int i = 0; i < 300; i++) { g_dirs["/Album/"].push_back({"track" + std::to_string(i) + ".wav", 0}); }
    g_dirs["/\xe0\xb8\x82 dir/"] = {{"in.wav", 0}};

    uint8_t fsType;
    CHECK(file_menu_init(&fsType) == FR_OK);

    // listing and order with UTF-8 names
    {
        g_stage = "list";
        CHECK(file_menu_open_dir("/") == FR_OK);
        file_menu_full_sort();
        auto got = listed();
        auto exp = expectedOrder(g_dirs["/"]);
        CHECK(got == exp);
        if (got != exp) { for (size_t i = 0; i < got.size(); i++) { printf("    %zu: %s\n", i, got[i].c_str()); } }
        CHECK(file_menu_get_num() == 18);
        CHECK(file_menu_get_dir_num() == 3);
        // Thai/Japanese after ASCII (unsigned compare); before the fix bytes >= 0x80 sorted first
        auto pos = [&](const std::string& n) { return std::find(got.begin(), got.end(), n) - got.begin(); };
        CHECK(pos(TH1) > pos("zeta.wav"));
        CHECK(pos(JP1) > pos(TH1));
        CHECK(pos("The Band.wav") < pos("cover.JPG"));  // sorted as "Band.wav"
        section("UTF-8 names listed and sorted in code point order");
    }

    // names: full name through the pointer, bounded copy cut at a character boundary
    {
        g_stage = "names";
        uint16_t idx = 0;
        for (uint16_t i = 0; i < file_menu_get_num(); i++) { if (longThai == file_menu_get_fname_ptr(i)) { idx = i; } }
        CHECK(idx != 0);
        char s[256];
        memset(s, 'x', sizeof(s));
        CHECK(file_menu_get_fname(idx, s, sizeof(s)) == FR_OK);
        CHECK(strlen(s) == 255);                    // 85 characters * 3 bytes, the 86th doesn't fit
        CHECK(longThai.compare(0, 255, s) == 0);
        char t[5];
        file_menu_get_fname(idx, t, sizeof(t));
        CHECK(strlen(t) == 3);                     // one Thai character, never half of one
        CHECK(file_menu_get_fname(9999, s, sizeof(s)) != FR_OK && s[0] == '\0');
        section("file names: full UTF-8 name, bounded copies never cut a character");
    }

    // extension match: whole extension, ASCII case-insensitive
    {
        g_stage = "ext";
        CHECK(file_menu_get_ext_num("wav", 3) == 9);  // zeta Alpha alpha TH1 TH2 JP1 "The Band" Mixed.WaV longThai
        int jpg = 0, jpeg = 0, wave = 0;
        for (uint16_t i = 0; i < file_menu_get_num(); i++) {
            jpg += file_menu_match_ext(i, "jpg", 3);
            jpeg += file_menu_match_ext(i, "jpeg", 4);
            wave += file_menu_match_ext(i, "WAVE", 4);
        }
        CHECK(jpg == 1 && jpeg == 1 && wave == 1);
        section("extension match: whole extension, case-insensitive, no double count");
    }

    // directories, f_opendir failure in ch_dir (use-after-free / stale count before)
    {
        g_stage = "chdir";
        auto got = listed();
        uint16_t sub = static_cast<uint16_t>(std::find(got.begin(), got.end(), "sub") - got.begin());
        CHECK(file_menu_ch_dir(sub) == FR_OK);
        CHECK(listed() == expectedOrder(g_dirs["/sub/"]));
        CHECK(file_menu_ch_dir(0) == FR_OK);  // ".."
        CHECK(file_menu_get_num() == 18);

        got = listed();
        uint16_t album = static_cast<uint16_t>(std::find(got.begin(), got.end(), "Album") - got.begin());
        g_failOpendir = true;
        CHECK(file_menu_ch_dir(album) == FR_DISK_ERR);
        g_failOpendir = false;
        CHECK(file_menu_get_num() == 1);          // ".." only, no stale count of the previous directory
        CHECK(listed() == std::vector<std::string>{".."});
        file_menu_idle();
        file_menu_sort_entry(0, 10);
        CHECK(file_menu_match_ext(5, "wav", 3) == 0);
        CHECK(file_menu_is_dir(0) == 1);
        CHECK(file_menu_ch_dir(0) == FR_OK);      // way back up
        CHECK(file_menu_get_num() == 18);
        CHECK(file_menu_ch_dir(500) == FR_INVALID_PARAMETER);
        CHECK(file_menu_get_num() == 18);          // unchanged
        section("f_opendir failure: '..' only, no use-after-free, recoverable");
    }

    // f_readdir errors: counting and lookup terminate (endless loop before)
    {
        g_stage = "readdir error";
        g_cwd = "/";
        g_failReaddirAt = 7;
        long before = g_readdirCalls;
        file_menu_open_dir("/");
        CHECK(file_menu_get_num() >= 1 && file_menu_get_num() <= 8);
        file_menu_full_sort();
        for (int i = 0; i < 3; i++) { file_menu_idle(); }
        listed();
        CHECK(g_readdirCalls - before < 100000);
        g_failReaddirAt = -1;

        // error after the index was built: lookups return an error and "" (not a stale name)
        file_menu_open_dir("/");
        file_menu_full_sort();
        g_failReaddirAt = 0;
        char s[64];
        bool allErr = true;
        for (uint16_t i = 1; i < file_menu_get_num(); i++) {
            allErr = allErr && file_menu_get_fname(i, s, sizeof(s)) != FR_OK && s[0] == '\0';
        }
        CHECK(allErr);
        CHECK(file_menu_get_fname(0, s, sizeof(s)) == FR_OK && std::string(s) == "..");
        auto got = listed();
        CHECK(file_menu_ch_dir(3) != FR_OK);       // name unknown: stays
        CHECK(file_menu_get_num() == got.size());
        g_failReaddirAt = -1;
        g_failRewind = true;
        CHECK(file_menu_get_fname(2, s, sizeof(s)) != FR_OK);
        g_failRewind = false;
        CHECK(file_menu_get_fname(2, s, sizeof(s)) == FR_OK && s[0] != '\0');
        section("f_readdir errors end the loops and give errors, not stale names");
    }

    // out of memory in each allocation of the index
    {
        for (int n = 0; n < 4; n++) {
            g_stage = "malloc";
            g_allocCount = 0;
            g_failAllocAt = n;
            CHECK(file_menu_open_dir("/") == FR_NOT_ENOUGH_CORE);
            g_failAllocAt = -1;
            CHECK(file_menu_get_num() == 0);
            char s[16];
            CHECK(file_menu_get_fname(0, s, sizeof(s)) != FR_OK);
            CHECK(std::string(file_menu_get_fname_ptr(0)).empty());
            file_menu_idle();
            file_menu_full_sort();
            CHECK(file_menu_is_dir(0) == -1);
            CHECK(file_menu_get_ext_num("wav", 3) == 0);
            CHECK(file_menu_ch_dir(0) == FR_INVALID_PARAMETER);
            CHECK(file_menu_open_dir("/") == FR_OK);   // memory back: works again
            CHECK(file_menu_get_num() == 18);
        }
        section("out of memory: empty list, every call safe, recovers");
    }

    // open_dir twice without close_dir and many directory changes: nothing leaks (LSan at exit)
    {
        g_stage = "leaks";
        for (int i = 0; i < 50; i++) {
            file_menu_open_dir("/");
            auto got = listed();
            uint16_t album = static_cast<uint16_t>(std::find(got.begin(), got.end(), "Album") - got.begin());
            file_menu_ch_dir(album);
            CHECK(file_menu_get_num() == 301);
            file_menu_ch_dir(0);
        }
        file_menu_close_dir();
        file_menu_close_dir();
        CHECK(file_menu_get_num() == 0);
        CHECK(file_menu_deinit() == FR_OK);
        section("no leaks across open/chdir/close");
    }

    printf("test_file_menu: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
