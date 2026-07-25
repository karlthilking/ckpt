/* xnd_coord.h */
#ifndef XND_COORD_H
#define XND_COORD_H

#include "xnd/xnd.h"
#include "proc_list.h"
#include "xnd_coord_api.h"

#include <unistd.h>
#include <sys/types.h>

#ifndef NSEC_PER_USEC
# define NSEC_PER_USEC (1000L)
#endif

#define INITIAL_VIRT_PID        1
#define INITIAL_XND_PID         1000

#define COORD_MAX_PROC          256
#define COORD_EXIT_SUCCESS      0
#define COORD_EXIT_FAILURE      222

#define COORD_TIMEOUT_USEC      10000
#define COORD_TIMEOUT_NSEC      COORD_TIMEOUT_USEC * NSEC_PER_USEC

#define COORD_HEARTBEAT_INTERVAL (16)
#define COORD_CHECK_HEARTBEAT(iteration) \
        ((iteration) % COORD_HEARTBEAT_INTERVAL == 0)

struct coord_info {
        uuid_t  xnd_uuid;
        u64     epoch;
        pid_t   next_virt_pid;
        u32     next_xnd_pid;
        u32     num_peers;
        int     listen_fd;
        int     ckpt_interval;
};

enum coord_comm_type {
        COMM_BROADCAST,
        COMM_REDUCE
};

void proc_exit_callback(struct proc *);

void coord_work(void);
void coord_init(void);
void coord_cleanup(void);
void coord_exit(int);

void coord_setup_handler(int);
void coord_handler(int);
void coord_broadcast_kill(void);
void coord_handle_command(int, struct xnd_msg *);
void coord_handle_msg(int, struct xnd_msg *);

int coord_send_msg(int, struct xnd_msg *);
int coord_recv_msg(int, struct xnd_msg *);
int coord_release_barrier(enum coord_barrier_type type);
int coord_collective_prepare(enum coord_comm_type);

void coord_wait_for_msg(void);
void coord_wait_for_connection(void);
void coord_connect_with_process_on_launch(int, struct xnd_msg *);
void coord_connect_with_process_on_restart(int, struct xnd_msg *);
void coord_send_handshake(struct proc *);

void coord_write_ckpt_manifest(void);
void coord_determine_roots(void);
void coord_suspend_processes(void);
void coord_wait_for_ckpt_completions(void);
void coord_do_checkpoint(void);

void coord_send_virt_to_real(int, struct xnd_msg *);
void coord_send_real_to_virt(int, struct xnd_msg *);

void coord_do_restart(void);
bool coord_is_restart(int, char **);

void coord_atfork(int, struct xnd_msg *);

#endif /* XND_COORD_H */
