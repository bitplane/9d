#include "../server.h"
#include "../platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int platform_remove_native(const ResolvedPath *path, int directory);

int platform_namespace_init(Namespace *ns) {
    return namespace_use_synthetic(ns);
}

int platform_namespace_discover(Namespace *ns) {
    const char *path = getenv("NINED_TEST_ROOTS");
    char line[S9_PATH_MAX * 2 + 2];
    FILE *file;

    if(!path) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "r");
    if(!file)
        return -1;
    while(fgets(line, sizeof(line), file)) {
        char *separator = strchr(line, '\t');
        char *newline = strchr(line, '\n');

        if(!separator || !newline) {
            fclose(file);
            errno = EINVAL;
            return -1;
        }
        *separator++ = '\0';
        *newline = '\0';
        if(namespace_add_root(ns, line, separator) < 0) {
            int error = errno;

            fclose(file);
            errno = error;
            return -1;
        }
    }
    if(ferror(file)) {
        int error = errno ? errno : EIO;

        fclose(file);
        errno = error;
        return -1;
    }
    return fclose(file);
}

int platform_remove(const ResolvedPath *path, int directory) {
    FidState *state;

    for(state = nined.fids; state; state = state->next) {
        ResolvedPath open_path;

        if(namespace_resolve(state->path, &open_path) == 0 &&
           strcmp(open_path.native_path, path->native_path) == 0 &&
           (state->fd >= 0 || state->dir || state->symlink)) {
            errno = EBUSY;
            return -1;
        }
    }
    return platform_remove_native(path, directory);
}
