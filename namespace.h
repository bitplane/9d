#ifndef NAMESPACE_H
#define NAMESPACE_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

#ifndef S9_PATH_MAX
#define S9_PATH_MAX PATH_MAX
#endif

typedef struct NamespaceRoot {
    char *name;
    char *path;
    uint64_t id;
} NamespaceRoot;

typedef struct Namespace {
    int synthetic;
    char *native_root;
    NamespaceRoot *roots;
    size_t nroots;
    uint32_t generation;
} Namespace;

typedef struct ResolvedPath {
    int synthetic;
    int export_root;
    uint64_t root_id;
    const char *root_path;
    char native_path[S9_PATH_MAX];
    char relative_path[S9_PATH_MAX];
} ResolvedPath;

extern Namespace namespace;

int namespace_init(const char *root);
void namespace_cleanup(void);
int namespace_use_native(Namespace *ns, const char *path);
int namespace_use_synthetic(Namespace *ns);
int namespace_add_root(Namespace *ns, const char *name, const char *path);
int namespace_refresh(void);
int namespace_copy_roots(NamespaceRoot **roots, size_t *count);
void namespace_free_roots(NamespaceRoot *roots, size_t count);
int namespace_resolve(const char *path, ResolvedPath *resolved);
int namespace_resolve_root(const NamespaceRoot *root, ResolvedPath *resolved);
char *namespace_join_virtual_alloc(const char *dir, const char *name);
int namespace_valid_component(const char *name);
int namespace_is_protected(const char *path);
int namespace_is_namespace_root(const char *path);
uint64_t namespace_qid(const ResolvedPath *resolved, const struct stat *st);
uint64_t namespace_root_qid(void);

int platform_namespace_init(Namespace *ns);
int platform_namespace_discover(Namespace *ns);

#endif
