/* virtual_id_table.h */
#ifndef VIRTUAL_ID_TABLE_H
#define VIRTUAL_ID_TABLE_H

#include "xnd/xnd.h"
#include <pthread.h>
#include <string.h>

#ifdef __cplusplus
#include <unordered_map>

namespace xnd {
template<typename ID>
class virtual_id_table {
private:
        std::unordered_map<ID, ID>      table;
        mutable pthread_mutex_t         mtx;

        virtual_id_table(void) noexcept
        {
                xnd_assert(pthread_mutex_init(&mtx, NULL) == 0);
                xnd_assert(table.empty());
        }

        ~virtual_id_table(void) noexcept
        {
                xnd_assert(pthread_mutex_destroy(&mtx) == 0);
        }
public:
        virtual_id_table(const virtual_id_table &) = delete;
        virtual_id_table &operator=(const virtual_id_table &) = delete;
        
        static virtual_id_table<ID> &instance(void) noexcept
        {
                static virtual_id_table<ID> _id_table;
                return _id_table;
        }

        static std::unordered_map<ID, ID> &get(void) noexcept
        {
                return instance().table;
        }

        void atfork_child(void) const noexcept
        {
                int err;

                if ((err = pthread_mutex_unlock(&mtx)) != 0) {
                        xnd_error("pthread_mutex_unlock: %s\n",
                                  strerror(err));
                }

                if ((err = pthread_mutex_init(&mtx, NULL)) != 0) {
                        xnd_error("pthread_mutex_init: %s\n",
                                  strerror(err));
                }
        }
        
        void acquire(void) const noexcept
        {
                xnd_assert(pthread_mutex_lock(&mtx) == 0);
        }

        void release(void) const noexcept
        {
                xnd_assert(pthread_mutex_unlock(&mtx) == 0);
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
                return table.contains(virt);
        }

        bool real_id_exists(ID real) const noexcept
        {
                for (auto [virt_id, real_id] : table) {
                        if (real_id == real) {
                                return true;
                        }
                }

                return false;
        }

        bool virtual_to_real(ID virt, ID &real) const noexcept
        {
                if (auto it = table.find(virt); it != table.end()) {
                        real = it->second;
                        return true;
                }

                return false;
        }

        bool real_to_virtual(ID real, ID &virt) const noexcept
        {
                for (auto [virt_id, real_id] : table) {
                        if (real_id == real) {
                                virt = virt_id;
                                return true;
                        }
                }

                return false;
        }

        ID virtual_to_real(ID virt) const noexcept
        {
                if (auto it = table.find(virt); it != table.end()) {
                        return it->second;
                }
                
                return -1;
        }

        ID real_to_virtual(ID real) const noexcept
        {
                for (auto [virt_id, real_id] : table) {
                        if (real_id == real) {
                                return virt_id;
                        }
                }

                return -1;
        }
};
} /* namespace xnd */
#endif /* __cplusplus */
#endif /* VIRTUAL_ID_TABLE_H */
