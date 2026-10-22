#ifndef RISCOS_IDENTITY_H
#define RISCOS_IDENTITY_H

#include <sys/stat.h>

/* FileSwitch has no inode number. Keep path identities stable across renames
 * performed by this server, including descendants not previously visited. */
void riscos_identity_stat(const char *path, struct stat *st);
int riscos_identity_rename(const char *old_path, const char *new_path);
void riscos_identity_cleanup(void);

#endif
