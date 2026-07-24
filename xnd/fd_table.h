/* fd_table.h */
#ifndef XND_FD_TABLE_H
#define XND_FD_TABLE_H

#include <dirent.h>
#include <limits.h>
#include <sys/types.h>

#define MAXFILES                512

#define FD_STATE_NULL           0x0
#define FD_STATE_SAVED          0x1
#define FD_STATE_RESTORED       0x2

#define FD_NULL         0x0
#define FD_INHERITED    0x1
#define FD_FILE         0x2
#define FD_DIRECTORY    0x3

struct fd_source {
        char type;
        char state;
        char path[PATH_MAX];
        int flags;
        mode_t mode;
        off_t offset;
        int ref;
        int root;
        DIR *dirp;
};

struct fd_table {
        struct fd_source *table[MAXFILES];
        pthread_mutex_t lock;
        bool init;
};

struct fd_ops {
        int (*save)(int);
        int (*restore)(int);
};

void fd_table_init(void);
void fd_table_destroy(void);

void fd_table_save(void);
void fd_table_restore(void);

void fd_table_open(int fd, const char *path, int flags, mode_t mode);
void fd_table_dup(int oldfd, int newfd);
void fd_table_close(int fd);

void fd_table_opendir(DIR *dirp, const char *path);

#endif /* XND_FD_TABLE_H */
