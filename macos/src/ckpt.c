/* ckpt.c */
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include "../include/ckpt.h"

int main(int argc, char *argv[])
{
        if (argc < 3) {
                fprintf(stderr,
                        "Usage: ./ckpt [options] [file] [args]\n"
                        "  -p [ckpt-file]          "
                        "Print checkpoint file contents\n"
                        "  -c [executable] [args]  "
                        "Run and inject executable with "
                        "libckpt.dylib\n"
                        "  -r [ckpt-file]          "
                        "Restart process from checkpoint file\n");
                exit(1);
        }

        if (!strncmp(argv[1], "-p", 2)) {
                if (execl("./printckpt", argv[2], NULL) < 0)
                        err(EXIT_FAILURE, "execl");
        } else if (!strncmp(argv[1], "-c", 2)) {
                if (setenv("DYLD_INSERT_LIBRARIES",
                           "./libckpt.dylib", 1) < 0)
                        err(EXIT_FAILURE, "setenv");
                printf("Executing %s, pid: %d\n",
                       argv[1], getpid());
                if (execvp(argv[2], argv + 2) < 0)
                        err(EXIT_FAILURE, "execvp");
        } else if (!strncmp(argv[1], "-r", 2)) {
                printf("Restarting process from: %s\n", argv[2]);
        } else {
                fprintf(stderr,
                        "Unrecognized option: %s\n", argv[1]);
                exit(EXIT_FAILURE);
        }
}
