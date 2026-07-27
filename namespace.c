#include "namespace.h"
#include "server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Namespace namespace;

static uint64_t hash_u64(uint64_t hash, uint64_t value) {
    unsigned int i;

    for(i = 0; i < 8; i++) {
        hash ^= value & 0xff;
        hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

static int encode_name(const char *name, char *encoded, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)name;

    if(!name || !name[0] || !encoded || size == 0)
        return -1;

    while(*p) {
        if(*p == '%' || *p == '/') {
            if(used + 3 >= size)
                return -1;
            encoded[used++] = '%';
            encoded[used++] = hex[*p >> 4];
            encoded[used++] = hex[*p & 0x0f];
        } else {
            if(used + 1 >= size)
                return -1;
            encoded[used++] = (char)*p;
        }
        p++;
    }
    encoded[used] = '\0';
    return 0;
}

void namespace_cleanup(void) {
    size_t i;

    free(namespace.native_root);
    for(i = 0; i < namespace.nroots; i++) {
        free(namespace.roots[i].name);
        free(namespace.roots[i].path);
    }
    free(namespace.roots);
    memset(&namespace, 0, sizeof(namespace));
}

int namespace_use_native(Namespace *ns, const char *path) {
    char *copy;

    if(!ns || !path)
        return -1;
    copy = strdup(path);
    if(!copy)
        return -1;
    ns->native_root = copy;
    ns->synthetic = 0;
    return 0;
}

int namespace_use_synthetic(Namespace *ns) {
    if(!ns)
        return -1;
    ns->synthetic = 1;
    return 0;
}

int namespace_add_root(Namespace *ns, const char *name, const char *path) {
    NamespaceRoot *roots;
    char encoded[PATH_MAX];
    char *name_copy;
    char *path_copy;
    size_t i;

    if(!ns || !ns->synthetic || encode_name(name, encoded, sizeof(encoded)) < 0)
        return -1;
    for(i = 0; i < ns->nroots; i++) {
        if(strcmp(ns->roots[i].name, encoded) == 0) {
            errno = EEXIST;
            return -1;
        }
    }

    name_copy = strdup(encoded);
    path_copy = strdup(path);
    if(!name_copy || !path_copy) {
        free(name_copy);
        free(path_copy);
        return -1;
    }

    roots = realloc(ns->roots, (ns->nroots + 1) * sizeof(*roots));
    if(!roots) {
        free(name_copy);
        free(path_copy);
        return -1;
    }
    ns->roots = roots;
    ns->roots[ns->nroots].name = name_copy;
    ns->roots[ns->nroots].path = path_copy;
    ns->roots[ns->nroots].id = ns->nroots + 1;
    ns->nroots++;
    return 0;
}

int namespace_init(const char *root) {
    int result;
    struct stat st;

    memset(&namespace, 0, sizeof(namespace));
    if(root) {
        if(stat(root, &st) < 0 || !S_ISDIR(st.st_mode)) {
            if(errno == 0)
                errno = ENOTDIR;
            return -1;
        }
        result = namespace_use_native(&namespace, root);
    } else {
        result = platform_namespace_init(&namespace);
    }
    if(result < 0) {
        namespace_cleanup();
        return -1;
    }
    return 0;
}

static int find_root(const char *path, size_t length, size_t *index) {
    size_t i;

    for(i = 0; i < namespace.nroots; i++) {
        if(strlen(namespace.roots[i].name) == length &&
           strncmp(namespace.roots[i].name, path, length) == 0) {
            *index = i;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

int namespace_resolve(const char *path, ResolvedPath *resolved) {
    char cleaned[PATH_MAX];
    const char *name;
    const char *remainder;
    size_t length;
    size_t index;

    if(!path || !resolved)
        return -1;
    if(strlen(path) >= sizeof(cleaned)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(cleaned, path);
    cleanname(cleaned);
    memset(resolved, 0, sizeof(*resolved));

    if(!namespace.synthetic) {
        if(joinpath(resolved->native_path, sizeof(resolved->native_path),
                    namespace.native_root, cleaned) < 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
        return 0;
    }

    if(strcmp(cleaned, "/") == 0) {
        resolved->synthetic = 1;
        return 0;
    }

    name = cleaned;
    while(*name == '/')
        name++;
    remainder = strchr(name, '/');
    length = remainder ? (size_t)(remainder - name) : strlen(name);
    if(find_root(name, length, &index) < 0)
        return -1;

    resolved->root_index = index;
    resolved->export_root = remainder == NULL || remainder[1] == '\0';
    if(resolved->export_root) {
        if(strlen(namespace.roots[index].path) >= sizeof(resolved->native_path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(resolved->native_path, namespace.roots[index].path);
        return 0;
    }
    if(joinpath(resolved->native_path, sizeof(resolved->native_path),
                namespace.roots[index].path,
                remainder + 1) < 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int namespace_join_virtual(char *dst, size_t dstsize, const char *dir,
                           const char *name) {
    if(!dst || !dir || !name || strchr(name, '/')) {
        errno = EINVAL;
        return -1;
    }
    if(strcmp(dir, "/") == 0) {
        if(snprintf(dst, dstsize, "/%s", name) >= (int)dstsize)
            return -1;
    } else {
        if(snprintf(dst, dstsize, "%s/%s", dir, name) >= (int)dstsize)
            return -1;
    }
    cleanname(dst);
    return 0;
}

int namespace_is_protected(const char *path) {
    ResolvedPath resolved;

    if(namespace_resolve(path, &resolved) < 0)
        return 0;
    return resolved.synthetic || (namespace.synthetic && resolved.export_root);
}

uint64_t namespace_root_qid(void) {
    return UINT64_C(1);
}

uint64_t namespace_qid(const ResolvedPath *resolved, const struct stat *st) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint64_t root_id = 0;

    if(resolved->synthetic)
        return namespace_root_qid();
    if(namespace.synthetic)
        root_id = namespace.roots[resolved->root_index].id;
    hash = hash_u64(hash, root_id);
    hash = hash_u64(hash, (uint64_t)st->st_dev);
    hash = hash_u64(hash, (uint64_t)st->st_ino);
    if(hash == namespace_root_qid())
        hash++;
    return hash;
}
