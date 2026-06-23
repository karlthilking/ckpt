/* xnd_coord.c */
#include "xnd/xnd.h"
#include "xnd/util/io.h"
#include "xnd/coordinator/xnd_coord.h"
#include "xnd/coordinator/xnd_coord_api.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/select.h>

static struct proc_list *proc_list      = NULL;
static pid_t            next_virt_pid   = 1;
static int              listen_fd       = -1;
static u32              num_peers       = 0;
static enum coord_state coord_state     = COORD_NULL;

void proc_list_init(void)
{
        proc_list = calloc(1, sizeof(struct proc_list));
        xnd_assert(proc_list != NULL);
        xnd_assert(proc_list->head == NULL && proc_list->size == 0);
}

void proc_list_destroy(void)
{
        struct proc *p, *next;

        for (p = proc_list->head; p; p = next) {
                next = p->next;
                close(p->fd);
                free(p);
        }

        free(proc_list);
}

void proc_list_add(struct proc *p)
{
        p->next = proc_list->head;
        proc_list->head = p;

        if (p->next) {
                p->next->prev = p;
        }

        proc_list->size++;
}

void proc_list_remove(struct proc *p)
{
        if (p->next) {
                p->next->prev = p->prev;
        }

        if (p->prev) {
                p->prev->next = p->next;
        }

        if (proc_list->head == p) {
                proc_list->head = p->next;
        }

        close(p->fd);
        free(p);
        proc_list->size--;
}

struct proc *proc_list_find_real(pid_t real_pid)
{
        struct proc *p;

        for (p = proc_list->head; p; p = p->next) {
                if (p->real_pid == real_pid) {
                        return p;
                }
        }

        return NULL;
}

struct proc *proc_list_find_virt(pid_t virt_pid)
{
        struct proc *p;

        for (p = proc_list->head; p; p = p->next) {
                if (p->virt_pid == virt_pid) {
                        return p;
                }
        }

        return NULL;
}

void coord_init(void)
{
        int                     err;
        struct sockaddr_un      addr;

        coord_setup_handler(SIGINT);
        coord_setup_handler(SIGTERM);
        coord_setup_handler(SIGQUIT);

        listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd < 0) {
                xnd_error("socket: %s\n", strerror(errno));
                coord_exit(COORD_EXIT_FAILURE);
        }

        bzero(&addr, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, XND_COORD_PATH, sizeof(addr.sun_path) - 1);
        
        if (access(XND_COORD_PATH, F_OK) == 0) {
                err = unlink(XND_COORD_PATH);
                if (err != 0) {
                        xnd_error("unlink: %s\n", strerror(errno));
                        coord_exit(COORD_EXIT_FAILURE);
                }
        }

        err = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
        if (err != 0) {
                xnd_error("bind: %s\n", strerror(errno));
                coord_exit(COORD_EXIT_FAILURE);
        }

        err = listen(listen_fd, COORD_MAX_PROC);
        if (err != 0) {
                xnd_error("listen: %s\n", strerror(errno));
                coord_exit(COORD_EXIT_FAILURE);
        }

        coord_state = COORD_RUNNING;
}

void coord_cleanup(void)
{
        int err;

        proc_list_destroy();
        if (listen_fd != -1) {
                err = close(listen_fd);
                if (err != 0) {
                        xnd_warn("close: %s\n", strerror(errno));
                }
        }

        if (access(XND_COORD_PATH, F_OK) == 0) {
                err = unlink(XND_COORD_PATH);
                if (err != 0) {
                        xnd_error("unlink: %s\n", strerror(errno));
                }
        }
}

void coord_exit(int status)
{
        if (status == COORD_EXIT_SUCCESS) {
                xnd_trace("Coordinator exiting: COORD_EXIT_SUCCESS\n");
        } else if (status == COORD_EXIT_FAILURE) {
                xnd_trace("Coordinator exiting: COORD_EXIT_FAILURE\n");
        }
        
        coord_cleanup();
        exit(status);
}

void coord_setup_handler(int sig)
{
        int                     err;
        struct sigaction        sa;

        sigfillset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;
        sa.sa_handler = coord_handler;
        
        err = sigaction(sig, &sa, NULL);
        if (err != 0) {
                xnd_error("sigaction: %s\n", strerror(errno));
        }
}

void coord_handler(int sig)
{
        xnd_trace("Coordinator sent signal: %d\n", sig);
        coord_cleanup();
        kill(getpid(), sig);
}

void coord_event_loop(void)
{
        while (proc_list->size == 0) {
                coord_await_connection();
        }

        while (coord_state != COORD_EXITING) {
                for (int iter = 0; iter < 100; iter++) {
                        coord_await_connection();
                        coord_await_msg();
                }
                coord_check_status();
        }
        
        xnd_assert(coord_state == COORD_EXITING);
        xnd_assert(proc_list->size == 0);
}

void coord_check_status(void)
{
        int             err;
        struct proc     *p, *next;

        for (p = proc_list->head; p; p = next) {
                next = p->next;
                err = kill(p->real_pid, 0);
                if (err != 0 && errno == ESRCH) {
                        proc_list_remove(p);
                }
        }

        if (proc_list->size == 0) {
                coord_state = COORD_EXITING;
        }
}

void coord_await_connection(void)
{
        fd_set          set;
        int             fd, err, nfds;
        ssize_t         bytes;
        struct xnd_msg  msg;
        struct timeval  tv = { 0, 1000 };

        nfds = listen_fd + 1;
        FD_ZERO(&set);
        FD_SET(listen_fd, &set);

        err = select(nfds, &set, NULL, NULL, &tv);
        if (err < 0) {
                xnd_error("select: %s\n", strerror(errno));
                return;
        } else if (err == 0) {
                return;
        }

        fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) {
                xnd_error("accept: %s\n", strerror(errno));
                return;
        }

        bytes = readall(fd, &msg, sizeof(msg));
        if (bytes != sizeof(msg)) {
                xnd_warn("Failed to receive message from new connection");
                return;
        }

        switch (msg.hdr) {
        case XND_PROC_CONNECT_LAUNCH:
                xnd_assert(coord_state == COORD_RUNNING);
                coord_proc_connect(fd, &msg);
                break;
        case XND_PROC_CONNECT_RESTART:
                coord_state = COORD_RESTART_IN_PROGRESS;
                coord_proc_connect(fd, &msg);
                coord_state = COORD_RUNNING;
                break;
        case XND_COMMAND:
                coord_handle_command(&msg);
                break;
        default:
                break;
        }
}

void coord_proc_connect(int fd, struct xnd_msg *msg)
{
        ssize_t         bytes;
        struct xnd_msg  resp;
        struct proc     *p, *dup, *parent;

        p = malloc(sizeof(struct proc));
        xnd_assert(p != NULL);

        p->fd = fd;
        p->real_pid = msg->real_pid;
        p->real_ppid = msg->real_ppid;
        
        if (unlikely((dup = proc_list_find_real(p->real_pid)) != NULL)) {
                /**
                 * If program (like python3) execs into new process image,
                 * then xnd_setup() in libxnd.dylib will run a second time,
                 * but the process is already registered with the
                 * coordinator.
                 *
                 * However, register_with_coord_on_launch() expects an ack
                 * from the coordinator, so just remove the process and
                 * let it re-register.
                 */
                 proc_list_remove(dup);
        }
        
        if (proc_list->size == 0) {
                p->root_of_tree = true;
        } else {
                p->root_of_tree = false;
        }

        if (coord_state == COORD_RESTART_IN_PROGRESS) {
                p->virt_pid = msg->virt_pid;
                p->virt_ppid = msg->virt_ppid;
                if (p->virt_pid + 1 > next_virt_pid) {
                        next_virt_pid = p->virt_pid + 1;
                }
        } else {
                if (p->root_of_tree) {
                        p->virt_ppid = next_virt_pid++;
                } else {
                        xnd_assert(proc_list->size != 0);
                        parent = proc_list_find_real(p->real_ppid);
                        xnd_assert(parent != NULL);
                        p->virt_ppid = parent->virt_pid;
                }
                p->virt_pid = next_virt_pid++;
        }

        p->state = PROC_RUNNING;
        p->next = NULL;
        p->prev = NULL;
        proc_list_add(p);

        resp.hdr = XND_COORD_ACK;
        resp.ret = XND_SUCCESS;
        resp.real_pid = p->real_pid;
        resp.real_ppid = p->real_ppid;
        resp.virt_pid = p->virt_pid;
        resp.virt_ppid = p->virt_ppid;
        
        bytes = writeall(p->fd, &resp, sizeof(resp));
        xnd_assert(bytes == sizeof(resp));
        xnd_trace("Process %d connected\n", p->real_pid);
}

void coord_handle_command(struct xnd_msg *msg)
{
        xnd_assert(msg->hdr == XND_COMMAND);
        switch (msg->cmd) {
        case XND_CKPT_CMD:
                if (!coord_do_checkpoint()) {
                        xnd_error("Checkpoint failed!\n");
                }
                break;
        case XND_EXIT_CMD:
                coord_broadcast_exit();
                break;
        case XND_KILL_CMD:
                coord_kill_processes();
                break;
        default:
                __builtin_trap();
        }
}

void coord_broadcast_exit(void)
{
        int             err, exited, total;
        ssize_t         bytes;
        fd_set          set;
        struct proc     *p, *next;
        struct xnd_msg  msg, resp;
        struct timeval  tv = { 0, 10000 };

        msg.hdr = XND_COMMAND;
        msg.cmd = XND_EXIT_CMD;
        
        total = 0;
        exited = 0;

        for (p = proc_list->head; p; p = next) {
                next = p->next;
                err = kill(p->real_pid, 0);
                if (err != 0 && errno == ESRCH) {
                        proc_list_remove(p);
                        continue;
                }
                bytes = writeall(p->fd, &msg, sizeof(msg));
                xnd_assert(bytes == sizeof(msg));
                total++;
        }
        
again:
        for (p = proc_list->head; p; p = next) {
                next = p->next;
                FD_ZERO(&set);
                FD_SET(p->fd, &set);
                err = select(p->fd + 1, &set, NULL, NULL, &tv);
                if (err > 0) {
                        bytes = readall(p->fd, &resp, sizeof(resp));
                        xnd_assert(bytes == sizeof(resp));
                        if (resp.hdr == XND_PROC_EXIT) {
                                proc_list_remove(p);
                                exited++;
                        }
                } else {
                        err = kill(p->real_pid, 0);
                        if (err != 0 && errno == ESRCH) {
                                proc_list_remove(p);
                                exited++;
                        }
                }
        }

        if (exited != total) {
                usleep(50);
                goto again;
        }

        coord_state = COORD_EXITING;
        xnd_assert(proc_list->size == 0);
}

void coord_kill_processes(void)
{
        struct proc     *p, *next;
        int             err;

        coord_state = COORD_EXITING;
        for (p = proc_list->head; p; p = next) {
                next = p->next;
                err = kill(p->real_pid, SIGKILL);
                if (err == 0) {
                        proc_list_remove(p);
                } else if (err != 0 && errno == ESRCH) {
                        proc_list_remove(p);
                }
        }

        xnd_assert(proc_list->size == 0);
}

bool coord_do_checkpoint(void)
{
        int             ready, sent, received;
        sigset_t        set;

        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGQUIT);
        sigprocmask(SIG_BLOCK, &set, NULL);

        coord_state = COORD_PRE_CHECKPOINT;
        
        ready = coord_prepare_for_collective(COORD_BROADCAST);
        sent = coord_broadcast(XND_CHECKPOINT_REQUEST, PROC_RECV_CKPT_REQUEST);
        if (sent != ready) {
                xnd_error("Broadcast failed: XND_CHECKPOINT_REQUEST\n");
                goto bad;
        }
        num_peers = sent;
        
        ready = coord_prepare_for_collective(COORD_REDUCE);
        if (ready != num_peers) {
                xnd_error("Error before reduction: "
                          "XND_READY_FOR_CHECKPOINT\n");
                goto bad;
        }

        received = coord_reduce(XND_READY_FOR_CHECKPOINT, PROC_READY_FOR_CKPT);
        if (received != ready) {
                xnd_error("Reduction failed: XND_READY_FOR_CHECKPOINT\n");
                goto bad;
        }

        ready = coord_prepare_for_collective(COORD_BROADCAST);
        if (ready != num_peers) {
                xnd_error("Error before broadcast: "
                          "XND_START_CHECKPOINT\n");
                goto bad;
        }

        sent = coord_broadcast(XND_START_CHECKPOINT, PROC_CKPT_IN_PROGRESS);
        if (sent != ready) {
                xnd_error("Broadcast failed: XND_START_CHECKPOINT\n");
                goto bad;
        }

        coord_state = COORD_CKPT_IN_PROGRESS;
        ready = coord_prepare_for_collective(COORD_REDUCE);
        if (ready != num_peers) {
                xnd_error("Error before reduction: "
                          "XND_CHECKPOINT_COMPLETE\n");
                goto bad;
        }
        
        received = coord_reduce(XND_CHECKPOINT_COMPLETE, PROC_CKPT_COMPLETE);
        if (received != ready) {
                xnd_error("Reduction failed: XND_CHECKPOINT_COMPLETE\n");
                goto bad;
        }

        coord_state = COORD_POST_CHECKPOINT;
        ready = coord_prepare_for_collective(COORD_BROADCAST);
        if (ready != num_peers) {
                xnd_error("Error before broadcast: "
                          "XND_RESUME_AFTER_CHECKPOINT\n");
                goto bad;
        }

        sent = coord_broadcast(XND_RESUME_AFTER_CHECKPOINT, PROC_RUNNING);
        if (sent != ready) {
                xnd_error("Broadcast failed: XND_RESUME_AFTER_CHECKPOINT\n");
                goto bad;
        }
        
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        coord_state = COORD_RUNNING;
        return true;

bad:
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        coord_state = COORD_RUNNING;
        return false;
}

int coord_prepare_for_collective(enum coord_comm_type comm)
{
        struct proc     *p, *next;
        int             err, nfds, total;
        fd_set          set;
        struct timeval  tv = { 0, 10000 };

again:
        nfds = 0;
        total = 0;
        FD_ZERO(&set);
        for (p = proc_list->head; p; p = next) {
                next = p->next;
                err = kill(p->real_pid, 0);
                if (err != 0 && errno == ESRCH) {
                        proc_list_remove(p);
                        continue;
                }
                total++;
                FD_SET(p->fd, &set);
                if (p->fd + 1 > nfds) {
                        nfds = p->fd + 1;
                }
        }

        if (comm == COORD_BROADCAST) {
                err = select(nfds, NULL, &set, NULL, &tv);
        } else if (comm == COORD_REDUCE) {
                err = select(nfds, &set, NULL, NULL, &tv);
        }
        
        if (err != total) {
                goto again;
        }

        return total;
}

int coord_broadcast(enum xnd_msghdr hdr, enum proc_state transition)
{
        struct proc     *p;
        struct xnd_msg  msg;
        ssize_t         bytes;
        int             total;
        
        total = 0;
        msg.hdr = hdr;
        for (p = proc_list->head; p; p = p->next) {
                bytes = writeall(p->fd, &msg, sizeof(msg));
                if (bytes == sizeof(msg)) {
                        p->state = transition;
                        total++;
                }
        }

        return total;
}

int coord_reduce(enum xnd_msghdr expected, enum proc_state transition)
{
        struct proc     *p;
        struct xnd_msg  msg;
        ssize_t         bytes;
        int             total;

        total = 0;
        for (p = proc_list->head; p; p = p->next) {
                bytes = readall(p->fd, &msg, sizeof(msg));
                if (bytes == sizeof(msg)) {
                        if (msg.hdr == expected) {
                                p->state = transition;
                                total++;
                        }
                }
        }

        return total;
}

void coord_await_msg(void)
{
        struct proc     *p, *next;
        fd_set          set;
        int             nfds, err;
        struct timeval  tv = { 0, 1000 };

        if (proc_list->size == 0) {
                return;
        }
        
        nfds = 0;
        FD_ZERO(&set);
        for (p = proc_list->head; p; p = p->next) {
                FD_SET(p->fd, &set);
                if (p->fd + 1 > nfds) {
                        nfds = p->fd + 1;
                }
        }

        err = select(nfds, &set, NULL, NULL, &tv);
        if (err < 0) {
                xnd_error("select: %s\n", strerror(errno));
                return;
        } else if (err == 0) {
                return;
        }

        for (p = proc_list->head; p; p = next) {
                next = p->next;
                if (FD_ISSET(p->fd, &set)) {
                        coord_handle_proc_msg(p);
                }
        }
}

void coord_handle_proc_msg(struct proc *p)
{
        int             err;
        ssize_t         bytes;
        struct xnd_msg  msg;

        bytes = readall(p->fd, &msg, sizeof(msg));
        if (bytes != sizeof(msg)) {
                err = kill(p->real_pid, 0);
                if (err != 0 && errno == ESRCH) {
                        xnd_warn("Process %d disconnected unexpectedly\n",
                                 p->real_pid);
                        proc_list_remove(p);
                } else {
                        xnd_warn("Failed to read message from process %d\n",
                                 p->real_pid);
                }
                if (proc_list->size == 0) {
                        coord_state = COORD_EXITING;
                }
                return;
        }
        
        switch (msg.hdr) {
        case XND_PROC_EXIT:
                proc_list_remove(p);
                if (proc_list->size == 0) {
                        coord_state = COORD_EXITING;
                }
                break;
        case XND_VIRT_TO_REAL_REQ:
                coord_send_virt_to_real(p, &msg);
                break;
        case XND_REAL_TO_VIRT_REQ:
                coord_send_real_to_virt(p, &msg);
                break;
        default:
                break;
        }
}

void coord_send_virt_to_real(struct proc *p, struct xnd_msg *msg)
{
        struct proc     *target;
        struct xnd_msg  resp;
        ssize_t         bytes;
        
        resp.hdr = XND_COORD_ACK;
        target = proc_list_find_virt(msg->virt_pid);
        if (target) {
                resp.real_pid = target->real_pid;
                resp.ret = XND_SUCCESS;
        } else {
                resp.real_pid = -1;
                resp.ret = XND_FAILURE;
        }

        bytes = writeall(p->fd, &resp, sizeof(resp));
        xnd_assert(bytes == sizeof(resp));
}

void coord_send_real_to_virt(struct proc *p, struct xnd_msg *msg)
{
        struct proc     *target;
        struct xnd_msg  resp;
        ssize_t         bytes;
        
        resp.hdr = XND_COORD_ACK;
        target = proc_list_find_real(msg->real_pid);
        if (target) {
                resp.virt_pid = target->virt_pid;
                resp.ret = XND_SUCCESS;
        } else {
                resp.virt_pid = -1;
                resp.ret = XND_FAILURE;
        }

        bytes = writeall(p->fd, &resp, sizeof(resp));
        xnd_assert(bytes == sizeof(resp));
}

int main(int argc, char *argv[])
{
        proc_list_init();
        coord_init();
        coord_event_loop();
        coord_exit(COORD_EXIT_SUCCESS);
}
