/* io.c */
#include "xnd/xnd.h"
#include "xnd/util/io.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>

ssize_t writeall(int fd, const void *buf, size_t nbyte)
{
        size_t  bytes;
        ssize_t retval;

        for (bytes = 0; bytes < nbyte; bytes += retval) {
                retval = write(fd, buf + bytes, nbyte - bytes);
                if (unlikely(retval < 0)) {
                        xnd_error("write(%d, %p, %zu): %s\n",
                                  fd, buf + bytes, nbyte - bytes,
                                  strerror(errno));
                        return -1;
                } else if (retval == 0) {
                        return 0;
                }
        }

        return bytes;
}

ssize_t readall(int fd, void *buf, size_t nbyte)
{
        size_t  bytes;
        ssize_t retval;

        for (bytes = 0; bytes < nbyte; bytes += retval) {
                retval = read(fd, buf + bytes, nbyte - bytes);
                if (retval < 0) {
                        xnd_error("read(%d, %p, %zu): %s\n",
                                  fd, buf + bytes, nbyte - bytes,
                                  strerror(errno));
                        return -1;
                } else if (retval == 0) {
                        return 0;
                }
        }

        return bytes;
}
