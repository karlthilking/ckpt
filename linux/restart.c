/* restart.c */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ucontext.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include <sys/auxv.h>

#define NAME_LEN            128
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

void
recursive(int levels)
{
    if (levels > 0)
        recursive(levels - 1);
}

int
restore_segment(int ckpt_fd, ckpt_header_t *ckpt_header)
{
    void *addr;
    size_t len;
    int rc, tmp, prot, flags, fd;
    len = (size_t)(ckpt_header->end - ckpt_header->start);
    prot = PROT_NONE;
    prot |= (ckpt_header->rwxp[0] == 'r') ? PROT_READ : PROT_NONE;
    prot |= (ckpt_header->rwxp[1] == 'w') ? PROT_WRITE : PROT_NONE;
    prot |= (ckpt_header->rwxp[2] == 'x') ? PROT_EXEC : PROT_NONE;
    flags = MAP_FIXED;
    flags |= (ckpt_header->rwxp[3] == 'p') ? MAP_PRIVATE : MAP_SHARED;
    if (!strcmp(ckpt_header->name, "[stack]")) {
        prot |= PROT_GROWSDOWN;
        flags |= MAP_GROWSDOWN;
    }
    // any name starting with [ should not be backed by any file
    // eg. [stack], [heap], [vdso], etc
    if (!strcmp(ckpt_header->name, "ANONYMOUS_SEGMENT") ||
        ckpt_header->name[0] == '[') {
        flags |= MAP_ANONYMOUS;
        fd = -1;
    } else {
        if ((fd = open(ckpt_header->name, O_RDONLY)) < 0) {
            fprintf(stderr, "open: %s, filename: %s", 
                    strerror(errno), ckpt_header->name);
            return -1;
        }
    }
    // mmap segment giving it full permissions temporarily
    if ((addr = mmap(ckpt_header->start, len, PROT_READ | PROT_WRITE |
                     PROT_EXEC, flags, fd, 0)) == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    rc = 0;
    while (rc < ckpt_header->data_size) {
        if ((tmp = read(ckpt_fd, addr + rc, 
                        ckpt_header->data_size - rc)) < 0) {
            perror("read");
            return -1;
        }
        rc += tmp;
    }
    assert(rc == ckpt_header->data_size);
    // reset mmap'ed segment permissions to reflect the permissions
    // specified by rwxp in the ckpt_header
    if (mprotect(ckpt_header->start, len, prot) < 0) {
        perror("mprotect");
        return -1;
    }
    return 0;
}

int
read_ckpt(int ckpt_fd, ckpt_header_t ckpt_headers[], ucontext_t *ucp)
{
    int rc, tmp;
    for (int i = 0; ; ++i) {
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
        if (restore_segment(ckpt_fd, &ckpt_headers[i]) < 0)
            return -1;
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

int
main(int argc, char *argv[])
{
    ucontext_t uc;
    int ckpt_fd;
    char *ckpt_file;
    ckpt_header_t ckpt_headers[MAX_CKPT_HEADERS];
    if (argc < 2) {
        puts("Usage: ./restart <checkpoint image file>");
        exit(EXIT_FAILURE);
    }
    ckpt_file = argv[1];
    // recursive function with 1000 iterations to grow stack
    recursive(1000);
    if ((ckpt_fd = open(ckpt_file, O_RDONLY)) < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    if (read_ckpt(ckpt_fd, ckpt_headers, &uc) < 0)
        exit(EXIT_FAILURE);
    // a successful call to setcontext should not return because
    // it should start executing from where getcontext was called
    // when the original context was saved
    if (setcontext(&uc) < 0) {
        perror("setcontext");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}
