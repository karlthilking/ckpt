/* ckpt.c */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

int
main(int argc, char *argv[])
{
    setenv("LD_PRELOAD", "./libckpt.so", 1);
    if (argc < 1) {
        puts("Usage: ./ckpt <executable>");
        exit(EXIT_FAILURE);
    }
    if (execvp(argv[1], &argv[1]) < 0) {
        perror("execvp");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}
