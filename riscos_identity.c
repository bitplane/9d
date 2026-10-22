#include "server.h"
#include "riscos_identity.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef S9_RISCOS_RENAME_PATHS
#define S9_RISCOS_RENAME_PATHS 256
#endif

typedef struct IdentityPath IdentityPath;
struct IdentityPath {
    IdentityPath *next;
    char *path;
    char *origin;
    uint64_t generation;
};

static IdentityPath *identities;
static uint64_t generation;

static int at_or_below(const char *path, const char *parent) {
    size_t length = strlen(parent);
    return strncmp(path, parent, length) == 0 &&
           (path[length] == '\0' || path[length] == '.');
}

static const IdentityPath *lookup(const char *path) {
    const IdentityPath *entry;
    const IdentityPath *best = NULL;
    for(entry = identities; entry; entry = entry->next)
        if(at_or_below(path, entry->path) &&
           (!best || strlen(entry->path) > strlen(best->path)))
            best = entry;
    return best;
}

static void hash_part(const char *part, uint32_t *low, uint32_t *high) {
    const unsigned char *p = (const unsigned char *)part;
    for(; *p; p++) {
        *low = (*low ^ *p) * UINT32_C(16777619);
        *high = (*high ^ *p) * UINT32_C(2246822519);
    }
}

void riscos_identity_stat(const char *path, struct stat *st) {
    const IdentityPath *entry = lookup(path);
    uint32_t low = UINT32_C(2166136261);
    uint32_t high = UINT32_C(0x9e3779b9);
    uint64_t epoch = entry ? entry->generation : 0;
    unsigned i;
    if(entry) {
        hash_part(entry->origin, &low, &high);
        hash_part(path + strlen(entry->path), &low, &high);
    } else
        hash_part(path, &low, &high);
    /* Separate the pathname and incarnation with a byte absent from names. */
    low *= UINT32_C(16777619);
    high *= UINT32_C(2246822519);
    for(i = 0; i < 8; i++) {
        uint32_t byte = (uint32_t)(epoch & 255);
        low = (low ^ byte) * UINT32_C(16777619);
        high = (high ^ byte) * UINT32_C(2246822519);
        epoch >>= 8;
    }
    st->st_dev = (dev_t)high;
    st->st_ino = (ino_t)low;
}

static void free_paths(IdentityPath *paths) {
    while(paths) {
        IdentityPath *next = paths->next;
        s9_free(paths->path);
        s9_free(paths->origin);
        s9_free(paths);
        paths = next;
    }
}

void riscos_identity_cleanup(void) {
    free_paths(identities);
    identities = NULL;
    generation = 0;
}

static char *append(const char *prefix, const char *suffix) {
    size_t a = strlen(prefix), b = strlen(suffix);
    char *result;
    if(a >= S9_PATH_MAX || b >= S9_PATH_MAX - a) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    result = s9_malloc(a + b + 1);
    if(result) {
        memcpy(result, prefix, a);
        memcpy(result + a, suffix, b + 1);
    }
    return result;
}

static int add_path(IdentityPath **list, size_t *count,
                     const char *path, const char *suffix,
                     const char *origin, uint64_t epoch) {
    IdentityPath *entry;
    if(*count >= S9_RISCOS_RENAME_PATHS) {
        errno = ENOSPC;
        return -1;
    }
    entry = s9_malloc(sizeof(*entry));
    if(!entry)
        return -1;
    entry->path = append(path, suffix);
    entry->origin = append(origin, "");
    if(!entry->path || !entry->origin) {
        s9_free(entry->path);
        s9_free(entry->origin);
        s9_free(entry);
        return -1;
    }
    entry->generation = epoch;
    entry->next = *list;
    *list = entry;
    (*count)++;
    return 0;
}

int riscos_identity_rename(const char *old_path, const char *new_path) {
    const IdentityPath *source = lookup(old_path);
    IdentityPath *candidate = NULL;
    const IdentityPath *entry;
    size_t count = 0;
    char *origin;
    int error;

    if(strcmp(old_path, new_path) == 0)
        return rename(old_path, new_path);
    if(generation == UINT64_MAX) {
        errno = ENOSPC;
        return -1;
    }
    origin = source ? append(source->origin, old_path + strlen(source->path))
                    : append(old_path, "");
    if(!origin)
        return -1;
    /* Prepare everything before touching the filesystem. At capacity a rename
     * fails, rather than forgetting identities or growing memory without bound. */
    if(add_path(&candidate, &count, new_path, "", origin,
                 source ? source->generation : 0) < 0 ||
       add_path(&candidate, &count, old_path, "", old_path, generation + 1) < 0)
        goto fail;
    for(entry = identities; entry; entry = entry->next) {
        if(strcmp(entry->path, old_path) == 0 ||
           at_or_below(entry->path, new_path))
            continue;
        if(at_or_below(entry->path, old_path)) {
            if(add_path(&candidate, &count, new_path,
                         entry->path + strlen(old_path), entry->origin,
                         entry->generation) < 0)
                goto fail;
        } else if(add_path(&candidate, &count, entry->path, "", entry->origin,
                            entry->generation) < 0)
            goto fail;
    }
    if(rename(old_path, new_path) < 0)
        goto fail;
    free_paths(identities);
    identities = candidate;
    generation++;
    s9_free(origin);
    return 0;
fail:
    error = errno;
    free_paths(candidate);
    s9_free(origin);
    errno = error;
    return -1;
}
