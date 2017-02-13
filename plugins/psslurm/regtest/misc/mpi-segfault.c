
/*
 * mpi-segfault.c: A simple MPI implementation that can be used to test
 *                 debuggers.
 *
 * Compile with 
 * $ mpicc -DRANK=5 -o mpi-segfault mpi-segfault.c
 */

#include <mpi.h>
#include <time.h>

int main(int argc, char **argv)
{
	int rank;
	struct timespec ts;
	volatile int *zp = 0;
	int fault;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	ts.tv_sec  = 10;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);

	if (RANK == rank)
		fault = *zp;

	return MPI_Finalize();
}

