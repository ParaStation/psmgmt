/*
 *               ParaStation
 * psispawn.c
 *
 * Spawning of processes and helper functions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psispawn.c,v 1.49 2003/10/30 16:36:25 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psispawn.c,v 1.49 2003/10/30 16:36:25 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "pshwtypes.h"
#include "pstask.h"

#include "psi.h"
#include "psilog.h"
#include "info.h"
#include "psienv.h"
#include "psipartition.h"

#include "psispawn.h"

#define ENV_NODE_RARG      "PSI_RARG_PRE_%d"

static char errtxt[256];

static uid_t defaultUID = 0;

void PSI_setUID(uid_t uid)
{
    if (!getuid()) {
	defaultUID = uid;
    }
}

void PSI_RemoteArgs(int Argc, char **Argv, int *RArgc, char ***RArgv)
{
    int new_argc=0;
    char **new_argv;
    char env_name[ sizeof(ENV_NODE_RARG) + 20];
    int cnt;
    int i;

    snprintf(errtxt, sizeof(errtxt), "%s()", __func__);
    PSI_errlog(errtxt, 10);

    cnt=0;
    for (;;) {
	snprintf(env_name, sizeof(env_name), ENV_NODE_RARG, cnt);
	if (getenv(env_name)) {
	    cnt++;
	} else {
	    break;
	}
    }

    if (cnt) {
	new_argc=cnt+Argc;
	new_argv=malloc(sizeof(char *)*(new_argc+1));
	new_argv[new_argc]=NULL;

	for (i=0; i<cnt; i++) {
	    snprintf(env_name, sizeof(env_name), ENV_NODE_RARG, i);
	    new_argv[i] = getenv(env_name);
	    /* Propagate the environment */
	    setPSIEnv(env_name, new_argv[i], 1);
	}
	for (i=0; i<Argc; i++) {
	    new_argv[i+cnt] = Argv[i];
	}
	*RArgc=new_argc;
	*RArgv=new_argv;
    } else {
	*RArgc=Argc;
	*RArgv=Argv;
    }
    return;
}

/**
 * @brief Get current working directory.
 *
 * Get the current working directory. If @a ext is given, it will be
 * appended to the determined string. If the working directory starts
 * with a string indicating the use of an automount ("/tmp_mnt" or
 * "/export" for now), this signiture will be cut from the string.
 *
 * The strategy to determine the current working directory is to
 * firstly look for the PWD environment variable and if this is not
 * present, to call getcwd(3).
 *
 * @param ext The extension to append to determined directory.
 *
 * @return On success, a pointer to a character array containing the
 * extended working directory is returned. This array is allocated via
 * malloc() and should be free()ed by the user when it is no longer
 * needed. If something went wrong, NULL is returned.
 */
static char *mygetwd(const char *ext)
{
    char *dir;

    if (!ext || (ext[0]!='/')) {
	char *temp = getenv("PWD");

	if (temp) {
	    dir = strdup(temp);
	} else {
#if defined __osf__ || defined __linux__
	    dir = getcwd(NULL, 0);
#else
#error wrong OS
#endif
	}
	if (!dir) goto error;

	/* Enlarge the string */
	dir = realloc(dir, strlen(dir) + (ext ? strlen(ext) : 0) + 2);
	if (!dir) goto error;

	strcat(dir, "/");
	strcat(dir, ext ? ext : "");

	/* remove automount directory name. */
	if (strncmp(dir, "/tmp_mnt", strlen("/tmp_mnt"))==0) {
	    temp = dir;
	    dir = strdup(&temp[strlen("/tmp_mnt")]);
	    free(temp);
	} else if (strncmp(dir, "/export", strlen("/export"))==0) {
	    temp = dir;
	    dir = strdup(&temp[strlen("/export")]);
	    free(temp);
	}
    } else {
	dir = strdup(ext);
    }
    if (!dir) goto error;

    return dir;

 error:
    errno = ENOMEM;
    return NULL;
}

/**
 * @brief Actually spawn processes.
 *
 * Actually spawn @a count processes on the nodes stored within @a
 * dstnodes. The spawned processes will be started within @a
 * workingdir as the current working directory. The @a argc arguments
 * used in order to start the processes are stored within @a argv. The
 * first process spawned will get the unique rank @a rank, all further
 * processes will get successive ranks. Upon return the array @a
 * errors will hold @a count error codes indicating if the
 * corresponding spawn was successful and if not, what cause the
 * failure. The array @a tids will hold the unique task ID of the
 * started processes.
 *
 * @param count The number of processes to spawn.
 *
 * @param dstnodes The nodes used in order to spawn processes.
 *
 * @param workingdir The initial working directory of the spawned processes.
 *
 * @param argc The number of arguments used to spawn the processes.
 *
 * @param argv The arguments used to spawn the processes.
 *
 * @param rank The rank of the first process spawned.
 *
 * @param errors Array holding error codes upon return.
 *
 * @param tids Array holding unique task IDs upon return.
 *
 * @return Upon success, the number of processes spawned is returned,
 * i.e. usually this is @a count. Otherwise a negativ value is
 * returned which indicates the number of answer got from spawn
 * requests.
 */
static int dospawn(int count, PSnodes_ID_t *dstnodes, char *workingdir,
		   int argc, char **argv,
		   int rank, int *errors, PStask_ID_t *tids)
{
    int outstanding_answers=0;
    DDBufferMsg_t msg;
    DDErrorMsg_t answer;
    char *mywd;

    int i;          /* count variable */
    int ret = 0;    /* return value */
    int error = 0;  /* error flag */
    int fd = 0;
    PStask_t* task; /* structure to store the information of the new process */

    /*
     * Send the request to my own daemon
     *----------------------------------
     */

    for (i=0; i<count; i++) {
	errors[i] = 0;
	tids[i] = 0;
    }

    /*
     * Init the Task structure
     */
    task = PStask_new();

    task->ptid = PSC_getMyTID();
    if (defaultUID) {
	task->uid = defaultUID;
    } else {
	task->uid = getuid();
    }
    task->gid = getgid();
    task->aretty = 0;
    if (isatty(STDERR_FILENO)) {
	task->aretty |= (1 << STDERR_FILENO);
	fd = STDERR_FILENO;
    }
    if (isatty(STDOUT_FILENO)) {
	task->aretty |= (1 << STDOUT_FILENO);
	fd = STDOUT_FILENO;
    }
    if (isatty(STDIN_FILENO)) {
	task->aretty |= (1 << STDIN_FILENO);
	fd = STDIN_FILENO;
    }
    if (task->aretty) {
	tcgetattr(fd, &task->termios);
	ioctl(fd, TIOCGWINSZ, &task->winsize);
    }
    task->group = TG_ANY;
    task->loggertid = INFO_request_taskinfo(PSC_getMyTID(), INFO_LOGGERTID, 0);

    mywd = mygetwd(workingdir);

    if (!mywd) return -1;

    task->workingdir = mywd;
    task->argc = argc;
    task->argv = (char**)malloc(sizeof(char*)*(task->argc+1));
    {
	struct stat statbuf;

	if (stat(argv[0], &statbuf)) {
#ifdef __linux__
	    char myexec[PATH_MAX];
	    int length;

	    length = readlink("/proc/self/exe", myexec, sizeof(myexec)-1);
	    if (length<0) {
		perror("readlink");
	    } else {
		myexec[length]='\0';
	    }

	    task->argv[0]=strdup(myexec);
#else
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: Can't start parallel jobs from PATH.\n", __func__);
	    PSI_errlog(errtxt, 0);
	    return -1;
#endif
	} else {
	    task->argv[0]=strdup(argv[0]);
	}
    }
    for (i=1;i<task->argc;i++)
	task->argv[i]=strdup(argv[i]);
    task->argv[task->argc]=0;

    task->environ = dumpPSIEnv();

    /* Test if task is small enough */
    if (PStask_encode(msg.buf, sizeof(msg.buf), task) > sizeof(msg.buf)) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: size of task too large. Too many environment variables?",
		 __func__);
	PSI_errlog(errtxt, 0);
	return -1;
    }

    outstanding_answers=0;
    for (i=0; i<count; i++) {
	/*
	 * check if dstnode is ok
	 */
	if (dstnodes[i] >= PSC_getNrOfNodes()) {
	    errors[i] = ENETUNREACH;
	    tids[i] = -1;
	} else {
	    /*
	     * put the type of the msg in the head
	     * put the length of the whole msg to the head of the msg
	     * and return this value
	     */
	    msg.header.type = PSP_CD_SPAWNREQUEST;
	    msg.header.dest = PSC_getTID(dstnodes[i],0);
	    msg.header.sender = PSC_getMyTID();
	    msg.header.len = sizeof(msg.header);;

	    /*
	     * set the correct rank
	     */
	    task->rank = rank++;

	    /* pack the task information in the msg */
	    msg.header.len += PStask_encode(msg.buf, sizeof(msg.buf), task);

	    if (PSI_sendMsg(&msg)<0) {
		char *errstr = strerror(errno);
		snprintf(errtxt, sizeof(errtxt),
			 "%s: PSI_sendMsg() failed: %s",
			 __func__, errstr ? errstr : "UNKNOWN");
		PSI_errlog(errtxt, 0);

		PStask_delete(task);
		return -1;
	    }

	    outstanding_answers++;
	}
    }/* for all new processes */

    PStask_delete(task);

    /*
     * Receive Answer from  my own daemon
     *----------------------------------
     */
    while (outstanding_answers>0) {
	if (PSI_recvMsg(&answer)<0) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt),
		     "%s: PSI_recvMsg() failed: %s",
		     __func__, errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	    ret = -1;
	    break;
	}
	switch (answer.header.type) {
	case PSP_CD_SPAWNFAILED:
	case PSP_CD_SPAWNSUCCESS:
	    /*
	     * find the right task request
	     */
	    for (i=0; i<count; i++) {
		if (dstnodes[i]==PSC_getID(answer.header.sender)
		    && !tids[i] && !errors[i]) {
		    /*
		     * We have to test for !errors[i], since daemon on node 0
		     * (which has tid 0) might have returned an error.
		     */
		    errors[i] = answer.error;
		    tids[i] = answer.header.sender;
		    ret++;
		    break;
		}
	    }

	    if (i==count) {
		if (PSC_getID(answer.header.sender)==PSC_getMyID()
		    && answer.error==EACCES && count==1) {
		    /* This might be due to 'starting not allowed' here */
		    errors[0] = answer.error;
		    tids[0] = answer.header.sender;
		    ret++;
		} else {
		    snprintf(errtxt, sizeof(errtxt),
			     "%s: SPAWNSUCCESS/FAILED from unknown node %d.",
			     __func__, PSC_getID(answer.header.sender));
		    PSI_errlog(errtxt, 0);
		}
	    }

	    if (answer.header.type==PSP_CD_SPAWNFAILED) {
		snprintf(errtxt, sizeof(errtxt),
			 "%s: spawn to node %d failed.", __func__,
			 PSC_getID(answer.header.sender));
		PSI_errlog(errtxt, 0);
		error = 1;
	    }
	    break;
	default:
	    snprintf(errtxt, sizeof(errtxt), "%s: UNKNOWN answer (%s)",
		     __func__, PSP_printMsg(answer.header.type));
	    PSI_errlog(errtxt, 0);
	    errors[0] = 0;
	    error = 1;
	    break;
	}
	outstanding_answers--;
    }

    if (error) ret = -ret;
    return ret;
}

int PSI_spawn(int count, char *workdir, int argc, char **argv,
	      int *errors, PStask_ID_t *tids)
{
    int total = 0;
    PSnodes_ID_t *nodes;

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, count);
    PSI_errlog(errtxt, 10);

    if (count<=0) return 0;

    nodes = malloc(sizeof(*nodes) * GETNODES_CHUNK);
    if (!nodes) {
	*errors = ENOMEM;
	return -1;
    }

    while (count>0) {
	int chunk = (count>GETNODES_CHUNK) ? GETNODES_CHUNK : count;
	int rank = PSI_getNodes(chunk, nodes);
	int i, ret;

	if (rank < 0) {
	    errors[total] = ENXIO;
	    free(nodes);
	    return -1;
	}

	snprintf(errtxt, sizeof(errtxt), "%s: will spawn to:", __func__);
	for (i=0; i<chunk; i++) {
	    snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt),
		     " %2d", nodes[i]);
	}
	snprintf(errtxt+strlen(errtxt), sizeof(errtxt)-strlen(errtxt), ".");
	snprintf(errtxt, sizeof(errtxt), "%s: first rank: %d", __func__, rank);
	PSI_errlog(errtxt, 1);

	ret = dospawn(chunk, nodes, workdir, argc, argv, rank, errors, tids);
	if (ret != chunk) {
	    free(nodes);
	    return -1;
	}
	    
	count -= chunk;
	total += chunk;
    }

    free(nodes);
    return total;
}

PStask_ID_t PSI_spawnRank(int rank, char *workdir, int argc, char **argv,
			  int *error)
{
    PSnodes_ID_t node;
    int ret;
    PStask_ID_t tid;

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, rank);
    PSI_errlog(errtxt, 10);

    node = INFO_request_rankID(rank, 1);
    if (node < 0) {
	*error = ENXIO;
	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "%s: will spawn to: %d", __func__, node);
    PSI_errlog(errtxt, 1);

    ret = dospawn(1, &node, workdir, argc, argv, rank, error, &tid);
    if (ret != 1) return 0;

    return tid;
}

PStask_ID_t PSI_spawnGMSpawner(int np, char *workdir, int argc, char **argv,
			       int *error)
{
    PSnodes_ID_t node;
    int ret;
    PStask_ID_t tid;

    snprintf(errtxt, sizeof(errtxt), "%s(%d)", __func__, np);
    PSI_errlog(errtxt, 10);

    node = INFO_request_rankID(0, 1);
    if (node < 0) {
	*error = ENXIO;
	return 0;
    }

    snprintf(errtxt, sizeof(errtxt), "%s: will spawn to: %d", __func__, node);
    PSI_errlog(errtxt, 1);

    ret = dospawn(1, &node, workdir, argc, argv, np, error, &tid);
    if (ret != 1) return 0;

    return tid;
}

char *PSI_createPGfile(int num, const char *prog, int local)
{
    char *PIfilename, *myprog, filename[20];
    FILE *PIfile;
    int i, j=0;

    myprog = mygetwd(prog);
    if (!myprog) {
	char *errstr = strerror(errno);

	snprintf(errtxt, sizeof(errtxt), "%s: mygetwd() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);

	return NULL;
    }

    snprintf(filename, sizeof(filename), "PI%d", getpid());
    PIfile = fopen(filename, "w+");

    if (PIfile) {
	PIfilename = strdup(filename);
    } else {	
	/* File open failed, lets try the user's home directory */
	char *home = getenv("HOME");
	PIfilename = malloc((strlen(home)+strlen(filename)+2) * sizeof(char));
	strcpy(PIfilename, home);
	strcat(PIfilename, "/");
	strcat(PIfilename, filename);

	PIfile = fopen(PIfilename, "w+");
	/* File open failed finally */
	if (!PIfile) {
	    char *errstr = strerror(errno);
	    snprintf(errtxt, sizeof(errtxt), "%s: fopen() failed: %s",
		     __func__, errstr ? errstr : "UNKNOWN");
	    PSI_errlog(errtxt, 0);
	    free(PIfilename);
	    return NULL;
	}
    }

    for (i=0; i<num; i++) {
	PSnodes_ID_t node;
	static struct in_addr hostaddr;

	if (!local || !i) {
	    node = INFO_request_rankID(i, 0);
	    if (node<0) {
		fclose(PIfile);
		free(PIfilename);
		return NULL;
	    }
	    hostaddr.s_addr = INFO_request_node(node, 0);
	}
	fprintf(PIfile, "%s %d %s\n", inet_ntoa(hostaddr), (i != 0), myprog);
    }
    fclose(PIfile);

    return PIfilename;
}

int PSI_kill(PStask_ID_t tid, short signal)
{
    DDSignalMsg_t  msg;

    snprintf(errtxt, sizeof(errtxt), "%s(%s, %d)", __func__,
	     PSC_printTID(tid), signal);
    PSI_errlog(errtxt, 10);

    msg.header.type = PSP_CD_SIGNAL;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = signal;
    msg.param = getuid();
    msg.pervasive = 0;

    if (PSI_sendMsg(&msg)<0) {
	char *errstr = strerror(errno);
	snprintf(errtxt, sizeof(errtxt),
		 "%s: PSI_sendMsg() failed: %s",
		 __func__, errstr ? errstr : "UNKNOWN");
	PSI_errlog(errtxt, 0);
	return -1;
    }

    return 0;
}
