/* file_wrappers.c */
#include "xnd/xnd.h"
#include "xnd/tls.h"
#include "xnd/xnd_lib.h"
#include "xnd/thread_info.h"
#include "file_wrappers.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static __always_inline bool skip_interpose(void)
{
        if (unlikely(get_xnd_state() == XND_UNINITIALIZED))
                return true;

        if (unlikely(tlv_ok() == false))
                return true;

        return false;
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

        if (skip_interpose()) {
                return openat(dirfd, path, flags, mode);
        }

        unsafe_enter();
        if ((retval = openat(dirfd, path, flags, mode)) != -1) {
                fd_table_open(retval, path, flags, mode);
        }
        unsafe_exit();

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

        if (skip_interpose()) {
                return close(fd);
        }

        unsafe_enter();
        if ((retval = close(fd)) != -1) {
                fd_table_close(fd);
        }
        unsafe_exit();

        return retval;
}

int __dup_hook(int oldfd)
{
        int newfd;

        if (skip_interpose()) {
                return dup(oldfd);
        }

        unsafe_enter();
        if ((newfd = dup(oldfd)) != -1) {
                fd_table_dup(oldfd, newfd);
        }
        unsafe_exit();

        return newfd;
}

int __dup2_hook(int oldfd, int newfd)
{
        int retval;

        if (skip_interpose()) {
                return dup2(oldfd, newfd);
        }

        unsafe_enter();
        if ((retval = dup2(oldfd, newfd)) != -1) {
                xnd_assert(retval == newfd);
                fd_table_dup(oldfd, newfd);
        }
        unsafe_exit();

        return retval;
}

int __fcntl_hook(int fd, int cmd, ...)
{
	int ret;
	va_list va;
	void *arg = NULL;

	va_start(va, cmd);
	arg = va_arg(va, void *);
	va_end(va);

	if (skip_interpose()) {
		return fcntl(fd, cmd, arg);
	}

	unsafe_enter();
	ret = fcntl(fd, cmd, arg);
	if (ret != -1 && (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC))
		fd_table_dup(fd, ret);
	unsafe_exit();

	return ret;
}

FILE *__fopen_hook(const char *path, const char *mode)
{
        FILE    *stream;
        int     flags;

        if (skip_interpose()) {
                return fopen(path, mode);
        }

        unsafe_enter();
        if ((stream = fopen(path, mode)) != NULL) {
                flags = mode_to_oflag(mode);
                fd_table_open(fileno(stream), path, flags, 0666);
        }
        unsafe_exit();

        return stream;
}

int __fclose_hook(FILE *stream)
{
	int ret, fd;

	if (skip_interpose()) {
		return fclose(stream);
	}

	unsafe_enter();
	fd = fileno(stream);
	if ((ret = fclose(stream)) != EOF)
		fd_table_close(fd);
	unsafe_exit();

        return ret;
}

FILE *__freopen_hook(const char *path, const char *mode, FILE *oldfp)
{
	FILE *newfp;
	char buf[PATH_MAX];
	int oldfd, newfd, oflag;

	if (skip_interpose()) {
		return freopen(path, mode, oldfp);
	}

	unsafe_enter();
	oldfd = fileno(oldfp);
	/**
	 * freopen(NULL, mode, fp) changes mode of the stream
	 * of the same underlying file, obtain the path with
	 * fcntl(fd, F_GETPATH, buf)
	 */
	if (path == NULL)
		path = (fcntl(oldfd, F_GETPATH, buf) == 0 ? buf : "");

	if ((newfp = freopen(path, mode, oldfp))) {
		newfd = fileno(newfp);
		oflag = mode_to_oflag(mode);
		/**
		 * If oldfd is not repruposed for new stream, close
		 * oldfd's entry in fd table
		 */
		if (newfd != oldfd)
			fd_table_close(oldfd);
		/**
		 * Close old entry at newfd and open with freopen
		 * arguments
		 */
		fd_table_close(newfd);
		fd_table_open(newfd, path, oflag, 0666);
	}
	unsafe_exit();

	return newfp;
}

DIR *__opendir_hook(const char *path)
{
        DIR *dirp;

	if (skip_interpose()) {
		return opendir(path);
	}

	unsafe_enter();
	dirp = opendir(path);
	if (dirp)
		fd_table_opendir(dirp, path);
	unsafe_exit();

        return dirp;
}

DIR *__fdopendir_hook(int fd)
{
        DIR *dirp;
	const char *path;
	char buf[PATH_MAX];

	if (skip_interpose()) {
		return fdopendir(fd);
	}

	path = (fcntl(fd, F_GETPATH, buf) == 0 ? buf : "");

	unsafe_enter();
	dirp = fdopendir(fd);
	if (dirp)
		fd_table_opendir(dirp, path);
	unsafe_exit();

        return dirp;
}

int __closedir_hook(DIR *dirp)
{
	int ret, fd;

	if (skip_interpose()) {
		return closedir(dirp);
	}

	fd = dirfd(dirp);

	unsafe_enter();
	ret = closedir(dirp);
	if (ret == 0)
		fd_table_close(fd);
	unsafe_exit();

	return ret;
}

struct dirent *__readdir_hook(DIR *dirp)
{
	struct dirent *ent;

	if (skip_interpose()) {
		return readdir(dirp);
	}

	unsafe_enter();
	ent = readdir(dirp);
	unsafe_exit();

	return ent;
}

int __readdir_r_hook(DIR *dirp, struct dirent *entry, struct dirent **result)
{
	int ret;

	if (skip_interpose()) {
		return readdir_r(dirp, entry, result);
	}

	unsafe_enter();
	ret = readdir_r(dirp, entry, result);
	unsafe_exit();

	return ret;
}

long __telldir_hook(DIR *dirp)
{
	long loc;

	if (skip_interpose()) {
		return telldir(dirp);
	}

	unsafe_enter();
	loc = telldir(dirp);
	unsafe_exit();

	return loc;
}

void __seekdir_hook(DIR *dirp, long loc)
{
	if (skip_interpose()) {
		seekdir(dirp, loc);
		return;
	}

	unsafe_enter();
	seekdir(dirp, loc);
	unsafe_exit();
}

void __rewinddir_hook(DIR *dirp)
{
	if (skip_interpose()) {
		rewinddir(dirp);
		return;
	}

	unsafe_enter();
	rewinddir(dirp);
	unsafe_exit();
}

INTERPOSE(__openat_hook, openat);
INTERPOSE(__open_hook, open);
INTERPOSE(__creat_hook, creat);
INTERPOSE(__close_hook, close);
INTERPOSE(__dup_hook, dup);
INTERPOSE(__dup2_hook, dup2);
INTERPOSE(__fcntl_hook, fcntl);
INTERPOSE(__fopen_hook, fopen);
INTERPOSE(__fclose_hook, fclose);
INTERPOSE(__freopen_hook, freopen);
INTERPOSE(__opendir_hook, opendir);
INTERPOSE(__fdopendir_hook, fdopendir);
INTERPOSE(__closedir_hook, closedir);
INTERPOSE(__readdir_hook, readdir);
INTERPOSE(__readdir_r_hook, readdir_r);
INTERPOSE(__telldir_hook, telldir);
INTERPOSE(__seekdir_hook, seekdir);
INTERPOSE(__rewinddir_hook, rewinddir);
