/* io.c */
#include "xnd/xnd.h"
#include "xnd/util/io.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>

ssize_t writeall(int fd, const void *buf, size_t nbyte)
{
	ssize_t ret;
	size_t bytes = 0;

	do {
		ret = write(fd, buf + bytes, nbyte - bytes);
	} while ((ret != -1) && (bytes += ret) < nbyte);

	return bytes;
}

ssize_t readall(int fd, void *buf, size_t nbyte)
{
	ssize_t ret;
	size_t bytes = 0;

	do {
		ret = read(fd, buf + bytes, nbyte - bytes);
	} while ((ret > 0) && ((bytes += ret) < nbyte));

	return bytes;
}
