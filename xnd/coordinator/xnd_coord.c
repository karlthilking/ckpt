/* xnd_coord.c */
#include "xnd/xnd.h"
#include "xnd/ckptfile.h"
#include "xnd/util/io.h"
#include "xnd/coordinator/xnd_coord.h"
#include "xnd/coordinator/xnd_coord_api.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/select.h>

static struct proc_list *proc_list      = NULL;
static struct proc      *group_leader   = NULL;
static enum coord_state coord_state     = COORD_NULL;

static uuid_t   xnd_uuid;
static u64      epoch           = 0;
static pid_t    next_virt_pid   = 1;
static u32      next_xnd_pid    = 1000;
static int      listen_fd       = -1;
static u32      num_peers       = 0;

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
        
        if (p->is_root_of_tree) {
                group_leader = p;
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
        
        uuid_generate(xnd_uuid);
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

__noreturn void coord_exit(int status)
{
        if (status == COORD_EXIT_SUCCESS) {
                xnd_trace("Coordinator exiting: COORD_EXIT_SUCCESS\n");
        } else if (status == COORD_EXIT_FAILURE) {
                xnd_trace("Coordinator exiting: COORD_EXIT_FAILURE\n");
        }
        
        coord_cleanup();
        exit(status);

        unreachable();
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

        for (;;) {
                for (int iter = 0; iter < 100; iter++) {
                        coord_await_connection();
                        coord_await_msg();
                }
                coord_check_status();
        }
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
                coord_exit(COORD_EXIT_SUCCESS);
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
                coord_register_process(fd, &msg);
                break;
        case XND_PROC_CONNECT_RESTART:
                coord_do_restart(fd, &msg);
                break;
        case XND_COMMAND:
                coord_handle_command(&msg);
                break;
        default:
                break;
        }
}

void coord_register_process(int fd, struct xnd_msg *msg)
{
        struct xnd_msg  resp;
        struct proc     *p, *parent;
        ssize_t         bytes;
        pid_t           virt_pid = -1, virt_ppid = -1;
        u32             xnd_pid, xnd_ppid, xnd_pgid;
        
        /**
         * If process with msg->real_pid has already registered,
         * then the process exec'd into a new process image and is
         * trying to re-register. Just remove the old process descriptor
         * from the process list and let it re-register.
         */
        p = proc_list_find_real(msg->real_pid);
        if (p != NULL) {
                xnd_pid = p->xnd_pid;
                xnd_ppid = p->xnd_ppid;
                xnd_pgid = p->xnd_pgid;
                virt_pid = p->virt_pid;
                virt_ppid = p->virt_ppid;
                proc_list_remove(p);
                p = NULL;
        }

        p = malloc(sizeof(struct proc));
        xnd_assert(p != NULL);

        p->fd = fd;
        p->real_pid = msg->real_pid;
        p->real_ppid = msg->real_ppid;
        p->is_root_of_tree = msg->is_root_of_tree;

        if (coord_state == COORD_RESTARTINPROG) {
                /**
                 * If restart in progress, process already knows its
                 * virtual pid and ppid, as well as unique xnd pid, ppid,
                 * and pgid.
                 */
                p->virt_pid = msg->virt_pid;
                p->virt_ppid = msg->virt_ppid;
                if (p->virt_pid + 1 > next_virt_pid) {
                        next_virt_pid = p->virt_pid + 1;
                }
                p->xnd_pid = msg->xnd_pid;
                p->xnd_ppid = msg->xnd_ppid;
                p->xnd_pgid = msg->xnd_pgid;
                if (p->xnd_pid + 1 > next_xnd_pid) {
                        next_xnd_pid = p->xnd_pid + 1;
                }
                p->state = P_RESTARTINPROG;
        } else {
                xnd_assert(epoch == 0);
                if (p->is_root_of_tree) {
                        if (virt_pid == -1 && virt_ppid == -1) {
                                p->virt_ppid = next_virt_pid++;
                                p->virt_pid = next_virt_pid++;
                                p->xnd_ppid = next_xnd_pid++;
                                p->xnd_pid = next_xnd_pid++;
                                p->xnd_pgid = p->xnd_pid;
                        } else {
                                p->virt_ppid = virt_ppid;
                                p->virt_pid = virt_pid;
                                p->xnd_ppid = xnd_ppid;
                                p->xnd_pid = xnd_pid;
                                p->xnd_pgid = xnd_pgid;
                        }
                } else {
                        parent = proc_list_find_real(p->real_ppid);
                        xnd_assert(parent != NULL);
                        p->virt_ppid = parent->virt_pid;
                        p->xnd_ppid = parent->xnd_pid;
                        p->xnd_pgid = parent->xnd_pgid;
                        if (virt_pid == -1 && virt_ppid == -1) {
                                p->virt_pid = next_virt_pid++;
                                p->xnd_pid = next_xnd_pid++;
                        } else {
                                p->virt_pid = virt_pid;
                                p->xnd_pid = xnd_pid;
                        }
                }
                p->state = P_RUNNING;
        }

        p->next = NULL;
        p->prev = NULL;

        resp.hdr = XND_COORD_ACK;
        resp.ret = XND_SUCCESS;

        resp.real_pid = p->real_pid;
        resp.real_ppid = p->real_ppid;
        resp.virt_pid = p->virt_pid;
        resp.virt_ppid = p->virt_ppid;
        
        memcpy(resp.xnd_uuid, xnd_uuid, sizeof(uuid_t));
        resp.xnd_pid = p->xnd_pid;
        resp.xnd_ppid = p->xnd_ppid;
        resp.xnd_pgid = p->xnd_pgid;

        bytes = writeall(p->fd, &resp, sizeof(resp));
        if (bytes == sizeof(resp)) {
                xnd_trace("Process %d connected\n", p->real_pid);
                proc_list_add(p);
        } else {
                xnd_error("Failed to register process %d\n", p->real_pid);
        }
}

void coord_handle_command(struct xnd_msg *msg)
{
        xnd_assert(msg->hdr == XND_COMMAND);
        switch (msg->cmd) {
        case XND_CKPT_CMD:
                coord_do_checkpoint();
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

__noreturn void coord_broadcast_exit(void)
{
        struct xnd_msg  msg;
        int             total, ready, sent, received;
        
        coord_state = COORD_EXITING;
        msg.hdr = XND_COMMAND;
        msg.cmd = XND_EXIT_CMD;
        
        sent = 0;
        total = coord_collective_prepare(COMM_BROADCAST);
        sent = coord_broadcast(&msg, P_RUNNING, P_RUNNING);
        if (sent != total) {
                xnd_error("Broadcast failed: XND_COMMAND (XND_EXIT_CMD)\n");
                xnd_abort();
        }

        ready = coord_collective_prepare(COMM_REDUCE);
        if (ready != total) {
                xnd_error("Error before reduction: XND_PROC_EXIT\n");
                xnd_abort();
        }

        received = coord_reduce(XND_PROC_EXIT, P_RUNNING, P_EXITED);
        if (received != total) {
                xnd_error("Reduction failed: XND_PROC_EXIT\n");
                xnd_abort();
        }

        coord_exit(COORD_EXIT_SUCCESS);
        unreachable();
}

__noreturn void coord_kill_processes(void)
{
        struct proc *p;

        coord_state = COORD_EXITING;
        for (p = proc_list->head; p; p = p->next) {
                (void)kill(p->real_pid, SIGKILL);
        }
        
        coord_exit(COORD_EXIT_SUCCESS);
        unreachable();
}

void coord_do_restart(int first_fd, struct xnd_msg *first_msg)
{
        int             fd, expected, sent;
        struct xnd_msg  msg, resp;
        ssize_t         bytes;

        coord_state = COORD_RESTARTINPROG;
        epoch = first_msg->epoch;
        num_peers = first_msg->num_peers;
        memcpy(xnd_uuid, first_msg->xnd_uuid, sizeof(uuid_t));
        coord_register_process(first_fd, first_msg);

        while (proc_list->size < num_peers) {
                fd = accept(listen_fd, NULL, NULL);
                if (unlikely(fd < 0)) {
                        xnd_error("accept: %s\n", strerror(errno));
                        continue;
                }
                bytes = readall(fd, &msg, sizeof(msg));
                xnd_assert(bytes == sizeof(msg));
                coord_register_process(fd, &msg);
        }

        resp.hdr = XND_RESUME_AFTER_RESTART;
        expected = coord_collective_prepare(COMM_BROADCAST);
        if (expected != num_peers) {
                xnd_error("Error before XND_RESUME_AFTER_RESTART\n");
                xnd_error("num_peers=%u, ready=%d\n", num_peers, expected);
                coord_exit(COORD_EXIT_FAILURE);
        }

        sent = coord_broadcast(&resp, P_RESTARTINPROG, P_RUNNING);
        if (expected != sent) {
                xnd_error("Broadcast failed: XND_RESUME_AFTER_RESTART\n");
                coord_exit(COORD_EXIT_FAILURE);
        }

        coord_state = COORD_RUNNING;
}

int coord_write_ckpt_manifest(void)
{
        u32             min_id = UINT32_MAX, max_id = 0;
        struct proc     *p;
        
        for (p = proc_list->head; p; p = p->next) {
                if (p->xnd_pid < min_id) {
                        min_id = p->xnd_pid;
                }
                if (p->xnd_pid > max_id) {
                        max_id = p->xnd_pid;
                }
        }

        return xnd_ckptfile_write_manifest(num_peers, min_id, max_id,
                                           xnd_uuid, epoch);
}

bool coord_do_checkpoint(void)
{
        struct xnd_msg  msg;
        int             expected, arrived, total_sent, total_recv;
        sigset_t        set;

        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGQUIT);
        sigprocmask(SIG_BLOCK, &set, NULL);
        
        coord_state = COORD_PRECKPT;
        msg.hdr = XND_CKPT_REQUEST;
        total_sent = 0;
        /**
         * Send XND_CKPT_REQUEST to all processes currently connected
         * to the coordinator. coord_prepare_for_multicast() will
         * determine how many processes in the P_RUNNING state are ready
         * to receive the checkpoint request. coord_broadcast() will
         * exclusively send only to processes who have not already received
         * a checkpoint request.
         */
send_ckpt_request:
        expected = coord_multicast_prepare(COMM_BROADCAST, P_RUNNING);
        if (expected != 0) {
                arrived = coord_broadcast(&msg, P_RUNNING, P_CKPTRECV);
                total_sent += arrived;
                if (arrived != expected) {
                        goto send_ckpt_request;
                }
        }

        if (unlikely(total_sent == 0)) {
                xnd_error("Broadcast failed: XND_CKPT_REQUEST\n");
                goto fail;
        }
        
        /**
         * Collect XND_CKPT_READY from each process that received an
         * XND_CKPT_REQUEST in the previous phase.
         */
        total_recv = 0;
recv_ckpt_ready:
        expected = coord_multicast_prepare(COMM_REDUCE, P_CKPTRECV);
        if (expected != 0) {
                arrived = coord_reduce(XND_CKPT_READY, P_CKPTRECV, P_CKPTRDY);
                total_recv += arrived;
                if (arrived != expected) {
                        goto recv_ckpt_ready;
                }
        }

        if (unlikely(total_recv != total_sent)) {
                xnd_error("Reduction failed: XND_CKPT_READY\n");
                goto fail;
        }
        
        num_peers = total_recv;
        /**
         * Now that every process is suspended, create the
         * checkpoint directory.
         */
        if (xnd_ckptdir_create(xnd_uuid, epoch) != 0) {
                xnd_error("Failed to make checkpoint directory\n");
                goto fail;
        }

        /**
         * Broadcast to all processes to inform them to start their local
         * checkpoint. If any processes have exited between the previous
         * phase and receiving XND_CKPT_START, the checkpoint will fail.
         */
        msg.hdr = XND_CKPT_START;
        msg.num_peers = num_peers;
        expected = coord_collective_prepare(COMM_BROADCAST);
        if (expected != num_peers) {
                xnd_error("Error before broadcast: XND_CKPT_START\n");
                goto fail;
        }
        arrived = coord_broadcast(&msg, P_CKPTRDY, P_CKPTINPROG);
        if (arrived != expected) {
                xnd_error("Broadcast failed: XND_CKPT_START\n");
                goto fail;
        }

        /**
         * Wait to receive XND_CKPT_DONE from all participating processes.
         * If any process fails to checkpoint, the global checkpoint will
         * be aborted.
         */
        coord_state = COORD_CKPTINPROG;
        expected = coord_collective_prepare(COMM_REDUCE);
        if (expected != num_peers) {
                xnd_error("Error before reduction: XND_CKPT_DONE\n");
                goto fail;
        }
        arrived = coord_reduce(XND_CKPT_DONE, P_CKPTINPROG, P_CKPTDONE);
        if (arrived != expected) {
                xnd_error("Reduction failed: XND_CKPT_DONE");
                goto fail;
        }
        
        /**
         * Now, every process has finished their local checkpoint;
         * write the checkpoint manifest.
         */
        if (coord_write_ckpt_manifest() != 0) {
                goto fail;
        }
        
        msg.hdr = XND_RESUME_AFTER_CKPT;
        coord_state = COORD_POSTCKPT;
        expected = coord_collective_prepare(COMM_BROADCAST);
        if (expected != num_peers) {
                xnd_error("Error before broadcast: XND_RESUME_AFTER_CKPT\n");
                goto fail;
        }
        arrived = coord_broadcast(&msg, P_CKPTDONE, P_RUNNING);
        if (arrived != expected) {
                xnd_error("Broadcast failed: XND_RESUME_AFTER_CKPT\n");
                goto fail;
        }
        
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        coord_state = COORD_RUNNING;
        return true;
fail:
        xnd_error("Aborting checkpoint...\n");
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        coord_state = COORD_RUNNING;
        return false;
}

int coord_multicast_prepare(enum coord_comm_type comm, 
                            enum proc_state expected)
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
                if (p->state == expected) {
                        total++;
                        FD_SET(p->fd, &set);
                        if (p->fd + 1 > nfds) {
                                nfds = p->fd + 1;
                        }
                }
        }

        if (total == 0) {
                return 0;
        }

        if (comm == COMM_BROADCAST) {
                err = select(nfds, NULL, &set, NULL, &tv);
        } else if (comm == COMM_REDUCE) {
                err = select(nfds, &set, NULL, NULL, &tv);
        }

        if (err != total) {
                goto again;
        }

        return total;
}

int coord_collective_prepare(enum coord_comm_type comm)
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

        if (comm == COMM_BROADCAST) {
                err = select(nfds, NULL, &set, NULL, &tv);
        } else if (comm == COMM_REDUCE) {
                err = select(nfds, &set, NULL, NULL, &tv);
        }
        
        if (err != total) {
                goto again;
        }

        return total;
}

int coord_broadcast(struct xnd_msg *msg, enum proc_state old, 
                    enum proc_state new)
{
        struct proc     *p;
        ssize_t         bytes;
        int             total;
        
        total = 0;
        for (p = proc_list->head; p; p = p->next) {
                if (p->state == new) {
                        continue;
                } else {
                        xnd_assert(p->state == old);
                }
                coord_write_msg_metainfo(p, msg);
                bytes = writeall(p->fd, msg, sizeof(struct xnd_msg));
                if (bytes == sizeof(struct xnd_msg)) {
                        p->state = new;
                        total++;
                }
        }

        return total;
}

int coord_reduce(enum xnd_msghdr expected, enum proc_state old,
                 enum proc_state new)
{
        struct proc     *p;
        struct xnd_msg  msg;
        ssize_t         bytes;
        int             total;

        total = 0;
        for (p = proc_list->head; p; p = p->next) {
                if (p->state == new) {
                        continue;
                } else if (p->state != old) {
                        xnd_assert(p->state == old);
                }
                bytes = readall(p->fd, &msg, sizeof(msg));
                if (bytes == sizeof(msg)) {
                        if (msg.hdr == expected) {
                                p->state = new;
                                total++;
                        }
                }
        }

        return total;
}

void coord_write_msg_metainfo(struct proc *p, struct xnd_msg *msg)
{
        msg->xnd_pid = p->xnd_pid;
        msg->xnd_ppid = p->xnd_ppid;
        msg->xnd_pgid = p->xnd_pgid;

        msg->virt_pid = p->virt_pid;
        msg->virt_ppid = p->virt_ppid;
        msg->real_pid = p->real_pid;
        msg->real_ppid = p->real_ppid;

        msg->is_root_of_tree = p->is_root_of_tree;
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
        case XND_VIRT_TO_REAL:
                coord_send_virt_to_real(p, &msg);
                break;
        case XND_REAL_TO_VIRT:
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
