/* 02_heat_diffusion.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <omp.h>

typedef unsigned long ulong;

void *xmalloc(size_t n)
{
	void *p = malloc(n);
	if (!p) {
		perror("malloc");
		exit(1);
	}
	return p;
}

static void initialize(double *grid, ulong nx, ulong ny)
{
        #pragma omp parallel for
	for (ulong i = 0; i < nx * ny; i++) {
		grid[i] = 0.0;
	}

	ulong cx = nx / 2, cy = ny / 2;
	ulong rx = nx / 10, ry = ny / 10;

        #pragma omp parallel for
	for (ulong y = cy - ry; y < cy + ry; y++) {
		for (ulong x = cx - rx; x < cx + rx; x++) {
			grid[y * nx + x] = 100.0;
		}
	}
}

static void step(const double *restrict ogrid, double *restrict ngrid,
                 ulong nx, ulong ny, double alpha)
{
        /* T_t+1 = T_t + alpha * (T_l + T_r + T_u + T_d - 4 * T_t) */
        #pragma omp parallel for collapse(2) schedule(static)
        for (ulong y = 1; y < ny - 1; y++) {
                for (ulong x = 1; x < nx - 1; x++) {
                        ulong idx = y * nx + x;
                        double T = ogrid[idx];
                        double l = ogrid[idx - 1];
                        double r = ogrid[idx + 1];
                        double u = ogrid[idx - nx];
                        double d = ogrid[idx + nx];
                        ngrid[idx] = T + alpha * (l + r + u + d - 4 * T);
                }
        }
}

static void compute_stats(const double *grid, ulong nx, ulong ny, 
                          double *total, double *max_T, double *mean_T)
{
	double sum = 0;
	double mx = 0;

        #pragma omp parallel for reduction(+ : sum) reduction(max : mx)
	for (ulong i = 0; i < nx * ny; i++) {
		sum += grid[i];
		if (grid[i] > mx)
			mx = grid[i];
	}

	*total = sum;
	*max_T = mx;
	*mean_T = sum / (nx * ny);
}

int main(int argc, char *argv[])
{
        ulong nx, ny, steps;

        nx = ny = 1 << 9;
        steps = (argc > 1) ? strtoul(argv[1], NULL, 10) : 100000u;
        
        printf("Heat diffusion: %lu x %lu grid, %lu steps\n", 
               nx, ny, steps);
        printf("# of threads: %d\n", omp_get_max_threads());
        fflush(stdout);
        
        double *grid_a = xmalloc(nx * ny * sizeof(double));
        double *grid_b = xmalloc(nx * ny * sizeof(double));
        
        initialize(grid_a, nx, ny);
        memcpy(grid_b, grid_a, nx * ny * sizeof(double));
        
        const double alpha = 0.2;
        
        double *cur = grid_a;
        double *next = grid_b;
        
        for (ulong s = 0; s < steps; s++) {
            	step(cur, next, nx, ny, alpha);

            	double *tmp = cur;
            	cur = next;
            	next = tmp;

            	if (s % 5000 == 0) {
            		double total, max_T, mean_T;
            		compute_stats(cur, nx, ny, &total, &max_T, &mean_T);
            		printf("step %6lu: total=%.6e max=%.4f "
                               "mean=%.4e\n", s, total, max_T, mean_T);
            		fflush(stdout);
            	}
        }
        
        double total, max_T, mean_T;
        compute_stats(cur, nx, ny, &total, &max_T, &mean_T);
        printf("Final: total=%.6e max=%.4f mean=%.4e\n", 
               total, max_T, mean_T);
        
        free(grid_a);
        free(grid_b);

        printf("%s exiting...\n", __FILE__);
        exit(0);
}
