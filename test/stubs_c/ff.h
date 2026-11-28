// Host stub (C) of the FatFs directory API used by lib/file_menu, implemented in test_file_menu.cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FF_LFN_UNICODE 2
#define FF_MAX_LFN 255
#define FF_LFN_BUF 765
typedef char TCHAR;
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH, FR_INVALID_NAME, FR_DENIED,
    FR_EXIST, FR_INVALID_OBJECT, FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED, FR_TIMEOUT, FR_LOCKED, FR_NOT_ENOUGH_CORE, FR_TOO_MANY_OPEN_FILES, FR_INVALID_PARAMETER
} FRESULT;
#define AM_HID 0x02
#define AM_DIR 0x10
typedef struct { BYTE fs_type; } FATFS;
typedef struct { int valid; int dirId; int index; } DIR;
typedef struct { BYTE fattrib; TCHAR altname[13]; TCHAR fname[FF_LFN_BUF + 1]; } FILINFO;

FRESULT f_mount(FATFS* fs, const TCHAR* path, BYTE opt);
FRESULT f_unmount(const TCHAR* path);
FRESULT f_opendir(DIR* dp, const TCHAR* path);
FRESULT f_closedir(DIR* dp);
FRESULT f_readdir(DIR* dp, FILINFO* fno);
FRESULT f_chdir(const TCHAR* path);

#ifdef __cplusplus
}
#endif
