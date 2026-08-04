#ifndef SERVER_H
#define SERVER_H

#include <ixp.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "namespace.h"

#define nil NULL

typedef struct FidState FidState;

typedef struct DirCheckpoint {
    uint64_t offset;
    long cookie;
    struct DirCheckpoint *next;
} DirCheckpoint;

typedef struct Simple9pServer {
    FidState *fids;
    struct QidGeneration *generations;
} Simple9pServer;

/* Global variables */
extern IxpServer server;
extern int debug;
extern Ixp9Srv p9srv;
extern Simple9pServer simple9p;

/* Fid state structure to track open files */
struct FidState {
    char *path;
    int open_mode;   /* 9P open mode */
    int open_flags;  /* Unix open flags */
    int fd;
    DIR *dir;
    char *symlink;
    size_t symlink_length;
    uint64_t dir_offset;
    DirCheckpoint *checkpoints;
    int remove_on_close;
    int stat_valid;
    struct stat opened_stat;
    FidState *previous;
    FidState *next;
};

void *s9_malloc(size_t size);
char *s9_strdup(const char *string);
void s9_free(void *pointer);
#ifdef SIMPLE9P_TESTING
void s9_fail_allocation_after(long count);
#endif

FidState *fid_state_create(const char *path);
void fid_state_destroy(FidState *state);
void fid_state_register(FidState *state);
void fid_state_unregister(FidState *state);
void fid_state_close(FidState *state);
uint32_t qid_version(uint64_t path, const struct stat *st);
int qid_prepare(uint64_t path);
void qid_bump(uint64_t path);
void simple9p_state_cleanup(void);

/* Path functions */
void cleanname(char *name);
const char *path_basename(const char *path);
int path_parent(const char *path, char *parent, size_t size);
int joinpath(char *dst, size_t dstsize, const char *dir, const char *name);
int safe_strcat(char *dst, const char *src, size_t dstsize);

/* Filesystem operations */
void fs_attach(Ixp9Req *r);
void fs_walk(Ixp9Req *r);
void fs_open(Ixp9Req *r);
void fs_read(Ixp9Req *r);
void fs_write(Ixp9Req *r);
void fs_create(Ixp9Req *r);
void fs_remove(Ixp9Req *r);
void fs_clunk(Ixp9Req *r);
void fs_stat(Ixp9Req *r);
void fs_wstat(Ixp9Req *r);
void fs_flush(Ixp9Req *r);
void fs_freefid(IxpFid *f);

/* Directory operations */
void read_directory(Ixp9Req *r, FidState *state);
void read_synthetic_directory(Ixp9Req *r, FidState *state);
void read_symlink(Ixp9Req *r, FidState *state);
void read_file(Ixp9Req *r, FidState *state);

/* Stat helpers */
int build_stat(IxpStat *s, const char *path, const ResolvedPath *resolved,
               const struct stat *st,
               const char *symlink_target);
int build_synthetic_stat(IxpStat *s, const char *name);
void free_stat_strings(IxpStat *s);
#ifdef SIMPLE9P_TESTING
int test_prepare_rename_updates(const char *old_path, const char *new_path);
#endif

#endif /* SERVER_H */
