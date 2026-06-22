/* virtual_id_table.h */
#ifndef VIRTUAL_ID_TABLE_H
#define VIRTUAL_ID_TABLE_H

#include "xnd/xnd.h"
#include <pthread.h>

#ifdef __cplusplus
#include <unordered_map>

namespace xnd {

template<typename ID>
class virtual_id_table {
private:
        std::unordered_map<ID, ID>      table;
        mutable pthread_mutex_t         mtx;
public:
        virtual_id_table(void) noexcept
        {
                pthread_mutex_init(&mtx, NULL);
        }

        ~virtual_id_table(void) noexcept
        {
                pthread_mutex_destroy(&mtx);
        }

        virtual_id_table(const virtual_id_table &) = delete;
        virtual_id_table &operator=(const virtual_id_table &) = delete;
        
        std::unordered_map<ID, ID> &get(void) noexcept
        {
                return table;
        }

        void acquire(void) const noexcept
        {
                xnd_assert(pthread_mutex_lock(&mtx) == 0);
        }

        void release(void) const noexcept
        {
                xnd_assert(pthread_mutex_unlock(&mtx) == 0);
        }

        void atfork_child(void) const noexcept
        {
                pthread_mutex_unlock(&mtx);
                pthread_mutex_init(&mtx, NULL);
        }

        void reset(void) noexcept
        {
                table.clear();
        }

        void update(ID virt, ID real) noexcept
        {
                table[virt] = real;
        }

        void erase(ID virt) noexcept
        {
                table.erase(virt);
        }

        size_t size(void) const noexcept
        {
                return table.size();
        }

        bool virtual_id_exists(ID virt) const noexcept
        {
                bool found = false;

                acquire();
                found = table.contains(virt);
                release();

                return found;
        }

        bool real_id_exists(ID real) const noexcept
        {
                bool found = false;

                acquire();
                for (auto it = table.begin(); it != table.end(); ++it) {
                        if (it->second == real) {
                                found = true;
                                break;
                        }
                }
                release();

                return found;
        }
        
        bool virtual_to_real(ID virt, ID &real) const noexcept
        {
                bool found = false;

                acquire();
                if (auto it = table.find(virt); it != table.end()) {
                        found = true;
                        real = it->second;
                }
                release();

                return found;
        }

        bool real_to_virtual(ID real, ID &virt) const noexcept
        {
                bool found = false;

                acquire();
                for (auto it = table.begin(); it != table.end(); ++it) {
                        if (it->second == real) {
                                found = true;
                                virt = it->first;
                                break;
                        }
                }
                release();
        
                return found;
        }
};

}
#endif /* __cplusplus */
#endif /* VIRTUAL_ID_TABLE_H */
