/* fd_table.c */
#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/fd_table.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct fd_table fd_table = {
	.table = {0},
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.init = false
};

/**
 * file_save:
 *  Save open file current offset and flags to recreate open file
 *  state during restore.
 */
static int file_save(int fd)
{
	int ret;
	struct fd_source *src = fd_table.table[fd];

	if ((src->offset = lseek(fd, 0, SEEK_CUR)) < 0) {
		xnd_perror("lseek");
		return -1;
	}

	if ((ret = fcntl(fd, F_GETFL)) < 0) {
		xnd_perror("fcntl");
		return -1;
	} else {
		src->flags |= (ret & (O_APPEND | O_NONBLOCK));
	}

	if ((ret = fcntl(fd, F_GETFD)) < 0) {
		xnd_perror("fcntl");
		return -1;
	}

	if ((src->flags & O_CLOEXEC) && !(ret & FD_CLOEXEC))
		src->flags &= ~O_CLOEXEC;
	else if ((ret & FD_CLOEXEC) && !(src->flags & O_CLOEXEC))
		src->flags |= O_CLOEXEC;

	src->flags &= ~O_TRUNC;
	src->flags &= ~O_EXCL;
	src->state = FD_STATE_SAVED;
	return 0;
}

/**
 * file_restore:
 *  Reopen file with saved (path, flags, mode) and restore file offset
 *  with lseek.
 */
static int file_restore(int fd)
{
	int ret;
	struct fd_source *src = fd_table.table[fd];

	ret = open(src->path, src->flags, src->mode);
	if (ret < 0) {
		xnd_perror("open");
		return -1;
	} else if (ret != fd) {
		if (dup2(ret, fd) != fd) {
			xnd_perror("dup2");
			return -1;
		}
		close(ret);
	}

	if (lseek(fd, src->offset, SEEK_SET) != src->offset) {
		xnd_perror("lseek");
		return -1;
	}

	src->root = fd;
	src->state = FD_STATE_RESTORED;
	return 0;
}

/**
 * dir_save:
 *  Save directory state (offset and flags) obtained with telldir
 *  and fcntl.
 */
static int dir_save(int fd)
{
	int ret;
	long tell;
	struct fd_source *src = fd_table.table[fd];

	if ((tell = telldir(src->dirp)) < 0) {
		xnd_perror("telldir");
		return -1;
	} else {
		src->offset = (off_t)tell;
	}

	if ((ret = fcntl(fd, F_GETFD)) < 0) {
		xnd_perror("fcntl");
		return -1;
	}

	if ((src->flags & O_CLOEXEC) && !(ret & FD_CLOEXEC))
		src->flags &= ~O_CLOEXEC;
	else if ((ret & FD_CLOEXEC) && !(src->flags & O_CLOEXEC))
		src->flags |= O_CLOEXEC;

	src->state = FD_STATE_SAVED;
	return 0;
}

static int dir_restore(int fd)
{
	int ret;
	struct fd_source *src = fd_table.table[fd];

	ret = open(src->path, src->flags, src->mode);
	if (ret < 0) {
		xnd_perror("open");
		return -1;
	} else if (ret != fd) {
		if (dup2(ret, fd) != fd) {
			xnd_perror("dup2");
			return -1;
		}
		close(ret);
	}

	seekdir(src->dirp, (long)src->offset);
	src->root = fd;
	src->state = FD_STATE_RESTORED;
	return 0;
}

static const struct fd_ops fd_ops[] = {
	[FD_FILE] = { file_save, file_restore },
	[FD_DIRECTORY] = { dir_save, dir_restore },
};

void fd_table_init(void)
{
	int fd;
	struct fd_source **table = fd_table.table;

	for (fd = 0; fd < 3; fd++) {
		table[fd] = malloc(sizeof(struct fd_source));
		if (table[fd] == NULL) {
			xnd_perror("malloc");
			xnd_abort();
		}
		table[fd]->type = FD_INHERITED;
		table[fd]->ref = 1;
		table[fd]->state = FD_STATE_NULL;
		table[fd]->root = fd;
	}

	fd_table.init = true;
}

void fd_table_destroy(void)
{
	int fd;
	struct fd_source **table = fd_table.table;

	pthread_mutex_lock(&fd_table.lock);
	for (fd = 0; fd < MAXFILES; fd++) {
		if (table[fd] == NULL)
			continue;
		table[fd]->ref--;
		if (table[fd]->ref == 0) {
			free(table[fd]);
			table[fd] = NULL;
		}
	}
	pthread_mutex_unlock(&fd_table.lock);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wchar-subscripts"
void fd_table_save(void)
{
	int fd;
	struct fd_source **table = fd_table.table;

	for (fd = 0; fd < MAXFILES; fd++) {
		if (table[fd] == NULL || table[fd]->type == FD_INHERITED)
			continue;
		if (table[fd]->state != FD_STATE_SAVED)
			fd_ops[table[fd]->type].save(fd);
	}
}

void fd_table_restore(void)
{
	int fd;
	struct fd_source **table = fd_table.table;

	for (fd = 0; fd < MAXFILES; fd++) {
		if (table[fd] == NULL || table[fd]->type != FD_INHERITED)
			continue;
		if (fd != table[fd]->root) {
			if (dup2(table[fd]->root, fd) != fd) {
				xnd_perror("dup2");
				goto bad;
			}
		}
	}

	for (fd = 0; fd < MAXFILES; fd++) {
		if (table[fd] == NULL || table[fd]->type == FD_INHERITED)
			continue;
		if (table[fd]->state == FD_STATE_RESTORED) {
			if (dup2(table[fd]->root, fd) != fd) {
				xnd_perror("dup2");
				goto bad;
			}
		} else {
			if (fd_ops[table[fd]->type].restore(fd) < 0) {
				xnd_error("Error restoring fd %d\n", fd);
				goto bad;
			}
		}
	}

	/**
	 * Now close inherited file descriptors if they were closed
	 * originally
	 */
	for (fd = 0; fd < 3; fd++) {
		if (table[fd] == NULL)
			close(fd);
	}
	return;
bad:
	xnd_error("%s failed, aborting...\n", __func__);
	xnd_abort();
}
#pragma clang diagnostic pop

void fd_table_open(int fd, const char *path, int flags, mode_t mode)
{
	struct fd_source **table = fd_table.table;

	if (!fd_table.init) {
		xnd_trace("Ignoring opened fd: %d\n", fd);
		return;
	}

	pthread_mutex_lock(&fd_table.lock);
	table[fd] = malloc(sizeof(struct fd_source));
	if (table[fd] == NULL) {
		xnd_perror("malloc");
		xnd_abort();
	}

	table[fd]->type = FD_FILE;
	table[fd]->ref = 1;
	table[fd]->mode = mode;
	table[fd]->flags = flags;
	table[fd]->state = FD_STATE_NULL;
	strncpy(table[fd]->path, path, strlen(path) + 1);
	pthread_mutex_unlock(&fd_table.lock);
}

void fd_table_opendir(DIR *dirp, const char *path)
{
	int fd;
	struct fd_source **table = fd_table.table;

	if (!fd_table.init) {
		xnd_trace("Ignored opened dir\n");
		return;
	}

	fd = dirfd(dirp);
	pthread_mutex_lock(&fd_table.lock);
	if (table[fd] == NULL) {
		table[fd] = malloc(sizeof(struct fd_source));
		if (table[fd] == NULL) {
			xnd_perror("malloc");
			xnd_abort();
		}
		table[fd]->ref = 1;
		table[fd]->mode = 0;
		table[fd]->state = FD_STATE_NULL;
		table[fd]->flags = O_RDONLY | O_DIRECTORY;
		strncpy(table[fd]->path, path, strlen(path) + 1);
	}

	table[fd]->type = FD_DIRECTORY;
	table[fd]->dirp = dirp;
	pthread_mutex_unlock(&fd_table.lock);
}

void fd_table_dup(int oldfd, int newfd)
{
	struct fd_source **table = fd_table.table;

	if (!fd_table.init) {
		xnd_trace("Ignoring dup fd (%d, %d)\n", oldfd, newfd);
		return;
	}

	pthread_mutex_lock(&fd_table.lock);
	if (table[oldfd] == NULL || table[oldfd]->type == FD_NULL) {
		xnd_warn("dup'd fd untracked: %d\n", oldfd);
		pthread_mutex_unlock(&fd_table.lock);
		return;
	}

	table[newfd] = table[oldfd];
	table[newfd]->ref++;
	pthread_mutex_unlock(&fd_table.lock);
}

void fd_table_close(int fd)
{
	struct fd_source **table = fd_table.table;

	if (!fd_table.init) {
		xnd_trace("Ignoring closed fd: %d\n", fd);
		return;
	}

	pthread_mutex_lock(&fd_table.lock);
	if (table[fd] == NULL || table[fd]->type == FD_NULL) {
		xnd_warn("Closed fd untracked: %d\n", fd);
		pthread_mutex_unlock(&fd_table.lock);
		return;
	}

	table[fd]->ref--;
	if (table[fd]->ref == 0)
		free(table[fd]);

	table[fd] = NULL;
	pthread_mutex_unlock(&fd_table.lock);
}
