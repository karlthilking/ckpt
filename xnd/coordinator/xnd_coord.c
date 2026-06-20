/* xnd_coord.c */
#include "xnd/xnd.h"
#include "xnd/coordinator/coord_common.h"
#include "xnd/coordinator/coord_server.h"

static struct coord_info *coord_info = NULL;
static struct proc_list proc_list;

static ssize_t readall(int fd, void *buf, size_t nbyte)
{
        size_t  bytes;
        ssize_t retval;

        for (bytes = 0; bytes < nbyte; bytes += retval) {
                retval = read(fd, buf + bytes, nbyte - bytes);
                if (retval < 0) {
                        xnd_error("read: %s\n", strerror(errno));
                        return -1;
                } else if (retval == 0) {
                        return 0;
                }
        }
        
        return (ssize_t)bytes;
}

static ssize_t writeall(int fd, const void *buf, size_t nbyte)
{
        size_t  bytes;
        ssize_t retval;

        for (bytes = 0; bytes < nbyte; bytes += retval) {
                retval = write(fd, buf + bytes, nbyte - bytes);
                if (retval < 0) {
                        xnd_error("write: %s\n", strerror(errno));
                        return (ssize_t)-1;
                } else if (retval == 0) {
                        return 0;
                }
        }
        
        return (ssize_t)bytes;
}

void proc_list_init(void)
{
        proc_list.list = malloc(sizeof(struct proc_info));
        proc_list.size = 0;
        proc_list.capacity = 1;
}

void proc_list_destroy(void)
{
        free(proc_list.list);
}

void proc_list_resize(size_t new_cap)
{
        struct proc_info *new_list, *old_list;

        old_list = proc_list.list;
        new_list = malloc(sizeof(struct proc_info) * new_cap);

        for (size_t i = 0; i < proc_list.size; i++) {
                memcpy(&new_list[i], &old_list[i], sizeof(struct proc_info));
        }
        
        free(old_list);
        proc_list.list = new_list;
        proc_list.capacity = new_cap;
}

struct proc_info *proc_list_find(pid_t real_pid)
{
        struct proc_info *p, *list;
        
        p = NULL;
        list = proc_list.list;
        
        for (size_t i = 0; i < proc_list.size; i++) {
                if (list[i].real_pid == real_pid) {
                        p = &list[i];
                        break;
                }
        }
        
        return p;
}

void proc_init(int fd)
{
        struct proc_info        *p, *parent;
        enum xnd_msghdr         hdr;
        struct pid_info         info;
        ssize_t                 bytes;

        if (proc_list.size == proc_list.capacity) {
                proc_list_resize(proc_list.capacity * 2);
        }
        
        p = proc_list.list + proc_list.size;
        p->fd = fd;
        p->root = proc_list.size == 0 ? true : false;
        p->state = XND_PROC_EMBRYO;

        /**
         * Receive process's real pid and ppid, then send virtual pid
         * and ppid to process.
         */
        coord_recv_pid_info(p);
        if (p->root) {
                p->virt_ppid = coord_next_virtual_pid();
        } else {
                parent = proc_list_find(p->real_ppid);
                xnd_assert(parent != NULL);
                p->virt_ppid = parent->virt_pid;
        }

        p->virt_pid = coord_next_virtual_pid();
        coord_send_pid_info(p);
        proc_list.size++;
}

void proc_exited(struct proc_info *p)
{
        close(p->fd);
        p->state = XND_PROC_EXITED;
}

void coord_init(void)
{
        int                     fd, err;
        struct sockaddr_un      *addr;

        coord_info = calloc(1, sizeof(struct coord_info));
        coord_info->next_virtual_pid = 1;

        coord_info->fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (coord_info->fd < 0) {
                xnd_error("Failed to create coordinator socket!\n");
                coord_exit();
        }

        addr = &coord_info->addr;
        bzero(addr, sizeof(*addr));
        addr->sun_family = AF_UNIX;
        strncpy(addr->sun_path, XND_COORD_PATH, sizeof(addr->sun_path) - 1);

        err = bind(coord_info->fd, (struct sockaddr *)addr, sizeof(*addr));
        if (err < 0) {
                xnd_error("Failed to bind coordinator socket!\n");
                coord_exit();
        }

        err = listen(coord_info->fd, MAX_PROC);
        if (err < 0) {
                xnd_error("Failed to listen on coordinator socket!\n");
                coord_exit();
        }

        proc_list_init();
}

void coord_exit(void)
{
        proc_list_destroy();
        
        if (coord_info) {
                close(coord_info->fd);
                unlink(coord_info->addr.sun_path);
        }
}

void coord_event_loop(void)
{
        /* Wait for the root process to connect with the coordinator */
        coord_await_connection();

        for (;;) {
                coord_await_msg();
        }
}

void coord_await_connection(void)
{
        int fd;

        fd = accept(coord_info->fd, NULL, NULL);
        if (fd != -1) {
                proc_init(fd);
        }
}

void coord_await_msg(void)
{
        int                     err, nfds;
        fd_set                  set;
        struct proc_info        *p;
        struct timeval          tv = { 0, 1000 };

        nfds = 0;
        FD_ZERO(&set);
        
        for (size_t i = 0; i < proc_list.size; i++) {
                p = proc_list.list + i;
                if (p->state == XND_PROC_EXITED) {
                        continue;
                }
                FD_SET(p->fd, &set);
                if (p->fd + 1 > nfds) {
                        nfds = p->fd + 1;
                }
        }

        err = select(nfds, &set, NULL, NULL, &tv);
        if (err <= 0) {
                return;
        }

        for (size_t i = 0; i < proc_list.size; i++) {
                p = proc_list.list + i;
                if (p->state == XND_PROC_EXITED) {
                        continue;
                }
                if (FD_ISSET(p->fd, &set)) {
                        coord_recv_msg(p);
                }
        }
}

void coord_recv_msg(struct proc_info *p)
{
        enum xnd_msghdr hdr;
        ssize_t         bytes;

        bytes = readall(p->fd, &hdr, sizeof(hdr));
        xnd_assert(bytes == sizeof(hdr));

        switch (hdr) {
        case XND_PROC_EXIT:
                proc_exited(p);
                break;
        default:
                break;
        }
}

/**
 * coord_recv_pid_info:
 *  Get the real pid and ppid of the process that just connected with
 *  the coordinator.
 */
void coord_recv_pid_info(struct proc_info *p)
{
        enum xnd_msghdr         hdr;
        struct pid_info         pid_info;
        ssize_t                 bytes;

        bytes = readall(p->fd, &hdr, sizeof(hdr));
        xnd_assert(bytes == sizeof(hdr) && hdr == XND_REAL_PID_INFO);

        bytes = readall(p->fd, &pid_info, sizeof(struct pid_info));
        xnd_assert(bytes == sizeof(struct pid_info));

        p->real_pid = pid_info.pid;
        p->real_ppid = pid_info.ppid;
}

/**
 * coord_send_pid_info:
 *  Send the elected virtual pid and ppid for the process that connected
 *  with the coordinator.
 */
void coord_send_pid_info(struct proc_info *p)
{
        enum xnd_msghdr         hdr;
        struct pid_info         pid_info;
        ssize_t                 bytes;

        hdr = XND_VIRT_PID_INFO;
        pid_info.pid = p->virt_pid;
        pid_info.ppid = p->virt_ppid;

        bytes = writeall(p->fd, &hdr, sizeof(hdr));
        xnd_assert(bytes == sizeof(hdr));

        bytes = writeall(p->fd, &pid_info, sizeof(struct pid_info));
        xnd_assert(bytes == sizeof(struct pid_info));
}

pid_t coord_next_virtual_pid(void)
{
        return coord_info->next_virtual_pid++;
}

int main(int argc, char *argv[])
{
        coord_init();
}
