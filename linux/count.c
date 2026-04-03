/* count.c */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

int
main(int argc, char *argv[])
{
    int start = (argc > 1) ? atoi(argv[1]) : 0;
    while (1) {
        printf("%d ", start);
        fflush(stdout);
        usleep(150000);
        start = ++start % INT_MAX;
    }
    return EXIT_SUCCESS;
}
