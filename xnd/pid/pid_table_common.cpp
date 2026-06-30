/* pid_table_common.cpp */
#include "xnd/xnd.h"
#include "xnd/virtual_id_table.h"
#include "pid_table_common.h"

using namespace xnd;

virtual_id_table<pid_t> *pid_table = nullptr;

extern "C" void pid_table_init(void)
{
        pid_table = &virtual_id_table<pid_t>::instance();
}

extern "C" void pid_table_acquire(void)
{
        pid_table->acquire();
}

extern "C" void pid_table_release(void)
{
        pid_table->release();
}

extern "C" void pid_table_update(pid_t virt, pid_t real)
{
        pid_table->update(virt, real);
}

extern "C" void pid_table_erase(pid_t virt)
{
        pid_table->erase(virt);
}

extern "C" size_t pid_table_size(void)
{
        return pid_table->size();
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
        pid_t real;

        if (pid_table->virtual_to_real(virt, real)) {
                return real;
        }

        return -1;
}

extern "C" pid_t pid_table_real_to_virtual(pid_t real)
{
        pid_t virt;

        if (pid_table->real_to_virtual(real, virt)) {
                return virt;
        }

        return -1;
}
