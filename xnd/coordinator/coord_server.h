/* coord_server.h */
#ifndef XND_COORD_SERVER_H
#define XND_COORD_SERVER_H

#include "xnd/xnd.h"
#include "xnd/coordinator/coord_common.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#define MAX_PROC 128 

enum proc_state {
        XND_PROC_EMBRYO,
        XND_PROC_RUNNING,
        XND_PROC_EXITED,
        XND_PROC_ZOMBIE
};

struct coord_info {
        int                     fd;
        struct sockaddr_un      addr;
        pid_t                   next_virtual_pid;
};

struct proc_info {
        int                     fd;
        pid_t                   virt_pid;
        pid_t                   virt_ppid;
        pid_t                   real_pid;
        pid_t                   real_ppid;
        bool                    root;
        enum proc_state         state;
};

struct proc_list {
        struct proc_info        *list;
        size_t                  size;
        size_t                  capacity;
};

void proc_list_init(void);
void proc_list_destroy(void);
void proc_list_resize(size_t);
struct proc_info *proc_list_find(pid_t);

void proc_init(int);
void proc_exited(struct proc_info *);

void coord_init(void);
void coord_exit(void);
void coord_event_loop(void);

void coord_await_connection(void);
void coord_await_msg(void);
void coord_recv_msg(int);

void coord_recv_pid_info(struct proc_info *p);
void coord_send_pid_info(struct proc_info *p);

pid_t coord_next_virtual_pid(void);

#endif /* XND_COORD_SERVER_H */
