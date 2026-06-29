/* fd.c */
#include "xnd/xnd.h"
#include "fd.h"
#include <fcntl.h>
#include <errno.h>

bool xnd_fd_available(int fd)
{
        int err;

        err = fcntl(fd, F_GETFL, 0);
        if (err == -1 && errno == EBADF) {
                return true;
        }

        return false;
}

int xnd_fd_change(int old, int desired)
{
        if (old != desired) {
                xnd_assert(dup2(old, desired) == desired);
                close(old);
        }

        return desired;
}
