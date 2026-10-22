#ifndef TEST_RISCOS_SWIS_H
#define TEST_RISCOS_SWIS_H
#include <kernel.h>
#define OS_File 1
#define OS_GBPB 2
#define OS_FSControl 3
#define SCSIFS_TestReady 4
#define CDFS_GetNumberOfDrives 5
#define _IN(n) (1U << (n))
#define _OUT(n) (1U << ((n) + 16))
#define _INR(a, b) ((1U << ((b) + 1)) - (1U << (a)))
_kernel_oserror *_swix(int number, unsigned mask, ...);
#endif
