/* 01_conjugate_gradient.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <err.h>

typedef unsigned long ulong;

static void usage(void)
{
        printf("Usage: ./01_conjugate_gradient [options]\n"
               " -i, --iterations NUMBER (default: 10000)\n"
               " -e, --epochs NUMBER (default: 20)\n"
               " -h, --help\n");
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n);

	if (!p)
		err(EXIT_FAILURE, "malloc");

	return p;
}

static void gen_spd(double *A, ulong N)
{
	double *M = xmalloc(N * N * sizeof(double));

	for (ulong i = 0; i < N * N; i++)
		M[i] = (double)rand() / RAND_MAX - 0.5;

	for (ulong i = 0; i < N; i++) {
		for (ulong j = 0; j < N; j++) {
			double sum = 0;
			for (ulong k = 0; k < N; k++)
				sum += M[k * N + i] * M[k * N + j];
			A[i * N + j] = sum;
		}
	}

	for (ulong i = 0; i < N; i++)
		A[i * N + i] += N;

	free(M);
}

static void matvec(const double *A, const double *x, double *y, ulong N)
{
	for (ulong i = 0; i < N; i++) {
		double sum = 0;
		for (ulong j = 0; j < N; j++)
			sum += A[i * N + j] * x[j];
		y[i] = sum;
	}
}

static double dot(const double *a, const double *b, ulong N)
{
	double sum = 0;

	for (ulong i = 0; i < N; i++)
		sum += a[i] * b[i];

	return sum;
}

static void cg(ulong N, int iters, double tolerance)
{
	double *A = xmalloc(N * N * sizeof(double));
	double *b = xmalloc(N * sizeof(double));
	double *x = xmalloc(N * sizeof(double));
	double *r = xmalloc(N * sizeof(double));
	double *p = xmalloc(N * sizeof(double));
	double *Ap = xmalloc(N * sizeof(double));

	srand(time(NULL));
	gen_spd(A, N);
	for (ulong i = 0; i < N; i++) {
		b[i] = (double)rand() / RAND_MAX;
		x[i] = 0;
	}

	memcpy(r, b, N * sizeof(double));
	memcpy(p, r, N * sizeof(double));
	double rsold = dot(r, r, N);

	int iter;
	for (iter = 0; iter < iters; iter++) {
		matvec(A, p, Ap, N);
		double alpha = rsold / dot(p, Ap, N);
		for (ulong i = 0; i < N; i++) {
			x[i] += alpha * p[i];
			r[i] -= alpha * Ap[i];
		}

		double rsnew = dot(r, r, N);
		double residual = sqrt(rsnew);

		if (residual < tolerance) {
			printf("Converged at iteration %d: "
			       "Residual=%.4e\n",
			       iter,
			       residual);
			break;
		}

		for (ulong i = 0; i < N; i++)
			p[i] = r[i] + (rsnew / rsold) * p[i];
		rsold = rsnew;
	}

	if (iter == iters) {
		printf("Final iteration (%d): Residual=%.4e\n",
		       iter,
		       sqrt(rsold));
	}

	free(A);
	free(b);
	free(x);
	free(r);
	free(p);
	free(Ap);
}

int main(int argc, char *argv[])
{
	int    iters = 10000, epochs = 20;
	ulong  N = 1u << 10;
	double tolerance;

        argv++;
        argc--;
        while (argc) {
                if (strcmp(argv[0], "-i") == 0 ||
                    strcmp(argv[0], "--iterations") == 0) {
                        iters = atoi(argv[1]);
                        argv++; argv++;
                        argc--; argc--;
                } else if (strcmp(argv[0], "-e") == 0 ||
                           strcmp(argv[0], "--epochs") == 0) {
                        epochs = atoi(argv[1]);
                        argv++; argv++;
                        argc--; argc--;
                } else if (strcmp(argv[0], "-h") == 0 ||
                           strcmp(argv[0], "--help") == 0) {
                        usage();
                        exit(0);
                } else {
                        printf("Unrecognized argument: %s\n", argv[0]);
                        usage();
                        exit(0);
                }
        }

	srand48(time(NULL));
        printf("CG solver (epoch=%d, iterations=%d)\n", epochs, iters);

	for (int i = 0; i < epochs; i++) {
		tolerance = fmod(drand48(), 1e-11);
		printf("Starting epoch %d:\n", i);
		cg(N, iters, tolerance);
	}

	printf("%s exiting...\n", __FILE__);
	exit(0);
}
