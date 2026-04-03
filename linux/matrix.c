/* matrix.c */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int interval, w;

void
display_matrix(int matrix[w * w])
{
    for (int i = 0; i < w; ++i) {
        for (int j = 0; j < w; ++j)
            printf("%-5d", matrix[i * w + j]);
        putchar('\n');
    }
}

int
main(int argc, char* argv[])
{
    interval = (argc > 1) ? atoi(argv[1]) : 10000;
    w = (argc > 2) ? atoi(argv[2]) : 5;
    int matrix[w * w];
    memset(&matrix, 0, sizeof(matrix));
    for (int i = 0; ; i = ++i % (w * w)) {
        system("clear");
        ++matrix[i];
        display_matrix(matrix);
        usleep(interval);
    }
    exit(EXIT_SUCCESS);
}
