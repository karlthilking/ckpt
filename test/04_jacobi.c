/* 04_jacobi.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#define N           2048 
#define MAX_ITER    5000
#define NTHREADS    8
#define CONV_CHECK  100

typedef struct {
        pthread_mutex_t lock;
        pthread_cond_t  cond;
        int             count;
        int             total;
        int             generation;
} barrier_t;

static void barrier_init(barrier_t *b, int n)
{
        pthread_mutex_init(&b->lock, NULL);
        pthread_cond_init(&b->cond, NULL);
        b->count      = 0;
        b->total      = n;
        b->generation = 0;
}

static void barrier_wait(barrier_t *b)
{
        pthread_mutex_lock(&b->lock);
        int gen = b->generation;
        b->count++;
        if (b->count == b->total) {
                b->count = 0;
                b->generation++;
                pthread_cond_broadcast(&b->cond);
        } else {
                while (gen == b->generation)
                        pthread_cond_wait(&b->cond, &b->lock);
        }
        pthread_mutex_unlock(&b->lock);
}

static void barrier_destroy(barrier_t *b)
{
        pthread_mutex_destroy(&b->lock);
        pthread_cond_destroy(&b->cond);
}

static double        grid[2][N][N];
static int           cur = 0;
static barrier_t     barrier;
static volatile int  iteration = 0;
static volatile int  done      = 0;
static double        global_diff;
static pthread_mutex_t diff_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
        int tid;
        int row_start;
        int row_end;
} worker_arg_t;

static void boundary_init(void)
{
        for (int j = 0; j < N; j++) {
                grid[0][0][j]   = 1.0;
                grid[1][0][j]   = 1.0;
                grid[0][N-1][j] = 0.0;
                grid[1][N-1][j] = 0.0;
        }
        for (int i = 0; i < N; i++) {
                grid[0][i][0]   = 0.0;
                grid[1][i][0]   = 0.0;
                grid[0][i][N-1] = 0.0;
                grid[1][i][N-1] = 0.0;
        }
}

static void *worker(void *arg)
{
        worker_arg_t *w = (worker_arg_t *)arg;

        while (!done) {
                int next = 1 - cur;

                double local_diff = 0.0;
                for (int i = w->row_start; i < w->row_end; i++) {
                        for (int j = 1; j < N-1; j++) {
                                double val = 0.25 * (
                                        grid[cur][i-1][j] +
                                        grid[cur][i+1][j] +
                                        grid[cur][i][j-1] +
                                        grid[cur][i][j+1]);
                                grid[next][i][j] = val;
                                double d = val - grid[cur][i][j];
                                local_diff += d * d;
                        }
                }

                barrier_wait(&barrier);
                if (w->tid == 0)
                        global_diff = 0.0;

                barrier_wait(&barrier);
                pthread_mutex_lock(&diff_lock);
                global_diff += local_diff;
                pthread_mutex_unlock(&diff_lock);

                barrier_wait(&barrier);
                if (w->tid == 0) {
                        iteration++;
                        cur = next;
                        if (iteration % CONV_CHECK == 0) {
                                double rmsd = sqrt(global_diff
                                                   / ((N-2) * (N-2)));
                                printf("iter %5d | rmsd = %.6e\n",
                                       iteration, rmsd);
                                fflush(stdout);
                                if (rmsd < 1e-6 || iteration >= MAX_ITER)
                                        done = 1;
                        }
                }

                barrier_wait(&barrier);
        }

        return NULL;
}

int main(void)
{
        pthread_t    threads[NTHREADS];
        worker_arg_t args[NTHREADS];

        memset(grid, 0, sizeof(grid));
        boundary_init();
        barrier_init(&barrier, NTHREADS);

        int rows_per = (N - 2) / NTHREADS;
        for (int t = 0; t < NTHREADS; t++) {
                args[t].tid       = t;
                args[t].row_start = 1 + t * rows_per;
                args[t].row_end   = (t == NTHREADS - 1)
                                  ? N - 1
                                  : 1 + (t + 1) * rows_per;
                pthread_create(&threads[t], NULL, worker, &args[t]);
        }

        for (int t = 0; t < NTHREADS; t++)
                pthread_join(threads[t], NULL);

        printf("done: iter=%d  center=%.6f\n",
               iteration, grid[cur][N/2][N/2]);

        barrier_destroy(&barrier);
        return 0;
}
