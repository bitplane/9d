#include "server.h"
#include "platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DirCheckpoint *find_checkpoint(FidState *state, uint64_t offset) {
    DirCheckpoint *checkpoint;

    for(checkpoint = state->checkpoints; checkpoint;
        checkpoint = checkpoint->next) {
        if(checkpoint->offset == offset)
            return checkpoint;
    }
    return NULL;
}

static int remember_checkpoint(FidState *state, uint64_t offset,
                               long cookie) {
    DirCheckpoint *checkpoint;

    if(find_checkpoint(state, offset))
        return 0;
    checkpoint = s9_malloc(sizeof(*checkpoint));
    if(!checkpoint)
        return -1;
    checkpoint->offset = offset;
    checkpoint->cookie = cookie;
    checkpoint->next = state->checkpoints;
    state->checkpoints = checkpoint;
    return 0;
}

static int seek_directory(FidState *state, uint64_t offset) {
    DirCheckpoint *checkpoint;

    if(offset == state->dir_offset)
        return 0;
    if(offset == 0) {
        rewinddir(state->dir);
        state->dir_offset = 0;
        return 0;
    }
    checkpoint = find_checkpoint(state, offset);
    if(!checkpoint) {
        errno = EINVAL;
        return -1;
    }
    seekdir(state->dir, checkpoint->cookie);
    state->dir_offset = offset;
    return 0;
}

void read_directory(Ixp9Req *r, FidState *state) {
    IxpMsg message;
    char *buffer;
    uint32_t count = fs_read_count(r);

    if(seek_directory(state, r->ifcall.tread.offset) < 0) {
        respond_errno(r, errno);
        return;
    }
    buffer = s9_malloc(count ? count : 1);
    if(!buffer) {
        respond_errno(r, ENOMEM);
        return;
    }
    message = ixp_message(buffer, count, MsgPack);
    message.version = ixp_req_getversion(r);

    for(;;) {
        long before = telldir(state->dir);
        struct dirent *entry = readdir(state->dir);
        char *virtual_path;
        ResolvedPath resolved;
        struct stat st;
        IxpStat stat;
        uint16_t length;
        long after;

        if(!entry)
            break;
        if(strcmp(entry->d_name, ".") == 0 ||
           strcmp(entry->d_name, "..") == 0)
            continue;
        virtual_path = namespace_join_virtual_alloc(state->path,
                                                    entry->d_name);
        if(!virtual_path)
            continue;
        if(namespace_resolve(virtual_path, &resolved) < 0 ||
           platform_lstat(&resolved, &st) < 0) {
            s9_free(virtual_path);
            continue;
        }
        memset(&stat, 0, sizeof(stat));
        if(build_stat(&stat, virtual_path, &resolved, &st, NULL) < 0) {
            s9_free(virtual_path);
            seekdir(state->dir, before);
            s9_free(buffer);
            respond_errno(r, errno);
            return;
        }
        s9_free(virtual_path);
        length = ixp_sizeof_stat(&stat, ixp_req_getversion(r));
        if((size_t)(message.end - message.pos) < length) {
            free_stat_strings(&stat);
            seekdir(state->dir, before);
            break;
        }
        after = telldir(state->dir);
        if(remember_checkpoint(state, state->dir_offset + length, after) < 0) {
            free_stat_strings(&stat);
            seekdir(state->dir, before);
            s9_free(buffer);
            respond_errno(r, errno);
            return;
        }
        ixp_pstat(&message, &stat);
        state->dir_offset += length;
        free_stat_strings(&stat);
    }
    r->ofcall.rread.count = (uint32_t)(message.pos - buffer);
    r->ofcall.rread.data = buffer;
    ixp_respond(r, nil);
}

void read_synthetic_directory(Ixp9Req *r, FidState *state) {
    IxpMsg message;
    char *buffer;
    uint64_t position = 0;
    size_t index;
    int boundary = r->ifcall.tread.offset == 0;
    uint32_t count = fs_read_count(r);

    (void)state;
    buffer = s9_malloc(count ? count : 1);
    if(!buffer) {
        respond_errno(r, ENOMEM);
        return;
    }
    message = ixp_message(buffer, count, MsgPack);
    message.version = ixp_req_getversion(r);

    for(index = 0; index < namespace.nroots; index++) {
        char *virtual_path;
        ResolvedPath resolved;
        struct stat st;
        IxpStat stat;
        uint16_t length;

        virtual_path = namespace_join_virtual_alloc("/",
                                                    namespace.roots[index].name);
        if(!virtual_path)
            continue;
        if(namespace_resolve(virtual_path, &resolved) < 0 ||
           platform_lstat(&resolved, &st) < 0) {
            s9_free(virtual_path);
            continue;
        }
        memset(&stat, 0, sizeof(stat));
        if(build_stat(&stat, virtual_path, &resolved, &st, NULL) < 0) {
            s9_free(virtual_path);
            s9_free(buffer);
            respond_errno(r, ENOMEM);
            return;
        }
        s9_free(virtual_path);
        length = ixp_sizeof_stat(&stat, ixp_req_getversion(r));
        if(position == r->ifcall.tread.offset)
            boundary = 1;
        if(position + length <= r->ifcall.tread.offset) {
            position += length;
            free_stat_strings(&stat);
            continue;
        }
        if(!boundary) {
            free_stat_strings(&stat);
            s9_free(buffer);
            respond_errno(r, EINVAL);
            return;
        }
        if((size_t)(message.end - message.pos) < length) {
            free_stat_strings(&stat);
            break;
        }
        ixp_pstat(&message, &stat);
        position += length;
        free_stat_strings(&stat);
    }
    if(r->ifcall.tread.offset > position) {
        s9_free(buffer);
        respond_errno(r, EINVAL);
        return;
    }
    r->ofcall.rread.count = (uint32_t)(message.pos - buffer);
    r->ofcall.rread.data = buffer;
    ixp_respond(r, nil);
}

void read_symlink(Ixp9Req *r, FidState *state) {
    uint64_t offset = r->ifcall.tread.offset;
    size_t count;
    uint32_t limit = fs_read_count(r);
    char *buffer;

    if(offset >= state->symlink_length)
        count = 0;
    else {
        count = state->symlink_length - (size_t)offset;
        if(count > limit)
            count = limit;
    }
    buffer = s9_malloc(count ? count : 1);
    if(!buffer) {
        respond_errno(r, ENOMEM);
        return;
    }
    if(count)
        memcpy(buffer, state->symlink + (size_t)offset, count);
    r->ofcall.rread.count = (uint32_t)count;
    r->ofcall.rread.data = buffer;
    ixp_respond(r, nil);
}

void read_file(Ixp9Req *r, FidState *state) {
    off_t offset = (off_t)r->ifcall.tread.offset;
    char *buffer;
    ssize_t count;
    uint32_t limit = fs_read_count(r);

    if(offset < 0 || (uint64_t)offset != r->ifcall.tread.offset) {
        respond_errno(r, EOVERFLOW);
        return;
    }
    buffer = s9_malloc(limit ? limit : 1);
    if(!buffer) {
        respond_errno(r, ENOMEM);
        return;
    }
    if(limit == 0)
        count = 0;
    else if(lseek(state->fd, offset, SEEK_SET) < 0) {
        int error = errno;
        s9_free(buffer);
        respond_errno(r, error);
        return;
    } else
        count = read(state->fd, buffer, limit);
    if(count < 0) {
        int error = errno;
        s9_free(buffer);
        respond_errno(r, error);
        return;
    }
    r->ofcall.rread.count = (uint32_t)count;
    r->ofcall.rread.data = buffer;
    ixp_respond(r, nil);
}
