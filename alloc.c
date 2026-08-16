#include "server.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef NINED_TESTING
static long allocations_before_failure = -1;

void s9_fail_allocation_after(long count) {
    allocations_before_failure = count;
}

static int allocation_fails(void) {
    if(allocations_before_failure < 0)
        return 0;
    if(allocations_before_failure == 0)
        return 1;
    allocations_before_failure--;
    return 0;
}
#else
static int allocation_fails(void) {
    return 0;
}
#endif

void *s9_malloc(size_t size) {
    if(allocation_fails()) {
        errno = ENOMEM;
        return NULL;
    }
    {
        void *pointer = malloc(size);
        if(!pointer)
            errno = ENOMEM;
        return pointer;
    }
}

char *s9_strdup(const char *string) {
    size_t length;
    char *copy;

    if(!string)
        return NULL;
    length = strlen(string) + 1;
    copy = s9_malloc(length);
    if(copy)
        memcpy(copy, string, length);
    return copy;
}

void s9_free(void *pointer) {
    free(pointer);
}
