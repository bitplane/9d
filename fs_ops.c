#include "server.h"
#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

Simple9pServer simple9p;

typedef struct QidGeneration {
    uint64_t path;
    uint32_t value;
    struct QidGeneration *next;
} QidGeneration;

uint32_t qid_version(uint64_t path, const struct stat *st) {
    QidGeneration *generation;
    uint32_t version = (uint32_t)st->st_mtime;

    for(generation = simple9p.generations; generation;
        generation = generation->next) {
        if(generation->path == path)
            return version ^ generation->value;
    }
    return version;
}

int qid_prepare(uint64_t path) {
    QidGeneration *generation;

    for(generation = simple9p.generations; generation;
        generation = generation->next) {
        if(generation->path == path) {
            return 0;
        }
    }
    generation = s9_malloc(sizeof(*generation));
    if(!generation)
        return -1;
    generation->path = path;
    generation->value = 0;
    generation->next = simple9p.generations;
    simple9p.generations = generation;
    return 0;
}

void qid_bump(uint64_t path) {
    QidGeneration *generation;

    for(generation = simple9p.generations; generation;
        generation = generation->next) {
        if(generation->path == path) {
            generation->value++;
            return;
        }
    }
}

void simple9p_state_cleanup(void) {
    QidGeneration *generation;

    while((generation = simple9p.generations) != NULL) {
        simple9p.generations = generation->next;
        s9_free(generation);
    }
}

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
    qid->version = qid_version(qid->path, st);
}

void fid_state_register(FidState *state) {
    state->previous = NULL;
    state->next = simple9p.fids;
    if(state->next)
        state->next->previous = state;
    simple9p.fids = state;
}

void fid_state_unregister(FidState *state) {
    if(state->previous)
        state->previous->next = state->next;
    else if(simple9p.fids == state)
        simple9p.fids = state->next;
    if(state->next)
        state->next->previous = state->previous;
    state->previous = NULL;
    state->next = NULL;
}

void fid_state_close(FidState *state) {
    DirCheckpoint *checkpoint;

    if(!state)
        return;
    if(state->fd >= 0) {
        close(state->fd);
        state->fd = -1;
    }
    if(state->dir) {
        closedir(state->dir);
        state->dir = NULL;
    }
    s9_free(state->symlink);
    state->symlink = NULL;
    state->symlink_length = 0;
    state->stat_valid = 0;
    state->remove_on_close = 0;
    state->open_mode = 0;
    state->open_flags = 0;
    while((checkpoint = state->checkpoints) != NULL) {
        state->checkpoints = checkpoint->next;
        s9_free(checkpoint);
    }
    state->dir_offset = 0;
}

FidState *fid_state_create(const char *path) {
    FidState *state;

    state = s9_malloc(sizeof(*state));
    if(!state)
        return NULL;
    memset(state, 0, sizeof(*state));
    state->fd = -1;
    state->path = s9_strdup(path);
    if(!state->path) {
        s9_free(state);
        return NULL;
    }
    fid_state_register(state);
    return state;
}

void fid_state_destroy(FidState *state) {
    if(!state)
        return;
    fid_state_unregister(state);
    fid_state_close(state);
    s9_free(state->path);
    s9_free(state);
}

void fs_attach(Ixp9Req *r) {
    FidState *state;
    ResolvedPath resolved;
    struct stat st;

    state = fid_state_create("/");
    if(!state) {
        ixp_respond(r, "out of memory");
        return;
    }
    if(namespace_resolve("/", &resolved) < 0 ||
       (!resolved.synthetic && platform_lstat(&resolved, &st) < 0)) {
        int error = errno;
        fid_state_destroy(state);
        ixp_respond(r, strerror(error));
        return;
    }
    if(resolved.synthetic)
        memset(&st, 0, sizeof(st));
    set_qid(&r->fid->qid, &resolved, &st);
    r->fid->aux = state;
    r->ofcall.rattach.qid = r->fid->qid;
    ixp_respond(r, nil);
}

void fs_walk(Ixp9Req *r) {
    FidState *state = r->fid->aux;
    char *candidate;
    unsigned int i;
    IxpQid final_qid;

    if(!state || !state->path) {
        ixp_respond(r, "invalid fid state for walk");
        return;
    }
    candidate = s9_strdup(state->path);
    if(!candidate) {
        ixp_respond(r, "out of memory");
        return;
    }
    final_qid = r->fid->qid;

    for(i = 0; i < r->ifcall.twalk.nwname; i++) {
        char *next;
        ResolvedPath resolved;
        struct stat st;

        next = namespace_join_virtual_alloc(candidate,
                                            r->ifcall.twalk.wname[i]);
        if(!next) {
            if(i == 0) {
                int error = errno;
                s9_free(candidate);
                ixp_respond(r, strerror(error));
                return;
            }
            break;
        }
        s9_free(candidate);
        candidate = next;
        if(namespace_resolve(candidate, &resolved) < 0 ||
           (!resolved.synthetic && platform_lstat(&resolved, &st) < 0)) {
            if(i == 0) {
                int error = errno;
                s9_free(candidate);
                ixp_respond(r, strerror(error));
                return;
            }
            break;
        }
        if(resolved.synthetic)
            memset(&st, 0, sizeof(st));
        set_qid(&r->ofcall.rwalk.wqid[i], &resolved, &st);
        final_qid = r->ofcall.rwalk.wqid[i];
        if(i + 1 < r->ifcall.twalk.nwname &&
           !(final_qid.type & P9_QTDIR)) {
            i++;
            break;
        }
    }
    r->ofcall.rwalk.nwqid = i;

    if(i == r->ifcall.twalk.nwname) {
        if(r->newfid == r->fid) {
            char *old_path = state->path;
            state->path = candidate;
            s9_free(old_path);
        } else {
            FidState *newstate = fid_state_create(candidate);
            if(!newstate) {
                s9_free(candidate);
                ixp_respond(r, "out of memory");
                return;
            }
            r->newfid->aux = newstate;
            s9_free(candidate);
        }
        r->newfid->qid = final_qid;
    } else {
        s9_free(candidate);
    }
    ixp_respond(r, nil);
}

void fs_clunk(Ixp9Req *r) {
    FidState *state = r->fid->aux;

    if(state && state->remove_on_close) {
        ResolvedPath resolved;
        struct stat st;

        if(namespace_resolve(state->path, &resolved) < 0 ||
           platform_lstat(&resolved, &st) < 0) {
            ixp_respond(r, strerror(errno));
            return;
        }
        if(platform_remove(&resolved, S_ISDIR(st.st_mode)) < 0) {
            ixp_respond(r, strerror(errno));
            return;
        }
        qid_bump(r->fid->qid.path);
    }
    ixp_respond(r, nil);
}

void fs_flush(Ixp9Req *r) {
    ixp_respond(r, nil);
}

void fs_freefid(IxpFid *f) {
    if(f && f->aux) {
        fid_state_destroy(f->aux);
        f->aux = NULL;
    }
}
