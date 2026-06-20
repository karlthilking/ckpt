/* pid_table.h */
#ifndef VIRTUAL_PID_TABLE_H
#define VIRTUAL_PID_TABLE_H

#include "xnd/xnd.h"
#include <sys/types.h>
#include <pthread.h>

#define VIRTUAL_PID_INIT        1
#define MAX_PIDS                512

struct pid_table {
        pid_t           table[MAX_PIDS];
        u8              bitmap[MAX_PIDS / BITS_PER_BYTE];
        pthread_mutex_t lock;
        
        pid_t           next;
        pid_t           _real_pid;
        pid_t           _virt_pid;
        pid_t           _real_ppid;
        pid_t           _virt_ppid;
};

void pid_table_init(void);
void pid_table_destroy(void);
void pid_table_reset(void);
void pid_table_refresh(void);
void pid_table_update(pid_t, pid_t);
void pid_table_erase(pid_t);
pid_t pid_table_add(pid_t);

void pid_table_postrestart(void);
void pid_table_atfork_prepare(pid_t);
void pid_table_atfork_child(void);
void pid_table_atfork_parent(pid_t, pid_t);
void pid_table_atfork_failed(void);

bool pid_table_virtual_pid_exists(pid_t);
bool pid_table_real_pid_exists(pid_t);
pid_t pid_table_virtual_to_real(pid_t);
pid_t pid_table_real_to_virtual(pid_t);
pid_t pid_table_next_virtual(void);

pid_t pid_table_getpid(void);
pid_t pid_table_getppid(void);

#endif /* VIRTUAL_PID_TABLE_H */
