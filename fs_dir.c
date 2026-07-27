#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

static int pack_entry(Ixp9Req *r, IxpMsg *message, IxpStat *stat,
                      uint64_t *position) {
    uint16_t length = ixp_sizeof_stat(stat, ixp_req_getversion(r));
    uint64_t offset = r->ifcall.tread.offset;

    if(*position + length <= offset) {
        *position += length;
        return 0;
    }
    if((size_t)(message->end - message->pos) < length)
        return 1;
    ixp_pstat(message, stat);
    *position += length;
    return 0;
}

void read_synthetic_directory(Ixp9Req *r) {
    IxpMsg message;
    char *buffer;
    uint64_t position = 0;
    size_t i;

    buffer = malloc(r->ifcall.tread.count);
    if(!buffer) {
        ixp_respond(r, "out of memory");
        return;
    }
    message = ixp_message(buffer, r->ifcall.tread.count, MsgPack);
    message.version = ixp_req_getversion(r);

    for(i = 0; i < namespace.nroots; i++) {
        char virtual_path[PATH_MAX];
        ResolvedPath resolved;
        struct stat st;
        IxpStat stat;

        if(snprintf(virtual_path, sizeof(virtual_path), "/%s",
                    namespace.roots[i].name) >= (int)sizeof(virtual_path))
            continue;
        if(namespace_resolve(virtual_path, &resolved) < 0 ||
           lstat(resolved.native_path, &st) < 0)
            continue;
        memset(&stat, 0, sizeof(stat));
        build_stat(&stat, virtual_path, resolved.native_path, &resolved, &st);
        if(pack_entry(r, &message, &stat, &position)) {
            free_stat_strings(&stat);
            break;
        }
        free_stat_strings(&stat);
    }

    r->ofcall.rread.count = message.pos - buffer;
    r->ofcall.rread.data = buffer;
    ixp_respond(r, nil);
}

void read_directory(Ixp9Req *r, const char *path,
                    const ResolvedPath *resolved) {
    DIR *dir = opendir(resolved->native_path);
    struct dirent *de;
    IxpMsg m;
    char *buf = NULL;
    uint64_t pos = 0;
    
    if (!dir) {
        ixp_respond(r, strerror(errno));
        return;
    }
    
    buf = malloc(r->ifcall.tread.count);
    if (!buf) {
        closedir(dir);
        ixp_respond(r, "out of memory");
        return;
    }
    
    m = ixp_message(buf, r->ifcall.tread.count, MsgPack);
    m.version = ixp_req_getversion(r);
    
    /* Read directory entries, skipping until we reach the requested offset */
    while ((de = readdir(dir))) {
        IxpStat s;
        struct stat st2;
        char childpath[PATH_MAX];
        char virtual_path[PATH_MAX];
        ResolvedPath child;
        
        /* Clients synthesize dot entries and walk ".." through fs_walk. */
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        if(namespace_join_virtual(virtual_path, sizeof(virtual_path),
                                  path, de->d_name) < 0)
            continue;
        if(namespace_resolve(virtual_path, &child) < 0)
            continue;
        strcpy(childpath, child.native_path);
            
        if (lstat(childpath, &st2) < 0) {
            /* Failed to stat the entry, skip it */
            continue;
        }
        
        memset(&s, 0, sizeof(IxpStat));
        build_stat(&s, virtual_path, childpath, &child, &st2);
        if(pack_entry(r, &m, &s, &pos)) {
            free_stat_strings(&s);
            break;
        }
        free_stat_strings(&s);
    }
    
    closedir(dir);
    r->ofcall.rread.count = m.pos - buf;
    r->ofcall.rread.data = buf;
    ixp_respond(r, nil);
    /* buf is now owned by libixp */
}

void read_symlink(Ixp9Req *r, const char *fullpath) {
    /* Add extra byte for null terminator */
    size_t buf_size = r->ifcall.tread.count + 1;
    char *buf = malloc(buf_size);
    int n;
    
    if (!buf) {
        ixp_respond(r, "out of memory");
        return;
    }
    
    /* We read one character less than the buffer size to ensure space for null terminator */
    n = readlink(fullpath, buf, buf_size - 1);
    if (n < 0) {
        free(buf);
        ixp_respond(r, strerror(errno));
        return;
    }
    
    /* Null-terminate the link target */
    buf[n] = '\0';
    
    /* Respect the offset */
    if (r->ifcall.tread.offset >= (uint64_t)n) {
        /* Offset is beyond the data, return empty result */
        r->ofcall.rread.count = 0;
        r->ofcall.rread.data = buf;
    } else {
        /* Calculate how much data to return */
        size_t len = n - r->ifcall.tread.offset;
        if (len > r->ifcall.tread.count)
            len = r->ifcall.tread.count;
        
        /* Move the data to the beginning of the buffer */
        memmove(buf, buf + r->ifcall.tread.offset, len);
        r->ofcall.rread.count = len;
        r->ofcall.rread.data = buf;
    }
    
    ixp_respond(r, nil);
    /* buf is now owned by libixp */
}

void read_file(Ixp9Req *r, const char *fullpath) {
    int fd = open(fullpath, O_RDONLY);
    char *buf = NULL;
    
    if (fd < 0) {
        ixp_respond(r, strerror(errno));
        return;
    }
    
    buf = malloc(r->ifcall.tread.count);
    if (!buf) {
        close(fd);
        ixp_respond(r, "out of memory");
        return;
    }
    
    /* Position file pointer at the requested offset */
    off_t seek_result = lseek(fd, r->ifcall.tread.offset, SEEK_SET);
    if (seek_result == (off_t)-1) {
        free(buf);
        close(fd);
        ixp_respond(r, strerror(errno));
        return;
    }
    
    /* Read the requested data */
    ssize_t n = read(fd, buf, r->ifcall.tread.count);
    close(fd);
    
    if (n < 0) {
        free(buf);
        ixp_respond(r, strerror(errno));
        return;
    }
    
    r->ofcall.rread.count = n;
    r->ofcall.rread.data = buf;
    ixp_respond(r, nil);
    /* buf is now owned by libixp */
}
