/* shared_cache.h */
#ifndef XND_SHARED_CACHE
#define XND_SHARED_CACHE

#include "xnd/xnd.h"
#include <uuid/uuid.h>

struct shared_cache_info {
        const void      *base;
        size_t          size;
        uuid_t          uuid;
};

extern int _dyld_get_shared_cache_uuid(uuid_t);
extern const void *_dyld_get_shared_cache_range(size_t *);
extern const char *dyld_shared_cache_file_path(void);

int shared_cache_get_info(struct shared_cache_info *);
int shared_cache_check(const struct shared_cache_info *);

#endif /* XND_SHARED_CACHE */
