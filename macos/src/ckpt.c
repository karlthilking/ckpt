/* ckpt.c */
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include "../include/ckpt.h"

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr,
                        "Usage: ./ckpt <executable> <args>\n");
                exit(1);
        }
        
        if (setenv("DYLD_INSERT_LIBRARIES", 
                   "./libckpt.dylib", 1) < 0)
                err(EXIT_FAILURE, "setenv");
        
        printf("Executing %s, pid: %d\n", argv[1], getpid());
        if (execvp(argv[1], argv + 1) < 0)
                err(EXIT_FAILURE, "execvp");
}
