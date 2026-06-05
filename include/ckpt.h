/* ckpt.h */
#ifndef __CKPT_H__
#define __CKPT_H__
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <ucontext.h>
#include "shared_cache.h"
#include "types.h"

/**
 * ckpt_metadata_t: Metadata written to the start of a checkpoint
 *                  file describing the expected contents when
 *                  reading the checkpoint image
 */
typedef struct ckpt_metadata {
        u32                     nr_headers;
        u32                     nr_regions;
        u32                     nr_contexts;
        shared_cache_info_t     shared_cache_info;
} ckpt_metadata_t;

/** 
 * ckpt_hdr_t:  
 *      Header that written before each saved segment of
 *      data in a checkpoint file, indicating the type of
 *      data following any header.
 */
typedef enum ckpt_header {
        CKPT_VM_REGION_HEADER   = 0u,
        CKPT_CONTEXT_HEADER     = 1u,
} ckpt_header_t;

#define CKPT_HEADER_STRING(__header) \
        (((__header) == CKPT_VM_REGION_HEADER)  ? "vm region"   : \
         ((__header) == CKPT_CONTEXT_HEADER)    ? "context"     : "")

#define MAX_CKPT_HEADERS        (512)
#define MAX_CKPT_VM_REGIONS     (128)
#define MAX_CKPT_CONTEXTS       (1)

typedef enum ckpt_state {
        LIBCKPT_UNINITIALIZED,
        LIBCKPT_RUNNING,
        LIBCKPT_CKPTINPROG
} ckpt_state;

ckpt_state      get_ckpt_state(void);
void            set_ckpt_state(ckpt_state);

void precheckpoint(void);
void postrestart(void);
void docheckpoint(ucontext_t *);

void setup(void);
void cleanup(void);

#endif // __CKPT_H__
