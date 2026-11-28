/*-----------------------------------------------------------/
/ fatfs_owner: which core may call FatFs (debug check)
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/

#pragma once

// FatFs cross-core ownership rule
//
// FatFs is built with FF_FS_REENTRANT = 0 (no locking) and its objects (FATFS, the open FIL of the
// playing track, the directory object of file_menu) are shared by both cores. So at any time only one
// core may call FatFs functions (f_open, f_read, f_lseek, f_readdir, f_stat, f_close, ...):
//
//   - core0 owns FatFs, except while a file is bound to ReadBuffer and core1 is still reading it.
//   - core1 owns FatFs from ReadBuffer::reqBind() (core0 hands it over before sending the request)
//     until core1 has queued the last item of the stream (end of data or read error),
//     or until it answers ReadBuffer::reqUnbind(), whichever comes first. core1 then hands it back.
//   - So while a track is streaming, core0 must call PlayAudio::stop() (which unbinds) before any FatFs
//     call, e.g. before browsing directories or reading tags. After the stream has reached its end,
//     core0 may use FatFs again (next-track search) before stop()/f_close().
//   - FatFs is never called from an interrupt handler (decode() in the I2S DMA IRQ only reads ReadBuffer).
//
// FATFS_ASSERT_OWNER() checks the rule at FatFs call sites. It costs one load and compare and is
// enabled when NDEBUG is not defined or FATFS_OWNER_CHECK is defined to 1 (cmake -DFATFS_OWNER_CHECK=ON);
// it is off in normal Release builds.

#include <stdint.h>

#include "pico.h"  // get_core_num()

#ifdef __cplusplus
extern "C" {
#endif

// core number (0 or 1) that may call FatFs now
extern volatile uint8_t fatfs_owner_core;

// called by FATFS_ASSERT_OWNER() on a violation (panics on the target)
void fatfs_owner_violation(const char* where);

// hand FatFs over to core (only the current owner calls this)
static inline void fatfs_owner_set(uint8_t core)
{
    fatfs_owner_core = core;
}

#ifdef __cplusplus
}
#endif

#if !defined(FATFS_OWNER_CHECK) && !defined(NDEBUG)
#define FATFS_OWNER_CHECK 1
#endif

#if defined(FATFS_OWNER_CHECK) && FATFS_OWNER_CHECK
#define FATFS_ASSERT_OWNER() \
    do { if (get_core_num() != fatfs_owner_core) { fatfs_owner_violation(__func__); } } while (0)
#else
#define FATFS_ASSERT_OWNER() ((void) 0)
#endif
