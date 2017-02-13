
/*
 * mpi-fail.c: A simple MPI implementation that can be used to test
 *             debuggers.
 *
 * Compile with 
 * $ mpicc -DABORT=1 -o mpi-fail-w-abort mpi-fail.c
 * $ mpicc -DEXIT=1  -o mpi-fail-w-exit  mpi-fail.c
 */


#include <sys/time.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>


int main(int argc, char **argv)
{
	struct timespec ts;
	int rank, size;

	MPI_Init(&argc, &argv);

	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	ts.tv_sec  = 10;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);

	if (5 == rank) {
#if 1 == ABORT
		MPI_Abort(MPI_COMM_WORLD, 1);
#endif
#if 1 == EXIT
		exit(1);
#endif
	}

	ts.tv_sec  = 10;
	ts.tv_nsec = 0;
	nanosleep(&ts, NULL);

	fprintf(stderr, "fail.\n");
	fflush(0);

	return MPI_Finalize();
}

