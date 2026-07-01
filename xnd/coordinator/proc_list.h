/* proc_list.h */
#ifndef PROC_LIST_H
#define PROC_LIST_H

#include "xnd/xnd.h"
#include <sys/types.h>

#define proc_foreach(p, list) \
        for ((p) = (list)->head; (p) != NULL; (p) = (p)->next)

#define proc_foreach_safe(p, next, list)        \
        for ((p) = (list)->head,                \
             (next) = (p) ? (p)->next : NULL;   \
             (p) != NULL;                       \
             (p) = (next),                      \
             (next) = (p) ? (p)->next : NULL)   \

enum proc_state {
        PROC_RUNNING,
        PROC_RECV_CKPT_REQUEST,
        PROC_READY_FOR_CKPT,
        PROC_CKPT_IN_PROGRESS,
        PROC_CKPT_COMPLETE,
        PROC_RESTART_IN_PROGRESS,
        PROC_EXITED
};

struct proc {
        int             fd;
        int             oob_fd;
        u32             xnd_pid;
        u32             xnd_ppid;
        u32             xnd_pgid;

        pid_t           virt_pid;
        pid_t           real_pid;
        pid_t           virt_ppid;
        pid_t           real_ppid;

        u32             num_peers;
        bool            is_root_of_tree;
        enum proc_state state;
        
        void            (*cleanup)(struct proc *);
        struct proc     *next;
        struct proc     *prev;
};

struct proc_list {
        struct proc     *head;
        size_t          size;
};

struct proc_list *proc_list_init(void);
void proc_list_destroy(struct proc_list *);
void proc_list_add(struct proc_list *, struct proc *);
void proc_list_remove(struct proc_list *, struct proc *);
void proc_list_filter(struct proc_list *);

struct proc *proc_list_find_by_real_pid(struct proc_list *, pid_t);
struct proc *proc_list_find_by_virt_pid(struct proc_list *, pid_t);
struct proc *proc_list_find_by_xnd_pid(struct proc_list *, u32);
pid_t proc_list_real_to_virt(struct proc_list *, pid_t);
pid_t proc_list_virt_to_real(struct proc_list *, pid_t);

#endif /* PROC_LIST_H */
