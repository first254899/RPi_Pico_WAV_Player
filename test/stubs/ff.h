// Host stub of the FatFs subset used by ReadBuffer and TagRead, with fault injection and access checks
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

typedef unsigned int UINT;
typedef uint64_t FSIZE_t;
typedef char TCHAR;
typedef enum { FR_OK = 0, FR_DISK_ERR = 1, FR_INT_ERR = 2, FR_NO_FILE = 4 } FRESULT;
#define FA_READ 0x01

typedef struct {
    const std::vector<uint8_t>* file = nullptr;
    FSIZE_t fptr = 0;
    // test hooks
    FSIZE_t failReadAt = UINT64_MAX;    // f_read crossing this offset fails (bytes before it are read)
    bool failSeek = false;
    unsigned readDelayUs = 0;           // simulated card latency per f_read
    std::atomic<FSIZE_t> maxReadEnd{0};  // highest offset ever read
    std::atomic<int> reads{0};
} FIL;

static inline FSIZE_t f_size(FIL* fp) { return fp->file->size(); }
static inline bool f_eof(FIL* fp) { return fp->fptr >= fp->file->size(); }
static inline FRESULT f_lseek(FIL* fp, FSIZE_t ofs)
{
    if (fp->failSeek) { return FR_DISK_ERR; }
    fp->fptr = (ofs < fp->file->size()) ? ofs : fp->file->size();  // read-only file: clipped to file size
    return FR_OK;
}
static inline FRESULT f_read(FIL* fp, void* buf, UINT btr, UINT* br)
{
    fp->reads++;
    if (fp->readDelayUs) { std::this_thread::sleep_for(std::chrono::microseconds(fp->readDelayUs)); }
    FSIZE_t left = fp->file->size() - fp->fptr;
    UINT n = (btr < left) ? btr : static_cast<UINT>(left);
    FRESULT fr = FR_OK;
    if (fp->fptr + n > fp->failReadAt) {
        n = (fp->failReadAt > fp->fptr) ? static_cast<UINT>(fp->failReadAt - fp->fptr) : 0;
        fr = FR_DISK_ERR;
    }
    if (n > 0) { memcpy(buf, fp->file->data() + fp->fptr, n); }
    fp->fptr += n;
    FSIZE_t end = fp->fptr;
    FSIZE_t prev = fp->maxReadEnd.load();
    while (end > prev && !fp->maxReadEnd.compare_exchange_weak(prev, end)) {}
    *br = n;
    return fr;
}

static inline FSIZE_t f_tell(FIL* fp) { return fp->fptr; }

// named files for f_open() (TagRead)
inline std::map<std::string, std::vector<uint8_t>>& stub_files()
{
    static std::map<std::string, std::vector<uint8_t>> files;
    return files;
}
static inline FRESULT f_open(FIL* fp, const TCHAR* path, int /*mode*/)
{
    auto it = stub_files().find(path);
    if (it == stub_files().end()) { return FR_NO_FILE; }
    fp->file = &it->second;
    fp->fptr = 0;
    fp->failReadAt = UINT64_MAX;
    fp->failSeek = false;
    return FR_OK;
}
static inline FRESULT f_close(FIL* fp) { fp->file = nullptr; return FR_OK; }
