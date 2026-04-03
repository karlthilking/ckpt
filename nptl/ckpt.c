#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>

#define LIBCKPT "./testlib.so"

int main(int argc, char *argv[])
{
        if (argc < 2) {
                fprintf(stderr, "Usage: ./ckpt <executable> "
                                "<args>\n");
                exit(1);
        }
        setenv("LD_PRELOAD", LIBCKPT, 1);
        if (execvp(argv[1], argv + 1) < 0)
                err(EXIT_FAILURE, "execvp");
}
