#include "../namespace.h"
#include "../server.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int platform_namespace_init(Namespace *ns) {
    return namespace_use_native(ns, "/");
}

int platform_namespace_ready(Namespace *ns) {
    (void)ns;
    return 0;
}

void platform_namespace_cleanup(Namespace *ns) {
    (void)ns;
}

static void test_native_namespace(const char *root) {
    ResolvedPath resolved;

    assert(strcmp(path_basename("/volume/file"), "file") == 0);
    assert(strcmp(path_basename("/volume"), "volume") == 0);
    assert(namespace_init(root) == 0);
    assert(namespace.synthetic == 0);
    assert(namespace_resolve("/child", &resolved) == 0);
    assert(resolved.synthetic == 0);
    assert(strstr(resolved.native_path, "/child") != NULL);
    assert(namespace_is_protected("/") == 0);
    namespace_cleanup();
}

static void test_synthetic_namespace(const char *first, const char *second) {
    ResolvedPath root;
    ResolvedPath child;
    ResolvedPath other;
    struct stat first_stat;
    struct stat second_stat;
    char *path;

    memset(&namespace, 0, sizeof(namespace));
    assert(namespace_use_synthetic(&namespace) == 0);
    assert(namespace_add_root(&namespace, "Work", first) == 0);
    assert(namespace_add_root(&namespace, "Games/%", second) == 0);
    assert(strcmp(namespace.roots[1].name, "Games%2F%25") == 0);

    errno = 0;
    assert(namespace_add_root(&namespace, "Work", second) == -1);
    assert(errno == EEXIST);

    assert(namespace_resolve("/", &root) == 0);
    assert(root.synthetic == 1);
    assert(namespace_resolve("/Work", &child) == 0);
    assert(child.export_root == 1);
    assert(strcmp(child.native_path, first) == 0);
    assert(namespace_resolve("/Games%2F%25/file", &other) == 0);
    assert(other.export_root == 0);
    assert(strstr(other.native_path, "/file") != NULL);

    assert(namespace_is_protected("/") == 1);
    assert(namespace_is_protected("/Work") == 1);
    assert(namespace_is_protected("/Work/file") == 0);

    assert(namespace_join_virtual_alloc("/Work", "..") == NULL);
    assert(namespace_join_virtual_alloc("/Work", ".") == NULL);
    assert(namespace_join_virtual_alloc("/Work", "") == NULL);
    assert(namespace_join_virtual_alloc("/Work", "a/b") == NULL);
    path = namespace_join_virtual_alloc("/Work", "file");
    assert(path != NULL);
    assert(strcmp(path, "/Work/file") == 0);
    s9_free(path);

    assert(stat(first, &first_stat) == 0);
    assert(stat(second, &second_stat) == 0);
    assert(namespace_qid(&child, &first_stat) !=
           namespace_qid(&other, &second_stat));
    assert(namespace_qid(&root, &first_stat) == namespace_root_qid());
    namespace_cleanup();
}

int main(void) {
    char temporary[PATH_MAX];
    char first[PATH_MAX];
    char second[PATH_MAX];

    assert(snprintf(temporary, sizeof(temporary),
                    "/tmp/9d-namespace-%ld", (long)getpid()) <
           (int)sizeof(temporary));
    assert(mkdir(temporary, 0700) == 0);
    assert(snprintf(first, sizeof(first), "%s/first", temporary) <
           (int)sizeof(first));
    assert(snprintf(second, sizeof(second), "%s/second", temporary) <
           (int)sizeof(second));
    assert(mkdir(first, 0700) == 0);
    assert(mkdir(second, 0700) == 0);

    test_native_namespace(first);
    test_synthetic_namespace(first, second);

    assert(rmdir(first) == 0);
    assert(rmdir(second) == 0);
    assert(rmdir(temporary) == 0);
    puts("namespace tests passed");
    return 0;
}
