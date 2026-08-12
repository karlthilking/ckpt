/* 03_nbody.cpp */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <execution>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

struct Body {
	double x, y, z;
	double vx, vy, vz;
	double mass;
};

static int N = 1 << 11;
static int steps = 25000, log_every = 250;

static std::vector<Body> bodies;
static std::vector<double> fx, fy, fz;

static double dt = 1e-3, softening = 0.01;
static int nthreads = std::thread::hardware_concurrency();

static void usage(void)
{
        printf("Usage: ./03_nbody [options]\n"
               " -n NUMBER (default: 2048)\n"
               " -s, --steps NUMBER (default: 10000)\n"
               " -t, --threads NUMBER (default: # of cores)\n"
               " -h, --help\n");
}

static double total_energy(void)
{
        double ke = 0.0, pe = 0.0;

        for (auto &b : bodies) {
                double v2 = b.vx * b.vx + b.vy * b.vy + b.vz * b.vz;
                ke += 0.5 * b.mass * v2;
        }

        for (auto &bi : bodies) {
                for (auto &bj : bodies) {
                        double dx = bj.x - bi.x;
                        double dy = bj.y - bi.y;
                        double dz = bj.z - bi.z;
                        double r = std::sqrt(
                                dx * dx + dy * dy + dz * dz +
                                softening * softening
                        );
                        pe -= bi.mass * bj.mass / r;
                }
        }

        return ke + pe;
}

static void compute_forces(int start, int end)
{
	for (int i = start; i < end; i++) {
		double ax = 0, ay = 0, az = 0;
		for (int j = 0; j < N; j++) {
			if (i == j)
				continue;
			double dx = bodies[j].x - bodies[i].x;
			double dy = bodies[j].y - bodies[i].y;
			double dz = bodies[j].z - bodies[i].z;

			double r2 =
                                dx * dx + dy * dy + dz * dz +
			        softening * softening;

                        double inv_r3 =
                                bodies[j].mass / (r2 * std::sqrt(r2));

			ax += dx * inv_r3;
			ay += dy * inv_r3;
			az += dz * inv_r3;
		}
		fx[i] = ax;
		fy[i] = ay;
		fz[i] = az;
	}
}

int main(int argc, char *argv[])
{
        const char *progname = argv[0];
	std::mt19937 rng(42);
	std::uniform_real_distribution<> pos(-1.0, 1.0);
	std::uniform_real_distribution<> vel(-0.1, 0.1);
	std::uniform_real_distribution<> mass(0.5, 1.5);

        argv++;
        argc--;
        while (argc) {
                if (strcmp(argv[0], "-t") == 0 ||
                    strcmp(argv[0], "--threads") == 0) {
                        nthreads = std::atoi(argv[1]);
                        argv++; argv++;
                        argc--; argc--;
                } else if (strcmp(argv[0], "-n") == 0) {
                        N = std::atoi(argv[1]);
                        argv++; argv++;
                        argc--; argc--;
                } else if (strcmp(argv[0], "-s") == 0 ||
                           strcmp(argv[0], "--steps") == 0) {
                        steps = std::atoi(argv[1]);
                        argv++; argv++;
                        argc--; argc--;
                } else {
                        printf("Unrecognized argument: %s\n", argv[0]);
                        usage();
                        exit(0);
                }
        }

        printf("%s (N: %d, steps: %d, threads: %d)\n",
               progname, N, steps, nthreads);

        auto t0 = std::chrono::high_resolution_clock::now();
        bodies = std::vector<Body>(N);
        fx = std::vector<double>(N);
        fy = std::vector<double>(N);
        fz = std::vector<double>(N);

	for (auto &b : bodies) {
		b.x = pos(rng);
		b.y = pos(rng);
		b.z = pos(rng);
		b.vx = vel(rng);
		b.vy = vel(rng);
		b.vz = vel(rng);
		b.mass = mass(rng);
	}

	double e0 = total_energy();
	printf("Initial energy: %.6e\n", e0);

        int chunk = N / nthreads;
        std::vector<std::thread> workers(nthreads);

	for (int step = 0; step < steps; step++) {
                for (int t = 0; t < nthreads; t++) {
                        int start = t * chunk;
                        int end = (t + 1 == nthreads) ? N : start + chunk;
                        workers[t] = std::thread(
                                compute_forces, start, end
                        );
                }
		
		for (auto &w : workers)
			w.join();

		for (int i = 0; i < N; i++) {
			bodies[i].vx += fx[i] * dt;
			bodies[i].vy += fy[i] * dt;
			bodies[i].vz += fz[i] * dt;
			bodies[i].x += bodies[i].vx * dt;
			bodies[i].y += bodies[i].vy * dt;
			bodies[i].z += bodies[i].vz * dt;
		}

		if (step % log_every == 0) {
			double e = total_energy();
                        printf("Step %5d | E = %.6e | dE/E0 = %.3e\n",
                               step, e, (e - e0) / std::abs(e0));
			fflush(stdout);
		}
	}

        auto t1 = std::chrono::high_resolution_clock::now();
        auto t = std::chrono::duration_cast<std::chrono::seconds>(t1 - t0);
	printf("Final energy: %.6e | drift: %.3e | time: %lld\n",
	       total_energy(), (total_energy() - e0) / std::abs(e0),
               t.count());
	return 0;
}
