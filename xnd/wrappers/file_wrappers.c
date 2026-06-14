/* file_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "file_wrappers.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static __always_inline bool skip_fd_tracking(void)
{
        if (unlikely(get_xnd_state() == XND_UNINITIALIZED))
                return true;

        return false;
}

static struct fd_state fd_table[MAXFILES];

void fd_backing_save(int fd)
{
        int                     retval;
        struct fd_state         *state;
        struct fd_backing       *backing;
        
        state = fd_table + fd;
        backing = state->src;

        if (FD_TYPE_INHERITED(state->type)) {
                backing->state = FD_STATE_SAVED;
                return;
        }
        
        /* Save current offset into open file */
        if ((backing->off = lseek(fd, 0, SEEK_CUR)) < 0) {
                perror("lseek");
                backing->off = 0;
                return;
        }

        if ((retval = fcntl(fd, F_GETFL)) < 0) {
                perror("fcntl(..., F_GETFL)");
                return;
        } else {
                backing->flags |= (retval & (O_APPEND | O_NONBLOCK));
        }

        if ((retval = fcntl(fd, F_GETFD)) < 0) {
                perror("fcntl(..., F_GETFD)");
                return;
        } else if (retval & FD_CLOEXEC) {
                backing->flags |= O_CLOEXEC;
        } else if (backing->flags & O_CLOEXEC) {
                backing->flags &= ~O_CLOEXEC;
        }

        backing->state = FD_STATE_SAVED;
}

void fd_backing_restore(int fd)
{
        int                     retval;
        struct fd_state         *state;
        struct fd_backing       *backing;

        state = fd_table + fd;
        backing = state->src;

        if (FD_TYPE_INHERITED(state->type)) {
                if (fd <= 2) {
                        /**
                         * If actual stdin, stdout, or stderr, and not
                         * dups there is nothing to do.
                         */
                        return;
                }

                switch (state->type) {
                case FD_STDIN:
                        retval = dup2(STDIN_FILENO, fd);
                        backing->reopen = STDIN_FILENO;
                        break;
                case FD_STDOUT:
                        retval = dup2(STDOUT_FILENO, fd);
                        backing->reopen = STDOUT_FILENO;
                        break;
                case FD_STDERR:
                        retval = dup2(STDERR_FILENO, fd);
                        backing->reopen = STDERR_FILENO;
                        break;
                default:
                        __builtin_trap();
                }

                if (retval < 0) {
                        perror("dup2");
                } else {
                        xnd_assert(retval == fd);
                        backing->state = FD_STATE_RESTORED;
                }

                return;
        }
        
        /**
         * Reopen backing file with original flags and mode
         */
        retval = open(backing->path, backing->flags, backing->mode);
        if (retval < 0) {
                perror("open");
                return;
        } else if (retval != fd) {
                /* Rearrange file descriptors as they were before */
                if (dup2(retval, fd) != fd) {
                        perror("dup2");
                        return;
                }
                close(retval);
        }
        
        /* Reposition file offset */
        if (lseek(fd, backing->off, SEEK_SET) < 0) {
                perror("lseek");
                return;
        }
        
        /**
         * Now other file descriptors referring to the same underlying
         * open that need be restored can call dup2(bacing->reopen, myfd)
         */
        backing->reopen = fd;
        backing->state  = FD_STATE_RESTORED;
}

void fd_table_init(void)
{
        bzero(&fd_table, sizeof(fd_table));
        
        /**
         * Set up inherited file descriptors with default state
         * information.
         */
        fd_table[0].type = FD_STDIN;
        fd_table[1].type = FD_STDOUT;
        fd_table[2].type = FD_STDERR;

        for (int fd = 0; fd <= 2; fd++) {
                fd_table[fd].src = malloc(sizeof(struct fd_backing));
                xnd_assert(fd_table[fd].src != NULL);
                fd_table[fd].src->ref = 1;
        }
}

void fd_table_destroy(void)
{
        int fd;

        for (fd = 0; fd < MAXFILES; fd++) {
                if (fd_table[fd].type == FD_UNUSED) {
                        xnd_assert(fd_table[fd].src == NULL);
                        continue;
                }
                fd_table[fd].src->ref--;
                if (fd_table[fd].src->ref == 0) {
                        free(fd_table[fd].src);
                        fd_table[fd].src = NULL;
                }
        }
}

void fd_table_save_state(void)
{
        int fd;

        for (fd = 0; fd < MAXFILES; fd++) {
                if (fd_table[fd].type == FD_UNUSED ||
                    fd_table[fd].src->state == FD_STATE_SAVED) {
                        continue;
                }
                fd_backing_save(fd);
        }
}

void fd_table_restore_state(void)
{
        int fd;

        for (fd = 0; fd < MAXFILES; fd++) {
                if (fd_table[fd].type == FD_UNUSED) {
                        continue;
                } else if (fd_table[fd].src->state == FD_STATE_RESTORED) {
                        if (dup2(fd_table[fd].src->reopen, fd) != fd)
                                perror("dup2");
                        continue;
                }
                fd_backing_restore(fd);
        }
}

void fd_table_open(int fd, const char *path, int flags, mode_t mode)
{
        if (skip_fd_tracking())
                return;

        fd_table[fd].src = malloc(sizeof(struct fd_backing));
        fd_table[fd].type = FD_REGFILE;
        fd_table[fd].src->flags = flags;
        fd_table[fd].src->mode = mode;
        fd_table[fd].src->ref = 1;
        fd_table[fd].src->state = FD_STATE_NONE;

        strncpy(fd_table[fd].src->path, path, strlen(path) + 1);
}

void fd_table_dup(int oldfd, int newfd)
{
        if (skip_fd_tracking() || oldfd == newfd)
                return;

        xnd_assert(fd_table[oldfd].src != NULL &&
                   fd_table[oldfd].type != FD_UNUSED);
        
        /* newfd now points to the same underlying open file */
        fd_table[newfd].src     = fd_table[oldfd].src;
        fd_table[newfd].type    = fd_table[oldfd].type;
        
        /* Increment reference count */
        fd_table[newfd].src->ref++;
}

void fd_table_close(int fd)
{
        if (skip_fd_tracking()) {
                return;
        } else if (fd_table[fd].type == FD_UNUSED) {
                xnd_assert(fd_table[fd].src == NULL);
                return;
        }
        
        fd_table[fd].type = FD_UNUSED;
        fd_table[fd].src->ref--;
        if (fd_table[fd].src->ref == 0) {
                /* Free fd backing info if last reference */
                free(fd_table[fd].src);
        }
        fd_table[fd].src = NULL;
}

int __openat_hook(int dirfd, const char *path, int flags, ...)
{
        int     retval;
        va_list va;
        mode_t  mode = 0;

        if (flags & O_CREAT) {
                va_start(va, flags);
                mode = va_arg(va, int);
                va_end(va);
        }

        if ((retval = openat(dirfd, path, flags, mode)) != -1) {
                fd_table_open(retval, path, flags, mode);
        }

        return retval;
}

int __open_hook(const char *path, int flags, ...)
{
        va_list va;
        mode_t  mode = 0;

        if (flags & O_CREAT) {
                va_start(va, flags);
                mode = va_arg(va, int);
                va_end(va);
        }
        
        /**
         * open(path, flags, mode) is equivalent to
         * openat(AT_FDCWD, path, flags, mode)
         */
        return __openat_hook(AT_FDCWD, path, flags, mode);
}

int __creat_hook(const char *path, mode_t mode)
{
        /**
         * creat(path, mode) is equivalent to
         * open(path, O_CREAT | O_TRUNC, O_WRONLY, mode) =
         * openat(AT_FDCWD, path, O_CREAT | O_TRUNC | O_WRONLY, mode)
         */
        return __openat_hook(AT_FDCWD, path, O_CREAT |
                             O_TRUNC | O_WRONLY, mode);
}

int __close_hook(int fd)
{
        int retval;

        if ((retval = close(fd)) != -1) {
                fd_table_close(fd);
        }

        return retval;
}

int __dup_hook(int oldfd)
{
        int newfd;

        if ((newfd = dup(oldfd)) != -1) {
                fd_table_dup(oldfd, newfd);
        }

        return newfd;
}

int __dup2_hook(int oldfd, int newfd)
{
        int retval;

        if ((retval = dup2(oldfd, newfd)) != -1) {
                xnd_assert(retval == newfd);
                fd_table_dup(oldfd, newfd);
        }

        return retval;
}

int __fcntl_hook(int fd, int cmd, ...)
{
        int     retval;
        va_list va;
        void    *arg = NULL;
        
        va_start(va, cmd);
        arg = va_arg(va, void *);
        va_end(va);
        
        if ((retval = fcntl(fd, cmd, arg)) != -1 &&
            (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC)) {
                fd_table_dup(fd, retval);
        }

        return retval;
}

FILE *__fopen_hook(const char *path, const char *mode)
{
        FILE    *stream;
        int     flags;

        if ((stream = fopen(path, mode)) != NULL) {
                flags = mode_to_oflag(mode);
                fd_table_open(fileno(stream), path, flags, 0666);
        }

        return stream;
}

int __fclose_hook(FILE *stream)
{
        int retval, fd;
        
        fd = fileno(stream);
        if ((retval = fclose(stream)) != EOF) {
                fd_table_close(fd);
        }

        return retval;
}
