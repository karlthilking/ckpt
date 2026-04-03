/* testlib.c */
#define _GNU_SOURCE
#include <pthread.h>
#include <err.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>
#include <assert.h>
#include <errno.h>
#include <sys/syscall.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include "testlib.h"

/* Globals */
rptc_t __pthread_create = NULL;
rptj_t __pthread_join   = NULL;
rpte_t __pthread_exit   = NULL;

__attribute__((visibility("hidden"))) thlist_t  *thlst = NULL;
__attribute__((visibility("hidden"))) ckpthdr_t hdrs[MAX_HDRS];
__attribute__((visibility("hidden"))) memrgn_t  rgns[MAX_RGNS];
__attribute__((visibility("hidden"))) regctx_t  ctxs[MAX_CTXS];

__attribute__((visibility("hidden"))) sem_t             gsem;
__attribute__((visibility("hidden"))) pthread_mutex_t   gmtx;
__attribute__((visibility("hidden"))) pthread_cond_t    gcond;

__attribute__((visibility("hidden"))) u32 nr_ckpthdrs;
__attribute__((visibility("hidden"))) u32 nr_memrgns;
__attribute__((visibility("hidden"))) u32 nr_regctxs;

/**
 * perm_stoi: Convert permission string to unsigned 8 bit integer
 * @rwxp: The string to representing the permissions
 * @perms: Pointer to the integer to store the permission info in
 */
u8 perm_stoi(char *rwxp, u8 *perms)
{
        *perms = 0;

        *perms |= (rwxp[0] == 'r') ? R : 0;
        *perms |= (rwxp[1] == 'w') ? W : 0;
        *perms |= (rwxp[2] == 'x') ? X : 0;
        *perms |= (rwxp[3] == 'p') ? P : 0;

        return *perms;
}

/**
 * perm_itos: Convert permission integer representation to string
 * @rwxp: String to store the permission info in
 * @perms: The integer representation of the permissions
 */
char *perm_itos(char *rwxp, u8 perms)
{
        rwxp[0] == (perms & R) ? 'r' : '-';
        rwxp[1] == (perms & W) ? 'w' : '-';
        rwxp[2] == (perms & X) ? 'x' : '-';
        rwxp[3] == (perms & P) ? 'p' : 's';

        return rwxp;
}

/**
 * thlist_insert: Insert a newly spawned posix thread into the
 *                thread list
 * @ptid: Posix thread identifier of the newly spawned thread
 * @return: Returns -1 if the insertion fails, 0 if the insertion
 *          is successful
 */
int thlist_insert(pthread_t ptid)
{
        thinfo_t *t = malloc(sizeof(thinfo_t));

        if (!t)
                return -1;

        t->ptid = ptid;

        pthread_mutex_lock(&__thlst->mtx);
        t->next = __thlst->head;
        __thlst->head = t;
        __thlst->nr_threads++;
        pthread_mutex_unlock(&__thlst->mtx);
        
        return 0;
}

/**
 * thlist_remove: Remove a thread from the thread list; called if
 *                a thread is joined or exits
 * @ptid: Posix thread identifier of the thread to delete
 * @return: Returns 0 if removal is successful, -1 if the given
 *          posix thread identifier does not match any thread
 *          in the thread list
 */
int thlist_remove(pthread_t ptid)
{
        int rc = -1;
        
        pthread_mutex_lock(&__thlst->mtx);
        for (thinfo_t *t = __thlst->head; t; t = t->next) {
                if (pthread_equal(t->next->ptid, ptid)) {
                        t->next = t->next->next;
                        __thlst->nr_threads--;
                        rc = 0;
                        break;
                }
        }
        pthread_mutex_unlock(&__thlst->mtx);

        return rc;
}

/**
 * ckpthdr_init: Initializes a checkpoint header structure that
 *               contains a pointer to either a memory region
 *               structure or a register context structure
 * @data: Data to intialize in the checkpoint header, either
 *        a pointer to a memory region structure or a pointer to
 *        a register context structure
 * @type: Indicates if this header is for a memory region or if
 *        it is for a register context
 */
void ckpthdr_init(ckpthdr_t *hdr, void *data, u8 type)
{
        if (type & TY_REGCTX) {
                memcpy(hdr->ctx, data, sizeof(regctx_t));
                hdr->type = TY_REGCTX;
        } else {
                memcpy(hdr->rgn, data, sizeof(memrgn_t));
                hdr->type = TY_MEMRGN;
        }
}

/**
 * memrgn_init: Initialize a memory region structure with the
 *              contents obtained from /proc/self/maps
 * @rgn: The memory region structure to initialize
 * @start: Start address of the memory region
 * @end: End address of the memory region
 * @rwxp: Permissions of the memory region
 * @name: Name of the region (could be anonymous (NULL))
 */
void memrgn_init(memrgn_t *rgn, u64 start,
                 u64 end, char *rwxp, char *name)
{
        rgn->start = (void *)start;
        rgn->end = (void *)end;
        
        rgn->rwxp = 0;
        perm_stoi(rwxp, &rgn->rwxp);
        
        /* Make sure not to save a guard page */
        assert(!GUARD(rgn->rwxp));
        
        /* Name is NULL for an anonymous region */
        if (name) {
                strncpy(rgn->name, name, strlen(name) + 1);
                rgn->name[strlen(name)] = '\0';
        } else {
                size_t len = strlen("anonymous");
                strncpy(rgn->name, "anonymous", len + 1);
                rgn->name[len] = '\0';
        }
}

/**
 * regctx_init: Initialize a register context structure that
 *              contains the register context and an indication
 *              of whether or not the context is from a posix
 *              thread
 * @regctx: The register context structure to initialize
 * @ptid: Posix thread identifier (if not the main thread)
 * @ucp: Pointer to ucontext_t structure for the register context
 * @type: Either TY_POSIX (posix thread) or TY_MAIN (main thread)
 */
void regctx_init(regctx_t *regctx, pthread_t *ptid,
                 ucontext_t *ucp, u8 type)
{
        regctx->type = type;

        if (ptid && regctx->type & TY_POSIX)
                regctx->ptid = *ptid;

        memcpy(&regctx->uc, ucp, sizeof(ucontext_t));
}

/**
 * pthread_create: Wrapper function for the real pthread_create.
 *                 Adds to thread to the thread list and calls
 *                 the actual pthread_create function using the
 *                 function pointer __pthread_create (real 
 *                 call to pthread_create).
 * @th: The pthread_t structure to spawn
 * @attr: Attributes for the new thread
 * @start: Start routine of the thread
 * @arg: Arguments for the thread's routine
 * @return: Forwards the return value of real call to       
 *          pthread_create
 */
int pthread_create(pthread_t *th, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg)
{
        int rc;
        
        assert(__pthread_create);
        if ((rc = __pthread_create(th, attr, start, arg)) == 0)
                assert(thlist_insert(*th) == 0);

        return rc;
}

/**
 * pthread_join: Wrapper function for the real pthread_join.
 *               Removes the thread from the thread list if the
 *               join is successful.
 * @th: The pthread_t structure to join
 * @ret: The value returned on the thread being joined
 * @return: Forwards the return value of the real call to 
 *          pthread_join
 */
int pthread_join(pthread_t th, void **ret)
{
        int rc;
        
        assert(__pthread_join);
        if ((rc = __pthread_join(th, ret)) == 0)
                assert(thlist_remove(th) == 0);

        return rc;
}

// void pthread_exit(void *ret)
// {
//         for (thinfo_t *t = head; t; t = t->next)
//                 if (pthread_equal(pthread_self(), t->next->ptid))
//                         t->next = t->next->next;
// 
//         rpte(ret);
// }

/**
 * get_one_memrgn: Save one memory region from /proc/self/maps
 *                 into a memory region structure
 * @fd: File descriptor used to open /proc/self/maps
 * @rgn: The memory region to save the information in
 */
int get_one_memrgn(int fd, memrgn_t *rgn)
{
        u64 start, end;
        char rwxp[4];
        char name[256];
        int stdindup, rc;
        
        memset(name, 0, sizeof(name));

        /**
         * Save original stdin and have stdin refer to the 
         * file descriptor used to open /proc/self/maps to
         * parse the contents of one memory region
         */
        stdindup = dup(STDIN_FILENO);
        dup2(fd, STDIN_FILENO);
        
        /**
         * Format of line in /proc/self/maps:
         * address permissions offset device inode pathname
         */
        rc = scanf("%lx-%lx %4c %*s %*s %*[0-9 ]%[^\n]\n",
                   &start, &end, rwxp, name);
        
        /* Restore stdin */
        clearerr(stdin);
        dup2(stdindup, STDIN_FILENO);
        close(stdindup);
        
        if (rc == 0 || rc == EOF)
                return EOF;
        else if (rc == 3) {
                if (!strncmp(rwxp, "---", 3))
                        return MEMRGN_FAILED;
                memrgn_init(rgn, start, end, rwxp, NULL);
        } else if (rc == 4) {
                /**
                 * Return -1 to indicate encountering a special
                 * region. i.e. [vsyscall], [vvar], [vdso], or
                 * guard page "---p"
                 */
                if (!strncmp(name, "[vsyscall]", 10) ||
                    !strncmp(name, "[vvar]", 6) ||
                    !strncmp(name, "[vdso]", 6) ||
                    !strncmp(rwxp, "---", 3))
                        return MEMRGN_FAILED;
                
                memrgn_init(rgn, start, end, rwxp, name);
        }

        return 0;
}

/**
 * get_memrgns: Get all the memory regions in /proc/self/maps
 *              and save the to the array of memrgn_t structures
 * @rgns: The array of memory region structures to save
 *        information about the address space in
 */
int get_memrgns()
{
        int rc, fd;

        if ((fd = open("/proc/self/maps", O_RDONLY)) < 0)
                err(EXIT_FAILURE, "open");
        
        for (int i = 0; i < MAX_MEMRGNS;) {
                memrgn_t *r = __rgns + i;

                rc = get_one_memrgn(fd, r);
                if (rc == EOF)
                        break;
                else if (rc == MEMRGN_FAILED)
                        continue;

                ckpthdr_init(__hdrs + nr_ckpthdrs,
                             (void *)(__rgns + nr_memrgns),
                             TY_MEMRGN);
                nr_ckpthdrs++;
                nr_memrgns++;
                i++;
        }
        
        close(fd);
        return 0;
}


int wr_ckptdata(int fd, void *data, u64 sz)
{
        if (!(data || sz)) {
                fprintf(stderr, "wr_ckptdata: Received a NULL"
                                "pointer or a size of 0\n");
                return -1;
        }

        int rc;
        u64 bytes = 0;

        while (bytes < sz) {
                rc = write(fd, data + bytes, sz - bytes);
                if (rc < 0) {
                        fprintf(stderr, "%s %d\n",
                                strerror(errno), errno);
                        return -1;
                }
                bytes += rc;
        }

        assert(bytes == sz);
        return 0;
}

/**
 * wr_ckpt: Write the a checkpoint file including all headers,
 *          register contexts for each thread of execution,
 *          as well as data for each memory region in the address
 *          space
 * @return: Returns -1 on error, 0 otherwise
 */
int wr_ckpt()
{
        assert(nr_ckpthdrs == nr_memrgns + nr_regctxs);

        int fd;
        u32 wr_ckpthdrs = 0, wr_memrgns = 0, wr_regctxs = 0;
        char buf[128];

        snprintf(buf, sizeof(buf), "%d-ckpt.dat", getpid());
        fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR);

        if (fd < 0) {
                perror("open");
                return -1;
        }
        
        void *data = NULL;
        size_t sz = 0;

        /**
         * For each checkpoint header:
         * - Write checkpoint header data
         * - If header is for a memory region:
         *      - write all data from memory segment
         */
        for (u32 i = 0; i < nr_ckpthdrs; i++) {
                ckpthdr_t *h = __hdrs + i;
                
                data = (void *)h;
                sz = sizeof(ckpthdr_t);
                if (wr_ckptdata(fd, data, sz) < 0)
                        return -1;
                
                if (h->type & TY_REGCTX)
                        wr_regctxs++;
                else if (h->type & TY_MEMRGN) {
                        data = h->rgn.start;
                        sz = (u64)(h->rgn.end - h->rgn.start);
                        if (wr_ckptdata(fd, data, sz) < 0)
                                return -1;
                        wr_memrgns++;
                }
                wr_ckpthdrs++;
        }
        
        assert(wr_ckpthdrs == nr_ckpthdrs);
        assert(wr_regctxs == nr_regctxs);
        assert(wr_memrgns == nr_memrgns);
        assert(wr_ckpthdrs == wr_memrgns + wr_regctxs);
        
        close(fd);
        return 0;
}

/**
 * pthread_sighandler: Signal handler for posix threads during
 *                     checkpoint. The thread will save its
 *                     register context do the checkpoint header
 *                     array and then suspend execution.
 * @signum: Signal that caused the invocation of the handler
 */
void pthread_sighandler(int signum)
{
        static int      is_restart;
        ucontext_t      uc;
        
        is_restart = 0;
        getcontext(&uc);

        if (is_restart) {
                is_restart = 0;
                return;
        } else {
                /**
                 * Initialize a register context and a checkpoint 
                 * header for this thread
                 */
                pthread_t ptid = pthread_self();

                pthread_mutex_lock(&__gmtx);
                
                regctx_init(__ctxs + nr_regctxs,
                            &ptid, &uc, TY_POSIX);
                ckpthdr_init(__hdrs + nr_ckpthdrs,
                             (void *)(__ctxs + nr_regctxs), 
                             TY_REGCTX);
                nr_regctxs++;
                nr_ckpthdrs++;
                
                pthread_cond_signal(&__gcond);
                pthread_mutex_unlock(&__gmtx);
                sem_wait(&__gsem);
        }
        return;
}

/**
 * main_sighandler: Signal handler for the main thread to
 *                  coordinate the checkpoint.
 * @signum: Signal that invoked the main thread's handler
 */
void main_sighandler(int signum)
{
        static int      is_restart; // checkpoint or restart
        ucontext_t      uc;         // main thread's context
        u64             fs;
        
        is_restart = 0;

        syscall(SYS_arch_prctl, ARCH_GET_FS, &fs);
        getcontext(&uc);
        syscall(SYS_arch_prctl, ARCH_SET_FS, fs);

        if (is_restart) {
                is_restart = 0;
                return;
        } else {
                is_restart = 1;
                for (thinfo_t *t = __thlst->head; t; t = t->next) {
                        /**
                         * Signal pthread to save its register
                         * context and then suspend execution.
                         * Posix thread calls sem_post when it
                         * is done saving its state
                         */
                        pthread_kill(t->ptid, SIGUSR1);
                }
                
                pthread_mutex_lock(&__gmtx);
                while (nr_ckpthdrs < __thlst->nr_threads)
                        pthread_cond_wait(&__gcond, &__gmtx);
                /**
                 * Now main thread saves it register context
                 * into a checkpoint header that will later
                 * be written
                 */
                regctx_init(__ctxs + nr_regctxs, 
                            NULL, &uc, TY_MAIN);
                ckpthdr_init(__hdrs + nr_ckpthdrs,
                             (void *)(__ctxs + nr_regctxs),
                             TY_REGCTX);
                nr_regctxs++;
                nr_ckpthdrs++;
                
                /**
                 * All register contexts for the main thread and
                 * any posix threads are now saved, proceed to
                 * save the process memory regions
                 */
                if (get_memrgns() < 0)
                        exit(EXIT_FAILURE);
                
                /* Now write checkpoint file */
                if (wr_ckpt() < 0)
                        exit(EXIT_FAILURE);

                pthread_mutex_unlock(&__gmtx);
                printf("Finished writing checkpoint file: "
                       "%d-ckpt.dat\n", getpid()); 
                
                for (u32 i = 0; i < __thlst->nr_threads; i++)
                        sem_post(&__gsem);

                return;
        }
}

void __attribute__((constructor)) setup()
{
        /* Initialize thread list */
        __thlst = malloc(sizeof(thlist_t));
        if (!__thlst)
                err(EXIT_FAILURE, "malloc");

        __thlst->head = NULL;
        __thlst->nr_threads = 0;
        pthread_mutex_init(&__thlst->mtx, NULL);

        __pthread_create = dlsym(RTLD_NEXT, "pthread_create");
        __pthread_join = dlsym(RTLD_NEXT, "pthread_join");
        // rpte = dlsym(RTLD_NEXT, "pthread_exit");

        if (!(__pthread_create && __pthread_join /* && rtpe */))
                err(EXIT_FAILURE, dlerror());
        
        sem_init(&__gsem, 0, 0);

        signal(SIGUSR2, main_sighandler);
        signal(SIGUSR1, pthread_sighandler);
}

void __attribute__((destructor)) cleanup()
{
        thinfo_t *t = __thlst->head;

        while (t) {
                thinfo_t *next = t->next;
                free(t);
                t = next;
        }
        
        pthread_mutex_destroy(&__thlst->mtx);
        free(__thlst);

        sem_destroy(&__gsem);
        pthread_mutex_destroy(&__gmtx);
        pthread_cond_destroy(&__gcond);
}
