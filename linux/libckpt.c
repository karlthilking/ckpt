/* libckpt.c */
#define _GNU_SOURCE
#include <ucontext.h>  
#include <signal.h>     
#include <stdio.h>      
#include <stdlib.h>    
#include <string.h>     
#include <fcntl.h>      
#include <assert.h>     
#include <unistd.h>    
#include <sys/mman.h>   
#include <asm/prctl.h>
#include <sys/syscall.h>

#define NAME_LEN            128
#define CKPT_FILE           "ckpt.dat"
#define MAX_CKPT_HEADERS    1000
#define CKPT_HEADER_SIZE    sizeof(ckpt_header_t)
#define UCONTEXT_SIZE       sizeof(ucontext_t)

// distinguishable error codes to use for indicating the encounter of
// any special memory segments in /proc/self/maps that should not
// be saved
enum {
    VSYSCALL    = -1,
    VVAR        = -2,
    VDSO        = -3,
    GUARD_PAGE  = -4
};

typedef struct {
    void *start;
    void *end;
    char rwxp[4];
    char name[NAME_LEN];
    int is_reg_context;
    int data_size;
} ckpt_header_t;

int 
proc_self_maps_line(int proc_maps_fd, ckpt_header_t *ckpt_header)
{
    unsigned long int start, end;
    int tmp_stdin, rc;
    tmp_stdin = dup(0);
    dup2(proc_maps_fd, 0);
    rc = scanf("%lx-%lx %4c %*s %*s %*[0-9 ]%[^\n]\n",
               &start, &end, ckpt_header->rwxp,
               ckpt_header->name);
    clearerr(stdin);
    dup2(tmp_stdin, 0);
    close(tmp_stdin);
    ckpt_header->is_reg_context = 0;
    if (rc == 0 || rc == EOF) {
        ckpt_header->start = NULL;
        ckpt_header->end = NULL;
        return EOF;
    } else if (rc == 3) {
        strncpy(ckpt_header->name, "ANONYMOUS_SEGMENT",
                strlen("ANONYMOUS_SEGMENT") + 1);
        ckpt_header->name[strlen("ANONYMOUS_SEGMENT")] = '\0';
    } else {
        assert(rc == 4);
        ckpt_header->name[NAME_LEN - 1] = '\0';
        if (!strcmp(ckpt_header->name, "[vsyscall]"))
            return VSYSCALL; 
        else if (!strcmp(ckpt_header->name, "[vvar]"))
            return VVAR;
        else if (!strcmp(ckpt_header->name, "[vdso]"))
            return VDSO;
        else if (ckpt_header->rwxp[0] == '-' && ckpt_header->rwxp[1] == '-' &&
                 ckpt_header->rwxp[2] == '-' && ckpt_header->rwxp[3] == 'p')
            return GUARD_PAGE;
    }
    ckpt_header->start = (void *)start;
    ckpt_header->end = (void *)end;
    ckpt_header->data_size = (int)(ckpt_header->end - ckpt_header->start);
    return 0;
}

int 
proc_self_maps(ckpt_header_t ckpt_headers[])
{
    int i, rc, proc_maps_fd;
    if ((proc_maps_fd = open("/proc/self/maps", O_RDONLY)) < 0) {
        perror("open");
        return -1;
    }
    for (i = 0; rc != EOF; ) {
        // if proc_self_maps_line returns a negative value this indicates
        // one of the following occurences:
        //  - encountered vsyscall memory segment
        //  - encountered vvar memory segment
        //  - encountered vdso memory segment
        //  - encountered a guard page (permissions = '---p')
        // in these cases, reset the current ckpt_header in order to avoid
        // saving these segments
        if ((rc = proc_self_maps_line(proc_maps_fd, &ckpt_headers[i])) < 0)
            memset(&ckpt_headers[i], 0, CKPT_HEADER_SIZE);
        else
            ++i;
    }
    close(proc_maps_fd);
    // i = next unused segment in the ckpt_headers array for saving a
    // ckpt_header for the ucontext_t
    return i;
}

int 
save_context(ckpt_header_t *ckpt_header) 
{
    ckpt_header->start = NULL;
    ckpt_header->end = NULL;
    strncpy(ckpt_header->rwxp, "----", 4);
    strncpy(ckpt_header->name, "CONTEXT", strlen("CONTEXT") + 1);
    ckpt_header->name[strlen("CONTEXT")] = '\0';
    ckpt_header->is_reg_context = 1;
    ckpt_header->data_size = UCONTEXT_SIZE;
    return 0;
}

int 
write_ckpt(int ckpt_fd, ckpt_header_t ckpt_headers[], ucontext_t *ucp)
{
    int rc, tmp, i;
    for (i = 0; ; ++i) {
        rc = 0;
        while (rc < CKPT_HEADER_SIZE) {
            if ((tmp = write(ckpt_fd, &ckpt_headers[i] + rc,
                             CKPT_HEADER_SIZE - rc)) < 0) {
                perror("write");
                return -1;
            }
            rc += tmp;
        }
        assert(rc == CKPT_HEADER_SIZE);
        // the ckpt image file format always include the ucontext_t
        // data at the very end so when is_reg_context is nonzero (true)
        // the only thing left to do is break from the loop and read the
        // remaining data to the ucontext_t variable
        if (ckpt_headers[i].is_reg_context)
            break;
        rc = 0;
        while (rc < ckpt_headers[i].data_size) {
            if ((tmp = write(ckpt_fd, ckpt_headers[i].start + rc,
                             ckpt_headers[i].data_size - rc)) < 0) {
                perror("write");
                return -1;
            }
            rc += tmp;
        }
        assert(rc == ckpt_headers[i].data_size);
    }
    rc = 0;
    while (rc < UCONTEXT_SIZE) {
        if ((tmp = write(ckpt_fd, ucp + rc, UCONTEXT_SIZE - rc)) < 0) {
            perror("write");
            return -1;
        }
        rc += tmp;
    }
    assert(rc == UCONTEXT_SIZE);
    return 0;
}

void sig_handler2(int);

void
sig_handler(int signum)
{
    static int is_restart;
    ucontext_t uc;
    is_restart = 0;
    unsigned long fs;
    // avoid *** stack smashing detected ***
    if (syscall(SYS_arch_prctl, ARCH_GET_FS, &fs) < 0) {
        perror("syscall ARCH_GET_FS");
        exit(EXIT_FAILURE);
    }
    if (getcontext(&uc) < 0) {
        perror("getcontext");
        exit(EXIT_FAILURE);
    }
    if (syscall(SYS_arch_prctl, ARCH_SET_FS, fs) < 0) {
        perror("syscall ARCH_SET_FS");
        exit(EXIT_FAILURE);
    }
    signal(SIGUSR2, sig_handler2);
    if (is_restart) {
        puts("Restarting...");
        is_restart = 0;
        return;
    } else {
        puts("Checkpointing...");
        is_restart = 1;
        int ckpt_fd;
        ckpt_header_t ckpt_headers[MAX_CKPT_HEADERS];
        if ((ckpt_fd = open(CKPT_FILE, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR)) < 0) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        int i;
        if ((i = proc_self_maps(ckpt_headers)) < 0)
            exit(EXIT_FAILURE);
        save_context(&ckpt_headers[i]);
        if (write_ckpt(ckpt_fd, ckpt_headers, &uc) < 0)
            exit(EXIT_FAILURE);
        exit(EXIT_SUCCESS);
    }
}

void
sig_handler2(int signum)
{
    sig_handler(signum);
}

void
__attribute__((constructor)) ctor()
{
    signal(SIGUSR2, sig_handler);
}
