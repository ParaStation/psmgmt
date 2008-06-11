/*
 *               ParaStation
 *
 * Copyright (C) 2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * mpi_startup: Simple MPI 'hello world' program. This is started
 * repeatedly as a work-load within the timing-test of the startup.
 *
 * $Id$ 
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <mpi.h>

int main(int argc,char **argv)
{
    int rank, size;
    char host[128];

    MPI_Init(&argc,&argv);

    gethostname(host, sizeof(host));

    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    MPI_Comm_size( MPI_COMM_WORLD, &size);

    printf("Hi from rank %d/%d %s\n",rank, size, host);

    MPI_Finalize();

    return 0;
}
