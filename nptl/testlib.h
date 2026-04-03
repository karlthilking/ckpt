/* testlib.h */
#ifndef TESTLIB_H
#define TESTLIB_H

#define _GNU_SOURCE
#include <pthread.h>
#include <ucontext.h>
#include <stdint.h>

/* Memory region permission bits */
#define R               0x1 // Read bit
#define W               0x2 // Write bit
#define X               0x4 // Execute bit
#define P               0x8 // Private bit
#define GUARD(p)        (!((p) & (R | W | X)))
#define MEMRGN_FAILED   (-532439)

/* Register context type */
#define TY_MAIN         0x1 // Register context of main thread
#define TY_POSIX        0x2 // Register context of posix thread

/* Checkpoint data types */
#define TY_CKPTHDR      0x1 // Checkpoint header structure
#define TY_REGCTX       0x2 // Register context structure
#define TY_MEMRGN       0x4 // Memory region structure

/* Checkpoint structure constants */
#define MAX_HDRS        100 
#define MAX_RGNS        100
#define MAX_CTXS        100

typedef int8_t          i8;
typedef uint8_t         u8;
typedef int16_t         i16;
typedef uint16_t        u16;
typedef int32_t         i32;
typedef uint32_t        u32;
typedef int64_t         i64;
typedef uint64_t        u64;

typedef struct __thlist_t       thlist_t;
typedef struct __thinfo_t       thinfo_t;
typedef struct __ckpthdr_t      ckpthdr_t;
typedef struct __memrgn_t       memrgn_t;
typedef struct __regctx_t       regctx_t;

/**
 * thlist_t: List of structures containing thread info
 * @mtx: Mutex for inserting or deleting from the list
 * @nr_threads: Number of active threads (other than main thread)
 */
struct __thlist_t {
        thinfo_t        *head;
        pthread_mutex_t mtx;
        int             nr_threads;
};

/**
 * thinfo_t: Structure representing one thread of execution
 * @ptid: Posix thread identifier
 * @next: Next thread in the thread list
 */
struct __thinfo_t {
        thinfo_t        *next;
        pthread_t       ptid;
};

/**
 * memrgn_t: Memory region in process address space
 * @start: Start address of memory region
 * @end: End address of memory region
 * @rwxp: Memory segment permissions
 * @name: Name of memory segment (if not anonymous)
 */
struct __memrgn_t {
        void    *start;
        void    *end;
        char    name[128];
        u8      rwxp;
};

/**
 * regctx_t: Register context of a thread of execution
 * @ptid: Posix thread identifier
 * @uc: Register context of the thread
 * @type: Either posix thread or main thread
 */
struct __regctx_t {
        pthread_t       ptid;
        ucontext_t      uc;
        u8              type;
};

/**
 * ckpthdr_t: Checkpoint header
 * @rgn: Pointer to memory region struct
 * @ctx: Pointer to register context struct
 * @type: Either a memory region or register context
 */
struct __ckpthdr_t {
        union {
                memrgn_t rgn;
                regctx_t ctx;
        };
        u8 type;
};

/* Utilities for memory segment permissions */
u8 perm_stoi(char *rwxp, u8 *perms);
char *perm_itos(char *rwxp, u8 perms);

/* Thread list insertion and deletion */
int thlist_insert(pthread_t);
int thlist_remove(pthread_t);

/* Structure initializers */
void memrgn_init(memrgn_t *, u64, u64, char *, char *);
void regctx_init(regctx_t *, pthread_t *,
                        ucontext_t *, u8);

/* Posix wrappers */
int pthread_create(pthread_t *, const pthread_attr_t *,
                          void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
void pthread_exit(void *);

/* Utility functions for saving address space */
int get_one_memrgn(int, memrgn_t *);
int get_memrgns();

/* Utility functions for writing checkpoint file */
int wr_ckptdata(int, void *, u64);
int wr_ckpt();

/* Signal handlers */
void pthread_sighandler(int);
void main_sighandler(int);
void restart_sighandler(int);

/* Constructor functions */
void __attribute__((constructor)) setup();

/**
 * NPTL function pointers:
 * rptc: Function pointer to real pthread_create
 * rptj: Function pointer to real pthread_join
 * rpte: Function pointer to real pthread_exit
 */
// int (*rptc)(pthread_t *, const pthread_attr_t *,
//             void *(*)(void *), void *);
// int (*rptj)(pthread_t, void **);
// int (*rpte)(void *);

typedef int (*rptc_t)(pthread_t *, const pthread_attr_t *,
                      void *(*)(void *), void *);
typedef int (*rptj_t)(pthread_t, void **);
typedef int (*rpte_t)(void *);

extern rptc_t   __pthread_create;
extern rptj_t   __pthread_join;
extern rpte_t   __pthread_exit;
#endif

