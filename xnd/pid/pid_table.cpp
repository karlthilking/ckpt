/* pid_table.cpp */
#include "xnd/xnd.h"
#include "xnd/virtual_id_table.h"
#include "xnd/pid/pid.h"
#include "xnd/pid/pid_table.h"
#include "xnd/coordinator/xnd_coord_api.h"
#include <sys/types.h>

using namespace xnd;

__hidden pid_t _virt_pid  = -1;
__hidden pid_t _virt_ppid = -1;
__hidden pid_t _real_pid  = -1;
__hidden pid_t _real_ppid = -1;

virtual_id_table<pid_t> *pid_table = nullptr;

extern "C" void pid_table_init(void)
{
        pid_table = new virtual_id_table<pid_t>();
        pid_table->acquire();

        _real_pid = _real_getpid();
        _real_ppid = _real_getppid();

        /**
         * Coordinator sends selected virtual pid and ppid after
         * connection, then XND_VIRT_PID and XND_VIRT_PPID are set
         * to the pids select by the coordinator.
         */
        xnd_trace("_virt_pid=%d -> _real_pid=%d\n"
                  "_virt_ppid=%d -> _real_ppid=%d\n",
                  _virt_pid, _real_pid, _virt_ppid, _real_ppid);
        xnd_assert(_virt_pid != -1 && _virt_ppid != -1);

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
        pid_t new_real_pid;

        _real_pid = _real_getpid();
        _real_ppid = _real_getppid();
        
        pid_table->acquire();
        pid_table->update(_virt_pid, _real_pid);
        pid_table->update(_virt_ppid, _real_ppid);
        
        /**
         * Consult coordinator for new real pids of virtuable pids
         * stored in the table
         */
        auto table = pid_table->get();
        for (auto it = table.begin(); it != table.end(); ++it) {
                if (it->first == _virt_pid || it->first == _virt_ppid) {
                        continue;
                }
                new_real_pid = recv_virt_to_real_pid(it->first);
                if (new_real_pid == -1) {
                        xnd_error("Couldn't find new real pid for "
                                  "virtual pid %d\n", it->first);
                        continue;
                }
                it->second = new_real_pid;
        }

        pid_table->release();
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
        if (likely(found)) {
                return real;
        }
        
        real = recv_virt_to_real_pid(virt);
        if (real == -1) {
                xnd_error("Failed to find virtual to real pid "
                          "translation for virtual pid %d\n", virt);
                return -1;
        }

        return real;
}

extern "C" pid_t pid_table_real_to_virtual(pid_t real)
{
        pid_t   virt;
        bool    found;

        found = pid_table->real_to_virtual(real, virt);
        if (likely(found)) {
                return virt;
        }
        
        virt = recv_real_to_virt_pid(real);
        if (virt == -1) {
                xnd_error("Failed to find real to virtual pid"
                          "translation for real pid %d\n", real);
                return -1;
        }

        return virt;
}

extern "C" pid_t pid_table_getpid(void)
{
        // if (_virt_pid == -1) {
        //         return _real_getpid();
        // }

        // return _virt_pid;
        return _real_getpid();
}

extern "C" pid_t pid_table_getppid(void)
{
        // if (_virt_ppid == -1) {
        //         /* Before static initializers have ran? */
        //         return _real_getppid();
        // }

        // return _virt_ppid;
        return _real_getppid();
}
