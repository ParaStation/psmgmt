
/*
 * hybrid.c: A trivial hybrid MPI/OpenMP applications for tests
 *
 * Compile with
 * $ mpicc -fopenmp/-openmp (depending on compiler -o hybrid hybrid.c
 */

#include <mpi.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>


int main(int argc, char **argv)
{
	int size;
	int rank;
	int nthreads;
	int i;
	struct timespec ts;

	MPI_Init(&argc, &argv);

	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	for (i = 0; i < 10; ++i) {
#pragma omp parallel
		{
			nthreads = omp_get_num_threads();
			printf(" This is thread %d (%d * %d + %d) of %d (%d * %d) reporting.\n",
			       nthreads*rank + omp_get_thread_num(),
			       nthreads, rank, omp_get_thread_num(),
			       nthreads*size,
			       nthreads, size);
		}

		ts.tv_sec  = 1;
		ts.tv_nsec = 0;

		nanosleep(&ts, 0);
	}

	return MPI_Finalize();
}

