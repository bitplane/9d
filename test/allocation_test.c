#define _GNU_SOURCE
#include "../server.h"
#include "../platform.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_fid_allocation(void) {
    FidState *state;

    s9_fail_allocation_after(0);
    assert(fid_state_create("/") == NULL);
    assert(nined.fids == NULL);
    s9_fail_allocation_after(1);
    assert(fid_state_create("/") == NULL);
    assert(nined.fids == NULL);
    s9_fail_allocation_after(2);
    state = fid_state_create("/");
    assert(state != NULL);
    assert(nined.fids == state);
    fid_state_destroy(state);
    assert(nined.fids == NULL);
}

static void test_path_allocation(void) {
    char *path;

    s9_fail_allocation_after(0);
    errno = 0;
    assert(namespace_join_virtual_alloc("/parent", "child") == NULL);
    assert(errno == ENOMEM);
    s9_fail_allocation_after(1);
    path = namespace_join_virtual_alloc("/parent", "child");
    assert(path != NULL);
    assert(strcmp(path, "/parent/child") == 0);
    s9_free(path);
}

static void test_stat_allocations(const char *root) {
    ResolvedPath resolved;
    struct stat native;
    IxpStat stat;
    long failure;

    assert(namespace_init(root) == 0);
    assert(namespace_resolve("/file", &resolved) == 0);
    assert(platform_lstat(&resolved, &native) == 0);
    for(failure = 0; failure < 5; failure++) {
        memset(&stat, 0, sizeof(stat));
        s9_fail_allocation_after(failure);
        assert(build_stat(&stat, "/file", &resolved, &native, NULL) == -1);
        assert(stat.name == NULL);
        assert(stat.uid == NULL);
        assert(stat.gid == NULL);
        assert(stat.muid == NULL);
        assert(stat.extension == NULL);
    }
    s9_fail_allocation_after(5);
    assert(build_stat(&stat, "/file", &resolved, &native, NULL) == 0);
    free_stat_strings(&stat);
    namespace_cleanup();
}

static void test_rename_allocations(void) {
    FidState *parent;
    FidState *child;
    FidState *other;
    long failure;

    s9_fail_allocation_after(-1);
    parent = fid_state_create("/tree");
    child = fid_state_create("/tree/child");
    other = fid_state_create("/other");
    assert(parent && child && other);
    for(failure = 0; failure < 4; failure++) {
        s9_fail_allocation_after(failure);
        assert(test_prepare_rename_updates("/tree", "/moved") == -1);
        assert(strcmp(parent->path, "/tree") == 0);
        assert(strcmp(child->path, "/tree/child") == 0);
        assert(strcmp(other->path, "/other") == 0);
    }
    s9_fail_allocation_after(4);
    assert(test_prepare_rename_updates("/tree", "/moved") == 0);
    fid_state_destroy(other);
    fid_state_destroy(child);
    fid_state_destroy(parent);
    assert(nined.fids == NULL);
}

static void test_qid_generation(void) {
    struct stat native;
    uint32_t before;

    memset(&native, 0, sizeof(native));
    native.st_mtime = 123;
    before = qid_version(&native);
    qid_bump();
    assert(qid_version(&native) != before);
    nined_state_cleanup();
    assert(nined.qid_generation == 0);
    assert(qid_version(&native) == before);
}

int main(void) {
    char template[] = "/tmp/nined-allocation-XXXXXX";
    char path[1024];
    char *root = mkdtemp(template);
    int descriptor;

    assert(root != NULL);
    assert(snprintf(path, sizeof(path), "%s/file", root) < (int)sizeof(path));
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(descriptor >= 0);
    assert(close(descriptor) == 0);
    test_fid_allocation();
    test_path_allocation();
    test_stat_allocations(root);
    test_rename_allocations();
    test_qid_generation();
    s9_fail_allocation_after(-1);
    assert(unlink(path) == 0);
    assert(rmdir(root) == 0);
    puts("allocation tests passed");
    return 0;
}
