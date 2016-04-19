
/*
 * Trivial MPI_Comm_spawn test. Compile with mpicc -o mpi-comm-spawn.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>


int main(int argc, char **argv)
{
	int err[3];
	MPI_Comm parentcomm, intercomm;

	MPI_Init(&argc, &argv);
	MPI_Comm_get_parent(&parentcomm);
	
	if (MPI_COMM_NULL == parentcomm) {
		MPI_Comm_spawn(argv[0], MPI_ARGV_NULL, 3, MPI_INFO_NULL, 0, MPI_COMM_WORLD, &intercomm, err);
		printf("OK P\n");
	} else {
		printf("OK S\n");
	}

	return MPI_Finalize();
}

