/* Host side definitions for lib/fatfs_owner: a violation of the ownership rule aborts the test */
#include <stdio.h>
#include <stdlib.h>

#include "fatfs_owner.h"

volatile uint8_t fatfs_owner_core = 0;

void fatfs_owner_violation(const char* where)
{
    printf("  FAIL: FatFs called in %s while core%u owns it\n", where, (unsigned) fatfs_owner_core);
    fflush(stdout);
    abort();
}
