/* shared_cache.c */
#include <stdio.h>
#include "shared_cache.h"

static inline int shared_cache_cmp(const struct shared_cache_info *old,
                                   const struct shared_cache_info *new)
{
        if (old->base != new->base || old->size != new->size) {
                xnd_error("dyld shared cache range differs:\n"
                          "\told shared cache region: %p-%p %zu\n"
                          "\tnew shared cache region: %p-%p %zu\n",
                          old->base, old->base + old->size, old->size,
                          new->base, new->base + new->size, new->size);
                return -1;
        }

        if (uuid_compare(old->uuid, new->uuid)) {
                char old_uuid[37], new_uuid[37];
                uuid_unparse(old->uuid, old_uuid);
                uuid_unparse(new->uuid, new_uuid);
                xnd_error("dyld shared cache uuid differs:\n"
                          "\told uuid: %s\n"
                          "\tnew uuid: %s\n",
                          old_uuid, new_uuid);
                return -1;
        }

        return 0;
}

int shared_cache_get_info(struct shared_cache_info *info)
{
        info->base = _dyld_get_shared_cache_range(&info->size);
        if (info->base == NULL) {
                xnd_error("_dyld_get_shared_cache_range failed!\n");
                return -1;
        }

        if (!_dyld_get_shared_cache_uuid(info->uuid)) {
                xnd_error("_dyld_get_shared_cache_uuid failed!\n");
                return -1;
        }

        return 0;
}

int shared_cache_check(const struct shared_cache_info *old)
{
        struct shared_cache_info new;
        
        if (shared_cache_get_info(&new) < 0) {
                xnd_error("Failed to get address range and uuid of the "
                          "current shared cache\n");
                return -1;
        }
        
        return shared_cache_cmp(old, &new);
}
