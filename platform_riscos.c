#include "namespace.h"
#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>
#endif

void platform_namespace_cleanup(Namespace *ns) {
    (void)ns;
}

int platform_namespace_ready(Namespace *ns) {
#ifdef __riscos__
    if(ns->synthetic)
        return 0;
#endif
    struct stat st;

    if(ns->synthetic) {
        errno = EINVAL;
        return -1;
    }
    return stat(ns->native_root, &st) < 0 || !S_ISDIR(st.st_mode) ? -1 : 0;
}

int platform_namespace_init(Namespace *ns) {
#ifdef __riscos__
    return namespace_use_synthetic(ns);
#else
    return namespace_use_native(ns, "/");
#endif
}

#ifdef __riscos__
static int native_directory(const char *path) {
    ResolvedPath resolved;
    struct stat st;

    memset(&resolved, 0, sizeof(resolved));
    if(snprintf(resolved.native_path, sizeof(resolved.native_path), "%s", path) >=
       (int)sizeof(resolved.native_path))
        return 0;
    return platform_lstat(&resolved, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

#ifdef __riscos__
static int add_drive_root(Namespace *ns, const char *name, unsigned drive) {
    char path[S9_PATH_MAX];
    char root_name[132];

    if(snprintf(path, sizeof(path), "%s::%u.$", name, drive) >=
       (int)sizeof(path) || !native_directory(path))
        return 0;
    if(snprintf(root_name, sizeof(root_name), "%s-%u", name, drive) >=
       (int)sizeof(root_name))
        return 0;
    return namespace_add_root(ns, root_name, path);
}
#endif

int platform_namespace_discover(Namespace *ns) {
#ifdef __riscos__
    unsigned number;

    for(number = 0; number < 256; number++) {
        char name[128];
        char path[S9_PATH_MAX];
        unsigned drive;
        int direct;
        int root_zero = 0;

        if(_swix(OS_FSControl, _INR(0, 3), 33, number, name,
                 sizeof(name)) != NULL || !name[0])
            continue;
        if(snprintf(path, sizeof(path), "%s:", name) >= (int)sizeof(path))
            continue;
        direct = native_directory(path);
        if(!direct) {
            if(snprintf(path, sizeof(path), "%s::0.$", name) >=
               (int)sizeof(path))
                continue;
            root_zero = native_directory(path);
        }
        if((direct || root_zero) && namespace_add_root(ns, name, path) < 0)
            return -1;

        if(strcmp(name, "SCSI") == 0) {
            for(drive = 0; drive < 8; drive++) {
                int ready = 0;
                if(drive == 0 && root_zero)
                    continue;
                if(drive >= 4 &&
                   (_swix(SCSIFS_TestReady, _IN(1)|_OUT(0), drive,
                          &ready) != NULL || ready != 2))
                    continue;
                if(add_drive_root(ns, name, drive) < 0)
                    return -1;
            }
        } else if(strcmp(name, "CDFS") == 0) {
            int configured = 0;
            if(_swix(CDFS_GetNumberOfDrives, _OUT(0), &configured) == NULL) {
                for(drive = 1; drive < (unsigned)configured; drive++) {
                    if(add_drive_root(ns, name, drive) < 0)
                        return -1;
                }
            }
        }
    }
#else
    (void)ns;
#endif
    return 0;
}

static int path_allowed(const ResolvedPath *path) {
    if(!path || path->synthetic || !path->native_path[0]) {
        errno = EINVAL;
        return 0;
    }
    return 1;
}

int platform_lstat(const ResolvedPath *path, struct stat *st) {
    if(!path_allowed(path))
        return -1;
#ifdef __riscos__
    /* UnixLib lstat can block on files supplied by non-FileCore filing
     * systems. Query the filing system directly for 9P metadata instead. */
    char native[S9_PATH_MAX];
    int type, load, exec, length, attributes;
    unsigned long long centiseconds;
    unsigned int identity_low = 2166136261u;
    unsigned int identity_high = 0x9e3779b9u;
    const unsigned char *part;
    _kernel_swi_regs regs;

    if(!__riscosify_std(path->native_path, 0, native, sizeof(native), NULL)) {
        errno = EILSEQ;
        return -1;
    }
    memset(&regs, 0, sizeof(regs));
    regs.r[0] = 5;
    regs.r[1] = (int)native;
    if(_kernel_swi(OS_File, &regs, &regs) != NULL) {
        /* Some filing systems reject OS_File on their volume root but
         * enumerate that root through OS_GBPB. */
        char first[S9_PATH_MAX];
        memset(&regs, 0, sizeof(regs));
        regs.r[0] = 9;
        regs.r[1] = (int)native;
        regs.r[2] = (int)first;
        regs.r[3] = 1;
        regs.r[4] = 0;
        regs.r[5] = sizeof(first);
        if(_kernel_swi(OS_GBPB, &regs, &regs) != NULL) {
            errno = EIO;
            return -1;
        }
        type = 3;
        load = exec = length = attributes = 0;
    } else {
        type = regs.r[0];
        load = regs.r[2];
        exec = regs.r[3];
        length = regs.r[4];
        attributes = regs.r[5];
    }
    if(type == 0) {
        errno = ENOENT;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    /* RISC OS does not provide inode numbers, so identify an object by path. */
    for(part = (const unsigned char *)path->native_path; *part; part++) {
        identity_low = (identity_low ^ *part) * 16777619u;
        identity_high = (identity_high ^ *part) * 2246822519u;
    }
    st->st_dev = (dev_t)identity_high;
    st->st_ino = (ino_t)identity_low;
    st->st_mode = type == 2 || type == 3 ? S_IFDIR : S_IFREG;
    if(attributes & 1) st->st_mode |= 0400;
    if(attributes & 2) st->st_mode |= 0200;
    if(attributes & 16) st->st_mode |= 0044;
    if(attributes & 32) st->st_mode |= 0022;
    if(S_ISDIR(st->st_mode)) {
        /* Some filing systems report no attributes for a volume root. */
        if(!(attributes & 0x33))
            st->st_mode |= 0755;
        else
            st->st_mode |= (st->st_mode & 0444) >> 2;
    }
    st->st_size = (unsigned int)length;
    if(((unsigned int)load & 0xfff00000u) == 0xfff00000u) {
        centiseconds = (((unsigned long long)(unsigned int)load & 0xffu) << 32) |
                       (unsigned int)exec;
        if(centiseconds >= 220898880000ULL)
            st->st_mtime = (time_t)(centiseconds / 100 - 2208988800ULL);
    }
    st->st_atime = st->st_mtime;
    return 0;
#else
    return lstat(path->native_path, st);
#endif
}

int platform_lstat_child(PlatformDir *directory, const ResolvedPath *path,
                         const char *name, struct stat *st) {
    (void)directory;
    (void)name;
    return platform_lstat(path, st);
}

int platform_open(const ResolvedPath *path, int flags, mode_t mode) {
    return path_allowed(path) ? open(path->native_path, flags, mode) : -1;
}

#ifdef __riscos__
struct PlatformDir {
    char native[S9_PATH_MAX];
    long position;
    struct dirent entry;
};
#endif

PlatformDir *platform_opendir(const ResolvedPath *path) {
#ifdef __riscos__
    PlatformDir *directory;
    struct stat st;

    /* FileSwitch skips FAT directory metadata entries that UnixLib readdir
     * exposes as repeated names. Use its directory cursor directly. */
    if(!path_allowed(path))
        return NULL;
    if(platform_lstat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    directory = calloc(1, sizeof(*directory));
    if(!directory)
        return NULL;
    if(!__riscosify_std(path->native_path, 0, directory->native,
                       sizeof(directory->native), NULL)) {
        free(directory);
        errno = ENOTDIR;
        return NULL;
    }
    directory->position = 0;
    return directory;
#else
    return path_allowed(path) ? opendir(path->native_path) : NULL;
#endif
}

struct dirent *platform_readdir(PlatformDir *directory) {
#ifdef __riscos__
    char name[S9_PATH_MAX];
    _kernel_swi_regs regs;
    _kernel_oserror *error;
    size_t length;
    size_t i;
    const char *end;

    if(directory->position == -1) {
        errno = 0;
        return NULL;
    }
    memset(&regs, 0, sizeof(regs));
    regs.r[0] = 9;
    regs.r[1] = (int)directory->native;
    regs.r[2] = (int)name;
    regs.r[3] = 1;
    regs.r[4] = directory->position;
    regs.r[5] = sizeof(name);
    error = _kernel_swi(OS_GBPB, &regs, &regs);
    if(error) {
        errno = EIO;
        return NULL;
    }
    directory->position = regs.r[4];
    if(regs.r[3] == 0) {
        errno = 0;
        return NULL;
    }
    end = memchr(name, 0, sizeof(name));
    if(!end) {
        errno = EIO;
        return NULL;
    }
    length = (size_t)(end - name);
    if(length >= sizeof(directory->entry.d_name)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    memcpy(directory->entry.d_name, name, length + 1);
    /* A slash in a RISC OS leaf name represents a Unix full stop. */
    for(i = 0; i < length; i++) {
        if(directory->entry.d_name[i] == '/')
            directory->entry.d_name[i] = '.';
    }
    return &directory->entry;
#else
    return readdir(directory);
#endif
}

long platform_telldir(PlatformDir *directory) {
#ifdef __riscos__
    return directory->position;
#else
    return telldir(directory);
#endif
}

void platform_seekdir(PlatformDir *directory, long position) {
#ifdef __riscos__
    directory->position = position;
#else
    seekdir(directory, position);
#endif
}

void platform_rewinddir(PlatformDir *directory) {
#ifdef __riscos__
    directory->position = 0;
#else
    rewinddir(directory);
#endif
}

int platform_closedir(PlatformDir *directory) {
#ifdef __riscos__
    free(directory);
    return 0;
#else
    return closedir(directory);
#endif
}

ssize_t platform_readlink(const ResolvedPath *path, char *buffer, size_t size) {
    return path_allowed(path) ? readlink(path->native_path, buffer, size) : -1;
}

int platform_access_execute(const ResolvedPath *path) {
    return path_allowed(path) ? access(path->native_path, X_OK) : -1;
}

int platform_mkdir(const ResolvedPath *path, mode_t mode) {
    return path_allowed(path) ? mkdir(path->native_path, mode) : -1;
}

int platform_symlink(const char *target, const ResolvedPath *path) {
    return path_allowed(path) ? symlink(target, path->native_path) : -1;
}

int platform_mknod(const ResolvedPath *path, mode_t mode, unsigned major,
                   unsigned minor) {
    (void)major;
    (void)minor;
    if(!path_allowed(path))
        return -1;
    if(S_ISFIFO(mode))
        return mkfifo(path->native_path, mode & 07777);
    errno = EOPNOTSUPP;
    return -1;
}

int platform_remove(const ResolvedPath *path, int directory) {
    if(!path_allowed(path))
        return -1;
    return directory ? rmdir(path->native_path) : unlink(path->native_path);
}

int platform_rename(const ResolvedPath *old_path,
                    const ResolvedPath *new_path) {
    if(!path_allowed(old_path) || !path_allowed(new_path))
        return -1;
    return rename(old_path->native_path, new_path->native_path);
}

int platform_chmod(const ResolvedPath *path, mode_t mode) {
    return path_allowed(path) ? chmod(path->native_path, mode) : -1;
}

int platform_chown(const ResolvedPath *path, uid_t uid, gid_t gid) {
    return path_allowed(path) ? chown(path->native_path, uid, gid) : -1;
}

int platform_device_spec(const struct stat *st, char *buffer, size_t size) {
    (void)st;
    (void)buffer;
    (void)size;
    errno = EINVAL;
    return -1;
}

int platform_set_times(const ResolvedPath *path, time_t atime, time_t mtime) {
    struct utimbuf times;

    if(!path_allowed(path))
        return -1;
    times.actime = atime;
    times.modtime = mtime;
    return utime(path->native_path, &times);
}

int platform_truncate(const ResolvedPath *path, off_t length) {
    return path_allowed(path) ? truncate(path->native_path, length) : -1;
}
