/* xnd_coord.c */
#include "xnd/xnd.h"
#include "common/time.h"
#include "xnd/ckptfile.h"
#include "xnd/util/io.h"
#include "xnd/util/env.h"
#include "xnd/pid/pid_table_common.h"
#include "proc_list.h"
#include "xnd_coord.h"
#include "xnd_coord_api.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/select.h>

static struct proc_list *proc_list = NULL;
static struct coord_info coord_info = {
        .epoch          = 0,
        .next_virt_pid  = INITIAL_VIRT_PID,
        .next_xnd_pid   = INITIAL_XND_PID,
        .listen_fd      = -1,
        .num_peers      = 0,
        .ckpt_interval  = 0
};

void proc_exit_callback(struct proc *p)
{
        if (p->fd != -1)
                close(p->fd);
        if (p->oob_fd != -1)
                close(p->oob_fd);

        pid_table_erase(p->virt_pid);
}

static inline pid_t coord_next_virt_pid(void)
{
        pid_t next = coord_info.next_virt_pid++;

        if (unlikely(pid_table_virtual_pid_exists(next))) {
                xnd_error("Virtual pid already exists: %d\n", next);
                coord_exit(COORD_EXIT_FAILURE);
        }

        return next;
}

static inline u32 coord_next_xnd_pid(void)
{
        return coord_info.next_xnd_pid++;
}

void coord_work(void)
{
        struct timeval  tv_start, tv_end;
        int             err, elapsed;
        bool            refresh;
        u64             iter = 0u;

        while (proc_list->size == 0) {
                err = kill(getppid(), 0);
                if (err != 0 && errno == ESRCH)
                        coord_exit(COORD_EXIT_FAILURE);
                coord_wait_for_connection();
        }

        refresh = false;
        gettimeofday(&tv_start, NULL);
        for (;;) {
                if (coord_info.ckpt_interval && refresh) {
                        gettimeofday(&tv_start, NULL);
                        refresh = false;
                }

                coord_wait_for_connection();
                coord_wait_for_msg();

                if (coord_info.ckpt_interval != 0) {
                        gettimeofday(&tv_end, NULL);
                        elapsed = tv_end.tv_sec - tv_start.tv_sec;
                        if (elapsed >= coord_info.ckpt_interval) {
                                coord_do_checkpoint();
                                refresh = true;
                        }
                }

                if (proc_list->size == 0) {
                        break;
                } else if (COORD_CHECK_HEARTBEAT(iter++)) {
                        proc_list_filter(proc_list);
                        if (proc_list->size == 0)
                                break;
                }
        }
}

void coord_init(void)
{
        int                     fd, err;
        struct sockaddr_un      addr;

        uuid_generate(coord_info.xnd_uuid);
        coord_setup_handler(SIGINT);
        coord_setup_handler(SIGTERM);
        coord_setup_handler(SIGQUIT);

        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
                xnd_error("socket: %s\n", strerror(errno));
                coord_exit(COORD_EXIT_FAILURE);
        }

        bzero(&addr, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, XND_COORD_PATH, sizeof(addr.sun_path) - 1);

        xnd_assert(access(XND_COORD_PATH, F_OK) != 0);
        err = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
        if (err != 0) {
                xnd_error("bind: %s\n", strerror(errno));
                coord_exit(COORD_EXIT_FAILURE);
        }

        err = listen(fd, COORD_MAX_PROC);
        if (err != 0) {
                xnd_error("listen: %s\n", strerror(errno));
                coord_exit(COORD_EXIT_FAILURE);
        }

        coord_info.listen_fd = fd;
        coord_info.ckpt_interval = env_get_ckpt_interval();

        proc_list = proc_list_init();
        pid_table_init();
}

void coord_cleanup(void)
{
        proc_list_destroy(proc_list);
        if (coord_info.listen_fd != -1) {
                if (close(coord_info.listen_fd) != 0)
                        xnd_perror("close");
        }

        if (access(XND_COORD_PATH, F_OK) == 0) {
                if (unlink(XND_COORD_PATH) != 0)
                        xnd_perror("unlink");
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
        struct sigaction sa;

        sigfillset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;
        sa.sa_handler = coord_handler;

        if (sigaction(sig, &sa, NULL) != 0)
                xnd_perror("sigaction");
}

void coord_handler(int sig)
{
        struct proc *p;

        xnd_trace("Coordinator sent signal: %d\n", sig);
        proc_foreach(p, proc_list) {
                kill(p->real_pid, sig);
        }

        coord_cleanup();
        kill(getpid(), sig);
}

void coord_broadcast_kill(void)
{
        struct proc *p;

        proc_foreach(p, proc_list) {
                kill(p->real_pid, SIGKILL);
        }

        coord_exit(COORD_EXIT_SUCCESS);
}

void coord_handle_command(int fd, struct xnd_msg *msg)
{
        struct xnd_msg resp;

        xnd_assert(msg->hdr == XND_COMMAND);
        xnd_trace("Received command: %s\n", xnd_cmd_string(msg->cmd));

        switch (msg->cmd) {
        case XND_CKPT_CMD:
                coord_do_checkpoint();
                break;
        case XND_KILL_CMD:
                coord_broadcast_kill();
                break;
        default:
                xnd_error("Invalid command: %s\n", xnd_cmd_string(msg->cmd));
                coord_exit(COORD_EXIT_FAILURE);
        }

        resp.hdr = XND_COORD_ACK;
        resp.ret = XND_SUCCESS;
        xnd_assert(coord_send_msg(fd, &resp) == 0);
}

void coord_handle_msg(int fd, struct xnd_msg *msg)
{
        struct proc *p;

        switch (msg->hdr) {
        case XND_EXIT: {
                p = proc_list_find_by_real_pid(proc_list, msg->real_pid);
                if (p) {
                        proc_list_remove(proc_list, p);
                } else {
                        xnd_error("Exited process not found (pid: %d)\n",
                                  msg->real_pid);
                }
                break;
        }
        case XND_VIRT_TO_REAL: {
                coord_send_virt_to_real(fd, msg);
                break;
        }
        case XND_REAL_TO_VIRT: {
                coord_send_real_to_virt(fd, msg);
                break;
        }
        default:
                xnd_error("Unexpected message: %s\n",
                          xnd_msghdr_string(msg->hdr));
                coord_exit(COORD_EXIT_FAILURE);
        }
}

int coord_send_msg(int fd, struct xnd_msg *msg)
{
        ssize_t bytes;

        bytes = writeall(fd, msg, sizeof(struct xnd_msg));
        if (bytes == sizeof(struct xnd_msg)) {
                return 0;
        }

        return -1;
}

int coord_recv_msg(int fd, struct xnd_msg *msg)
{
        ssize_t bytes;

        bytes = readall(fd, msg, sizeof(struct xnd_msg));
        if (bytes == sizeof(struct xnd_msg)) {
                return 0;
        }

        return -1;
}

int coord_release_barrier(enum coord_barrier_type type)
{
        int             err;
        struct xnd_msg  msg;
        enum proc_state expected, next;
        struct proc     *p;

        switch (type) {
        case COORD_BARRIER_PRECKPT:
                msg.hdr = XND_CKPT_START;
                expected = PROC_READY_FOR_CKPT;
                next = PROC_CKPT_IN_PROGRESS;
                break;
        case COORD_BARRIER_POSTCKPT: {
                msg.hdr = XND_RESUME_AFTER_CKPT;
                expected = PROC_CKPT_COMPLETE;
                next = PROC_RUNNING;
                break;
        }
        case COORD_BARRIER_POSTRESTART: {
                msg.hdr = XND_RESUME_AFTER_RESTART;
                expected = PROC_RESTART_IN_PROGRESS;
                next = PROC_RUNNING;
                break;
        }
        default:
                coord_exit(COORD_EXIT_FAILURE);
        }

        proc_foreach(p, proc_list) {
                xnd_assert(p->state == expected);
                /**
                 * If pre-checkpoint, it is necessary to include num_peers
                 * (number of processes in the checkpointed computation)
                 * and is_root_of_tree (root of process tree) information
                 * to inform each process.
                 */
                if (type == COORD_BARRIER_PRECKPT) {
                        msg.num_peers = coord_info.num_peers;
                        msg.is_root_of_tree = p->is_root_of_tree;
                }

                err = coord_send_msg(p->fd, &msg);
                if (err != 0) {
                        xnd_error("Failed to send %s to process %d\n",
                                  xnd_msghdr_string(msg.hdr), p->real_pid);
                        return -1;
                }
                p->state = next;
        }

        return 0;
}

int coord_collective_prepare(enum coord_comm_type type)
{
        struct proc     *p, *next;
        int             err, nfds, total;
        fd_set          set;
        struct timeval  tv = { 0, COORD_TIMEOUT_USEC };

        do {
                nfds = 0;
                total = 0;
                FD_ZERO(&set);
                proc_foreach_safe(p, next, proc_list) {
                        err = kill(p->real_pid, 0);
                        if (err != 0 && errno == ESRCH) {
                                proc_list_remove(proc_list, p);
                                continue;
                        }
                        total++;
                        FD_SET(p->fd, &set);
                        nfds = max(nfds, p->fd + 1);
                }

                if (type == COMM_BROADCAST) {
                        err = select(nfds, NULL, &set, NULL, &tv);
                } else if (type == COMM_REDUCE) {
                        err = select(nfds, &set, NULL, NULL, &tv);
                }

                if (err < 0) {
                        xnd_error("select: %s\n", strerror(errno));
                        coord_exit(COORD_EXIT_FAILURE);
                }
        } while (err != total);

        return total;
}

void coord_wait_for_msg(void)
{
        fd_set          set;
        int             err, nfds;
        struct proc     *p, *next;
        struct xnd_msg  msg;
        struct timeval  tv = { 0, COORD_TIMEOUT_USEC };

        nfds = 0;
        FD_ZERO(&set);
        proc_foreach(p, proc_list) {
                nfds = max(nfds, p->fd + 1);
                FD_SET(p->fd, &set);
                if (p->oob_fd != -1) {
                        nfds = max(nfds, p->oob_fd + 1);
                        FD_SET(p->oob_fd, &set);
                }
        }

        if ((err = select(nfds, &set, NULL, NULL, &tv)) <= 0) {
                if (err == -1) {
                        xnd_error("select: %s\n", strerror(errno));
                }
                return;
        }

        proc_foreach_safe(p, next, proc_list) {
                if (FD_ISSET(p->fd, &set)) {
                        err = coord_recv_msg(p->fd, &msg);
                        if (err != 0) {
                                proc_list_remove(proc_list, p);
                                continue;
                        }
                        coord_handle_msg(p->fd, &msg);
                }
                if (p->oob_fd != -1 && FD_ISSET(p->oob_fd, &set)) {
                        err = coord_recv_msg(p->oob_fd, &msg);
                        if (err != 0) {
                                proc_list_remove(proc_list, p);
                                continue;
                        }
                        coord_handle_msg(p->oob_fd, &msg);
                }
        }
}

void coord_wait_for_connection(void)
{
        fd_set          set;
        int             fd, err, nfds;
        struct xnd_msg  msg;
        struct proc     *p;
        struct timeval  tv = { 0, COORD_TIMEOUT_USEC };

        nfds = coord_info.listen_fd + 1;
        FD_ZERO(&set);
        FD_SET(coord_info.listen_fd, &set);
        if ((err = select(nfds, &set, NULL, NULL, &tv)) <= 0) {
                if (err == -1) {
                        xnd_error("select: %s\n", strerror(errno));
                }
                return;
        }

        fd = accept(coord_info.listen_fd, NULL, NULL);
        if (fd < 0) {
                xnd_error("accept: %s\n", strerror(errno));
                return;
        }

        xnd_assert(coord_recv_msg(fd, &msg) == 0);
        switch (msg.hdr) {
        case XND_CONNECT_LAUNCH: {
                coord_connect_with_process_on_launch(fd, &msg);
                break;
        }
        case XND_ATFORK_PREPARE: {
                coord_atfork(fd, &msg);
                break;
        }
        case XND_COMMAND: {
                coord_handle_command(fd, &msg);
                close(fd);
                break;
        }
        case XND_REAL_TO_VIRT: {
                p = proc_list_find_by_xnd_pid(proc_list, msg.xnd_pid);
                xnd_assert(p != NULL);
                p->oob_fd = fd;
                coord_send_real_to_virt(fd, &msg);
                break;
        }
        case XND_VIRT_TO_REAL: {
                p = proc_list_find_by_xnd_pid(proc_list, msg.xnd_pid);
                xnd_assert(p != NULL);
                p->oob_fd = fd;
                coord_send_virt_to_real(fd, &msg);
                break;
        }
        default:
                xnd_error("Unexpected message: %s\n",
                          xnd_msghdr_string(msg.hdr));
                coord_exit(COORD_EXIT_FAILURE);
        }
}

void coord_connect_with_process_on_launch(int fd, struct xnd_msg *msg)
{
        struct proc *p, *parent;
        
        /**
         * If msg->real_pid already corresponds to a process in the process
         * list, then this process exec'd into a new process image and is
         * re-registering. Just allow it to re-register.
         */
        if (pid_table_real_pid_exists(msg->real_pid)) {
                xnd_trace("Process %d re-connecting\n", msg->real_pid);
                p = proc_list_find_by_real_pid(proc_list, msg->real_pid);
                close(p->fd);
                p->fd = fd;
                coord_send_handshake(p);
                return;
        }
        
        p = malloc(sizeof(struct proc));
        xnd_assert(p != NULL);
        
        p->fd = fd;
        p->oob_fd = -1;
        p->real_pid = msg->real_pid;
        p->real_ppid = msg->real_ppid;
        
        /* Should be initial startup (epoch == 0) */
        xnd_assert(coord_info.epoch == 0);
        
        if (pid_table_real_pid_exists(p->real_ppid)) {
                parent = proc_list_find_by_real_pid(proc_list, p->real_ppid);
                xnd_assert(parent != NULL);
                p->virt_ppid = parent->virt_pid;
                p->xnd_ppid = parent->xnd_pid;
                p->xnd_pgid = parent->xnd_pgid;
                p->virt_pid = coord_next_virt_pid();
                p->xnd_pid = coord_next_xnd_pid(); 
        } else {
                p->virt_ppid = coord_next_virt_pid();
                p->virt_pid = coord_next_virt_pid();
                p->xnd_ppid = coord_next_xnd_pid(); 
                p->xnd_pid = coord_next_xnd_pid();
                p->xnd_pgid = p->xnd_pid;
        }
        
        coord_send_handshake(p);
        p->state = PROC_RUNNING;
        p->cleanup = proc_exit_callback;

        pid_table_update(p->virt_pid, p->real_pid);
        pid_table_update(p->virt_ppid, p->real_ppid);
        proc_list_add(proc_list, p);
}

void coord_connect_with_process_on_restart(int fd, struct xnd_msg *msg)
{
        struct proc *p;

        p = malloc(sizeof(struct proc));
        xnd_assert(p != NULL);
        p->fd = fd;
        p->oob_fd = -1;

        p->real_pid = msg->real_pid;
        p->virt_pid = msg->virt_pid;
        p->real_ppid = msg->real_ppid;
        p->virt_ppid = msg->virt_ppid;

        p->xnd_pid = msg->xnd_pid;
        p->xnd_ppid = msg->xnd_ppid;
        p->xnd_pgid = msg->xnd_pgid;

        if (p->virt_pid + 1 > coord_info.next_virt_pid) {
                coord_info.next_virt_pid = p->virt_pid + 1;
        }
        
        if (p->xnd_pid + 1 > coord_info.next_xnd_pid) {
                coord_info.next_xnd_pid = p->xnd_pid + 1;
        }
        
        coord_send_handshake(p);
        p->state = PROC_RESTART_IN_PROGRESS;
        p->cleanup = proc_exit_callback;

        pid_table_update(p->virt_pid, p->real_pid);
        pid_table_update(p->virt_ppid, p->real_ppid);
        proc_list_add(proc_list, p);
}

void coord_send_handshake(struct proc *p)
{
        struct xnd_msg msg;

        msg.hdr = XND_COORD_ACK;
        msg.ret = XND_SUCCESS;

        msg.real_pid = p->real_pid;
        msg.virt_pid = p->virt_pid;
        msg.real_ppid = p->real_ppid;
        msg.virt_ppid = p->virt_ppid;

        memcpy(msg.xnd_uuid, coord_info.xnd_uuid, sizeof(uuid_t));
        msg.xnd_pid = p->xnd_pid;
        msg.xnd_ppid = p->xnd_ppid;
        msg.xnd_pgid = p->xnd_pgid;

        if (coord_send_msg(p->fd, &msg) != 0) {
                xnd_trace("Process %d failed to connect\n", p->real_pid);
                coord_exit(COORD_EXIT_FAILURE);
        }

        xnd_trace("Process %d connected to coordinator\n", p->real_pid);
}

void coord_write_ckpt_manifest(void)
{
        u32             max_xnd_pid = 0, min_xnd_pid = UINT32_MAX;
        struct proc     *p;
        int             err;
        
        proc_foreach(p, proc_list) {
                max_xnd_pid = max(p->xnd_pid, max_xnd_pid);
                min_xnd_pid = min(p->xnd_pid, min_xnd_pid);
        }

        err = xnd_ckptfile_write_manifest(coord_info.num_peers, 
                                          min_xnd_pid, max_xnd_pid,
                                          coord_info.xnd_uuid, 
                                          coord_info.epoch);
        if (err != 0) {
                xnd_error("Failed to write checkpoint manifest\n");
                coord_exit(COORD_EXIT_FAILURE);
        }
}

void coord_determine_roots(void)
{
        struct proc *p, *parent;

        proc_foreach(p, proc_list) {
                parent = proc_list_find_by_real_pid(proc_list, p->real_ppid);
                if (parent) {
                        p->is_root_of_tree = false;
                } else {
                        p->is_root_of_tree = true;
                }
        }
}

void coord_suspend_processes(void)
{
        struct proc     *p, *next;
        struct xnd_msg  resp, msg = { .hdr = XND_CKPT_REQUEST };
        int             err, total, ready;
        
        do {
                total = 0;
                ready = 0;
                proc_foreach_safe(p, next, proc_list) {
                        switch (p->state) {
                        case PROC_RUNNING: {
                                err = coord_send_msg(p->fd, &msg);
                                if (err == 0) {
                                        p->state = PROC_RECV_CKPT_REQUEST;
                                }
                                break;
                        }
                        case PROC_RECV_CKPT_REQUEST: {
                                err = coord_recv_msg(p->fd, &resp);
                                if (err == 0 && resp.hdr == XND_CKPT_READY) {
                                        p->state = PROC_READY_FOR_CKPT;
                                        ready++;
                                }
                                break;
                        }
                        case PROC_READY_FOR_CKPT: {
                                ready++;
                                break;
                        }
                        default:
                                break;
                        }
                        
                        err = kill(p->real_pid, 0);
                        if (err != 0 && errno == ESRCH) {
                                proc_list_remove(proc_list, p);
                                continue;
                        }
                        total++;
                }

                if (ready != total) {
                        usleep(100);
                }
        } while (ready != total);
}

void coord_wait_for_ckpt_completions(void)
{
        int             err, total;
        struct proc     *p;
        struct xnd_msg  msg;

        total = coord_collective_prepare(COMM_REDUCE);
        if (unlikely(total != coord_info.num_peers)) {
                xnd_error("  Pending checkpoint done messages: %d\n"
                          "  Expected checkpoint participants: %d\n",
                          total, coord_info.num_peers);
                coord_exit(COORD_EXIT_FAILURE);
        }
        
        total = 0;
        proc_foreach(p, proc_list) {
                xnd_assert(p->state == PROC_CKPT_IN_PROGRESS);
                err = coord_recv_msg(p->fd, &msg);
                if (err == 0 && msg.hdr == XND_CKPT_DONE) {
                        p->state = PROC_CKPT_COMPLETE;
                        total++;
                }
        }

        if (unlikely(total != coord_info.num_peers)) {
                xnd_error("     Total checkpoint completions: %d\n"
                          "  Expected checkpoint completions: %d\n",
                          total, coord_info.num_peers);
                coord_exit(COORD_EXIT_FAILURE);
        }
}

void coord_do_checkpoint(void)
{
        int             err, total;
        sigset_t        set;
        u64             cur_epoch = coord_info.epoch;
        
        TIMER_PUSH(Checkpoint);
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGQUIT);
        sigaddset(&set, SIGTERM);
        sigprocmask(SIG_BLOCK, &set, NULL);
        
        /**
         * Wait until each process's checkpoint thread is ready to receive
         * the checkpoint request on the read end of the socket. Then,
         * broadcast XND_CKPT_REQUEST and receive XND_CKPT_READY individually
         * from each process.
         *
         * Now, each process will enter a barrier until the coordinator
         * allows them to complete their individual checkpoints (suspended)
         */
        total = coord_collective_prepare(COMM_BROADCAST);
        if (unlikely(total == 0))
                return;
        coord_suspend_processes();

        /**
         * Now that each process is suspended, create the checkpoint
         * directory, determine which processes are roots of their process
         * trees, and determine the number of total peers in the computation.
         */
        err = xnd_ckptdir_create(coord_info.xnd_uuid, cur_epoch);
        if (err != 0) {
                xnd_error("Failed to create checkpoint directory\n");
                goto out;
        }
        
        coord_info.num_peers = proc_list->size;
        coord_determine_roots();
        
        /**
         * Broadcast XND_CKPT_START to each process
         * (releasing pre-checkpoint barrier)
         */
        total = coord_collective_prepare(COMM_BROADCAST);
        xnd_assert(total == coord_info.num_peers);
        xnd_assert(coord_release_barrier(COORD_BARRIER_PRECKPT) == 0);
        
        /**
         * Wait for every process to finish their checkpoint (and send
         * XND_CKPT_DONE).
         */
        coord_wait_for_ckpt_completions();
        coord_write_ckpt_manifest();
        
        /**
         * All checkpoints are complete (each process is currently blocked
         * in another coordinator barrier). 
         * 
         * Now, broadcast XND_RESUME_AFTER_CKPT to allow each process
         * to continue.
         */
        xnd_assert(coord_release_barrier(COORD_BARRIER_POSTCKPT) == 0);

        /**
         * Remove previous checkpoint directory
         *  (consider making this configurable)
         */
        if (cur_epoch > 0)
                xnd_ckptdir_unlink(coord_info.xnd_uuid, cur_epoch - 1);

        coord_info.epoch++;
        TIMER_POP();
out:
        sigprocmask(SIG_UNBLOCK, &set, NULL);
}

void coord_send_virt_to_real(int fd, struct xnd_msg *msg)
{
        pid_t           real, virt = msg->virt_pid;
        struct xnd_msg  resp = { .hdr = XND_COORD_ACK, .ret = XND_SUCCESS };

        if ((real = pid_table_virtual_to_real(virt)) != -1) {
                goto found;
        }

        if ((real = proc_list_virt_to_real(proc_list, virt)) != -1) {
                goto found;
        }

        resp.ret = XND_FAILURE;
        resp.real_pid = -1;
        xnd_assert(coord_send_msg(fd, &resp) == 0);
        return;

found:
        resp.real_pid = real;
        xnd_assert(coord_send_msg(fd, &resp) == 0);
}

void coord_send_real_to_virt(int fd, struct xnd_msg *msg)
{
        pid_t           virt, real = msg->real_pid;
        struct xnd_msg  resp = { .hdr = XND_COORD_ACK, .ret = XND_SUCCESS };

        if ((virt = pid_table_real_to_virtual(real)) != -1) {
                goto found;
        }

        if ((virt = proc_list_real_to_virt(proc_list, real)) != -1) {
                goto found;
        }

        resp.ret = XND_FAILURE;
        resp.virt_pid = -1;
        xnd_assert(coord_send_msg(fd, &resp) == 0);
        return;

found:
        resp.virt_pid = virt;
        xnd_assert(coord_send_msg(fd, &resp) == 0);
}

void coord_do_restart(void)
{
        struct xnd_msg  msg;
        int             fd, ready;

        TIMER_PUSH(Restart);
        for (;;) {
                fd = accept(coord_info.listen_fd, NULL, NULL);
                if (fd < 0) {
                        xnd_error("accept: %s\n", strerror(errno));
                        coord_exit(COORD_EXIT_FAILURE);
                }

                xnd_assert(coord_recv_msg(fd, &msg) == 0);
                xnd_assert(msg.hdr == XND_CONNECT_RESTART);

                if (proc_list->size == 0) {
                        coord_info.ckpt_interval = msg.ckpt_interval;
                        coord_info.epoch = msg.epoch;
                        coord_info.num_peers = msg.num_peers;
                        memcpy(coord_info.xnd_uuid, msg.xnd_uuid,
                               sizeof(uuid_t));
                }

                coord_connect_with_process_on_restart(fd, &msg);
                if (proc_list->size == coord_info.num_peers) {
                        break;
                }
        }

        ready = coord_collective_prepare(COMM_BROADCAST);
        xnd_assert(ready == coord_info.num_peers);
        xnd_assert(coord_release_barrier(COORD_BARRIER_POSTRESTART) == 0);
        TIMER_POP();
}

bool coord_is_restart(int argc, char **argv)
{
        if (argc < 2 || strcmp(argv[1], XND_COORD_RESTART_FLAG))
                return false;

        return true;
}

void coord_atfork(int fd, struct xnd_msg *parent_msg)
{
        struct xnd_msg  resp, child_msg;
        struct proc     *child, *parent;
        int             err;

        child = malloc(sizeof(struct proc));
        xnd_assert(child != NULL);

        /**
         * Reserve virtual pid and xnd_pid for child.
         * First, send child's virtual pid to parent.
         * Then, wait for the child to connect and initiate
         * handshake/registration with the child process.
         */
        child->fd = fd;
        child->oob_fd = -1;
        child->virt_pid = coord_next_virt_pid();
        child->xnd_pid = coord_next_xnd_pid();

        resp.hdr = XND_COORD_ACK;
        resp.ret = XND_SUCCESS;
        resp.virt_pid = child->virt_pid;

        err = coord_send_msg(fd, &resp);
        if (err != 0) {
                xnd_error("Failed to send virtual pid to parent\n");
                coord_exit(COORD_EXIT_FAILURE);
        }

        /**
         * Parent (in coord_client_atfork_prepare) should have sent their
         * own virtual pid and xnd_pid (child's virt_ppid and xnd_ppid)
         */
        child->virt_ppid = parent_msg->virt_ppid;
        child->xnd_ppid = parent_msg->xnd_ppid;
        child->xnd_pgid = parent_msg->xnd_pgid;

        parent = proc_list_find_by_virt_pid(proc_list, child->virt_ppid);
        if (!parent) {
                xnd_error("Parent not in process list (virtual pid: %d)\n",
                          child->virt_ppid);
                coord_exit(COORD_EXIT_FAILURE);
        }

        /* Wait for child to connect (and do handshake) */
        err = coord_recv_msg(fd, &child_msg);
        if (err != 0) {
                xnd_error("Failed to receive child's message "
                          "(virtual pid: %d)\n", child->virt_pid);
                coord_exit(COORD_EXIT_FAILURE);
        }

        xnd_assert(child_msg.hdr == XND_ATFORK_CHILD);
        child->real_pid = child_msg.real_pid;
        child->real_ppid = child_msg.real_ppid;
        xnd_assert(child->real_ppid == parent->real_pid);

        coord_send_handshake(child);

        child->state = PROC_RUNNING;
        child->cleanup = proc_exit_callback;
        pid_table_update(child->virt_pid, child->real_pid);
        pid_table_update(child->virt_ppid, child->real_ppid);
        proc_list_add(proc_list, child);
}

int main(int argc, char *argv[])
{
        coord_init();

        if (coord_is_restart(argc, argv))
                coord_do_restart();

        coord_work();
        coord_exit(COORD_EXIT_SUCCESS);
}
