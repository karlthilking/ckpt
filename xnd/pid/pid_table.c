/* pid_table.c */
#include "xnd/xnd.h"
#include "xnd/pid/pid.h"
#include "xnd/pid/pid_table.h"
#include <sys/types.h>

static struct pid_table pid_table = {
        ._real_pid      = -1,
        ._virt_pid      = -1,
        ._real_ppid     = -1,
        ._virt_ppid     = -1
}, *p = &pid_table;

void pid_table_init(void)
{
        pthread_mutex_init(&p->lock, NULL);
        pthread_mutex_lock(&p->lock);

        p->_real_pid = _real_getpid();
        p->_real_ppid = _real_getppid();
        p->_virt_pid = p->_real_pid;
        p->_virt_ppid = p->_real_pid + 1;

        p->table[PID_TABLE_PID_SLOT] = p->_real_pid;
        p->table[PID_TABLE_PPID_SLOT] = p->_real_ppid;
        set_bit(PID_TABLE_PID_SLOT, p->bitmap);
        set_bit(PID_TABLE_PPID_SLOT, p->bitmap);
        
        p->next = p->_virt_ppid + 1;
        p->base = p->_real_pid;

        pthread_mutex_unlock(&p->lock);
}

void pid_table_destroy(void)
{
        pthread_mutex_destroy(&p->lock);
}

void pid_table_reset(void)
{
        size_t size;

        pthread_mutex_lock(&p->lock);
        
        /**
         * leave p->table[0] and p->table[1] reserved for this process's
         * pid and ppid mappings
         */
        size = (array_len(a) - 2) * sizeof(p->bitmap[0]);
        bzero(p->bitmap + 2, size);
        p->next = p->base + 2;

        pthread_mutex_unlock(&p->lock);
}

/**
 * pid_table_update:
 *  Update virtual pid to real pid mapping, lock should be held by the
 *  caller of pid_table_update.
 */
void pid_table_update(pid_t virt, pid_t real)
{
        xnd_assert(test_bit(virt - p->base, p->bitmap));
        p->table[virt - p->base] = real;
}

/**
 * pid_table_add:
 *  Let the next available virtual pid map to the new real
 *  pid which is not already in the table. Lock should be
 *  held by the caller of pid_table_add.
 */
pid_t pid_table_add(pid_t real)
{
        pid_t   virt;
        uint    idx;

        virt = pid_table_next_virtual();
        idx = virt - p->base;
        p->table[idx] = real;
        set_bit(idx, p->bitmap);
        
        return virt;
}

/**
 * pid_table_postrestart:
 *  Refresh real pid and ppid. Exchange new pid with other processes and
 *  update mappings with new pids of other processes.
 */
void pid_table_postrestart(void)
{
        pthread_mutex_lock(&p->lock);
        
        p->_real_pid = _real_getpid();
        p->_real_ppid = _real_getppid();
        p->table[PID_TABLE_PID_SLOT] = p->_real_pid;
        p->table[PID_TABLE_PPID_SLOT] = p->_real_ppid;

        /**
         * TODO: coordinate with other processes and update virtual to
         * real pid mappings
         */

        pthread_mutex_unlock(&p->lock);
}

void pid_table_atfork(void)
{
        /**
         * TODO:
         */
}

void pid_table_postfork_parent(pid_t child)
{
        /* TODO */
}

void pid_table_postfork_child(void)
{
        /* TODO */
}

bool pid_table_virtual_pid_exists(pid_t virt)
{
        bool            present;
        const uint      idx = virt - p->base;

        if (unlikely(idx >= MAX_PIDS)) {
                return false;
        }
        
        pthread_mutex_lock(&p->lock);
        present = test_bit(idx, p->bitmap);
        pthread_mutex_unlock(&p->lock);

        return present;
}

bool pid_table_real_pid_exists(pid_t real)
{
        bool present = false;

        pthread_mutex_lock(&p->lock);
        for (uint idx = 0; idx < p->next; idx++) {
                if (p->table[idx] != real) {
                        continue;
                }
                if (test_bit(idx, p->bitmap)) {
                        present = true;
                        break;
                }
        }
        pthread_mutex_unlock(&p->lock);

        return present;
}

pid_t pid_table_virtual_to_real(pid_t virt)
{
        pid_t           real = -1;
        const uint      idx = virt - p->base;

        if (unlikely(idx >= MAX_PIDS)) {
                return -1;
        }

        pthread_mutex_lock(&p->lock);
        if (test_bit(idx, p->bitmap)) {
                real = p->bitmap[idx];
        }
        pthread_mutex_unlock(&p->lock);

        return real;
}

pid_t pid_table_real_to_virtual(pid_t real)
{
        pid_t virt = -1;

        pthread_mutex_lock(&p->lock);
        for (uint idx = 0; idx < p->next; idx++) {
                if (p->table[idx] != real) {
                        continue;
                }
                if (test_bit(idx, p->bitmap)) {
                        virt = p->base + idx;
                        break;
                }
        }
        
        if (unlikely(virt == -1)) {
                virt = pid_table_add(real);
        }
        pthread_mutex_unlock(&p->lock);

        return virt;
}

/**
 * pid_table_next_virtual:
 *  Get the next virtual id that is not currently in use. The pid table
 *  mutex should be held by the caller of pid_table_next_virtual.
 */
pid_t pid_table_next_virtual(void)
{
        pid_t   virt;
        uint    idx;
        
        for (;;) {
                virt = p->next++;
                idx = virt - p->base;
                if (unlikely(idx >= MAX_PIDS)) {
                        xnd_error("pid table size exceeded!\n");
                        xnd_abort();
                }
                if (test_bit(idx, p->bitmap)) {
                        continue;
                }
                break;
        }

        return virt;
}

pid_t pid_table_getpid(void)
{
        if (unlikely(p->_virt_pid == -1)) {
                return _real_getpid();
        }
        
        return p->_virt_pid;
}

pid_t pid_table_getppid(void)
{
        if (unlikely(p->_virt_ppid == -1)) {
                /**
                 * p->_virt_ppid == -1 iff pid_table_init hasn't executed yet
                 * (called by xnd_setup constructor). i.e. this call is
                 * intercepting dyld during early init so return
                 * _real_getpid + 1 (the value _virt_ppid will be initialized
                 * to after pid_table_init runs).
                 */
                return _real_getppid();
        }

        return p->_virt_ppid;
}
