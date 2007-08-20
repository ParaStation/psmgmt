/*
 *               ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file Replacement for the standard mpirun_rsh command provided by
 * MVAPIch in order to start such applications within a ParaStation
 * cluster.
 *
 * Part of this code is inspired by the original mpirun_rsh.c code of
 * the MVAPICH software package developed by the team members of The
 * Ohio State University's Network-Based Computing Laboratory (NBCL),
 * headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <popt.h>

#include <pse.h>
#include <psi.h>
#include <psienv.h>
#include <psiinfo.h>
#include <psispawn.h>

/** Magic versionv number of mpirun <-> application-process protocol */
#define PMGR_VERSION 5

typedef enum {
    P_NOTSTARTED,
    P_STARTED,
    P_CONNECTED,
    P_DISCONNECTED,
    P_RUNNING,
    P_FINISHED,
} process_state;

typedef struct {
    int control_socket;
    process_state state;
} process;

#define RUNNING(i) ((plist[i].state == P_STARTED ||                 \
            plist[i].state == P_CONNECTED ||                        \
            plist[i].state == P_RUNNING) ? 1 : 0)

process *plist;

/** Relevant info for reconnection of clients */   
char mpirun_host[256];   /**< hostname of current process */
int port;                /**< port process is listening on */

char msgstr[512]; /* Space for error messages */

#ifndef PARAM_GLOBAL
#define PARAM_GLOBAL "/etc/mvapich.conf"
#endif

static char version[] = "$Revision$";

/*
 * Print version info
 */
static void printVersion(void)
{
    fprintf(stderr,
	    "mpirun_openib psmgmt-%s-%s (rev. %s\b\b) (MVAPIch proto %d)\n",
	    VERSION_psmgmt, RELEASE_psmgmt, version+11, PMGR_VERSION);
}


static void createSpawner(int argc, char *argv[], int np)
{
    char *ldpath = getenv("LD_LIBRARY_PATH");
    int rank;

    if (ldpath != NULL) {
        setPSIEnv("LD_LIBRARY_PATH", ldpath, 1);
    }

    PSE_initialize();
    rank = PSE_getRank();

    if (rank<0) {
        PSnodes_ID_t *nds;
        int error, spawnedProc;
        char* hwList[] = { "openib", NULL };

        nds = malloc(np*sizeof(*nds));
        if (! nds) {
            fprintf(stderr, "%s: No memory\n", argv[0]);
            exit(1);
        }

	/* Set default HW to openib: */
	if (PSE_setHWList(hwList) < 0) {
	    fprintf(stderr,
		    "%s: Unknown hardware type '%s'. Please configure...\n",
		    __func__, hwList[0]);
	    exit(1);
	}

        if (PSE_getPartition(np)<0) exit(1);

        PSI_infoList(-1, PSP_INFO_LIST_PARTITION, NULL,
                     nds, np*sizeof(*nds), 0);

        PSI_spawnService(nds[0], NULL, argc, argv, np, &error, &spawnedProc);

        free(nds);

        if (error) {
            fprintf(stderr, "Could not spawn master process (%s)",argv[0]);
            perror("");
            exit(1);
        }

	/* Don't irritate the user with logger messages */
	setenv("PSI_NOMSGLOGGERDONE", "", 1);

        /* Switch to psilogger */
        PSI_execLogger(NULL);

        printf("never be here\n");
        exit(1);
    }

    return;
}

/* finds first non-whitespace char in input string */
static char *skip_white(char *s)
{
    int len;
    /* return pointer to first non-whitespace char in string */
    /* Assumes string is null terminated */
    /* Clean from start */
    while ((*s == ' ') || (*s == '\t'))
        s++;
    /* Clean from end */
    len = strlen(s) - 1; 
    while (((s[len] == ' ') || (s[len] == '\t')) && (len >=0)){
        s[len]='\0';
        len--;
    }
    return s;
}

/*
 * reads the param file and stores each of the environment variables
 * to the ParaStation environment.
 */
static void read_param_file(char *paramfile, int verbose)
{
    FILE *pf;
    char name[128], value[256];
    char line[512];
    char *p;

    if ((pf = fopen(paramfile, "r")) == NULL) {
        fprintf(stderr, "Cannot open paramfile = %s", paramfile);
        perror("");
        exit(1);
    }

    while (fgets(line, sizeof(line), pf)) {
        p = skip_white(line);
        if (*p == '#' || *p == '\n') {
            /* a comment or a blank line, ignore it */
            continue;
        }

        name[0] = value[0] = '\0';
        if (sscanf(p, "%64[A-Z_] = %192s", name, value) != 2) continue;
	p = skip_white(value);

	setPSIEnv(name, p, 1);

        if (verbose) printf("Added: [%s=%s]\n", name, p);
    }

    fclose(pf);

    return;
}

/**
 * @brief Handle signals.
 *
 * Handle the signal @a sig sent to the spawner. For the time being
 * only SIGTERM ist handled.
 *
 * @param sig Signal to handle.
 *
 * @return No return value.
 */
static void sighandler(int sig)
{
    switch(sig) {
    case SIGTERM:
	exit(0);
	break;
    default:
	fprintf(stderr, "Got signal %d.\n", sig);
    }

    fflush(stdout);
    fflush(stderr);

    signal(sig, sighandler);
}


static int startProcs(int i, int np, int argc, char *argv[], int verbose)
{
    int pid = getpid();
    char tmp[1024];

    setPSIEnv("MPIRUN_MPD", "0", 1);
    setPSIEnv("MPIRUN_HOST", mpirun_host, 1);

    snprintf(tmp, sizeof(tmp), "%d", port);
    setPSIEnv("MPIRUN_PORT", tmp, 1);

    snprintf(tmp, sizeof(tmp), "%d", i);
    setPSIEnv("MPIRUN_RANK", tmp, 1);

    snprintf(tmp, sizeof(tmp), "%d", np);
    setPSIEnv("MPIRUN_NPROCS", tmp, 1);

    snprintf(tmp, sizeof(tmp), "%d", pid);
    setPSIEnv("MPIRUN_ID", tmp, 1);

    setPSIEnv("NOT_USE_TOTALVIEW", "1", 1);

    if (verbose) printf("spawn rank %d: %s\n", i, argv[0]);

    {
	PStask_ID_t spawnedProcess = -1;
	int error;

	if (PSI_spawn(1, NULL, argc, argv, &error, &spawnedProcess) < 0 ) {
	    if (error) {
		perror("Spawn failed!");
	    }
	    exit(10);
	}
    }

    plist[i].state = P_STARTED; /* putting after fork() avoids a race */

    return (0);
}


static void exchangeHostIDs(int sock, int nprocs)
{
    int global_hostidlen = 0, hostidlen, tot_nread, i;
    int *hostids = NULL;
    char *msg;

    /* accept incoming connections, read port numbers */
    for (i = 0; i < nprocs; i++) {
        int s, version, rank, nread;

    ACCEPT_HID:
	s = accept(sock, NULL, NULL);
        if (s < 0) {
            if (errno == EINTR) goto ACCEPT_HID;
	    msg = "accept";
	    goto errexit;
        }

        /*
         * protocol:
         *  0. read protocol version number
         *  1. read rank of process
         *  2. read hostid length
         *  3. read hostid itself
         *  4. send array of all addresses
         */

        /* 0. Find out what version of the startup protocol the executable
         * was compiled to use. */

        nread = read(s, &version, sizeof(version));
        if (nread != sizeof(version)) {
	    msg = "read";
	    goto errexit;
        }
        if (version != PMGR_VERSION) {
            snprintf(msgstr, sizeof(msgstr), "executable version %d"
		     " does not match our version %d.", version, PMGR_VERSION);
	    msg = msgstr;
	    goto errexit;
        }

        /* 1. Find out who we're talking to */
        nread = read(s, &rank, sizeof(rank));
        if (nread != sizeof(rank)) {
	    msg = "read";
	    goto errexit;
        }

        if (rank < 0 || rank >= nprocs || plist[rank].state != P_STARTED) {
            msg = "invalid rank received.";
	    goto errexit;
        }
        plist[rank].control_socket = s;

        /* 2. Find out length of the data */
        nread = read(s, &hostidlen, sizeof(hostidlen));
        if (nread != sizeof(hostidlen)) {
            /* nread == 0 is not actually an error! */
            if (nread == 0) continue;

	    msg = "read";
	    goto errexit;
        }

        if (!i) {
            global_hostidlen = hostidlen;

	    /* allocate as soon as we know the address length */
            hostids = (int *) malloc(hostidlen * nprocs);
            if (!hostids) {
		msg = "malloc";
		goto errexit;
            }
	} else if (hostidlen != global_hostidlen) {
	    snprintf(msgstr, sizeof(msgstr),
		     "Address lengths %d and %d do not match",
		     hostidlen, global_hostidlen);
	    msg = msgstr;
	    goto errexit;
        }

        /* 3. Read info from each process */
        tot_nread=0;
        while(tot_nread < hostidlen) {
            nread = read(s, (void*)((&hostids[rank])+tot_nread),
                    hostidlen- tot_nread);
            if(nread < 0) {
		msg = "read";
		goto errexit;
            }
            tot_nread += nread;
        }

    }

    /* at this point, all processes have checked in hostids */

    /* write back all hostids */
    for (i = 0; i < nprocs; i++) {
        int nwritten = write(plist[i].control_socket,
			     hostids, nprocs*hostidlen);
        if (nwritten != nprocs*hostidlen ) {
	    msg = "write";
	    goto errexit;
        }
    }

    /* close all open sockets */
    for (i = 0; i < nprocs; i++) close(plist[i].control_socket);

    return;

 errexit:
    fprintf(stderr, "%s: ", __func__);
    if (errno) {
	perror(msg);
    } else {
	fprintf(stderr, "%s\n", msg);
    }
    exit(1);
}

static void exchangeInfo(int sock, int nprocs)
{
    int addrlen, global_addrlen = 0, tot_nread, i;
    int *alladdrs = NULL;
    char *alladdrs_char = NULL; /* for byte location */
    int pidlen, global_pidlen = 0;
    char *allpids=NULL;
    int out_addrs_len;
    int *out_addrs;
    char * msg;

    /* accept incoming connections, read port numbers */
    for (i = 0; i < nprocs; i++) {
        int s, rank, nread;

    ACCEPT:
	s = accept(sock, NULL, NULL);
	if (s < 0) {
            if (errno == EINTR) goto ACCEPT;
	    msg = "accept";
	    goto errexit;
        }

        /* 
         * protocol: 
         *  We don't need version number,
         *  0. read rank of process
         *  1. read address length
         *  2. read address itself
         *  3. send array of all addresses
         */

        /* 0. Find out who we're talking to */
        nread = read(s, &rank, sizeof(rank));
        if (nread != sizeof(rank)) {
	    msg = "read";
	    goto errexit;
        }

        if (rank < 0 || rank >= nprocs || plist[rank].state != P_STARTED) {
	    msg = "invalid rank received.";
	    goto errexit;
        }
        plist[rank].control_socket = s;
        plist[rank].state = P_CONNECTED;

        /* 1. Find out length of the data */
        nread = read(s, &addrlen, sizeof(addrlen));
        if (nread != sizeof(addrlen)) {

            /* nread == 0 is not actually an error! */
            if (nread == 0) continue;

	    msg = "read";
	    goto errexit;
        }

        if (!i)
            global_addrlen = addrlen;
        else if (addrlen != global_addrlen) {
	    snprintf(msgstr, sizeof(msgstr),
		     "Address lengths %d and %d do not match", 
		     addrlen, global_addrlen);
	    msg = msgstr;
	    goto errexit;
        }

        if (addrlen) {
	    if (!i) {
		/* allocate as soon as we know the address length */
		alladdrs = (int *) malloc(addrlen * nprocs);
		if (!alladdrs) {
		    msg = "malloc";
		    goto errexit;
		}
	    }

	    /* 2. Read info from each process */

	    /* for byte location */
	    alladdrs_char = (char *) &alladdrs[rank * addrlen / sizeof(int)];

	    tot_nread = 0;

	    while (tot_nread < addrlen) {
		nread = read(s, (void *) (alladdrs_char + tot_nread),
			     addrlen - tot_nread);

		if (nread < 0) {
		    msg = "read";
		    goto errexit;
		}

		tot_nread += nread;
	    }
	}

        /* 3. Find out length of the data */
        nread = read(s, &pidlen, sizeof(pidlen));
        if (nread != sizeof(pidlen)) {
	    msg = "read";
	    goto errexit;
        }

        if (i == 0) {
            global_pidlen = pidlen;
        } else {
            if (pidlen != global_pidlen) {
                snprintf(msgstr, sizeof(msgstr),
			 "Pid lengths %d and %d do not match\n", 
			 pidlen, global_pidlen);
		msg = msgstr;
		goto errexit;
            }
        }

        if (i == 0) {
            /* allocate as soon as we know the address length */
            allpids = (char *)malloc(pidlen*nprocs);
            if (!allpids) {
		msg = "malloc";
		goto errexit;
            }
        }

        tot_nread=0;
        while(tot_nread < pidlen) {
            nread = read(s, (void*)(allpids+rank*pidlen+tot_nread), 
			 pidlen - tot_nread);
            if(nread < 0) {
		msg = "read";
		goto errexit;
            }
            tot_nread += nread;
        }
    }


    /* at this point, all processes have checked in. */

    /* send ports to all but highest ranking process, as it needs none */

    out_addrs_len = 3 * nprocs * sizeof(int);
    out_addrs = (int *) malloc(out_addrs_len);
    if (!out_addrs) {
	msg = "malloc";
	goto errexit;
    }

    for (i = 0; i < nprocs; i++) {
        /* put hca_lid information at the first beginning */
        out_addrs[i] = alladdrs[i * addrlen / sizeof(int) + i];

        /* put host id information in the third round */
        out_addrs[2 * nprocs + i] =
            alladdrs[i * addrlen / sizeof(int) + nprocs];
    }

    for (i = 0; i < nprocs; i++) {
        int nwritten, j;

        /* personalized address information for each process */
        for (j = 0; j < nprocs; j++) {
            /* put qp information here */
            if (i == j)
                /* No QP is allocated for a process itself,
                 * If you change this, please change viainit.cc:1514 too */
                out_addrs[nprocs + j] = -1;
            else
                out_addrs[nprocs + j] =
                    alladdrs[j * addrlen / sizeof(int) + i];
        }

        nwritten =
            write(plist[i].control_socket, out_addrs, out_addrs_len);
        if (nwritten != out_addrs_len) {
	    msg = "write";
	    goto errexit;
        }

	if(pidlen != 0) {
	    nwritten = 0;
	    nwritten = write(plist[i].control_socket, allpids, nprocs*pidlen);
	    if (nwritten != nprocs*pidlen) {
		msg = "write";
		goto errexit;
	    }
	}

        plist[i].state = P_RUNNING;
    }

    return;

 errexit:
    fprintf(stderr, "%s: ", __func__);
    if (errno) {
	perror(msg);
    } else {
	fprintf(stderr, "%s\n", msg);
    }
    exit(1);
}

static void QP_estabBarrier(int np)
{
    int i;
    int send_val = 1000;
    int remote_id;

    for (i = 0; i < np; i++) {
        int s = plist[i].control_socket;
        int nread;
        remote_id = -1;
        nread = read(s, &remote_id, sizeof(remote_id));
        if (nread == -1) {
            perror("termination socket read failed");
            plist[i].state = P_DISCONNECTED;
        } else if (nread == 0) {
            plist[i].state = P_DISCONNECTED;
        } else if (nread != sizeof(remote_id)) {
            printf("Invalid termination socket read on [%d] "
                   "returned [%d] got [%d]\n", i, nread, remote_id);
	    exit(1);
        } else {
            plist[i].state = P_FINISHED;
        }
    }

    /* now, everyone who is still alive has responded */
    for (i = 0; i < np; i++) {
        int s = plist[i].control_socket;
        if (plist[i].state == P_FINISHED) {
            int nwritten = write(s, &send_val, sizeof(send_val));
            if (nwritten != sizeof(send_val)) {
                perror("socket write");
		exit(1);
            }
        }
    }
}

static void wait_for_errors(int sock)
{
    int s, nread, remote_id;

    s = accept(sock, NULL, NULL);
    nread = read(s, &remote_id, sizeof(remote_id));
    if (nread == -1) {
        perror("Termination socket read failed");
    } else if (nread == 0) {
    } else if (nread != sizeof(remote_id)) {
        printf("Invalid termination socket on read\n");
	exit(1);
    } else {
        printf("mpirun_rsh: Abort signaled from [%d]\n",remote_id); 

        close(s);
        close(sock);

	exit(1);
    }
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, char *argv[])
{
    int np, dest, version, verbose, source, rusage, show;
    int sock, i, rc;
    char *nodelist, *hostlist, *hostfile, *sort, *envlist, *paramfile;
    char *envstr, *msg;
    struct sockaddr_in sockaddr;
    socklen_t sockaddr_len = sizeof(sockaddr);
    int dup_argc;
    char **dup_argv;

    /*
     * We can't use popt for argument parsing here. popt is not
     * capable to stop at the first unrecogniced option, i.e. at the
     * executable separation options to the mpirun command from
     * options to the application.
     */

    poptContext optCon;   /* context for parsing command-line options */

    struct poptOption optionsTable[] = {
        { "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &np, 0, "number of processes to start", "num"},
        { "nodes", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &nodelist, 0, "list of nodes to use", "nodelist"},
        { "hosts", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hostlist, 0, "list of hosts to use", "hostlist"},
        { "hostfile", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &hostfile, 0, "hostfile to use", "hostfile"},
        { "sort", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &sort, 0, "sorting criterium to use", "{proc|load|proc+load|none}"},
        { "inputdest", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &dest, 0, "direction to forward input", "dest"},
        { "sourceprintf", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &source, 0, "print output-source info", NULL},
        { "rusage", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &rusage, 0, "print consumed sys/user time", NULL},
        { "exports", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &envlist, 0, "environment to export to foreign nodes", "envlist"},
        { "paramfile", '\0', POPT_ARG_STRING | POPT_ARGFLAG_ONEDASH,
	  &paramfile, 0, "file containing run-time MVICH parameters", "file"},
        { "show", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &show, 0, "show command for remote execution but dont run it", NULL},
	{ "verbose", 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
        { "version", 'V', POPT_ARG_NONE,
	  &version, -1, "output version information and exit", NULL},
        POPT_AUTOHELP
        { NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    /* The duplicated argv will contain the apps commandline */
    poptDupArgv(argc, (const char **)argv,
		&dup_argc, (const char ***)&dup_argv);

    optCon = poptGetContext(NULL, dup_argc, (const char **)dup_argv,
			    optionsTable, 0);
    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);

    /*
     * Split the argv into two parts:
     *  - first one containing the mpirun options
     *  - second one containing the apps argv
     * The first one is already parsed while splitting
     */
    while (1) {
	const char *unknownArg;

	np = dest = -1;
	version = verbose = source = rusage = show = 0;
	nodelist = hostlist = hostfile = sort = envlist = paramfile = NULL;

	rc = poptGetNextOpt(optCon);

	if ((unknownArg=poptGetArg(optCon))) {
	    /*
	     * Find the first unknown argument (which is the apps
	     * name) within dup_argv. Start searching from dup_argv's end
	     * since the apps name might be used within another
	     * options argument.
	     */
	    for (i=argc-1; i>0; i--) {
		if (strcmp(dup_argv[i], unknownArg)==0) {
		    dup_argc = i;
		    dup_argv[dup_argc] = NULL;
		    poptFreeContext(optCon);	
		    optCon = poptGetContext(NULL,
					    dup_argc, (const char **)dup_argv,
					    optionsTable, 0);
		    poptSetOtherOptionHelp(optCon, OTHER_OPTIONS_STR);
		    break;
		}
	    }
	    if (i==0) {
		fprintf(stderr, "unknownArg '%s' not found !?\n", unknownArg);
		exit(1);
	    }
	} else {
	    /* No unknownArg left, we are done */
	    break;
	}
    }

    if (rc < -1) {
        /* an error occurred during option processing */
        snprintf(msgstr, sizeof(msgstr), "%s: %s",
		 poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		 poptStrerror(rc));
	msg = msgstr;
        goto errexit;
    }

    if (version) {
        printVersion();
        return 0;
    }

    if (np == -1) {
	msg = "Give at least the -np argument.";
	goto errexit;
    }

    if (np < 1) {
	snprintf(msgstr, sizeof(msgstr), "'-np %d' makes no sense.", np);
	msg = msgstr;
	goto errexit;
    }

    if (!argv[dup_argc]) {
        poptPrintUsage(optCon, stderr, 0);
	msg = "No <command> specified.";
	goto errexit;
    }

    free(dup_argv);

    /* Setup various environment variables depending on passed arguments */
    if (dest >= 0) {
	char val[6];

	snprintf(val, sizeof(val), "%d", dest);
	setenv("PSI_INPUTDEST", val, 1);
	if (verbose) printf("Send all input to node with rank %d.\n", dest);
    }

    if (source) {
	setenv("PSI_SOURCEPRINTF", "", 1);
	if (verbose) printf("Print output sources.\n");
    }

    if (rusage) {
	setenv("PSI_RUSAGE", "", 1);
	if (verbose) printf("Will print info on consumed sys/user time.\n");
    }

    if (envlist) {
	char *val;

	envstr = getenv("PSI_EXPORTS");
	if (envstr) {
	    val = malloc(strlen(envstr) + strlen(envlist) + 2);
	    sprintf(val, "%s,%s", envstr, envlist);
	} else {
	    val = strdup(envlist);
	}
	setenv("PSI_EXPORTS", val, 1);
	free(val);
	if (verbose) printf("Environment variables to be exported: %s\n", val);
    }

    envstr = getenv("PSI_NODES");
    if (!envstr) envstr = getenv("PSI_HOSTS");
    if (!envstr) envstr = getenv("PSI_HOSTFILE");
    /* envstr marks if any of PSI_NODES, PSI_HOSTS or PSI_HOSTFILE is set */
    if (nodelist) {
	if (hostlist) {
	    msg = "Don't use -nodes and -hosts simultatniously.";
	    goto errexit;
	} else if (hostfile) {
	    msg = "Don't use -nodes and -hostfile simultatniously.";
	    goto errexit;
	} else if (envstr) {
	    msg = "Don't use -nodes with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    goto errexit;
	}
	setenv("PSI_NODES", nodelist, 1);
	if (verbose) printf("PSI_NODES set to '%s'\n", nodelist);
    } else if (hostlist) {
	if (hostfile) {
	    msg = "Don't use -hosts and -hostfile simultatniously.";
	    goto errexit;
	} else if (envstr) {
	    msg = "Don't use -hosts with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    goto errexit;
	}
	setenv("PSI_HOSTS", hostlist, 1);
	if (verbose) printf("PSI_HOSTS set to '%s'\n", hostlist);
    } else if (hostfile) {
	if (envstr) {
	    msg = "Don't use -hostfile with any of"
		" PSI_NODES, PSI_HOSTS or PSI_HOSTFILE set.";
	    goto errexit;
	}
	setenv("PSI_HOSTFILE", hostfile, 1);
	if (verbose) printf("PSI_HOSTFILE set to '%s'\n", hostfile);
    }

    envstr = getenv("PSI_NODES_SORT");
    if (sort) {
	char *val;
	if (envstr) {
	    msg = "Don't use -sort with PSI_NODES_SORT set.";
	    goto errexit;
	}
	if (!strcmp(sort, "proc")) {
	    val = "PROC";
	} else if (!strcmp(sort, "load")) {
	    val = "LOAD_1";
	} else if (!strcmp(sort, "proc+load")) {
	    val = "PROC+LOAD";
	} else if (!strcmp(sort, "none")) {
	    val = "NONE";
	} else {
	    snprintf(msgstr, sizeof(msgstr), "Unknown -sort value: %s", sort);
	    msg = msgstr;
	    goto errexit;
	}
	setenv("PSI_NODES_SORT", val, 1);
	if (verbose) printf("PSI_NODES_SORT set to '%s'\n", val);
    }
    
    createSpawner(argc, argv, np);

    /* Check for LSF-Parallel */
    PSI_RemoteArgs(argc-dup_argc, &argv[dup_argc], &dup_argc, &dup_argv);

    /* reading default param file */
    if (access(PARAM_GLOBAL, R_OK) == 0) {
	read_param_file(PARAM_GLOBAL, verbose);
    }

    /* reading file specified by user env */
    envstr = getenv("MVAPICH_DEF_PARAMFILE");
    if (envstr) {
	read_param_file(envstr, verbose);
    }
    if (paramfile) {
        /* construct a string of environment variable definitions from
         * the entries in the paramfile.  These environment variables
         * will be available to the remote processes, which
         * will use them to over-ride default parameter settings
         */
        read_param_file(paramfile, verbose);
    }
	    	

    plist = malloc(np * sizeof(process));
    if (plist == NULL) {
        perror("malloc");
        exit(1);
    }

    for (i = 0; i < np; i++) {
        plist[i].state = P_NOTSTARTED;
    }

    gethostname(mpirun_host, sizeof(mpirun_host));

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }
    sockaddr.sin_addr.s_addr = INADDR_ANY;
    sockaddr.sin_port = 0;
    if (bind(sock, (struct sockaddr *) &sockaddr, sockaddr_len) < 0) {
        perror("bind");
        exit(1);
    }

    if (getsockname(sock, (struct sockaddr *) &sockaddr, &sockaddr_len) < 0) {
        perror("getsockname");
        exit(1);
    }

    port = (int) ntohs(sockaddr.sin_port);
    listen(sock, np);


    /* start all processes */
    alarm(1000);
    for (i = 0; i < np; i++) {
	if (startProcs(i, np, dup_argc, dup_argv, verbose||show) < 0) {
	    fprintf(stderr, "Unable to start process %d. Aborting.\n", i);
	    exit(1);
	} 
    }

    if (show) {
	exit(0);
    } else {
	signal(SIGTERM, sighandler);
    }

    /* Hostid exchange start */
    alarm(1000);
    exchangeHostIDs(sock, np);
    /* cancel the timeout */
    alarm(0);
    

    /* Lets read all other information, LID QP,etc..*/
    alarm(1000); /* enable the timer again */
    exchangeInfo(sock, np);
    /* cancel the timeout */
    alarm(0);

    /* Wait for all QPs to be established */
    QP_estabBarrier(np);

    /* close all open sockets */
    for (i = 0; i < np; i++) close(plist[i].control_socket);

    wait_for_errors(sock);
    printf("Never be here.\n");

    close(sock);
    return 0;

 errexit:
    poptPrintUsage(optCon, stderr, 0);
    fprintf(stderr, "%s\n", msg);
    return 1;
}
