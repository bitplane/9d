#include "../server.h"
#include "../riscos_identity.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int same(const char *path, const struct stat *before) {
    struct stat after = {0};
    riscos_identity_stat(path, &after);
    return after.st_dev == before->st_dev && after.st_ino == before->st_ino;
}

int main(void) {
    char temporary[] = "/tmp/9d-identities-XXXXXX";
    char cwd[S9_PATH_MAX];
    struct stat file = {0}, child = {0}, replaced = {0};
    long failure;
    FILE *stream;

    assert(getcwd(cwd, sizeof(cwd)));
    assert(mkdtemp(temporary));
    assert(chdir(temporary) == 0);
    stream = fopen("FS:old", "w");
    assert(stream && fclose(stream) == 0);
    stream = fopen("FS:destination", "w");
    assert(stream && fclose(stream) == 0);
    riscos_identity_stat("FS:old", &file);
    riscos_identity_stat("FS:old.child", &child);
    riscos_identity_stat("FS:destination", &replaced);

    /* Every allocation failure must leave both identity and filesystem intact. */
    for(failure = 0;; failure++) {
        s9_fail_allocation_after(failure);
        if(riscos_identity_rename("FS:old", "FS:destination") == 0)
            break;
        assert(errno == ENOMEM);
        assert(access("FS:old", F_OK) == 0);
        assert(same("FS:old", &file));
        assert(same("FS:destination", &replaced));
    }
    s9_fail_allocation_after(-1);
    assert(same("FS:destination", &file));
    assert(same("FS:destination.child", &child));
    assert(!same("FS:old", &file));
    assert(!same("FS:oldish", &file));
    assert(riscos_identity_rename("FS:destination", "FS:third") == 0);
    assert(same("FS:third", &file));
    assert(same("FS:third.child", &child));
    assert(riscos_identity_rename("FS:third", "FS:old") == 0);
    assert(same("FS:old", &file));
    assert(same("FS:old.child", &child));

    /* A child moved separately keeps its identity when its parent later moves. */
    stream = fopen("FS:old.child", "w");
    assert(stream && fclose(stream) == 0);
    assert(riscos_identity_rename("FS:old.child", "FS:old.renamed") == 0);
    assert(same("FS:old.renamed", &child));
    assert(riscos_identity_rename("FS:old", "FS:tree") == 0);
    assert(same("FS:tree", &file));
    assert(same("FS:tree.renamed", &child));
    assert(!same("FS:tree.child", &child));
    assert(riscos_identity_rename("FS:missing", "FS:tree") == -1);
    assert(errno == ENOENT);
    assert(same("FS:tree", &file));

    /* Repeated renames have a bounded cost; exhaustion must precede mutation. */
    for(unsigned i = 0;; i++) {
        char next[64];
        snprintf(next, sizeof(next), "FS:next%u", i);
        if(riscos_identity_rename("FS:tree", next) < 0) {
            assert(errno == ENOSPC);
            assert(access("FS:tree", F_OK) == 0);
            assert(same("FS:tree", &file));
            break;
        }
        assert(same(next, &file));
        assert(riscos_identity_rename(next, "FS:tree") == 0);
    }
    {
        char too_long[S9_PATH_MAX + 1];
        memset(too_long, 'x', sizeof(too_long) - 1);
        too_long[sizeof(too_long) - 1] = '\0';
        assert(riscos_identity_rename("FS:tree", too_long) == -1);
        assert(errno == ENAMETOOLONG);
        assert(access("FS:tree", F_OK) == 0);
        assert(same("FS:tree", &file));
    }
    riscos_identity_cleanup();
    assert(same("FS:old", &file));
    assert(unlink("FS:tree") == 0);
    assert(unlink("FS:old.renamed") == 0);
    assert(chdir(cwd) == 0);
    assert(rmdir(temporary) == 0);
    puts("RISC OS identity tests passed");
}
