/* file_wrappers.h */
#ifndef FILE_WRAPPERS_H
#define FILE_WRAPPERS_H

#include "xnd/xnd.h"
#include "xnd/inject.h"

#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#define MAXFILES                512
#define FD_STATE_NONE           0x0
#define FD_STATE_SAVED          0x1
#define FD_STATE_RESTORED       0x2

#define FD_TYPE_INHERITED(__type)       \
        (((__type) == FD_STDIN) ||      \
         ((__type) == FD_STDOUT) ||     \
         ((__type) == FD_STDERR))

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

enum fd_type {
        FD_UNUSED,
        FD_STDIN,
        FD_STDOUT,
        FD_STDERR,
        FD_REGFILE
};

struct fd_backing {
        char    path[PATH_MAX];
        int     ref;
        int     flags;
        mode_t  mode;
        off_t   off;
        int     reopen;
        char    state;
};

struct fd_state {
        struct fd_backing       *src;
        enum fd_type            type;
};

void fd_backing_save(int);
void fd_backing_restore(int);

void fd_table_save_state(void);
void fd_table_restore_state(void);

void fd_table_init(void);
void fd_table_destroy(void);

void fd_table_open(int, const char *, int, mode_t);
void fd_table_dup(int, int);
void fd_table_close(int);

int     __openat_hook(int, const char *, int, ...);
int     __open_hook(const char *, int, ...);
int     __creat_hook(const char *, mode_t);
int     __close_hook(int);
int     __dup_hook(int);
int     __dup2_hook(int, int);
int     __fcntl_hook(int, int, ...);
FILE    *__fopen_hook(const char *, const char *);
int     __fclose_hook(FILE *);

#endif /* FILE_WRAPPERS_H */
