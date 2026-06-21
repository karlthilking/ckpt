/* pid_table.cpp */
#include "xnd/xnd.h"
#include "xnd/virtual_id_table.h"
#include "xnd/pid/pid.h"
#include "xnd/pid/pid_table.h"
#include <sys/types.h>

using namespace xnd;

static pid_t _virt_pid  = -1;
static pid_t _virt_ppid = -1;
static pid_t _real_pid  = -1;
static pid_t _real_ppid = -1;

virtual_id_table<pid_t> *pid_table = nullptr;

extern "C" void pid_table_init(void)
{
        char *virt_pid_str, *virt_ppid_str;

        pid_table = new virtual_id_table<pid_t>();
        pid_table->acquire();

        _real_pid = _real_getpid();
        _real_ppid = _real_getppid();
        /**
         * TODO:
         * Get virtual pid and ppid from coordinator
         * pid_table->update(_virt_pid, _real_pid)
         * pid_table->update(_virt_pppid, _real_ppid)
         */
        virt_pid_str = getenv("XND_VIRT_PID");
        virt_ppid_str = getenv("XND_VIRT_PPID");
        xnd_assert(virt_pid_str && virt_ppid_str);

        _virt_pid = (pid_t)atoi(virt_pid_str);
        _virt_ppid = (pid_t)atoi(virt_ppid_str);

        pid_table->update(_virt_pid, _real_pid);
        pid_table->update(_virt_ppid, _real_ppid);

        pid_table->release();
}

extern "C" void pid_table_destroy(void)
{
        delete pid_table;
}

extern "C" void pid_table_acquire(void)
{
        pid_table->acquire();
}

extern "C" void pid_table_release(void)
{
        pid_table->release();
}

extern "C" void pid_table_reset(void)
{
        pid_table->acquire();
        pid_table->reset();
        pid_table->release();
}

extern "C" void pid_table_update(pid_t virt, pid_t real)
{
        pid_table->acquire();
        pid_table->update(virt, real);
        pid_table->release();
}

extern "C" void pid_table_erase(pid_t virt)
{
        pid_table->acquire();
        pid_table->erase(virt);
        pid_table->release();
}

extern "C" size_t pid_table_size(void)
{
        size_t size;

        pid_table->acquire();
        size = pid_table->size();
        pid_table->release();

        return size;
}

extern "C" void pid_table_postrestart(void)
{
        /**
         * TODO:
         * Get new real pids from coordinator (or maybe do it lazily
         * instead of right after restart).
         */
        _real_pid = _real_getpid();
        _real_ppid = _real_getppid();

        pid_table->update(_virt_pid, _real_pid);
        pid_table->update(_virt_ppid, _real_ppid);
}

extern "C" void pid_table_atfork_prepare(void)
{
        pid_table->acquire();
}

/**
 * pid_table_atfork_child:
 *  Child resets its pid table and initializes its real pid and ppid.
 */
extern "C" void pid_table_atfork_child(void)
{
        pid_table->atfork_child();
        pid_table->acquire();

        _real_pid = _real_getpid();
        _real_ppid = _real_getppid();

        /**
         * TODO: get virtual pid and ppid from coordinator
         * pid_table->update(_virt_pid, _real_pid);
         * pid_table->update(_virt_ppid, _real_ppid);
         */

        pid_table->release();
}

extern "C" void pid_table_atfork_parent(void)
{
        /**
         * TODO:
         *  pid_table->update(virtual_child_pid, real_child_pid)
         */
        pid_table->release();
}

extern "C" void pid_table_atfork_failed(void)
{
        pid_table->release();
}

extern "C" bool pid_table_virtual_pid_exists(pid_t virt)
{
        return pid_table->virtual_id_exists(virt);
}

extern "C" bool pid_table_real_pid_exists(pid_t real)
{
        return pid_table->real_id_exists(real);
}

extern "C" pid_t pid_table_virtual_to_real(pid_t virt)
{
        pid_t   real;
        bool    found;

        found = pid_table->virtual_to_real(virt, real);
        if (found) {
                return real;
        }

        /**
         * TODO:
         */
        return -1;
}

extern "C" pid_t pid_table_real_to_virtual(pid_t real)
{
        pid_t   virt;
        bool    found;

        found = pid_table->real_to_virtual(real, virt);
        if (found) {
                return virt;
        }

        /**
         * TODO: Try to consult coordinator if virt->real mapping has
         * changed after a restart and update mapping
         */
        return -1;
}

extern "C" pid_t pid_table_getpid(void)
{
        if (_virt_pid == -1) {
                return _real_getpid();
        }

        return _virt_pid;
}

extern "C" pid_t pid_table_getppid(void)
{
        if (_virt_ppid == -1) {
                /* Before static initializers have ran? */
                return _real_getppid();
        }

        return _virt_ppid;
}
