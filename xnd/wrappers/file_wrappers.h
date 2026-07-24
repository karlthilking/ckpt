/* file_wrappers.h */
#ifndef FILE_WRAPPERS_H
#define FILE_WRAPPERS_H

#include "xnd/xnd.h"
#include "xnd/inject.h"
#include "xnd/fd_table.h"

#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

/**
 * mode_to_oflag:
 *  Convert mode string parameter for fopen, fdopen, etc.
 *  to integer flag that can be used with open/openat.
 */
static inline int mode_to_oflag(const char *mode)
{
        int flags;

        switch (mode[0]) {
        case 'r':
                flags = (strchr(mode, '+')) ? O_RDWR : O_RDONLY;
                break;
        case 'w':
                flags = O_CREAT | O_TRUNC;
                flags |= (strchr(mode, '+')) ? O_RDWR : O_WRONLY;
                break;
        case 'a':
                flags = O_CREAT | O_APPEND;
                flags |= (strchr(mode, '+')) ? O_RDWR : O_WRONLY;
                break;
        default:
                __builtin_trap();
        }

        if (strchr(mode, 'e'))
                flags |= O_CLOEXEC;
        else if (strchr(mode, 'x'))
                flags |= O_EXCL;

        return flags;
}

int     __openat_hook(int, const char *, int, ...);
int     __open_hook(const char *, int, ...);
int     __creat_hook(const char *, mode_t);
int     __close_hook(int);
int     __dup_hook(int);
int     __dup2_hook(int, int);
int     __fcntl_hook(int, int, ...);
FILE    *__fopen_hook(const char *, const char *);
int     __fclose_hook(FILE *);
FILE    *__freopen_hook(const char *, const char *, FILE *);

DIR     *__opendir_hook(const char *);
DIR     *__fdopendir_hook(int);
int     __closedir_hook(DIR *);
struct dirent *__readdir_hook(DIR *);
int     __readdir_r_hook(DIR *, struct dirent *, struct dirent **);
long    __telldir_hook(DIR *);
void    __seekdir_hook(DIR *, long);
void    __rewinddir_hook(DIR *);

#endif /* FILE_WRAPPERS_H */
