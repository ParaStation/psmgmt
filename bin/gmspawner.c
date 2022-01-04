/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Helper in order to start MPIch/GM applications within a ParaStation
 * cluster.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <popt.h>

#include "pscommon.h"

#include "psi.h"
#include "psienv.h"
#include "psiinfo.h"
#include "psispawn.h"

static pthread_t listener;

static struct {
    unsigned int port;
    unsigned int board;
    unsigned int node;
    unsigned int numanode;
    unsigned int pid;
    unsigned int slave_host;
    unsigned short slave_port;
} *clients = NULL;

static void collectInfo(int listensock, unsigned int np, unsigned int magic,
			int verbose)
{
    unsigned int index = np, i;
    bool valid = true;

    if (clients) {
	for (i=0; i<np; i++) valid = valid && !clients[i].pid;
    } else {
	valid = false;
    }

    if (!valid) {
	fprintf(stderr, "clients structure not initialized");
	exit(1);
    }

    while (index > 0) {
	int sock, count, ret;
	unsigned int thismagic;
	unsigned int rank, port, board, node, numanode, pid;
	unsigned short slave_port;
	struct sockaddr_in addr;
	socklen_t len;
	char buf[256];

	len = sizeof(addr);
	sock = accept(listensock, (struct sockaddr *)&addr, &len);

	count = recv(sock, buf, sizeof(buf), 0);

	if (!count) {
	    fprintf(stderr, "Connection closed unexpectedly !\n");
	    close (sock);
	    continue;
	}

	ret = sscanf(buf, "<<<%u:%u:%u:%u:%u:%u:%u::%hu>>>\n", &thismagic,
		     &rank, &port, &board, &node, &numanode, &pid,
		     &slave_port);

	if (ret != 8) {
	    fprintf(stderr, "Received invalid data format !\n");
	    close (sock);
	    continue;
	}

	/* Check the magic number. */
	if (thismagic != magic) {
	    fprintf(stderr, "Received bad magic number !\n");
	    close(sock);
	    continue;
	}

	if (rank >= np) {
	    fprintf(stderr, "MPI Id received is out of range (%u over %u)\n",
		    rank, np);
	    exit(1);
	}

	if (port == 0) {
	    fprintf(stderr, "MPI Id %u was unable to open a GM port.\n", rank);
	    exit(1);
	}

	if (clients[rank].pid) {
	    fprintf(stderr, "Ignoring message from the MPI Id %u (%s) !\n",
		    rank, buf);
	    close(sock);
	    continue;
	}

	clients[rank].port = port;
	clients[rank].board = board;
	clients[rank].node = node;
	clients[rank].numanode = numanode;
	clients[rank].pid = pid;
	clients[rank].slave_host = addr.sin_addr.s_addr;
	clients[rank].slave_port = slave_port;

	index--;
	close(sock);

	if (verbose) {
	    printf("MPI Id %u is using GM port %u, board %u, GM_id %u.\n",
		   rank, port, board, node);
	}
    }
}

static void distributeInfo(unsigned int np, int verbose)
{
    unsigned int index = 0, i;

    while (index < np) {
	struct protoent *proto;
	struct sockaddr_in addr;

	int sock, one=1, ret;
	char *token;

	proto = getprotobyname("TCP");

	if (!proto) {
	    fprintf(stderr, "Unknown protocol 'TCP'\n");
	    exit(1);
	}

	sock = socket(PF_INET, SOCK_STREAM, proto->p_proto);
	if (sock<0) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "Second socket creation failed: %s\n",
		    errstr ? errstr:"UNKNOWN");
	    exit(1);
	}

	ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	if (ret<0) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "Error setting second socket option: %s\n",
		    errstr ? errstr : "UNKNOWN");
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(clients[index].slave_port);
	addr.sin_addr.s_addr = clients[index].slave_host;

	ret = connect(sock, (struct sockaddr *) &addr, sizeof(addr));
	if (ret<0) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "Cannot connect to %s on port %u: %s\n",
		    inet_ntoa(addr.sin_addr), ntohs(addr.sin_port),
		    errstr ? errstr:"UNKNOWN");
	    exit(1);
	}

	if (verbose) {
	    printf("Sending mapping to MPI Id %u.\n", index);
	}

	/* Global mapping */
	token = "[[[";
	if (write(sock, token, strlen(token)) < 0) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "%s: write('%s') failed: %s\n", __func__, token,
		    errstr ? errstr : "UNKNOWN");
	    exit(1);
	}
	for (i=0; i<np; i++) {
	    char entry[80];
	    snprintf(entry, sizeof(entry), "<%u:%u:%u:%u>",
		     clients[i].port, clients[i].board, clients[i].node,
		     clients[i].numanode);
	    if (write(sock, entry, strlen(entry)) < 0) {
		char *errstr = strerror(errno);
		fprintf(stderr, "%s: write('%s') failed: %s\n", __func__,
			entry, errstr ? errstr : "UNKNOWN");
		exit(1);
	    }
	}
	token="|||";
	if (write(sock, token, strlen(token)) < 0) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "%s: write('%s') failed: %s\n", __func__, token,
		    errstr ? errstr : "UNKNOWN");
	    exit(1);
	}
	/* Local mapping */
	for (i=0; i<np; i++) {
	    if (clients[index].slave_host==clients[i].slave_host
		&& clients[index].numanode==clients[i].numanode) {
		char entry[80];
		snprintf(entry, sizeof(entry), "<%u>", i);
		if (write(sock, entry, strlen(entry)) < 0) {
		    char *errstr = strerror(errno);
		    fprintf(stderr, "%s: write('%s') failed: %s\n", __func__,
			    entry, errstr ? errstr : "UNKNOWN");
		    exit(1);
		}
	    }
	}
	token="]]]";
	if (write(sock, token, strlen(token)) < 0) {
	    char *errstr = strerror(errno);
	    fprintf(stderr, "%s: write('%s') failed: %s\n", __func__, token,
		    errstr ? errstr : "UNKNOWN");
	    exit(1);
	}

	close (sock);

	clients[index].pid = 0;
	index++;
    }
}

typedef struct {
    int sock;
    unsigned int np;
    unsigned int magic;
    int verbose;
} listenerArgs;

static listenerArgs args;

static void *listenToClients(void *val)
{
    /* Gather the information from all remote processes via sockets. */
    unsigned int i;

    if (!args.np) exit(1);

    clients = malloc(args.np * sizeof(*clients));

    if (!clients) {
	fprintf(stderr, "%s: No memory\n", __func__);
	exit(1);
    }

    for (i=0; i<args.np; i++) {
	clients[i].pid = 0;
    }

    collectInfo(args.sock, args.np, args.magic, args.verbose);

    if (args.verbose) {
	printf("Received data from all %u MPI processes.\n", args.np);
    }

    /* Send the Port ID/Board ID mapping to all remote processes. */
    distributeInfo(args.np, args.verbose);

    if (args.verbose) {
	printf("Data sent to all processes.\n");
    }

    /* Keep the first socket opened for abort messages. */
    while (1) {
	int sock, count, ret;
	unsigned int thismagic;
	char buf[256];

	sock = accept(args.sock, NULL, 0);

	count = recv(sock, buf, sizeof(buf), 0);
	if (count < 0) {
	    int eno = errno;
	    fprintf(stderr, "%s: recv(): %s\n keep listening...\n", __func__,
		    strerror(eno));
	    close(sock);
	    continue;
	}

	ret = sscanf(buf, "<<<ABORT_%u_ABORT>>>", &thismagic);

	if (ret != 1) {
	    fprintf(stderr,
		    "Received spurious abort message, keep listening...\n");
	    close(sock);
	    continue;
	}

	/* Check the magic number. */
	if (thismagic != args.magic) {
	    fprintf(stderr, "Received bad magic number in abort message!\n");
	    close(sock);
	    continue;
	}

	close(sock);
	close(args.sock);

	if (args.verbose) {
	    printf("Received valid abort message !\n");
	}

	exit (0);
    }
}


static int createListener(int startport, unsigned int np, unsigned int magic,
			  int verbose)
{
    int one=1, ret, port = startport;
    struct sockaddr_in addr;

    struct protoent *proto = getprotobyname("TCP");
    if (!proto) {
	fprintf(stderr, "%s: Unable to lookup 'TCP' protocol\n", __func__);
	return -1;
    }

    int sock = socket(PF_INET, SOCK_STREAM, proto->p_proto);
    if (sock<0) {
	perror(__func__);
	return -1;
    }

    ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (ret<0) {
	perror(__func__);
	return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_addr = (struct in_addr) { .s_addr = INADDR_ANY };
    do {
	addr.sin_port = htons(port);
	ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    } while (ret && ++port<20000);
    if (port==20000) {
	fprintf(stderr, "%s: Unable to bind socket\n", __func__);
	return -1;
    }

    ret = listen(sock, SOMAXCONN);
    if (ret<0) {
	perror(__func__);
	return -1;
    }

    args = (listenerArgs) { .sock = sock,
			    .np = np,
			    .magic = magic,
			    .verbose=verbose };

    pthread_create(&listener, NULL, listenToClients, NULL);

    return port;
}

/**
 * @brief Handle signals.
 *
 * Handle the signal @a sig sent to the psilogger.
 *
 * @param sig Signal to handle.
 *
 * @return No return value.
 */
static void sighandler(int sig)
{
    switch(sig) {
    case SIGALRM:
	exit(0);
	break;
    default:
	fprintf(stderr, "gmspawner: Got signal %d.\n", sig);
    }
}

static inline void propagateEnv(const char *env, int req)
{
    char *value = getenv(env);

    if (req && !value) {
	fprintf(stderr, "No value for required environment '%s'\n", env);
	exit (1);
    }

    if (value) setPSIEnv(env, value, 1);
}

static inline int setIntEnv(const char *env, const int val)
{
    char valstring[16];

    snprintf(valstring, sizeof(valstring), "%d", val);

    return setPSIEnv(env, valstring, 1);
}

#define OTHER_OPTIONS_STR "<command> [options]"

int main(int argc, const char *argv[])
{
    int np, verbose;
    int rank, i, rc;
    int dup_argc;
    char **dup_argv;

    int waittime, killtime;
    unsigned int magic;

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
	{ "wait", 'w', POPT_ARG_INT, &waittime, 0,
	  "Wait <n> seconds between each spawning step", "n"},
	{ "kill", 'k', POPT_ARG_INT, &killtime, 0,
	  "Kill all processes <n> seconds after the first exits", "n"},
	{ NULL, 'v', POPT_ARG_NONE,
	  &verbose, 0, "verbose mode", NULL},
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    /* The duplicated argv will contain the apps commandline */
    poptDupArgv(argc, argv, &dup_argc, (const char ***)&dup_argv);

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

	np = -1;
	verbose = 0;
	waittime = 0;
	killtime = -1;

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
		printf("unknownArg '%s' not found !?\n", unknownArg);
		exit(1);
	    }
	} else {
	    /* No unknownArg left, we are finished */
	    break;
	}
    }

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	exit(1);
    }

    if (np == -1) {
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "You have to give at least the -np argument.\n");
	exit(1);
    }

    if (!argv[dup_argc]) {
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "No <command> specified.\n");
	exit(1);
    }

    free(dup_argv);

    if (verbose) {
	printf("The 'gmspawner' command-line is:\n");
	for (i=0; i<dup_argc; i++) {
	    printf("%s ", argv[i]);
	}
	printf("\b\n\n");

	printf("The applications command-line is:\n");
	for (i=dup_argc; i<argc; i++) {
	    printf("%s ", argv[i]);
	}
	printf("\b\n\n");
    }

    /* init PSI */
    if (!PSI_initClient(TG_GMSPAWNER)) {
	fprintf(stderr, "Initialization of PSI failed.");
	exit(10);
    }

    PSI_infoInt(-1, PSP_INFO_TASKRANK, NULL, &rank, false);
    if (rank != np) {
	fprintf(stderr, "%s: rank(%d) != np(%d).\n", argv[dup_argc], rank, np);

	exit(1);
    }

    /* Propagate some environment variables */
    PSI_propEnv();
    PSI_propEnvList("PSI_EXPORTS");
    PSI_propEnvList("__PSI_EXPORTS");

    srandom(time(NULL));
    magic = random()%9999999;
    setIntEnv("GMPI_MAGIC", magic);

    setIntEnv("GMPI_NP", np);

    {
	char hostname[256];
	gethostname(hostname, sizeof(hostname));

	setPSIEnv("GMPI_MASTER", hostname, 1);
    }

    {
	int port = createListener(8000, np, magic, verbose);

	if (port>=0) setIntEnv("GMPI_PORT", port);
    }

    propagateEnv("GMPI_SHMEM", 1);
    propagateEnv("DISPLAY", 0);
    propagateEnv("GMPI_EAGER", 0);
    propagateEnv("GMPI_RECV", 1);

    PSC_setSigHandler(SIGALRM, sighandler);

    /* spawn all processes */

    PSI_RemoteArgs(argc - dup_argc, (char **)&argv[dup_argc],
		   &dup_argc, &dup_argv);

    for (rank=0; rank<np; rank++) {
	if (waittime && rank) sleep(waittime);

	setIntEnv("GMPI_ID", rank);
	setIntEnv("GMPI_BOARD", -1);

	char slavestring[20];
	PSnodes_ID_t node;
	struct in_addr ip;
	int err;

	err = PSI_infoNodeID(-1, PSP_INFO_RANKID, &rank, &node, true);
	if (err) {
	    fprintf(stderr, "Could not determine rank %d's node.\n", rank);
	    exit(1);
	}

	err = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &ip.s_addr, true);
	if (err) {
	    fprintf(stderr, "Could not determine node %d's IP\n", node);
	    exit(1);
	}

	snprintf(slavestring, sizeof(slavestring), "%s", inet_ntoa(ip));
	setPSIEnv("GMPI_SLAVE", slavestring, 1);

	/* spawn the process */
	int error;
	if (!PSI_spawnRank(rank, ".", dup_argc, dup_argv, &error)) {
	    if (error) {
		char *errstr = strerror(error);
		fprintf(stderr, "Could not spawn process %d (%s) error = %s.\n",
			rank, dup_argv[0], errstr ? errstr : "UNKNOWN");
		exit(1);
	    }
	}
    }

    /* Wait for the spawned processes to complete */
    while (np) {
	static int firstClient=1;
	DDErrorMsg_t msg;
	int ret;

	ret = PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg));
	if (msg.header.type != PSP_CD_SPAWNFINISH || ret != sizeof(msg)) {
	    fprintf(stderr, "got strange message type %s\n",
		    PSP_printMsg(msg.header.type));
	} else {
	    if (firstClient && killtime!=-1) {
		// printf("Alarm set to %d\n", killtime);
		if (killtime) {
		    alarm(killtime);
		    firstClient=0;
		} else {
		    /* Stop immediately */
		    exit(0);
		}
	    }
	    np--;
	    // printf("%d clients left\n", np);
	}
    }
    PSI_release(PSC_getMyTID());
    PSI_exitClient();

    return 0;
}
