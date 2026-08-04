#ifndef PLATFORM_H
#define PLATFORM_H

#include "namespace.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

int platform_lstat(const ResolvedPath *path, struct stat *st);
int platform_open(const ResolvedPath *path, int flags, mode_t mode);
DIR *platform_opendir(const ResolvedPath *path);
ssize_t platform_readlink(const ResolvedPath *path, char *buffer, size_t size);
int platform_access_execute(const ResolvedPath *path);
int platform_mkdir(const ResolvedPath *path, mode_t mode);
int platform_symlink(const char *target, const ResolvedPath *path);
int platform_remove(const ResolvedPath *path, int directory);
int platform_rename(const ResolvedPath *old_path,
                    const ResolvedPath *new_path);
int platform_chmod(const ResolvedPath *path, mode_t mode);
int platform_set_times(const ResolvedPath *path, time_t atime, time_t mtime);
int platform_truncate(const ResolvedPath *path, off_t length);
int platform_namespace_ready(Namespace *ns);
void platform_namespace_cleanup(Namespace *ns);

#endif
