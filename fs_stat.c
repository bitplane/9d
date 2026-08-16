#include "server.h"
#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>

typedef struct RenameUpdate {
    FidState *state;
    char *path;
    struct RenameUpdate *next;
} RenameUpdate;

static char *number_string(uint32_t value) {
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%u", value);
    return s9_strdup(buffer);
}

int build_stat(IxpStat *s, const char *path, const ResolvedPath *resolved,
               const struct stat *st,
               const char *symlink_target) {
    memset(s, 0, sizeof(*s));
    s->qid.type = S_ISDIR(st->st_mode) ? P9_QTDIR :
                  S_ISLNK(st->st_mode) ? P9_QTSYMLINK : P9_QTFILE;
    s->qid.path = namespace_qid(resolved, st);
    s->qid.version = qid_version(st);
    s->mode = st->st_mode & 0777;
    if(S_ISDIR(st->st_mode))
        s->mode |= P9_DMDIR;
    else if(S_ISLNK(st->st_mode))
        s->mode |= P9_DMSYMLINK;
    s->atime = (uint32_t)st->st_atime;
    s->mtime = (uint32_t)st->st_mtime;
    s->length = (uint64_t)st->st_size;
    s->n_uid = (uint32_t)st->st_uid;
    s->n_gid = (uint32_t)st->st_gid;
    s->n_muid = (uint32_t)~0;
    s->name = s9_strdup(strcmp(path, "/") == 0 ? "/" : path_basename(path));
    s->uid = number_string(s->n_uid);
    s->gid = number_string(s->n_gid);
    s->muid = s9_strdup("");
    if(S_ISLNK(st->st_mode)) {
        s->extension = symlink_target ? s9_strdup(symlink_target)
                                      : read_symlink_target(resolved,
                                                            (size_t)st->st_size,
                                                            NULL);
        if(s->extension)
            s->length = strlen(s->extension);
    } else
        s->extension = s9_strdup("");
    if(!s->name || !s->uid || !s->gid || !s->muid || !s->extension) {
        int error = errno ? errno : ENOMEM;
        free_stat_strings(s);
        memset(s, 0, sizeof(*s));
        errno = error;
        return -1;
    }
    return 0;
}

int build_synthetic_stat(IxpStat *s, const char *name) {
    memset(s, 0, sizeof(*s));
    s->qid.type = P9_QTDIR;
    s->qid.path = namespace_root_qid();
    s->qid.version = nined.qid_generation;
    s->mode = P9_DMDIR | 0555;
    s->name = s9_strdup(name);
    s->uid = s9_strdup("none");
    s->gid = s9_strdup("none");
    s->muid = s9_strdup("");
    s->extension = s9_strdup("");
    s->n_uid = (uint32_t)~0;
    s->n_gid = (uint32_t)~0;
    s->n_muid = (uint32_t)~0;
    if(!s->name || !s->uid || !s->gid || !s->muid || !s->extension) {
        free_stat_strings(s);
        memset(s, 0, sizeof(*s));
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void free_stat_strings(IxpStat *s) {
    s9_free(s->name);
    s9_free(s->uid);
    s9_free(s->gid);
    s9_free(s->muid);
    s9_free(s->extension);
}

static int state_stat(FidState *state, const ResolvedPath *resolved,
                      struct stat *st) {
    if(resolved->synthetic) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFDIR | 0555;
        return 0;
    }
    if(state->fd >= 0)
        return fstat(state->fd, st);
    if(state->dir)
        return platform_lstat(resolved, st);
    if(state->symlink && state->stat_valid) {
        *st = state->opened_stat;
        return 0;
    }
    return platform_lstat(resolved, st);
}

void fs_stat(Ixp9Req *r) {
    FidState *state = r->fid->aux;
    ResolvedPath resolved;
    struct stat st;
    IxpStat stat;
    IxpMsg message;
    uint16_t size;

    if(!state || !state->path) {
        respond_errno(r, EBADF);
        return;
    }
    if(namespace_resolve(state->path, &resolved) < 0 ||
       state_stat(state, &resolved, &st) < 0) {
        respond_errno(r, errno);
        return;
    }
    if(resolved.synthetic) {
        if(build_synthetic_stat(&stat, "/") < 0) {
            respond_errno(r, ENOMEM);
            return;
        }
    } else if(build_stat(&stat, state->path, &resolved, &st,
                         state->symlink) < 0) {
        respond_errno(r, ENOMEM);
        return;
    }
    size = ixp_sizeof_stat(&stat, ixp_req_getversion(r));
    r->ofcall.rstat.nstat = size;
    r->ofcall.rstat.stat = s9_malloc(size);
    if(!r->ofcall.rstat.stat) {
        free_stat_strings(&stat);
        respond_errno(r, ENOMEM);
        return;
    }
    message = ixp_message((char *)r->ofcall.rstat.stat, size, MsgPack);
    message.version = ixp_req_getversion(r);
    ixp_pstat(&message, &stat);
    free_stat_strings(&stat);
    ixp_respond(r, nil);
}

static int path_is_at_or_below(const char *path, const char *parent) {
    size_t length = strlen(parent);

    return strcmp(path, parent) == 0 ||
           (strncmp(path, parent, length) == 0 && path[length] == '/');
}

static void free_updates(RenameUpdate *updates) {
    while(updates) {
        RenameUpdate *next = updates->next;
        s9_free(updates->path);
        s9_free(updates);
        updates = next;
    }
}

static RenameUpdate *prepare_updates(const char *old_path,
                                     const char *new_path) {
    FidState *state;
    RenameUpdate *updates = NULL;
    size_t old_length = strlen(old_path);

    for(state = nined.fids; state; state = state->next) {
        RenameUpdate *update;
        const char *suffix;
        size_t new_length;
        size_t suffix_length;
        size_t length;

        if(!path_is_at_or_below(state->path, old_path))
            continue;
        suffix = state->path + old_length;
        new_length = strlen(new_path);
        suffix_length = strlen(suffix);
        if(new_length > SIZE_MAX - suffix_length - 1) {
            errno = ENAMETOOLONG;
            free_updates(updates);
            return NULL;
        }
        length = new_length + suffix_length + 1;
        update = s9_malloc(sizeof(*update));
        if(!update) {
            free_updates(updates);
            return NULL;
        }
        update->path = s9_malloc(length);
        if(!update->path) {
            s9_free(update);
            free_updates(updates);
            return NULL;
        }
        snprintf(update->path, length, "%s%s", new_path, suffix);
        update->state = state;
        update->next = updates;
        updates = update;
    }
    return updates;
}

#ifdef NINED_TESTING
int test_prepare_rename_updates(const char *old_path, const char *new_path) {
    RenameUpdate *updates = prepare_updates(old_path, new_path);

    if(!updates)
        return -1;
    free_updates(updates);
    return 0;
}
#endif

static void commit_updates(RenameUpdate *updates) {
    while(updates) {
        RenameUpdate *next = updates->next;
        char *old_path = updates->state->path;
        updates->state->path = updates->path;
        updates->path = NULL;
        s9_free(old_path);
        s9_free(updates);
        updates = next;
    }
}

static int ownership_requested(const IxpStat *stat) {
    return (stat->uid && stat->uid[0]) || (stat->gid && stat->gid[0]) ||
           (stat->muid && stat->muid[0]) || stat->n_uid != (uint32_t)~0 ||
           stat->n_gid != (uint32_t)~0 || stat->n_muid != (uint32_t)~0;
}

void fs_wstat(Ixp9Req *r) {
    FidState *state = r->fid->aux;
    IxpStat *requested = &r->ifcall.twstat.stat;
    ResolvedPath resolved;
    struct stat original;
    int change_name;
    int change_length = requested->length != UINT64_MAX;
    int change_mode = requested->mode != UINT32_MAX;
    int change_atime = requested->atime != UINT32_MAX;
    int change_mtime = requested->mtime != UINT32_MAX;
    int change_ownership;
    int has_changes;
    int mutated = 0;

    if(!state || !state->path) {
        respond_errno(r, EBADF);
        return;
    }
    change_name = requested->name && requested->name[0] &&
                  strcmp(requested->name, path_basename(state->path)) != 0;
    change_ownership = (requested->uid && requested->uid[0]) ||
                       (requested->gid && requested->gid[0]) ||
                       (requested->muid && requested->muid[0]) ||
                       (ixp_req_getversion(r) == IXP_V9P2000U &&
                        ownership_requested(requested));
    has_changes = change_name || change_length || change_mode ||
                  change_atime || change_mtime || change_ownership;
    if(nined.read_only && has_changes) {
        respond_errno(r, EROFS);
        return;
    }
    if(namespace_resolve(state->path, &resolved) < 0 ||
       state_stat(state, &resolved, &original) < 0) {
        respond_errno(r, errno);
        return;
    }
    if(!has_changes) {
        ixp_respond(r, nil);
        return;
    }
    if(namespace_is_protected(state->path)) {
        respond_errno(r, EPERM);
        return;
    }
    if(change_ownership) {
        respond_errno(r, EOPNOTSUPP);
        return;
    }
    if(S_ISLNK(original.st_mode) &&
       (change_length || change_mode || change_atime || change_mtime)) {
        respond_errno(r, EOPNOTSUPP);
        return;
    }
    if(change_name && (change_length || change_mode || change_atime ||
                       change_mtime)) {
        respond_errno(r, EINVAL);
        return;
    }
    if(change_name && !namespace_valid_component(requested->name)) {
        respond_errno(r, EINVAL);
        return;
    }
    if(change_mode) {
        uint32_t allowed_type = S_ISDIR(original.st_mode) ? P9_DMDIR :
                                S_ISLNK(original.st_mode) ? P9_DMSYMLINK : 0;
        if((requested->mode & ~0777U) != allowed_type) {
            respond_errno(r, EOPNOTSUPP);
            return;
        }
    }
    if(change_length) {
        off_t length = (off_t)requested->length;
        if(S_ISDIR(original.st_mode)) {
            respond_errno(r, EISDIR);
            return;
        }
        if(length < 0 || (uint64_t)length != requested->length) {
            respond_errno(r, EFBIG);
            return;
        }
    }

    if(change_name) {
        const char *slash = strrchr(state->path, '/');
        size_t parent_length = slash == state->path ? 1 :
                               (size_t)(slash - state->path);
        char *parent = s9_malloc(parent_length + 1);
        char *new_path;
        ResolvedPath renamed;
        RenameUpdate *updates;

        if(!parent) {
            respond_errno(r, ENOMEM);
            return;
        }
        memcpy(parent, state->path, parent_length);
        parent[parent_length] = '\0';
        new_path = namespace_join_virtual_alloc(parent, requested->name);
        s9_free(parent);
        if(!new_path || namespace_resolve(new_path, &renamed) < 0) {
            int error = errno;
            s9_free(new_path);
            respond_errno(r, error);
            return;
        }
        updates = prepare_updates(state->path, new_path);
        if(!updates) {
            s9_free(new_path);
            respond_errno(r, ENOMEM);
            return;
        }
        if(platform_rename(&resolved, &renamed) < 0) {
            int error = errno;
            free_updates(updates);
            s9_free(new_path);
            respond_errno(r, error);
            return;
        }
        commit_updates(updates);
        s9_free(new_path);
        qid_bump();
        ixp_respond(r, nil);
        return;
    }

    if(change_mode) {
        int result = state->fd >= 0 ? fchmod(state->fd,
                                             requested->mode & 0777)
                                    : platform_chmod(&resolved,
                                                     requested->mode & 0777);
        if(result < 0) {
            respond_errno(r, errno);
            return;
        }
        mutated = 1;
    }
    if(change_length) {
        off_t length = (off_t)requested->length;
        int result = state->fd >= 0 ? ftruncate(state->fd, length)
                                    : platform_truncate(&resolved, length);
        if(result < 0) {
            int error = errno;
            if(change_mode)
                platform_chmod(&resolved, original.st_mode & 0777);
            if(mutated)
                qid_bump();
            respond_errno(r, error);
            return;
        }
        mutated = 1;
    }
    if(change_atime || change_mtime) {
        time_t atime = change_atime ? requested->atime : original.st_atime;
        time_t mtime = change_mtime ? requested->mtime : original.st_mtime;
        if(platform_set_times(&resolved, atime, mtime) < 0) {
            int error = errno;
            if(change_mode)
                platform_chmod(&resolved, original.st_mode & 0777);
            if(mutated)
                qid_bump();
            respond_errno(r, error);
            return;
        }
        mutated = 1;
    }
    if(mutated)
        qid_bump();
    if(state->stat_valid && platform_lstat(&resolved, &state->opened_stat) < 0)
        state->stat_valid = 0;
    ixp_respond(r, nil);
}
