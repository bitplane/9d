#include "../server.h"
#include "../platform.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int platform_remove_native(const ResolvedPath *path, int directory);

int platform_namespace_init(Namespace *ns) {
    const char *first = getenv("NINED_TEST_ROOT_1");
    const char *second = getenv("NINED_TEST_ROOT_2");

    if(!first || !second || namespace_use_synthetic(ns) < 0)
        return -1;
    if(namespace_add_root(ns, "First", first) < 0)
        return -1;
    return namespace_add_root(ns, "Second", second);
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
