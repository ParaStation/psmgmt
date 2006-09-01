/*
 *               ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
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
#include "pstask.h"

#include "psi.h"
#include "psilog.h"
#include "psiinfo.h"
#include "psienv.h"
#include "psipartition.h"

#include "psispawn.h"

#define ENV_NODE_RARG      "PSI_RARG_PRE_%d"

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

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

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
	new_argv=malloc(sizeof(char*)*(new_argc+1));
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
#ifdef __linux__
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
 * @param taskGroup Task-group under which the spawned process shall
 * be started. At the time, TG_ANY and TG_ADMINTASK are good
 * values. The latter is used for admin-tasks, i.e. unaccounted tasks.
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
		   int argc, char **argv, PStask_group_t taskGroup,
		   unsigned int rank, int *errors, PStask_ID_t *tids)
{
    int outstanding_answers=0;
    DDTypedBufferMsg_t msg;
    DDErrorMsg_t answer;
    char *mywd;

    int i;          /* count variable */
    int ret = 0;    /* return value */
    int error = 0;  /* error flag */
    int fd = 0;
    int envNum = 0;
    PStask_t* task; /* structure to store the information of the new process */

    for (i=0; i<count; i++) {
	errors[i] = 0;
	tids[i] = 0;
    }

    /* setup task structure */
    task = PStask_new();
    if (!task) {
	PSI_log(-1, "%s: Cannot create task structure.\n", __func__);
	return -1;
    }

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
    task->group = taskGroup;
    PSI_infoTaskID(-1, PSP_INFO_LOGGERTID, NULL, &(task->loggertid), 0);

    mywd = mygetwd(workingdir);

    if (!mywd) goto error;

    task->workingdir = mywd;
    task->argc = argc;
    task->argv = (char**)malloc(sizeof(char*)*(task->argc+1));
    if (!task->argv) goto error;
    {
	struct stat statbuf;

	if (stat(argv[0], &statbuf)) {
#ifdef __linux__
	    char myexec[PATH_MAX];
	    int length;

	    length = readlink("/proc/self/exe", myexec, sizeof(myexec)-1);
	    if (length<0) {
		PSI_warn(-1, errno, "%s: readlink", __func__);
	    } else {
		myexec[length]='\0';
	    }

	    task->argv[0]=strdup(myexec);
#else
	    PSI_log(-1, "%s: Cannot start job from PATH.\n", __func__);
	    goto error;
#endif
	} else {
	    task->argv[0]=strdup(argv[0]);
	}
    }
    for (i=1;i<task->argc;i++) {
	task->argv[i]=strdup(argv[i]);
	if (!task->argv[i]) goto error;
    }
    task->argv[task->argc]=0;

    task->environ = dumpPSIEnv();
    if (!task->environ) {
	PSI_log(-1, "%s: cannot dump environment.", __func__);
	goto error;
    }

    /* test if task can be send */
    if (PStask_encodeTask(msg.buf, sizeof(msg.buf), task) > sizeof(msg.buf)) {
	PSI_log(-1, "%s: size of task too large.", __func__);
	PSI_log(-1, " Working directory '%s' too long?\n", task->workingdir);
	goto error;
    }
    if (PStask_encodeArgs(msg.buf, sizeof(msg.buf), task) > sizeof(msg.buf)) {
	PSI_log(-1, "%s: size of task too large.", __func__);
	PSI_log(-1, " Too many/too long arguments?\n");
	goto error;
    }
    do {
	
	if (PStask_encodeEnv(msg.buf, sizeof(msg.buf),
			     task, &envNum) > sizeof(msg.buf)) {
	    PSI_log(-1, "%s: size of task too large.", __func__);
	    PSI_log(-1, " Environment '%s' too long?\n",task->environ[envNum]);
	    goto error;
	}
    } while (task->environ[envNum]);

    /* send actual requests */
    outstanding_answers=0;
    for (i=0; i<count; i++) {
	/* check if dstnode is ok */
	if (dstnodes[i] < 0 || dstnodes[i] >= PSC_getNrOfNodes()) {
	    errors[i] = ENETUNREACH;
	    tids[i] = -1;
	} else {
	    size_t len;

	    msg.header.type = PSP_CD_SPAWNREQ;
	    msg.header.dest = PSC_getTID(dstnodes[i],0);
	    msg.header.sender = PSC_getMyTID();
	    msg.header.len = sizeof(msg.header) + sizeof(msg.type);
	    msg.type = PSP_SPAWN_TASK;

	    /* set correct rank */
	    task->rank = rank++;

	    /* pack the task information in the msg */
	    len = PStask_encodeTask(msg.buf, sizeof(msg.buf), task);
	    msg.header.len += len;
	    if (PSI_sendMsg(&msg)<0) {
		PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
		goto error;
	    }
	    msg.header.len -= len;

	    msg.type = PSP_SPAWN_ARG;
	    len = PStask_encodeArgs(msg.buf, sizeof(msg.buf), task);
	    msg.header.len += len;
	    if (PSI_sendMsg(&msg)<0) {
		PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
		goto error;
	    }
	    msg.header.len -= len;

	    msg.type = PSP_SPAWN_ENV;
	    envNum=0;
	    do {
		len = PStask_encodeEnv(msg.buf, sizeof(msg.buf),task, &envNum);
		msg.header.len += len;
		if (!task->environ[envNum]) msg.type = PSP_SPAWN_END;

		if (PSI_sendMsg(&msg)<0) {
		    PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
		    goto error;
		}
		msg.header.len -= len;
	    } while (task->environ[envNum]);

	    outstanding_answers++;
	}
    }/* for all new processes */

    PStask_delete(task);

    /* receive answers */
    while (outstanding_answers>0) {
	if (PSI_recvMsg(&answer)<0) {
	    PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	    ret = -1;
	    break;
	}
	switch (answer.header.type) {
	case PSP_CD_SPAWNFAILED:
	case PSP_CD_SPAWNSUCCESS:
	    /* find the right task request */
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
		    PSI_log(-1, "%s: %s from unknown node %d\n", __func__,
			    PSP_printMsg(answer.header.type),
			    PSC_getID(answer.header.sender));
		}
	    }

	    if (answer.header.type==PSP_CD_SPAWNFAILED) {
		PSI_log(-1, "%s: spawn to node %d failed\n", __func__,
			PSC_getID(answer.header.sender));
		error = 1;
	    }
	    break;
	default:
	    PSI_log(-1, "%s: UNKNOWN answer (%s)\n", __func__,
		    PSP_printMsg(answer.header.type));
	    errors[0] = 0;
	    error = 1;
	    break;
	}
	outstanding_answers--;
    }

    if (error) ret = -ret;
    return ret;

 error:
    PStask_delete(task);

    return -1;
}

int PSI_spawn(int count, char *workdir, int argc, char **argv,
	      int *errors, PStask_ID_t *tids)
{
    int total = 0;
    PSnodes_ID_t *nodes;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, count);

    if (count<=0) return 0;

    nodes = malloc(sizeof(*nodes) * NODES_CHUNK);
    if (!nodes) {
	*errors = ENOMEM;
	return -1;
    }

    while (count>0) {
	int chunk = (count>NODES_CHUNK) ? NODES_CHUNK : count;
	int rank = PSI_getNodes(chunk, nodes);
	int i, ret;

	if (rank < 0) {
	    errors[total] = ENXIO;
	    free(nodes);
	    return -1;
	}

	PSI_log(PSI_LOG_SPAWN, "%s: will spawn to:", __func__);
	for (i=0; i<chunk; i++) {
	    PSI_log(PSI_LOG_SPAWN, " %2d", nodes[i]);
	}
	PSI_log(PSI_LOG_SPAWN, ".\n");
	PSI_log(PSI_LOG_SPAWN, "%s: first rank: %d\n", __func__, rank);

	ret = dospawn(chunk, nodes, workdir, argc, argv,
		      TG_ANY, rank, errors, tids);
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

int PSI_spawnSingle(char *workdir, int argc, char **argv,
		    int *error, PStask_ID_t *tid)
{
    /* @todo Here we should get the node to spawn to and test if this
     * is corret */
    int ret;
    PSnodes_ID_t node;
    int rank = PSI_getNodes(1, &node);

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);


    if (rank < 0) {
	*error = ENXIO;
	return -1;
    }

    PSI_log(PSI_LOG_SPAWN, "%s: will spawn to: %d  rank %d\n",
	    __func__, node, rank);

    ret = dospawn(1, &node, workdir, argc, argv, TG_ANY, rank, error, tid);
    if (ret != 1) {
	return -1;
    }

    return rank;
}

int PSI_spawnAdmin(PSnodes_ID_t node, char *workdir, int argc, char **argv,
		   unsigned int rank, int *error, PStask_ID_t *tid)
{
    int ret;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, node);

    if (node == -1) node = PSC_getMyID();
    ret = dospawn(1, &node, workdir, argc, argv,
		  TG_ADMINTASK, rank, error, tid);
    if (ret != 1) {
	return -1;
    }

    return 1;
}

PStask_ID_t PSI_spawnRank(int rank, char *workdir, int argc, char **argv,
			  int *error)
{
    PSnodes_ID_t node;
    int ret;
    PStask_ID_t tid;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, rank);

    ret = PSI_infoNodeID(-1, PSP_INFO_RANKID, &rank, &node, 1);
    if (ret || (node < 0)) {
	*error = ENXIO;
	return 0;
    }

    PSI_log(PSI_LOG_SPAWN, "%s: will spawn to: %d\n", __func__, node);

    ret = dospawn(1, &node, workdir, argc, argv, TG_ANY, rank, error, &tid);
    if (ret != 1) return 0;

    return tid;
}

PStask_ID_t PSI_spawnGMSpawner(int np, char *workdir, int argc, char **argv,
			       int *error)
{
    PSnodes_ID_t node;
    int ret, rank0 = 0;
    PStask_ID_t tid;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, np);

    ret = PSI_infoNodeID(-1, PSP_INFO_RANKID, &rank0, &node, 1);
    if (ret || node < 0) {
	*error = ENXIO;
	return 0;
    }

    PSI_log(PSI_LOG_SPAWN, "%s: will spawn to: %d", __func__, node);

    ret = dospawn(1, &node, workdir, argc, argv, TG_ANY, np, error, &tid);
    if (ret != 1) return 0;

    return tid;
}

char *PSI_createPGfile(int num, const char *prog, int local)
{
    char *PIfilename, *myprog, filename[20];
    FILE *PIfile;
    int i;

    myprog = mygetwd(prog);
    if (!myprog) {
	PSI_warn(-1, errno, "%s: mygetwd", __func__);
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
	    PSI_warn(-1, errno, "%s: fopen", __func__);
	    free(PIfilename);
	    return NULL;
	}
    }

    for (i=0; i<num; i++) {
	PSnodes_ID_t node;
	static struct in_addr hostaddr;

	if (!local || !i) {
	    int ret = PSI_infoNodeID(-1, PSP_INFO_RANKID, &i, &node, 1);
	    if (ret || (node < 0)) {
		fclose(PIfile);
		free(PIfilename);
		return NULL;
	    }
	    PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr.s_addr, 0);
	}
	fprintf(PIfile, "%s %d %s\n", inet_ntoa(hostaddr), (i != 0), myprog);
    }
    fclose(PIfile);

    return PIfilename;
}

char *PSI_createMPIhosts(int num, int local)
{
    char *MPIhostsFilename, filename[20];
    FILE *MPIhostsFile;
    int i;

    snprintf(filename, sizeof(filename), "mpihosts%d", getpid());
    MPIhostsFile = fopen(filename, "w+");

    if (MPIhostsFile) {
	MPIhostsFilename = strdup(filename);
    } else {	
	/* File open failed, lets try the user's home directory */
	char *home = getenv("HOME");
	MPIhostsFilename = malloc((strlen(home) + strlen(filename) + 2)
				  * sizeof(char));
	strcpy(MPIhostsFilename, home);
	strcat(MPIhostsFilename, "/");
	strcat(MPIhostsFilename, filename);

	MPIhostsFile = fopen(MPIhostsFilename, "w+");
	/* File open failed finally */
	if (!MPIhostsFile) {
	    PSI_warn(-1, errno, "%s: fopen", __func__);
	    free(MPIhostsFilename);
	    return NULL;
	}
    }

    for (i=0; i<num; i++) {
	PSnodes_ID_t node;
	static struct in_addr hostaddr;

	if (!local || !i) {
	    int ret = PSI_infoNodeID(-1, PSP_INFO_RANKID, &i, &node, 1);
	    if (ret || (node < 0)) {
		fclose(MPIhostsFile);
		free(MPIhostsFilename);
		return NULL;
	    }
	    ret = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr.s_addr, 0);
	    if (ret || (hostaddr.s_addr == INADDR_ANY)) {
		fclose(MPIhostsFile);
		free(MPIhostsFilename);
		return NULL;
	    }
	}
	fprintf(MPIhostsFile, "%s\n", inet_ntoa(hostaddr));
    }
    fclose(MPIhostsFile);

    return MPIhostsFilename;
}

int PSI_kill(PStask_ID_t tid, short signal)
{
    DDSignalMsg_t  msg;

    PSI_log(PSI_LOG_VERB, "%s(%s, %d)\n", __func__, PSC_printTID(tid), signal);

    msg.header.type = PSP_CD_SIGNAL;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = signal;
    msg.param = getuid();
    msg.pervasive = 0;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    return 0;
}
