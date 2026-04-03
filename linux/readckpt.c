/* readckpt.c */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>

#define NAME_LEN            128
#define CKPT_FILE           "ckpt.dat"
#define MAX_CKPT_HEADERS    1000
#define CKPT_HEADER_SIZE    sizeof(ckpt_header_t)
#define UCONTEXT_SIZE       sizeof(ucontext_t)

typedef struct {
    void *start;
    void *end;
    char rwxp[4];
    char name[NAME_LEN];
    int is_reg_context;
    int data_size;
} ckpt_header_t;

// ordering of general registers in the 'gregset_t' array
// in the struct mcontext_t which is contained in the struct
// ucontext_t (as uc_mcontext) from glibc in
// sysdeps/x86_64/sys/ucontext.h; NGREG is the number of 
// general registers in the 'gregset_t' array
static char *regs[NGREG] = {
    "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
    "RDI", "RSI", "RBP", "RBX", "RDX", "RAX", "RCX", "RSP",
    "RIP", "EFL", "CSGSFS", "ERR", "TRAPNO", "OLDMASK", "CR2"
};

int
read_ckpt(int ckpt_fd, ckpt_header_t ckpt_headers[], ucontext_t *ucp)
{
    int rc, tmp, i;
    for (i = 0; ; ++i) {
        rc = 0;
        while (rc < CKPT_HEADER_SIZE) {
            if ((tmp = read(ckpt_fd, &ckpt_headers[i] + rc,
                            CKPT_HEADER_SIZE - rc)) < 0) {
                perror("read");
                return -1;
            }
            rc += tmp;
        }
        assert(rc == CKPT_HEADER_SIZE);
        if (ckpt_headers[i].is_reg_context)
            break;
        // use lseek to skip past the actual ckpt data so we only read
        // the ckpt header for general info
        if (lseek(ckpt_fd, ckpt_headers[i].data_size, SEEK_CUR) < 0) {
            perror("lseek");
            return -1;
        }
    }
    rc = 0;
    while (rc < UCONTEXT_SIZE) {
        if ((tmp = read(ckpt_fd, ucp + rc, UCONTEXT_SIZE - rc)) < 0) {
            perror("read");
            return -1;
        }
        rc += tmp;
    }
    assert(rc == UCONTEXT_SIZE);
    return 0;
}

void
print_ckpt_headers(ckpt_header_t ckpt_headers[])
{
    for (int i = 0; ; ++i) {
        printf("%p-%p %c%c%c%c %s %d %d\n",
                ckpt_headers[i].start, ckpt_headers[i].end,
                ckpt_headers[i].rwxp[0], ckpt_headers[i].rwxp[1],
                ckpt_headers[i].rwxp[2], ckpt_headers[i].rwxp[3],
                ckpt_headers[i].name, 
                ckpt_headers[i].is_reg_context,
                ckpt_headers[i].data_size);
        if (ckpt_headers[i].is_reg_context)
            break;
    }
}

void
print_ucontext_regs(ucontext_t *ucp) 
{
    for (int i = 0; i < NGREG; ++i)
        printf("%-12s%p\n", regs[i], ucp->uc_mcontext.gregs[i]);
}

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        puts("Usage: ./readckpt <checkpoint image file>");
        exit(EXIT_FAILURE);
    }
    char *ckpt_file = argv[1];
    int ckpt_fd;
    ckpt_header_t ckpt_headers[MAX_CKPT_HEADERS];
    ucontext_t uc;
    if ((ckpt_fd = open(ckpt_file, O_RDONLY)) < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    if (read_ckpt(ckpt_fd, ckpt_headers, &uc) < 0)
        exit(EXIT_FAILURE);
    print_ckpt_headers(ckpt_headers);
    print_ucontext_regs(&uc);
    close(ckpt_fd);
    exit(EXIT_SUCCESS);
}

