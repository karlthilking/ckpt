/* pid_table.cpp */
#include "xnd/xnd.h"
#include "xnd/virtual_id_table.h"
#include "xnd/coordinator/xnd_coord_api.h"
#include "xnd/coordinator/xnd_coord_client.h"
#include "pid.h"
#include "pid_table.h"
#include <sys/types.h>

using namespace xnd;

__hidden pid_t _virt_pid        = -1;
__hidden pid_t _real_pid        = -1;
__hidden pid_t _virt_ppid       = -1;
__hidden pid_t _real_ppid       = -1;

extern virtual_id_table<pid_t> *pid_table;

extern "C" void pid_table_init_pid_info(void)
{
        xnd_assert(pid_table != nullptr);
        xnd_assert(_virt_pid != -1 && _virt_ppid != -1);

        _real_pid = _real_getpid();
        _real_ppid = _real_getppid();
        
        pid_table->acquire();
        pid_table->update(_virt_pid, _real_pid);
        pid_table->update(_virt_ppid, _real_ppid);
        pid_table->release();
}

extern "C" void pid_table_postrestart(void)
{
        pid_t new_real;

        _real_pid = _real_getpid();
        _real_ppid = _real_getppid();

        pid_table->acquire();
        xnd_assert(_virt_pid != -1 && _virt_ppid != -1);
        pid_table->update(_virt_pid, _real_pid);
        pid_table->update(_virt_ppid, _real_ppid);
        
        /**
         * Update all virtual -> real mappings by asking coordinator
         * for new real pid.
         */
        auto &table = virtual_id_table<pid_t>::get();
        for (auto &[virt, real] : table) {
                if (virt == _virt_pid || virt == _virt_ppid)
                        continue;
                new_real = virt_to_real_pid_from_coord(virt);
                if (new_real == -1) {
                        xnd_warn("pid translation failed: virt=%d\n", virt);
                        pid_table->erase(virt);
                } else {
                        pid_table->update(virt, new_real);
                }
        }

        pid_table->release();
}

extern "C" void pid_table_atfork_prepare(void)
{
        pid_table->acquire();
}

extern "C" void pid_table_atfork_child(void)
{
        /**
         * Re-initialize pid table mutex and update virtual to real
         * mappings with virtual pid (received from coordinator).
         */
        pid_table->atfork_child();
        pid_table->acquire();

        _real_pid = _real_getpid();
        _real_ppid = _real_getppid();
        
        /**
         * Child should have received virtual pid and ppid by now
         * (and _virt_pid + _virt_ppid updated accordingly) via
         * coord_client_atfork_child -> send_recv_coord_handshake.
         */
        pid_table->update(_virt_pid, _real_pid);
        /**
         * virtual ppid -> real ppid should already exist in pid table
         * after fork
         */
        xnd_assert(pid_table->virtual_to_real(_virt_ppid) == _real_ppid);
        pid_table->release();
}

extern "C" void pid_table_atfork_parent(void)
{
        /**
         * pid_table_atfork_parent will execute before the parent resumes
         * execution in __fork_hook after fork(), so pid_table_atfork_parent
         * can not yet know the real pid of the child (via environment 
         * variable or otherwise). Therefore, it is the parent's 
         * responsibility to update the pid table within __fork_hook.
         */
        pid_table->release();
}

extern "C" void pid_table_atfork_failed(void)
{
        pid_table->release();
}
