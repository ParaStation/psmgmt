/*
 *               ParaStation3
 * psispawn.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psispawn.c,v 1.1 2003/02/14 16:30:37 eicker Exp $
 *
 */
/**
 * @file Simple wrapper to allow MPIch/P4 programs to run under control of
 * ParaStation.
 *
 * $Id: psispawn.c,v 1.1 2003/02/14 16:30:37 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 * */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psispawn.c,v 1.1 2003/02/14 16:30:37 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>

#include "pscommon.h"
#include "pshwtypes.h"
#include "psi.h"
#include "psienv.h"
#include "psispawn.h"
#include "info.h"

void usage(char *progname)
{
    fprintf(stderr, "%s: host -l user -n prog [args]\n", progname);
}

int main(int argc, char *argv[])
{
    int worldRank, i, totlen = 0;

    char *host, command;

    if (argc < 6) {
	fprintf(stderr, "You need to give at least five argument\n");
	exit(1);
    }

    host = argv[1]; // @todo test if valid

    if (strstr(argv[2], "-l") != argv[2]) {
	usage(argv[0]);
    }

    {
	struct passwd *passwd;

	passwd = getpwnam(argv[3]);

	if (passwd->pw_uid != getuid()) {
	    fprintf(stderr, "Don't try to change the uid to %s.\n", argv[3]);
	    exit(1);
	}
    }

    if (strstr(argv[4], "-n") != argv[4]) {
	usage(argv[0]);
    }

    worldRank = -1; // get rank from command line.
    for (i=0; i<argc; i++) {
	if (strstr(argv[i], "-p4rmrank") && i<argc-1) {
	    worldRank = atoi(argv[i+1]);
	    break;
	}
    }

    if (worldRank == -1) {
	fprintf(stderr, "Could not determine the rank.\n");
    }

    /* We will use PSI instead of PSE since our task is more low-level */
    /**********************************************************************/ 
    /* init PSI */
    if (!PSI_initClient(TG_SPAWNER)) {
        fprintf(stderr, "Initialization of PSI failed.");
	exit(1);
    }


    fprintf(stderr, "[%d] My TID is %s.",
	    worldRank, PSC_printTID(PSC_getMyTID()));

    /* Propagate some environment variables */
    {
	char *env_str;

	if ((env_str = getenv("HOME"))) {
	    setPSIEnv("HOME", env_str, 1);
	}
	if ((env_str = getenv("USER"))) {
	    setPSIEnv("USER", env_str, 1);
	}
	if ((env_str = getenv("SHELL"))) {
	    setPSIEnv("SHELL", env_str, 1);
	}
	if ((env_str = getenv("TERM"))) {
	    setPSIEnv("TERM", env_str, 1);
	}
    }

    // void PSE_spawnTasks(int num, int node, int port, int argc, char *argv[])
    {
	/* spawning processes */
	int error, ret;
	long spawnedProcess = -1;

	/* get the partition with hardware type none */
	PSI_getPartition(0, worldRank-1);

	/* spawn client processes */
	ret = PSI_spawnM(1, NULL, ".", argc-5, &argv[5],
			 INFO_request_taskinfo(PSC_getTID(-1, getppid()),
					       INFO_LOGGERTID, 0),
			 worldRank, &error, &spawnedProcess);
	if (ret<0) {
	    char *errstr = strerror(error);

	    fprintf(stderr, "Could%s spawn '%s' process %d%s%s.",
		    error ? " not" : " ", argv[0], i+1,
		    error ? ", error = " : "",
		    error ? (errstr ? errstr : "UNKNOWN") : "");
	    exit(1);
	}
    }

    fprintf(stderr, "Spawned process %d.\n", worldRank);

    /* One minute should be sufficient for the client to connect the master */
    sleep(60);

    PSI_exitClient();
    return(0);
}
