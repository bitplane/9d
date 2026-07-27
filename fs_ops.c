#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h> // For O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_TRUNC

static void set_qid(IxpQid *qid, const ResolvedPath *resolved,
                    const struct stat *st) {
    if(resolved->synthetic) {
        qid->type = P9_QTDIR;
        qid->path = namespace_root_qid();
        qid->version = 0;
        return;
    }
    qid->type = P9_QTFILE;
    if(S_ISDIR(st->st_mode))
        qid->type = P9_QTDIR;
    else if(S_ISLNK(st->st_mode))
        qid->type = P9_QTSYMLINK;
    qid->path = namespace_qid(resolved, st);
    qid->version = st->st_mtime;
}

// fs_attach handles the Tattach Fcall.
// It initializes a new FidState for the root of the filesystem.
void fs_attach(Ixp9Req *r) {
    FidState *state = malloc(sizeof(FidState));
    if (!state) {
        ixp_respond(r, "out of memory");
        return;
    }

    state->path = strdup("/"); // Represents the root of the served directory
    if (!state->path) {
        free(state);
        ixp_respond(r, "out of memory");
        return;
    }
    state->open_mode = 0;  // Not opened in a specific mode yet
    state->open_flags = 0; // No OS flags yet

    ResolvedPath resolved;
    struct stat st_root;
    if (namespace_resolve("/", &resolved) < 0) {
         free(state->path);
         free(state);
         ixp_respond(r, strerror(errno));
         return;
    }
    if (!resolved.synthetic && lstat(resolved.native_path, &st_root) < 0) {
        free(state->path);
        free(state);
        ixp_respond(r, strerror(errno));
        return;
    }

    set_qid(&r->fid->qid, &resolved, &st_root);
    r->fid->aux = state;
    r->ofcall.rattach.qid = r->fid->qid;
    ixp_respond(r, nil);
}

// fs_walk handles the Twalk Fcall.
// It navigates the filesystem, creating a new FID (newfid) for the target path.
void fs_walk(Ixp9Req *r) {
    FidState *state = r->fid->aux; // Current FID's state
    FidState *newstate;            // State for the new FID (r->newfid)
    char current_relative_path[PATH_MAX];
    ResolvedPath resolved;
    struct stat st;
    int i;

    if (!state || !state->path) {
        ixp_respond(r, "invalid fid state for walk");
        return;
    }

    // Clone current fid state for the new fid
    newstate = malloc(sizeof(FidState));
    if (!newstate) {
        ixp_respond(r, "out of memory");
        return;
    }
    newstate->path = strdup(state->path);
    if (!newstate->path) {
        free(newstate);
        ixp_respond(r, "out of memory");
        return;
    }
    newstate->open_mode = 0;  // New FID is not opened yet
    newstate->open_flags = 0;
    r->newfid->aux = newstate; // Attach new state to the new FID

    // If no names to walk (nwname == 0), newfid is a clone of fid
    if (r->ifcall.twalk.nwname == 0) {
        r->newfid->qid = r->fid->qid; // QID is the same
        // newstate->path is already a copy of state->path
        ixp_respond(r, nil);
        return;
    }

    // Make a mutable copy of the path for constructing the new path
    strncpy(current_relative_path, newstate->path, PATH_MAX -1);
    current_relative_path[PATH_MAX -1] = '\0';

    for (i = 0; i < r->ifcall.twalk.nwname; i++) {
        const char *name_component = r->ifcall.twalk.wname[i];

        char next_path[PATH_MAX];

        if(namespace_join_virtual(next_path, sizeof(next_path),
                                  current_relative_path, name_component) < 0) {
            ixp_respond(r, "path too long during walk");
            return;
        }
        strcpy(current_relative_path, next_path);

        if(namespace_resolve(current_relative_path, &resolved) < 0) {
            r->ofcall.rwalk.nwqid = i;
            ixp_respond(r, strerror(errno));
            return;
        }

        if(!resolved.synthetic && lstat(resolved.native_path, &st) < 0) {
            // If any component doesn't exist, walk fails.
            // Respond with error, and number of successful walks (i)
            r->ofcall.rwalk.nwqid = i; // Report how many names were successfully walked
            ixp_respond(r, strerror(errno));
            return;
        }

        set_qid(&r->ofcall.rwalk.wqid[i], &resolved, &st);
    }

    // All components walked successfully
    r->ofcall.rwalk.nwqid = i;
    r->newfid->qid = r->ofcall.rwalk.wqid[i - 1]; // QID of the final target

    // Update the path in newstate to the final walked path
    free(newstate->path);
    newstate->path = strdup(current_relative_path);
    if (!newstate->path) {
        // This is tricky: walk succeeded, but server ran out of memory for final path.
        // Should ideally not happen.
        ixp_respond(r, "out of memory storing final path for walk");
        // The newfid is now in an inconsistent state.
        return;
    }

    ixp_respond(r, nil);
}

// fs_clunk handles the Tclunk Fcall.
// It signifies that a FID is no longer needed by the client.
// The server should release any resources associated with the FID.
void fs_clunk(Ixp9Req *r) {
    // FidState is freed by fs_freefid, which is called by libixp
    // after fs_clunk responds or if the FID is implicitly clunked (e.g. Tremove).
    ixp_respond(r, nil);
}

// fs_flush handles the Tflush Fcall.
// It's used to abort a pending request. This simple server doesn't
// have complex pending requests that would need explicit flushing logic beyond what libixp handles.
void fs_flush(Ixp9Req *r) {
    // For a simple server, responding nil is usually sufficient.
    // libixp handles the actual flushing of messages for the old tag.
    ixp_respond(r, nil);
}

// fs_freefid is called by libixp when a FID is destroyed.
// It should free any auxiliary data (FidState) attached to the FID.
void fs_freefid(IxpFid *f) {
    if (f && f->aux) {
        FidState *state = f->aux;
        if (state->path) {
            free(state->path);
            state->path = NULL;
        }
        free(state);
        f->aux = NULL;
    }
}
