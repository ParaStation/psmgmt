/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Simple wrapper to allow MPIch/P4 programs to run under the
 * control of ParaStation.
 *
 * $Id$
 *
 * @author Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
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
#include "psprotocol.h"
#include "psi.h"
#include "psienv.h"
#include "psipartition.h"
#include "psispawn.h"

static void usage(char *progname)
{
    fprintf(stderr, "%s: host -l user -n prog [args]\n", progname);
}

int main(int argc, char *argv[])
{
    int rank = -1, i;

    if (argc < 6) {
	fprintf(stderr, "You need to give at least five argument\n");
	exit(1);
    }

    /** @todo test validity of argv[1] (hostname) */

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

    // get rank from command line.
    for (i=0; i<argc; i++) {
	if (strstr(argv[i], "-p4rmrank") && i<argc-1) {
	    rank = atoi(argv[i+1]);
	    break;
	}
    }

    if (rank == -1) {
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
	int error, dup_argc;
	PStask_ID_t spawnedProcess;
	char **dup_argv;

	PSI_RemoteArgs(argc-5, &argv[5], &dup_argc, &dup_argv);

	/* spawn client processes */
	spawnedProcess = PSI_spawnRank(rank, ".", dup_argc, dup_argv, &error);
	if (!spawnedProcess) {
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

	ret = PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg));
	if (msg.header.type != PSP_CD_SPAWNFINISH || ret != sizeof(msg)) {
	    fprintf(stderr, "[%d] got strange message type %s\n",
		    rank, PSP_printMsg(msg.header.type));
	} else {
	    /* I'm done */
	    break;
	}
    }

    PSI_exitClient();

    return(0);
}
