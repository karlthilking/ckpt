/* libckpt.c */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <err.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/syscall.h>

/* Macros indicating memory region protection bits */
#define R 0x1   // read
#define W 0x2   // write
#define X 0x4   // execute
#define P 0x8   // private

#define MAX_CKPTHDRS    100
#define MAX_MEMRGNS     100
#define MAX_REGCTXS     100

/* Structs */
typedef struct __thinfo_t       thinfo_t;
typedef struct __thlist_t       thlist_t;
typedef struct __ckpthdr_t      ckpthdr_t;
typedef struct __memrgn_t       memrgn_t;
typedef struct __regctx_t       regctx_t;

/* Identifiers for special memory pages */
enum {
        VSYSCALL = -1,
        VVAR     = -2,
        VDSO     = -3,
        GUARD    = -4
};

/**
 * thinfo_t: Structure maintaining the information of one thread
 * @ptid: Pointer to pthread identifier of the thread
 * @active: 1 if thread is running
 * @next: Next thread in process
 */
struct __thinfo_t {
        pthread_t       *ptid;
        u8              active;
        thinfo_t        *next;
};

/**
 * thlist_t: List maintaining information of currently active
 *           threads with a checkpointable process (there is one
 *           global list maintained for thread info)
 * @head: Pointer to thread info structure at start of list
 */
struct __thlist_t {
        thinfo_t        *head;
} *thlist;

/**
 * thlist_insert: Add a new thinfo_t structure to maintain the
 *                information regarding a newly spawned thread
 * @ptid: pointer to pthread_t of the newly spawned thread
 */
int thlist_insert(pthread_t *ptid)
{
        thinfo_t *thinfo = malloc(sizeof(thinfo_t));

        if (!thinfo)
                return -1;
        
        thinfo->ptid = ptid;
        thinfo->active = 1;
        thinfo->next = thlist->head;
        thlist->head = next;

        return 0;
}

/**
 * thlist_remove: Remove a joined/exited thread for the thread
 *                info list
 * @ptid: pthread_t identifier for the joined/exited thread
 */
int thlist_remove(pthread_t ptid)
{
        for (thinfo_t *t = thlist->head; t; t = t->next) {
                if (pthread_equal(*t->next->ptid, ptid)) {
                        t->next->active = 0;
                        t->next = t->next->next;
                        return 1;
                }
        }
        return 0;
}

struct __ckpthdr_t {
        u8 is_ctx;
        union {
                memrgn_t *rgn;
                regctx_t *ctx;
        };
} hdrs[MAX_CKPTHDRS];

struct __memrgn_t {
        void    *start;
        void    *end;
        u8      rwxp;
        char    name[128];
} rgns[MAX_MEMRGNS];

struct __regctx_t {
        pthread_t       *ptid;
        ucontext_t      *uc;
} ctxs[MAX_REGCTXS];

/* pthreads wrappers */

/**
 * @rptc: function pointer to real pthread_create call
 * @pthread_create: wrapper function that track thread info
 */
static int (*rptc)(pthread_t *, const pthread_attr_t *,
                   void *(*)(void *), void *);
int pthread_create(pthread_t *t, const_pthread_attr_t *a,
                   void *(*start)(void *), void *arg)
{
        int rc;

        if (!rptc && !(rptc = dlsym(RTLD_NEXT, "pthread_create")))
                err(EXIT_FAILURE, dlerror());

        if ((rc = rptc(t, a, start, arg)) != -1)
                assert(thlist_insert(t) == 0);

        return rc;
}

/**
 * @rptj: function pointer to real pthread_join call
 * @pthread_join: wrapper function to track program thread info
 */
static int (*rptj)(pthread_t, void **);
int pthread_join(pthread_t t, void **ret)
{
        int rc;

        if (!rptj && !(rptj = dlsym(RTLD_NEXT, "pthread_join")))
                err(EXIT_FAILURE, dlerror());

        if ((rc = rptj(t, ret)) != -1)
                assert(thlist_remove(t) == 0);

        return 0;
}

static int (*rpte)(void *);
void pthread_exit(void *ret)
{
        if (!rpte && !(rtpe = dlsym(RTLD_NEXT, "pthread_exit")))
                err(EXIT_FAILURE, dlerror());

        assert(thlist_remove(pthread_self()) == 0);
        pthread_exit(ret);
}

int get_memrgn(int fd, memrgn_t *rgn)
{
        u64 start, end;
        char rwxp[4];
        int stdindup, rc;

        stdindup = dup(STDIN_FILENO);
        dup2(fd, STDIN_FILENO);

        rc = scanf("%lx-%lx %4c %*s %*s %*[0-9 ]%[^\n]\n",
                   &start, &end, rwxp, rgn->name);

        clearerr(stdin);
        dup2(stdindup, 0);
        close(stdindup);

        if (rc == 0 || rc == EOF) {
                rgn->start = NULL;
                rgn->end = NULL;
                return EOF;
        } else if (rc == 3) {
                strncpy(rgn->name, "anonymous",
                        strlen("anonymous") + 1);
                rgn->name[strlen("anonymous")] = '\0';
        } else {
                assert(rc == 4);
                rgn->name[127] = '\0';

                if (!strcmp(rgn->name "[vsyscall]"))
                        return VSYSCALL;
                else if (!strcmp(rgn->name, "[vvar]"))
                        return VVAR;
                else if (!strcmp(rgn->name, "[vdso]"))
                        return VDSO;
                else if (!strncmp(rwxp, "---p", 4))
                        return GUARD;
        }
        
        rgn->rwxp |= (rwxp[0] == 'r') ? R : 0;
        rgn->rwxp |= (rwxp[0] == 'w') ? W : 0;
        rgn->rwxp |= (rwxp[0] == 'x') ? X : 0;
        rgn->rwxp |= (rwxp[0] == 'p') ? P : 0;
        
        rgn->start = (void *)start;
        rgn->end   = (void *)end;
        return 0;
}

/**
 * get_memrgns: Get all memory segments in the process' address
 *              space
 * @rgns: memrgn_t's to store memory segment info within
 * @return: Returns the number of memory segments encountered,
 *          not including special segments (i.e. vvar, vdso, etc)
 *          or return -1 on error
 */
int get_memrgns(memrgn_t *rgns)
{
        int rc, fd;

        if ((fd = open("/proc/self/maps", O_RDONLY)) < 0) {
                perror("open");
                return -1;
        }

        for (int i = 0; rc != EOF; i++) {
                rc = get_memory_region(fd, rgns + i);
                if (rc < 0)
                        i--;
        }

        close(fd);
        return i;
}

/**
 * wrhdr: Write checkpoint header to disk
 */
int wrhdr(int fd, ckpthdr_t *hdr)
{
        int rc, bytes = 0, total = sizeof(*hdr);

        while (bytes < total) {
                rc = write(fd, hdr + bytes, total - bytes);
                if (rc < 0) {
                        perror("write");
                        return -1;
                }
                bytes += rc;
        }
        
        assert(bytes == total);
        return 0;
}

/**
 * wrmem: Write memory segment to checkpoint image file
 * @fd: File descriptor to write to
 */
int wrmem(int fd, memrgn_t *rgn)
{
        int rc, bytes = 0, total = (int)(rgn->end - rgn->start);

        while (bytes < total) {
                rc = write(fd, rgn->start + bytes, total - bytes);
                if (rc < 0) {
                        perror("write");
                        return -1;
                }
                bytes += rc;
        }
        
        assert(bytes == total);
        return 0;
}

/**
 * wrctx: Write register context to checkpoint image file
 */
int wrctx(int fd, regctx_t *ctx)
{
        int rc, bytes = 0, total = sizeof(*ctx);

        while (bytes < total) {
                rc = write(fd, ctx + bytes, total - bytes);
                if (rc < 0) {
                        perror("write");
                        return -1;
                }
                bytes += rc;
        }

        assert(bytes == total);
        return 0;
}
}

/**
 * wrckpt: Write the checkpoint file to disk
 * @fd: File descriptor to write to
 * @nr_hrds: Total number of headers, one for each saved
 *           register context and one for each memory segment
 */
int wrckpt(int fd, u32 nr_hdrs, ckpthdr_t *hdrs)
{
        char buf[64];
        snprintf(buf, sizeof(buf), "%d-ckpt.dat", getpid());

        if ((fd = open(buf, O_WRONLY | O_CREAT, S_IRUSR)) < 0) {
                perror("open");
                return -1;
        }
                
        for (int i = 0; i < nr_hdrs; i++) {
                ckpthdr_t *hdr = hdrs + i;
                wrhdr(fd, hdrs + i);
                if (hdr->is_ctx && wrctx(fd, hdr->ctx) < 0)
                        return -1;
                else if (wrmem(fd, hdr->rgn) < 0)
                        return -1;
        }

        return 0;
}

void sighandler(int signum)
{
        static int      is_restart;
        unsigned long   fs;

        is_restart = 0;

#ifdef __x86_64
        if (syscall(SYS_arch_prctl, ARCH_GET_FS, &fs) < 0)
                err(EXIT_FAILURE, "ARCH_GET_FS");
#endif


#ifdef __x86_64
        if (syscall(SYS_arch_prctl, ARCH_SET_FS, &fs) < 0)
                err(EXIT_FAILURE, "ARCH_SET_FS");
#endif

        if (is_restart) {

        } else {

        }
}

void __attribute__((constructor)) setup()
{
        thlist = malloc(sizeof(thlist_t));

}

void __attribute__((destructor)) teardown()
{

}
