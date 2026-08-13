/* 04_jacobi.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#define TOSTRING_X(s) #s
#define TOSTRING(s) TOSTRING_X (s)

#define __xpthread_op(op, ...)						\
  do									\
    {									\
      int __e = op (__VA_ARGS__);					\
      if (__builtin_expect (__e, 0))					\
	{								\
	  fprintf (stderr, "%s: %s\n", TOSTRING (op), strerror (__e));	\
	  exit (-1);							\
	}								\
    }									\
  while (0)

#define xpthread_create(thread, attr, start_routine, arg) \
  __xpthread_op (pthread_create, thread, attr, start_routine, arg)
#define xpthread_join(thread, value_ptr) \
  __xpthread_op (pthread_join, thread, value_ptr)
#define xpthread_exit(value_ptr) \
  pthread_exit (value_ptr)
#define xpthread_mutex_init(mutex, attr) \
  __xpthread_op (pthread_mutex_init, mutex, attr)
#define xpthread_mutex_lock(mutex) \
  __xpthread_op (pthread_mutex_lock, mutex)
#define xpthread_mutex_unlock(mutex) \
  __xpthread_op (pthread_mutex_unlock, mutex)
#define xpthread_mutex_destroy(mutex) \
  __xpthread_op (pthread_mutex_destroy, mutex)
#define xpthread_cond_init(cond, attr) \
  __xpthread_op (pthread_cond_init, cond, attr)
#define xpthread_cond_wait(cond, mutex) \
  __xpthread_op (pthread_cond_wait, cond, mutex)
#define xpthread_cond_broadcast(cond) \
  __xpthread_op (pthread_cond_broadcast, cond)
#define xpthread_cond_destroy(cond) \
  __xpthread_op (pthread_cond_destroy, cond)

#define __xalloc(op, ...)				\
  ({							\
    void *__p = op (__VA_ARGS__);			\
    if (__builtin_expect (!__p, 0))			\
      {							\
	fprintf (stderr, "%s failed\n", TOSTRING (op)); \
	exit (-1);					\
      }							\
    __p;						\
  })

#define xmalloc(size) __xalloc (malloc, size)
#define xcalloc(nmemb, size) __xalloc (calloc, nmemb, size)
#define xrealloc(ptr, size) __xalloc (realloc, ptr, size)
#define xfree(ptr) free (ptr)

static int N = 2048;
static int iters = 10000;
static int nthreads = 8;
static int conv_check = 250;

struct barrier
{
  pthread_mutex_t lock;
  pthread_cond_t cond;
  int count;
  int total;
  int generation;
};

static void
usage_and_exit (void)
{
  static const char *help =
    "Usage: ./04_jacobi [options]\n"
    "Options:\n"
    " -t NUMBER\n"
    "   Number of worker threads\n"
    " -i NUMBER\n"
    "   Number of iterations\n"
    " -n NUMBER\n"
    "   Grid x and y dimension\n";

  printf ("%s", help);
  exit (0);
}

static void
barrier_init (struct barrier *b, int n)
{
  xpthread_mutex_init (&b->lock, NULL);
  xpthread_cond_init (&b->cond, NULL);
  b->count = 0;
  b->total = n;
  b->generation = 0;
}

static void
barrier_wait (struct barrier *b)
{
  int gen;

  xpthread_mutex_lock (&b->lock);
  gen = b->generation;
  b->count++;

  if (b->count == b->total)
    {
      b->count = 0;
      b->generation++;
      xpthread_cond_broadcast (&b->cond);
    }
  else
    {
      while (gen == b->generation)
	xpthread_cond_wait (&b->cond, &b->lock);
    }

  xpthread_mutex_unlock (&b->lock);
}

static void
barrier_destroy (struct barrier *b)
{
  xpthread_mutex_destroy (&b->lock);
  xpthread_cond_destroy (&b->cond);
}

static double **grid[2];
static int cur = 0;
static struct barrier barrier;
static volatile int iteration = 0;
static volatile int done = 0;
static double global_diff;
static pthread_mutex_t diff_lock = PTHREAD_MUTEX_INITIALIZER;

static void
grid_init (void)
{
  grid[0] = xmalloc (N * sizeof(double *));
  grid[1] = xmalloc (N * sizeof(double *));

  for (int i = 0; i < N; i++)
    {
      grid[0][i] = xcalloc (N, sizeof(double));
      grid[1][i] = xcalloc (N, sizeof(double));
    }
}

static void
grid_destroy (void)
{
  for (int i = 0; i < N; i++)
    {
      xfree (grid[0][i]);
      xfree (grid[1][i]);
    }

  xfree (grid[0]);
  xfree (grid[1]);
}

struct worker_args
{
  int tid;
  int row_start;
  int row_end;
};

static void
boundary_init (void)
{
  int i, j;

  for (j = 0; j < N; j++)
    {
      grid[0][0][j] = 1.0;
      grid[1][0][j] = 1.0;
      grid[0][N - 1][j] = 0.0;
      grid[1][N - 1][j] = 0.0;
    }
  for (i = 0; i < N; i++)
    {
      grid[0][i][0] = 0.0;
      grid[1][i][0] = 0.0;
      grid[0][i][N - 1] = 0.0;
      grid[1][i][N - 1] = 0.0;
    }
}

static void *
worker (void *arg)
{
  struct worker_args *w = (struct worker_args *)arg;

  while (!done)
    {
      int next = 1 - cur;
      double local_diff = 0.0;
      for (int i = w->row_start; i < w->row_end; i++)
	{
	  for (int j = 1; j < N - 1; j++)
	    {
	      double val = 0.25 * (grid[cur][i - 1][j]
				   + grid[cur][i + 1][j]
				   + grid[cur][i][j - 1]
				   + grid[cur][i][j + 1]);
	      grid[next][i][j] = val;
	      double d = val - grid[cur][i][j];
	      local_diff += d * d;
	    }
	}

      barrier_wait (&barrier);
      if (w->tid == 0)
	global_diff = 0.0;

      barrier_wait (&barrier);
      xpthread_mutex_lock (&diff_lock);
      global_diff += local_diff;
      xpthread_mutex_unlock (&diff_lock);

      barrier_wait (&barrier);
      if (w->tid == 0)
	{
	  iteration++;
	  cur = next;
	  if (iteration % conv_check == 0)
	    {
	      double rmsd = sqrt (global_diff / ((N - 2) * (N - 2)));
	      printf ("iter %5d | rmsd = %.6e\n", iteration, rmsd);
	      fflush (stdout);
	      if (rmsd < 1e-6 || iteration >= iters)
		done = 1;
	    }
	}

      barrier_wait (&barrier);
    }

  xpthread_exit (NULL);
}

int
main (int argc, char *argv[])
{
  int rows_per;
  pthread_t *threads = NULL;
  struct worker_args *args = NULL;

#define shift argv++; argc--
  shift;
  while (argc)
    {
      if (strncmp (argv[0], "-t", 2) == 0)
	{
	  nthreads = atoi (argv[1]);
	  shift; shift;
	}
      else if (strncmp (argv[0], "-i", 2) == 0)
	{
	  iters = atoi (argv[1]);
	  shift; shift;
	}
      else if (strncmp (argv[0], "-g", 2) == 0)
	{
	  N = atoi (argv[1]);
	  shift; shift;
	}
      else
	{
	  fprintf (stderr, "Unrecognized argument: %s\n", argv[0]);
	  usage_and_exit ();
	}
    }

  args = xmalloc (sizeof(*args) * nthreads);
  threads = xmalloc (sizeof(pthread_t) * nthreads);

  grid_init ();
  boundary_init ();
  barrier_init (&barrier, nthreads);

  rows_per = (N - 2) / nthreads;
  for (int t = 0; t < nthreads; t++)
    {
      args[t].tid = t;
      args[t].row_start = 1 + t * rows_per;
      args[t].row_end = (t == nthreads - 1
			 ? N - 1
			 : 1 + (t + 1) * rows_per);

      xpthread_create (&threads[t], NULL, worker, &args[t]);
    }

  for (int t = 0; t < nthreads; t++)
    xpthread_join (threads[t], NULL);

  printf ("done: iter=%d, center=%.6f\n",
	  iteration, grid[cur][N >> 1][N >> 1]);

  barrier_destroy (&barrier);
  grid_destroy ();
  xfree (threads);
  xfree (args);
  exit (0);
}
