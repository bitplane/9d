#include "namespace.h"

#include <dos/dosextens.h>
#include <exec/types.h>
#include <proto/dos.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct VolumeName {
    struct VolumeName *next;
    char name[256];
} VolumeName;

static void free_names(VolumeName *names) {
    while(names) {
        VolumeName *next = names->next;
        free(names);
        names = next;
    }
}

static VolumeName *snapshot_names(void) {
    struct DosList *list;
    struct DosList *entry;
    VolumeName *head = NULL;
    VolumeName **tail = &head;

    list = LockDosList(LDF_VOLUMES | LDF_READ);
    if(!list)
        return NULL;

    entry = list;
    while((entry = NextDosEntry(entry, LDF_VOLUMES)) != NULL) {
        const unsigned char *bstr = (const unsigned char *)BADDR(entry->dol_Name);
        size_t length = bstr[0];
        VolumeName *volume = malloc(sizeof(*volume));
        if(!volume) {
            UnLockDosList(LDF_VOLUMES | LDF_READ);
            free_names(head);
            return NULL;
        }
        memcpy(volume->name, bstr + 1, length);
        volume->name[length] = '\0';
        volume->next = NULL;
        *tail = volume;
        tail = &volume->next;
    }
    UnLockDosList(LDF_VOLUMES | LDF_READ);
    return head;
}

int platform_namespace_init(Namespace *ns) {
    VolumeName *names;
    VolumeName *volume;
    int added = 0;

    if(namespace_use_synthetic(ns) < 0)
        return -1;
    names = snapshot_names();
    if(!names)
        return -1;

    for(volume = names; volume; volume = volume->next) {
        char path[258];
        struct stat st;

        if(snprintf(path, sizeof(path), "%s:", volume->name) >= (int)sizeof(path))
            continue;
        if(stat(path, &st) < 0 || !S_ISDIR(st.st_mode))
            continue;
        if(namespace_add_root(ns, volume->name, path) < 0) {
            fprintf(stderr, "Cannot export Amiga volume %s: %s\n",
                    volume->name, strerror(errno));
            free_names(names);
            return -1;
        }
        added++;
    }
    free_names(names);
    if(!added) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
