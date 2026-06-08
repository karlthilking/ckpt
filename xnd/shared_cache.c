/* shared_cache.c */
#include <stdio.h>
#include "shared_cache.h"

int shared_cache_get_info(shared_cache_info_t *info)
{
        info->base = _dyld_get_shared_cache_range(&info->size);
        if (info->base == NULL) {
                fprintf(stderr, "_dyld_get_shared_cache_range error\n");
                return -1;
        }

        if (!_dyld_get_shared_cache_uuid(info->uuid)) {
                fprintf(stderr, "_dyld_get_shared_cache_uuid error\n");
                return -1;
        }

        return 0;
}

int shared_cache_check(const shared_cache_info_t *info)
{
        const void      *base;
        size_t          size;
        uuid_t          uuid;
        
        if ((base = _dyld_get_shared_cache_range(&size)) == NULL) {
                fprintf(stderr, "_dyld_get_shared_cache_range error\n");
                return -1;
        }

        if (!_dyld_get_shared_cache_uuid(uuid)) {
                fprintf(stderr, "_dyld_get_shared_cache_uuid error\n");
                return -1;
        }

        if (base != info->base || size != info->size ||
            uuid_compare(uuid, info->uuid)) {
                char ckpt_uuid[37], curr_uuid[37];

                uuid_unparse(info->uuid, ckpt_uuid);
                uuid_unparse(uuid, curr_uuid);

                fprintf(stderr,
                        "dyld shared cache was rebuilt/slid:\n"
                        "    current: (base=%p) (size=%zu) (uuid=%s)\n"
                        " checkpoint: (base=%p) (size=%zu) (uuid=%s)\n",
                        base, size, curr_uuid, 
                        info->base, info->size, ckpt_uuid);

                return -1;
        }
        
        return 0;
}
