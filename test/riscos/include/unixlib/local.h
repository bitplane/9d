#ifndef TEST_RISCOS_LOCAL_H
#define TEST_RISCOS_LOCAL_H
#include <stddef.h>
char *__riscosify_std(const char *path, int flags, char *buffer,
                      size_t size, int *filetype);
#endif
