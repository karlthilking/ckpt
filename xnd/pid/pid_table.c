/* pid_table.c */
#include "xnd/xnd.h"
#include "xnd/pid/pid.h"
#include "xnd/pid/pid_table.h"
#include <string.h>
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

        p->_virt_pid = VIRTUAL_PID_INIT;
        p->_virt_ppid = VIRTUAL_PID_INIT + 1;
        p->next = VIRTUAL_PID_INIT + 2;

        p->table[p->_virt_pid] = p->_real_pid;
        p->table[p->_virt_ppid] = p->_real_ppid;
        
        set_bit(p->_virt_pid, p->bitmap);
        set_bit(p->_virt_ppid, p->bitmap);

        pthread_mutex_unlock(&p->lock);
}

void pid_table_destroy(void)
{
        pthread_mutex_destroy(&p->lock);
}

void pid_table_acquire(void)
{
        xnd_assert(pthread_mutex_lock(&p->lock) == 0);
}

void pid_table_release(void)
{
        xnd_assert(pthread_mutex_unlock(&p->lock) == 0);
}

void pid_table_reset(void)
{
        bzero(p->bitmap, sizeof(p->bitmap));
        p->next = 0;
}

void pid_table_refresh(void)
{
        for (uint idx = p->next - 1; idx >= 0; idx--) {
                if (test_bit(idx, p->bitmap)) {
                        break;
                }
                p->next = idx;
        }
}

/**
 * pid_table_update:
 *  Update virtual pid to real pid mapping, lock should be held by the
 *  caller of pid_table_update.
 */
void pid_table_update(pid_t virt, pid_t real)
{
        p->table[virt] = real;
        set_bit(virt, p->bitmap);
}

void pid_table_erase(pid_t virt)
{
        pid_table_acquire();
        clear_bit(virt, p->bitmap);
        pid_table_release();
}

/**
 * pid_table_add:
 *  Let the next available virtual pid map to the new real
 *  pid which is not already in the table. Lock should be
 *  held by the caller of pid_table_add.
 */
pid_t pid_table_add(pid_t real)
{
        pid_t virt;

        virt = pid_table_next_virtual();
        p->table[virt] = real;
        set_bit(virt, p->bitmap);

        return virt;
}

/**
 * pid_table_postrestart:
 *  Refresh real pid and ppid. Send new virt_pid -> new_real_pid to
 *  coordinator.
 */
void pid_table_postrestart(void)
{
        pthread_mutex_lock(&p->lock);

        p->_real_pid = _real_getpid();
        p->_real_ppid = _real_getppid();
        p->table[p->_virt_pid] = p->_real_pid;
        p->table[p->_virt_ppid] = p->_real_ppid;
        
        /**
         * TODO:
         *  Send updated virt -> real mappings to coordinator so they
         *  can be discovered on demand.
         */

        pthread_mutex_unlock(&p->lock);
}

void pid_table_atfork_prepare(void)
{
        pthread_mutex_lock(&p->lock);
}

/**
 * pid_table_atfork_child:
 *  Child resets its pid table based on its real pid and ppid.
 *  The pid table mutex was acquired in pid_table_atfork_prepare, and
 *  should be released once the pid table is reset.
 */
void pid_table_atfork_child(pid_t virt_pid, pid_t virt_ppid)
{
        pid_table_reset();
        p->_virt_pid = virt_pid;
        p->_virt_ppid = virt_ppid;

        p->_real_pid = _real_getpid();
        p->_real_ppid = _real_getppid();
        p->table[p->_virt_pid] = p->_real_pid;
        p->table[p->_virt_ppid] = p->_real_ppid;

        set_bit(p->_virt_pid, p->bitmap);
        set_bit(p->_virt_ppid, p->bitmap);

        p->next = p->_virt_ppid + 1;
        pid_table_release();
        xnd_assert(pthread_mutex_init(&p->lock, NULL) == 0);
}

void pid_table_atfork_parent(pid_t virt_cpid, pid_t real_cpid)
{
        p->table[virt_cpid] = real_cpid;
        set_bit(virt_cpid, p->bitmap);
        pid_table_release();
}

void pid_table_atfork_failed(void)
{
        pid_table_refresh();
        pid_table_release();
}

bool pid_table_virtual_pid_exists(pid_t virt)
{
        bool present;

        if (unlikely(virt >= MAX_PIDS)) {
                return false;
        }

        pthread_mutex_lock(&p->lock);
        present = test_bit(virt, p->bitmap);
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
        pid_t real = -1;

        if (unlikely(virt >= MAX_PIDS)) {
                return -1;
        }

        pthread_mutex_lock(&p->lock);
        if (test_bit(virt, p->bitmap)) {
                real = p->table[virt];
        } else {
                /* TODO:
                real = get new real pid... (from coordinator presumably)
                pid_table_update(virt, real)
                */
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
                        virt = idx;
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
        pid_t virt;

        for (;;) {
                virt = p->next++;
                if (unlikely(virt >= MAX_PIDS)) {
                        xnd_error("pid table size exceeded!\n");
                        xnd_abort();
                }
                if (test_bit(virt, p->bitmap)) {
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
