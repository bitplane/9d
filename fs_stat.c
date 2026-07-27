#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For truncate, chmod, readlink
#include <errno.h>
#include <limits.h> // For LONG_MAX

// build_stat populates an IxpStat structure from a file's stat data.
// s: The IxpStat structure to populate.
// path: The relative 9P path of the file.
// fullpath: The absolute OS path of the file.
// st: The struct stat obtained from lstat() on fullpath.
void build_stat(IxpStat *s, const char *path, const char *fullpath,
                const ResolvedPath *resolved, struct stat *st) {
    // Initialize basic fields
    s->type = 0; // Typically 0 for 9P2000
    s->dev = 0;  // Typically 0 for 9P2000

    // Determine QID type
    s->qid.type = P9_QTFILE; // Default to file
    if (S_ISDIR(st->st_mode)) {
        s->qid.type = P9_QTDIR;
    } else if (S_ISLNK(st->st_mode)) {
        s->qid.type = P9_QTSYMLINK;
    }

    // QID path and version
    s->qid.path = namespace_qid(resolved, st);
    s->qid.version = st->st_mtime;  // Use modification time for version

    // Mode: 9P permissions and directory/symlink flags
    s->mode = st->st_mode & 0777; // Basic Unix permissions
    if (S_ISDIR(st->st_mode)) {
        s->mode |= P9_DMDIR;
    } else if (S_ISLNK(st->st_mode)) {
        s->mode |= P9_DMSYMLINK;
    }
    // Other types like P9_DMAPPEND, P9_DMEXCL, P9_DMAUTH could be set if applicable

    // Timestamps
    s->atime = st->st_atime;
    s->mtime = st->st_mtime;

    // Length and blocks - use exactly what the OS reports
    s->length = st->st_size;
    
    // For symlinks, store target in extension field and set length
    // For non-symlinks, extension must be empty string (not NULL) for 9P2000.u
    if (S_ISLNK(st->st_mode)) {
        char target_buf[PATH_MAX];
        ssize_t len = readlink(fullpath, target_buf, sizeof(target_buf) - 1);
        if (len != -1) {
            target_buf[len] = '\0';
            s->length = len;
            s->extension = strdup(target_buf);
        } else {
            s->extension = strdup("");
        }
    } else {
        s->extension = strdup("");
    }

    // 9P2000.u numeric IDs
    s->n_uid = st->st_uid;
    s->n_gid = st->st_gid;
    s->n_muid = st->st_uid;

    // Name: The last component of the path
    // Handle root path specifically
    if (strcmp(path, "/") == 0) {
        s->name = strdup("/"); // Allocate a new copy to be consistent with other cases
    } else {
        s->name = strdup(path_basename(path));
    }

    // User and group names
    // For simplicity, using environment USER or "none". 9P allows string UIDs.
    const char *user = getenv("USER");
    s->uid = user ? strdup(user) : strdup("none");
    s->gid = strdup(s->uid);  // Same as uid
    s->muid = strdup(s->uid); // Last modifier same as uid
}

void build_synthetic_stat(IxpStat *s, const char *name) {
    memset(s, 0, sizeof(*s));
    s->qid.type = P9_QTDIR;
    s->qid.path = namespace_root_qid();
    s->mode = P9_DMDIR | 0555;
    s->name = strdup(name);
    s->uid = strdup("none");
    s->gid = strdup("none");
    s->muid = strdup("none");
    s->extension = strdup("");
    s->n_uid = (uint32_t)~0;
    s->n_gid = (uint32_t)~0;
    s->n_muid = (uint32_t)~0;
}

// Helper to free allocated strings in IxpStat
void free_stat_strings(IxpStat *s) {
    if (s->name) free((char*)s->name);
    if (s->uid) free((char*)s->uid);
    if (s->gid) free((char*)s->gid);
    if (s->muid) free((char*)s->muid);
    if (s->extension) free((char*)s->extension);
}

// fs_stat handles Tstat messages.
void fs_stat(Ixp9Req *r) {
    FidState *state = r->fid->aux;
    ResolvedPath resolved;
    struct stat st_os; // OS stat structure
    IxpStat s_ixp;     // 9P stat structure
    IxpMsg m;
    uint16_t size_of_ixp_stat;

    if (!state || !state->path) {
        ixp_respond(r, "invalid fid state");
        return;
    }

    if (namespace_resolve(state->path, &resolved) < 0) {
        ixp_respond(r, strerror(errno));
        return;
    }

    if (!resolved.synthetic && lstat(resolved.native_path, &st_os) < 0) {
        ixp_respond(r, strerror(errno));
        return;
    }

    memset(&s_ixp, 0, sizeof(IxpStat)); // Zero out the structure
    if(resolved.synthetic)
        build_synthetic_stat(&s_ixp, "/");
    else
        build_stat(&s_ixp, state->path, resolved.native_path, &resolved,
                   &st_os);

    size_of_ixp_stat = ixp_sizeof_stat(&s_ixp, ixp_req_getversion(r));
    r->ofcall.rstat.nstat = size_of_ixp_stat;
    r->ofcall.rstat.stat = malloc(size_of_ixp_stat);

    if (!r->ofcall.rstat.stat) {
        free_stat_strings(&s_ixp);
        ixp_respond(r, "out of memory");
        return;
    }

    m = ixp_message((char *)r->ofcall.rstat.stat, size_of_ixp_stat, MsgPack);
    m.version = ixp_req_getversion(r);
    ixp_pstat(&m, &s_ixp);

    // Free allocated strings after packing
    free_stat_strings(&s_ixp);

    ixp_respond(r, nil);
    // r->ofcall.rstat.stat is now owned by libixp and will be freed by it.
}

// fs_wstat handles Twstat messages.
void fs_wstat(Ixp9Req *r) {
    FidState *state = r->fid->aux;
    ResolvedPath resolved;
    IxpStat *s_new = &r->ifcall.twstat.stat; // The new stat data from client
    struct stat current_st_os;               // Current OS attributes of the file
    int respond_early = 0;
    char *original_fid_path_on_success_rename = NULL;

    if (debug) {
        fprintf(stderr, "fs_wstat: path=%s, length=%llu (mask=%llu)\n", 
                state ? state->path : "NULL", 
                (unsigned long long)s_new->length, 
                (unsigned long long)~0ULL);
    }

    if (!state || !state->path) {
        ixp_respond(r, "invalid fid state");
        return;
    }

    if(namespace_is_protected(state->path)) {
        ixp_respond(r, strerror(EPERM));
        return;
    }
    if (namespace_resolve(state->path, &resolved) < 0) {
        ixp_respond(r, strerror(errno));
        return;
    }

    if (lstat(resolved.native_path, &current_st_os) < 0) {
        ixp_respond(r, strerror(errno));
        return;
    }

    // Handle length change (truncate)
    // This is a special case that's particularly important to handle correctly
    // The FUSE protocol uses ~0ULL as a "don't change" marker for the length field
    if (s_new->length != (uint64_t)~0ULL) {
        if (debug) {
            fprintf(stderr, "fs_wstat: truncating file to %llu (current %llu)\n", 
                    (unsigned long long)s_new->length, 
                    (unsigned long long)current_st_os.st_size);
        }
        
        if (S_ISDIR(current_st_os.st_mode)) {
            // Can't truncate a directory
            ixp_respond(r, strerror(EISDIR));
            respond_early = 1;
        } else {
            // For regular files, perform the truncate
            // First check if we actually need to truncate (optimization)
            if (s_new->length != (uint64_t)current_st_os.st_size) {
                // Validate the truncate length is reasonable
                if (s_new->length > (uint64_t)LONG_MAX) {
                    // Most filesystem APIs can't handle sizes larger than LONG_MAX
                    ixp_respond(r, strerror(EFBIG));
                    respond_early = 1;
                } else {
                    if (truncate(resolved.native_path, (off_t)s_new->length) < 0) {
                        ixp_respond(r, strerror(errno));
                        respond_early = 1;
                    }
                }
            }
        }
    }

    if (respond_early)
        return;

    // Handle mode changes (chmod)
    // P9_BIT32_MASK (~0U) is the "don't change" marker for uint32_t fields.
    if (s_new->mode != (uint32_t)~0) {
        mode_t requested_perms = s_new->mode & 0777; // Apply only permission bits
        if (requested_perms != (current_st_os.st_mode & 0777)) {
            if (chmod(resolved.native_path, requested_perms) < 0) {
                ixp_respond(r, strerror(errno));
                respond_early = 1;
            }
        }
    }

    if (respond_early)
        return;

    // Handle name changes (rename)
    // s_new->name being NULL or empty means "don't change name".
    if (s_new->name != NULL && s_new->name[0] != '\0') {
        const char *current_basename = path_basename(state->path);

            // Check if the new name is actually different from the current one.
            if (strcmp(current_basename, s_new->name) != 0) {
                char new_relative_path[PATH_MAX];
                char new_absolute_fullpath[PATH_MAX];
                char parent[PATH_MAX];

                if(path_parent(state->path, parent, sizeof(parent)) < 0) {
                    ixp_respond(r, "path too long for wstat rename");
                    respond_early = 1;
                } else if(namespace_join_virtual(
                              new_relative_path, sizeof(new_relative_path),
                              parent, s_new->name) < 0) {
                    ixp_respond(r, "invalid name for wstat rename");
                    respond_early = 1;
                } else {
                    ResolvedPath renamed;
                    if (namespace_resolve(new_relative_path, &renamed) < 0) {
                        ixp_respond(r, strerror(errno));
                        respond_early = 1;
                    } else {
                        strcpy(new_absolute_fullpath, renamed.native_path);
                        if (rename(resolved.native_path, new_absolute_fullpath) < 0) {
                            ixp_respond(r, strerror(errno));
                            respond_early = 1;
                        } else {
                            original_fid_path_on_success_rename = state->path; // Keep old path pointer
                            state->path = strdup(new_relative_path);
                            if (!state->path) {
                                // Critical: OS rename succeeded, but server state update failed.
                                // Try to restore old path to prevent FID from being totally broken.
                                state->path = original_fid_path_on_success_rename;
                                original_fid_path_on_success_rename = NULL; // Don't free it below
                                ixp_respond(r, "out of memory after rename, server state inconsistent");
                                respond_early = 1;
                                // Consider logging this critical failure.
                            } else {
                                free(original_fid_path_on_success_rename); // Free the old path string
                            }
                        }
                    }
                }
            }
    }

    if (respond_early)
        return;

    // Other wstat operations (e.g., mtime, uid, gid) are not implemented.
    // Client would set s_new->mtime, s_new->uid, etc. to non-"don't change" values.

    ixp_respond(r, nil);
}
