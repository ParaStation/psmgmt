#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <mpi.h>

int main(int argc,char **argv)
{
    int rank, size, i,j;
    char host[16];

    srandom(getpid());
    usleep(random() % 2500000);

    MPI_Init(&argc,&argv);

    gethostname(host, sizeof(host));

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size);
    printf("Hi from rank %d %s\n",rank, host);

    for(j = 0; j < 100; j++) {
	for (i = 0; i < size; i++) {
	    if (i == rank && j % 1000 == 0) {
		printf("Hello World from rank %d %s (%d)\n",rank, host, j);
	    }
	    MPI_Barrier(MPI_COMM_WORLD);
	}
    }

    MPI_Finalize();

    return 0;
}
