/* vm.c */
#define _XOPEN_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ucontext.h>
#include "../include/ckpt.h"

int restore_mem_rgn(int fd, off_t offset, mem_rgn_t *r)
{
        void *rc;

        rc = mmap((void *)r->start,
                  (size_t)r->size,
                  PROT_READ | PROT_WRITE | PROT_EXEC,
                  MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE,
                  -1, 0);
        if (rc == MAP_FAILED) {
                perror("mmap");
                return -1;
        } else if ((mach_vm_address_t)rc != r->start) {
                fprintf(stderr,
                        "mmap(...,MAP_FIXED,...) could not allocate "
                        "memory at the original address.\n"
                        "Desired: %llu\nActual: %llu\n",
                        (u64)r->start, (u64)rc);
                return -1;
        }

        if (lseek(fd, offset, SEEK_SET) < 0) {
                perror("lseek");
                return -1;
        }
        
        if (read_data(fd, rc, (size_t)r->size) < 0)
                return -1;

        return 0;
}
