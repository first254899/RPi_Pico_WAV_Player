/*-----------------------------------------------------------/
/ fatfs_owner: which core may call FatFs (debug check)
/------------------------------------------------------------/
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/-----------------------------------------------------------*/

#include "fatfs_owner.h"

#include "pico/stdlib.h"

volatile uint8_t fatfs_owner_core = 0;  // core0 owns FatFs at start-up

void fatfs_owner_violation(const char* where)
{
    panic("FatFs called on core%u in %s while core%u owns it", get_core_num(), where, fatfs_owner_core);
}
