/*
 * ParaStation
 *
 * Copyright (C) 2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file mpi_hello: Simple MPI 'hello world' program. This is started
 * repeatedly as a work-load within the stress-testing scripts.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <mpi.h>

int main(int argc,char **argv)
{
    int rank, size, i,j;
    unsigned int sec = 0;
    char host[128];

    /* srandom(getpid()); */
    /* usleep(random() % 500000); */

    MPI_Init(&argc,&argv);

    gethostname(host, sizeof(host));

    if (argc > 1) {
	char *end;
	sec = strtol(argv[1], &end, 0);
	if (*end) {
	    sec = 0;
	}
    }

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size);
    printf("Hi from rank %d %s\n",rank, host);

    for(j = 0; j < 1; j++) {
	for (i = 0; i < size; i++) {
	    if (i == rank && j % 100 == 0) {
		printf("Hello World from rank %d %s (%d)\n",rank, host, j);
	    }
	    MPI_Barrier(MPI_COMM_WORLD);
	}
    }

    if (sec) sleep(sec);

    MPI_Finalize();

    return 0;
}
