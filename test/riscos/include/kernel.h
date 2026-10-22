#ifndef TEST_RISCOS_KERNEL_H
#define TEST_RISCOS_KERNEL_H
#include <stdint.h>
/* Pointer-width registers let the native adapter run in a host fixture.
 * The real RISC OS ABI uses 32-bit registers and pointers. */
typedef struct { intptr_t r[10]; } _kernel_swi_regs;
typedef struct { int errnum; char errmess[252]; } _kernel_oserror;
_kernel_oserror *_kernel_swi(int number, const _kernel_swi_regs *input,
                              _kernel_swi_regs *output);
#endif
