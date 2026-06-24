/* xnd_coord.h */
#ifndef XND_COORD_H
#define XND_COORD_H

#include "xnd/xnd.h"
#include "xnd/coordinator/xnd_coord_api.h"

#include <sys/types.h>

#define COORD_MAX_PROC          256
#define COORD_EXIT_SUCCESS      0
#define COORD_EXIT_FAILURE      222

enum coord_state {
        COORD_NULL,
        COORD_RUNNING,
        COORD_PRECKPT,
        COORD_CKPTINPROG,
        COORD_POSTCKPT,
        COORD_RESTARTINPROG,
        COORD_EXITING
};

enum coord_comm_type {
        COMM_BROADCAST,
        COMM_REDUCE
};

enum proc_state {
        P_RUNNING,
        P_CKPTRECV,
        P_CKPTRDY,
        P_CKPTINPROG,
        P_CKPTDONE,
        P_EXITED
};

struct proc {
        int             fd;
        u32             xnd_pid;
        u32             xnd_ppid;
        u32             xnd_pgid;

        pid_t           virt_pid;
        pid_t           virt_ppid;
        pid_t           real_pid;
        pid_t           real_ppid;

        u32             num_peers;
        bool            is_root_of_tree;
        enum proc_state state;

        struct proc     *next;
        struct proc     *prev;
};

struct proc_list {
        struct proc     *head;
        size_t          size;
};

void proc_list_init(void);
void proc_list_destroy(void);
void proc_list_add(struct proc *);
void proc_list_remove(struct proc *);
struct proc *proc_list_find_real(pid_t);
struct proc *proc_list_find_virt(pid_t);

void coord_init(void);
void coord_cleanup(void);
void coord_exit(int);
void coord_setup_handler(int);
void coord_handler(int);
void coord_event_loop(void);
void coord_check_status(void);

void coord_await_connection(void);
void coord_register_process(int, struct xnd_msg *);

void coord_handle_command(struct xnd_msg *);

void coord_broadcast_exit(void);
void coord_kill_processes(void);

bool coord_do_checkpoint(void);

int coord_multicast_prepare(enum coord_comm_type, enum proc_state);
int coord_collective_prepare(enum coord_comm_type);

int coord_broadcast(struct xnd_msg *, enum proc_state, enum proc_state);
int coord_reduce(enum xnd_msghdr, enum proc_state, enum proc_state);

void coord_write_msg_metainfo(struct proc *, struct xnd_msg *);

void coord_await_msg(void);
void coord_handle_proc_msg(struct proc *);
void coord_send_virt_to_real(struct proc *, struct xnd_msg *);
void coord_send_real_to_virt(struct proc *, struct xnd_msg *);

#endif /* XND_COORD_H */
