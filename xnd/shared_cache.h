/* shared_cache.h */
#ifndef __CKPT_SHARED_CACHE_H__
#define __CKPT_SHARED_CACHE_H__
#include <uuid/uuid.h>

typedef struct shared_cache_info {
        const void      *base;
        size_t          size;
        uuid_t          uuid;
} shared_cache_info_t;

extern int _dyld_get_shared_cache_uuid(uuid_t);
extern const void *_dyld_get_shared_cache_range(size_t *);
extern const char *dyld_shared_cache_file_path(void);

int shared_cache_get_info(shared_cache_info_t *);
int shared_cache_check(const shared_cache_info_t *);

#endif // __CKPT_SHARED_CACHE_H__
