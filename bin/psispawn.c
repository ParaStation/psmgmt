/*
 *               ParaStation3
 * psispawn.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psispawn.c,v 1.6 2003/07/31 17:56:40 eicker Exp $
 *
 */
/**
 * @file Simple wrapper to allow MPIch/P4 programs to run under the
 * control of ParaStation.
 *
 * $Id: psispawn.c,v 1.6 2003/07/31 17:56:40 eicker Exp $
 *
 * @author Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psispawn.c,v 1.6 2003/07/31 17:56:40 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>

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
    int worldRank, i;

    char *host;

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

    /* Replace '\-' at begin of argument by '-' */
    for (i=5; i<argc; i++) {
	if (argv[i][0] == '\\' && argv[i][1] == '-') {
	    argv[i]++;
	}
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
	exit(1);
    }

    signal(SIGHUP,  SIG_IGN);
    signal(SIGINT,  SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    /* We will use PSI instead of PSE since our task is more low-level */
    if (!PSI_initClient(TG_SPAWNER)) {
        fprintf(stderr, "Initialization of PSI failed.");
	exit(1);
    }

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

    /* spawning the process */
    {
	int error, ret, dup_argc;
	long spawnedProcess = -1;
	long loggerTID;
	char **dup_argv;

	/* get the partition with hardware type none */
	PSI_getPartition(0, worldRank-1);

	/* Get my logger */
	loggerTID = INFO_request_taskinfo(PSC_getTID(-1, getpgrp()),
					  INFO_LOGGERTID, 0);
	if (!loggerTID) {
	    fprintf(stderr, "Unable to determine logger.\n");
	    exit(1);
	}

	PSI_RemoteArgs(argc-5, &argv[5], &dup_argc, &dup_argv);

	/* spawn client processes */
	ret = PSI_spawnM(1, NULL, ".", dup_argc, dup_argv, loggerTID,
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

    /* Wait for the spawned process to complete */
    while (1) {
	int ret;
	DDErrorMsg_t msg;

	ret = PSI_recvMsg(&msg);
	if (msg.header.type != PSP_CD_SPAWNFINISH || ret != sizeof(msg)) {
	    fprintf(stderr, "[%d] got strange message type %s\n",
		    worldRank, PSP_printMsg(msg.header.type));
	} else {
	    /* I'm done */
	    break;
	}
    }

    PSI_exitClient();

    return(0);
}
