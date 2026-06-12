/* 03_nbody.cpp */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <execution>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

static constexpr int    N = 2048;
static constexpr int    STEPS = 10000;
static constexpr double DT = 1e-3;
static constexpr double SOFTENING = 0.01;
static constexpr int    NTHREADS = 8;
static constexpr int    LOG_EVERY = 100;

struct Body {
	double x, y, z;
	double vx, vy, vz;
	double mass;
};

static std::vector<Body>   bodies(N);
static std::vector<double> fx(N), fy(N), fz(N);

static double total_energy(void)
{
	double ke = 0.0, pe = 0.0;
	for (int i = 0; i < N; i++) {
		double v2 = bodies[i].vx * bodies[i].vx +
		            bodies[i].vy * bodies[i].vy +
		            bodies[i].vz * bodies[i].vz;
		ke += 0.5 * bodies[i].mass * v2;
	}
	for (int i = 0; i < N; i++) {
		for (int j = i + 1; j < N; j++) {
			double dx = bodies[j].x - bodies[i].x;
			double dy = bodies[j].y - bodies[i].y;
			double dz = bodies[j].z - bodies[i].z;
			double r = std::sqrt(dx * dx + dy * dy + dz * dz +
			                     SOFTENING * SOFTENING);
			pe -= bodies[i].mass * bodies[j].mass / r;
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
			double r2 = dx * dx + dy * dy + dz * dz +
			            SOFTENING * SOFTENING;
			double inv_r3 = bodies[j].mass / (r2 * std::sqrt(r2));
			ax += dx * inv_r3;
			ay += dy * inv_r3;
			az += dz * inv_r3;
		}
		fx[i] = ax;
		fy[i] = ay;
		fz[i] = az;
	}
}

int main(void)
{
	std::mt19937                     rng(42);
	std::uniform_real_distribution<> pos(-1.0, 1.0);
	std::uniform_real_distribution<> vel(-0.1, 0.1);
	std::uniform_real_distribution<> mass(0.5, 1.5);

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

        int chunk = N / NTHREADS;
        std::vector<std::thread> workers(NTHREADS);

        for (int step = 0; step < STEPS; step++) {
		for (int t = 0; t < NTHREADS; t++) {
			int start = t * chunk;
			int end = (t == NTHREADS - 1) ? N : start + chunk;
			workers[t] = std::thread(compute_forces, start, end);
		}
		for (auto &w : workers)
			w.join();

		for (int i = 0; i < N; i++) {
        		bodies[i].vx += fx[i] * DT;
        		bodies[i].vy += fy[i] * DT;
        		bodies[i].vz += fz[i] * DT;
        		bodies[i].x  += bodies[i].vx * DT;
        		bodies[i].y  += bodies[i].vy * DT;
        		bodies[i].z  += bodies[i].vz * DT;
		}

		if (step % LOG_EVERY == 0) {
			double e = total_energy();
			printf("step %5d | E = %.6e | dE/E0 = %.3e\n",
			       step, e, (e - e0) / std::abs(e0));
			fflush(stdout);
		}
        }

        printf("Final energy: %.6e | drift: %.3e\n",
               total_energy(), (total_energy() - e0) / std::abs(e0));
        return 0;
}
