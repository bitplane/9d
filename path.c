#include "server.h"
#include <string.h>
#include <stdio.h>

/* Clean a path - remove . and .. components, duplicate slashes, etc. */
void cleanname(char *name) {
    char *p, *q, *dotdot;
    int rooted;

    if(name[0] == '\0')
        return;

    rooted = (name[0] == '/');
    
    /* invariants:
     *  p points at beginning of path element we're considering.
     *  q points just past the last path element we wrote (no slash).
     *  dotdot points just past the point where .. cannot backtrack
     *    any further (no slash).
     */
    p = q = dotdot = name + rooted;
    while(*p) {
        if(p[0] == '/') {
            p++;
        } else if(p[0] == '.' && (p[1] == '\0' || p[1] == '/')) {
            p++;
        } else if(p[0] == '.' && p[1] == '.' && (p[2] == '\0' || p[2] == '/')) {
            p += 2;
            if(q > dotdot) {
                /* can backtrack */
                while(--q > dotdot && q[-1] != '/')
                    ;
            } else if(!rooted) {
                /* /.. is / but ./../ is .. */
                if(q != name)
                    *q++ = '/';
                *q++ = '.';
                *q++ = '.';
                dotdot = q;
            }
        } else {
            /* real path element */
            if(q != name + rooted)
                *q++ = '/';
            while((*q = *p) != '\0' && *q != '/')
                p++, q++;
        }
    }
    
    if(q == name) {
        if(rooted) {
            *q++ = '/';
        } else {
            *q++ = '.';
        }
    }
    *q = '\0';
}

/*
 * Join host paths without assuming that a root ends in '/'. Amiga-style
 * volume roots, such as "SYS:", already contain their path separator.
 */
int joinpath(char *dst, size_t dstsize, const char *dir, const char *name) {
    size_t dirlen;
    const char *separator = "";

    if(!dst || !dir || !name || dstsize == 0)
        return -1;

    dirlen = strlen(dir);
    if(dirlen > 0 && (dir[dirlen - 1] == '/' || dir[dirlen - 1] == ':')) {
        while(*name == '/')
            name++;
    } else if(*name != '/') {
        separator = "/";
    }

    if(snprintf(dst, dstsize, "%s%s%s", dir, separator, name) >= (int)dstsize)
        return -1;
    return 0;
}

/* Safe string operations */
int safe_strcat(char *dst, const char *src, size_t dstsize) {
    size_t dstlen = strlen(dst);
    size_t srclen = strlen(src);
    
    if(dstlen + srclen >= dstsize) {
        return -1;
    }
    
    strcat(dst, src);
    return 0;
}
