/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
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
	char *temp = getenv("PWD"), *tmp;

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
	tmp = dir;
	dir = realloc(dir, strlen(dir) + (ext ? strlen(ext) : 0) + 2);
	if (!dir) {
	    if (tmp) free(tmp);
	    goto error;
	}

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
 * @brief Get protocol version
 *
 * Get the protocol-version supported by the ParaStation daemon
 * running on node @a node.
 *
 * This function does some simple caching, i.e. consecutive calls
 * asking for the version of the same node will not request this
 * information from the daemon more than once.
 *
 * @param node The node to ask.
 *
 * @return On success, return the protocol-version supported by the
 * node's daemon. Otherwise, -1 is returned.
 */
static int getProtoVersion(PSnodes_ID_t node)
{
    static PSnodes_ID_t lastNode = -2;
    static int lastProtoVersion = 0;
    PSP_Option_t opt = PSP_OP_PROTOCOLVERSION;
    PSP_Optval_t val;
    int err;

    if (node == lastNode) {
	return lastProtoVersion;
    }

    lastNode = node;
    err = PSI_infoOption(node, 1, &opt, &val, 0);
    if (err == -1) {
	PSI_log(-1, "%s: error getting info\n", __func__);
	lastNode = -2;
    }

    switch (opt) {
    case PSP_OP_PROTOCOLVERSION:
	lastProtoVersion = val;
	return lastProtoVersion;
	break;
    case PSP_OP_UNKNOWN:
	PSI_log(-1, "%s: PSP_OP_PROTOCOLVERSION unknown\n", __func__);
	break;
    default:
	PSI_log(-1, "%s: got option type %d\n", __func__, opt);
    }

    lastNode = -2;

    return -1;
}

/**
 * @brief Send task-structure
 *
 * Send the actual task structure stored in @a task using the message
 * template @a msg. @a msg has to have the destination address already
 * filled in.
 *
 * This function might send -- besides the initial PSP_SPAWN_TASK
 * message -- more messages containing trailing part of the task's
 * working-directory. The latter will use messages of type
 * PSP_SPAWN_WDIRCNTD.
 *
 * @param msg Message template prepared to send the task-structure.
 *
 * @param task The task to send.
 *
 * @return If an error occurs, -1 is returned. On success this
 * function returns 0.
 */
static int sendTask(DDTypedBufferMsg_t *msg, PStask_t *task)
{
    /* pack the task information in the msg */
    char *offset = NULL;
    size_t len = PStask_encodeTask(msg->buf, sizeof(msg->buf), task, &offset);

    msg->header.len += len;
    if (PSI_sendMsg(msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }
    msg->header.len -= len;

    while (offset) {
	msg->type = PSP_SPAWN_WDIRCNTD;
	if (strlen(offset) < sizeof(msg->buf)) {
	    /* tail fits into buffer */
	    strcpy(msg->buf, offset);
	    len = strlen(offset) + 1;
	    offset = NULL;
	} else {
	    /* buffer to small */
	    strncpy(msg->buf, offset, sizeof(msg->buf) - 1);
	    msg->buf[sizeof(msg->buf) - 1] = '\0';
	    len =  sizeof(msg->buf);
	    offset += sizeof(msg->buf) - 1;
	}

	msg->header.len += len;
	if (PSI_sendMsg(msg)<0) {
	    PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	    return -1;
	}
	msg->header.len -= len;
    }

    return 0;
}

/**
 * @brief Send argument-vector
 *
 * Send the argument-vector @a argv using the message template @a
 * msg. @a msg has to have the destination address already filled in.
 *
 * This function might send several messages of both types,
 * PSP_SPAWN_ARG and PSP_SPAWN_ARGCNTD, depending on the size of @a
 * argv as a whole and the single arguments.
 *
 * @param msg Message template prepared to send the argument-vector.
 *
 * @param argv The argument-vector to send
 *
 * @return If an error occurred, -1 is return. On success this
 * function returns 0.
 */
static int sendArgv(DDTypedBufferMsg_t *msg, char **argv)
{
    char *off = NULL;
    int num = 0, len;

    if (!argv) return 0;

    msg->header.len = sizeof(msg->header) + sizeof(msg->type);

    while (num != -1) {
	msg->type = (off) ? PSP_SPAWN_ARGCNTD : PSP_SPAWN_ARG;

	len = PStask_encodeArgv(msg->buf, sizeof(msg->buf), argv, &num, &off);

	msg->header.len += len;
	if (PSI_sendMsg(msg)<0) {
	    PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	    return -1;
	}
	msg->header.len -= len;
    }

    return 0;
}

/** Function called to create per rank environment */
static char **(*extraEnvFunc)(int, void *) = NULL;

static void *extraEnvInfo = NULL;

void PSI_registerRankEnvFunc(char **(*func)(int, void *), void *info)
{
    PSI_log(PSI_LOG_SPAWN, "%s(%p)\n", __func__, func);

    extraEnvFunc = func;
    extraEnvInfo = info;
}

/**
 * @brief Send environment
 *
 * Send the environment @a env using the message template @a msg. @a
 * msg has to have the destination address already filled in.
 *
 * This function might send several messages of both types,
 * PSP_SPAWN_ENV and PSP_SPAWN_ENVCNTD, depending on the size of @a
 * env as a whole and the single key-value pairs of the environment.
 *
 * @param msg Message template prepared to send the environment
 *
 * @param env The environment to send
 *
 * @param len Pointer to current length of @a msg's buffer.
 *
 * @return If an error occurred, -1 is return. On success this
 * function returns 0. In the latter case, @a msg will point to the
 * last message still to be sent to the destination. The current
 * length of the message's buffer is given back in @a len.
 */
static int sendEnv(DDTypedBufferMsg_t *msg, char **env, size_t *len)
{
    char *off = NULL;
    int num = 0;

    *len = 0;

    if (!env) return 0;

    msg->header.len = sizeof(msg->header) + sizeof(msg->type);

    while (1) {
	if (off) msg->type = PSP_SPAWN_ENVCNTD;

	*len = PStask_encodeEnv(msg->buf, sizeof(msg->buf), env, &num, &off);
	msg->header.len += *len;

	if (num == -1) {
	    /* last entry done */
	    if (msg->type == PSP_SPAWN_ENVCNTD) {
		if (PSI_sendMsg(msg)<0) {
		    PSI_warn(-1, errno, "%s: PSI_sendMsg(CNTD)", __func__);
		    return -1;
		}
		msg->header.len -= *len;
		*len = 0;
	    }
	    return 0;
	}

	if (PSI_sendMsg(msg)<0) {
	    PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	    return -1;
	}
	msg->header.len -= *len;
	msg->type = PSP_SPAWN_ENV;
    }

    PSI_log(-1, "%s: Never be here\n", __func__);
    return -1;
}

/**
 * @brief Handle answer on spawn requests
 *
 * Handle pending answers sent by the remote side on spawn
 * requests. The original spawn request asked for spawning @a count
 * processes on the nodes listed in @a dstnodes starting with rank @a
 * firstRank. Errors are logged within @a errors, the resulting task
 * IDs will be stored within @a tids.
 *
 * @param firstRank
 *
 * @param count The number of processes to spawn.
 *
 * @param dstnodes The nodes used in order to spawn processes.
 *
 * @param errors Array holding error codes upon return.
 *
 * @param tids Array holding unique task IDs upon return. If this is
 * NULL, no such information will be stored.
 *
 * @return Return might have 4 different values: -1: fatal error, 0:
 * general error, but answer from known node, 1: no error, 2: ignore
 * message, e.g. from unknown node or answer on "who died" question.
 */
static int handleAnswer(unsigned int firstRank, int count, PSnodes_ID_t *dstnodes,
		 int *errors, PStask_ID_t *tids)
{
    DDBufferMsg_t answer;
    DDErrorMsg_t *errMsg = (DDErrorMsg_t *)&answer;
    DDSignalMsg_t *sigMsg = (DDSignalMsg_t *)&answer;
    int rank, fallback = 0;

recv_retry:
    if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer))<0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }
    switch (answer.header.type) {
    case PSP_CD_SPAWNFAILED:
    case PSP_CD_SPAWNSUCCESS:
	rank = errMsg->request - firstRank;
	/* find the right task request */
	if (rank && (rank >= count
		     || (dstnodes[rank] != PSC_getID(answer.header.sender))
		     || (tids && tids[rank])
		     || errors[rank])) {
	    fallback = 1;
	}

	if (!rank || fallback) {
	    for (rank=0; rank<count; rank++) {
		if (dstnodes[rank]==PSC_getID(answer.header.sender)
		    && (!tids || !tids[rank]) && !errors[rank]) {
		    /*
		     * We have to test for !errors[i], since daemon on node 0
		     * (which has tid 0) might have returned an error.
		     */
		    break;
		}
	    }
	}

	if (rank < count) {
	    errors[rank] = errMsg->error;
	    if (tids) tids[rank] = answer.header.sender;
	}

	if (rank==count) {
	    if (PSC_getID(answer.header.sender)==PSC_getMyID()
		&& errMsg->error==EACCES && count==1) {
		/* This might be due to 'starting not allowed' here */
		errors[0] = errMsg->error;
		if (tids) tids[0] = answer.header.sender;
	    } else {
		PSI_log(-1, "%s: %s from unknown node %d\n", __func__,
			PSP_printMsg(answer.header.type),
			PSC_getID(answer.header.sender));
		return 2; /* Ignore answer */
	    }
	}

	if (answer.header.type==PSP_CD_SPAWNFAILED) {
	    if ((size_t)answer.header.len > sizeof(*errMsg)) {
		size_t bufUsed = sizeof(*errMsg) - sizeof(answer.header);
		char *note = answer.buf + bufUsed;

		if (note[strlen(note)-1] == '\n') note[strlen(note)-1] = '\0';
		if (note[strlen(note)-1] == '\r') note[strlen(note)-1] = '\0';

		PSI_log(-1, "%s: spawn to node %d failed: \"%s\"\n",
			__func__, PSC_getID(answer.header.sender), note);
	    } else {
		PSI_warn(-1, errMsg->error, "%s: spawn to node %d failed",
			 __func__, PSC_getID(answer.header.sender));
	    }
	    return 0;
	}
	break;
    case PSP_CD_WHODIED:
	PSI_log(-1, "%s: got signal %d from %s\n", __func__, sigMsg->signal,
		PSC_printTID(sigMsg->header.sender));
	return 2; /* Ignore answer */
	break;
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	/* Wait for answer */
	goto recv_retry;
	break;
    default:
	PSI_log(-1, "%s: unexpected answer %s\n", __func__,
		PSP_printMsg(answer.header.type));
	return 2; /* Ignore answer */
    }
    return 1;
}

/**
 * @brief Actually spawn processes.
 *
 * Actually spawn @a count processes on the nodes stored within @a
 * dstnodes. The spawned processes will be started within @a
 * workingdir as the current working directory. The @a argc arguments
 * used in order to start the processes are stored within @a argv. The
 * first process spawned -- if not a service process -- will get the
 * unique rank @a rank, all further processes will get successive
 * ranks.
 *
 * Service processes always get rank -2. Only a single service-process
 * is allowed to be spawned.
 *
 * Upon return the array @a errors will hold @a count error codes
 * indicating if the corresponding spawn was successful and if not,
 * what cause the failure. The array @a tids will hold the unique task
 * ID of the started processes, if @a tids was different from NULL.
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
 * @param strictArgv Flag to prevent "smart" replacement of argv[0].
 *
 * @param taskGroup Task-group under which the spawned process shall
 * be started. At the time, TG_ANY and TG_ADMINTASK are good
 * values. The latter is used for admin-tasks, i.e. unaccounted tasks.
 *
 * @param rank The rank of the first process spawned.
 *
 * @param errors Array holding error codes upon return.
 *
 * @param tids Array holding unique task IDs upon return. If this is
 * NULL, no such information will be stored.
 *
 * @return Upon success, the number of processes spawned is returned,
 * i.e. usually this is @a count. Otherwise a negativ value is
 * returned which indicates the number of answer got from spawn
 * requests.
 */
static int dospawn(int count, PSnodes_ID_t *dstnodes, char *workingdir,
		   int argc, char **argv, int strictArgv,
		   PStask_group_t taskGroup,
		   unsigned int rank, int *errors, PStask_ID_t *tids)
{
    int outstanding_answers=0;
    DDTypedBufferMsg_t msg;
    char *mywd;

    int i;          /* count variable */
    int ret = 0;    /* return value */
    int error = 0;  /* error flag */
    int fd = 0;
    PStask_t* task; /* structure to store the information of the new process */
    unsigned int firstRank = rank;
    char *offset = NULL;
    int hugeTask = 0, hugeArgv = 0;
    char *valgrind;
    char *callgrind;
    PSnodes_ID_t lastNode = -1;

    if (!errors) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    valgrind = getenv("PSI_USE_VALGRIND");
    callgrind = getenv("PSI_USE_CALLGRIND");

    if ((taskGroup == TG_SERVICE || taskGroup == TG_SERVICE_SIG
	|| taskGroup == TG_KVS) && count != 1) {
	PSI_log(-1, "%s: spawn %d SERVICE tasks not allowed\n",
		__func__, count);
	return -1;
    }

    for (i=0; i<count; i++) errors[i] = 0;

    if (tids) for (i=0; i<count; i++) tids[i] = 0;

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
    if (isatty(STDIN_FILENO) && !getenv("__PSI_NO_TERM")) {
	task->aretty |= (1 << STDIN_FILENO);
	fd = STDIN_FILENO;
    }
    if (task->aretty) {
	tcgetattr(fd, &task->termios);
	ioctl(fd, TIOCGWINSZ, &task->winsize);
    }
    task->group = taskGroup;
    if (PSI_infoTaskID(-1, PSP_INFO_LOGGERTID, NULL, &(task->loggertid), 0)) {
	PSI_warn(-1, errno, "%s: unable to determine logger's TID", __func__);
	goto error;
    }

    mywd = mygetwd(workingdir);

    if (!mywd) {
	PSI_warn(-1, errno, "%s: unable to get working directory", __func__);
	goto error;
    }

    task->workingdir = mywd;
    task->argc = argc;

    if(!valgrind) {
	 task->argv = malloc(sizeof(char*)*(task->argc+1));
    }
    else {
	 /* add 'valgrind' and its parameters before executable: (see below)*/
	 task->argv = malloc(sizeof(char*)*(task->argc+4));
    }

    if (!task->argv) {
	PSI_warn(-1, errno, "%s: unable to store argument vector", __func__);
	goto error;
    }
    {
	struct stat statbuf;

	if (stat(argv[0], &statbuf) && !strictArgv) {
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

    /* check for valgrind support and whether this is the actual executable: */
    if( valgrind && (strcmp(basename(argv[0]), "mpiexec") !=0 )
	&& (strcmp(argv[0], "valgrind") !=0 ) ) {
	/* add 'valgrind' and its parameters before the executable name: */
	 task->argv[3]=strdup(argv[0]);
	 task->argv[0]=strdup("valgrind");
	 task->argv[1]=strdup("--quiet");

	 if (!callgrind) {
	      if(strcmp(valgrind, "1") == 0) {
		   task->argv[2]=strdup("--leak-check=no");
	      } else {
		   task->argv[2]=strdup("--leak-check=full");
	      }
	 } else {
	      task->argv[2]=strdup("--tool=callgrind");
	 }

	 for (i=1;i<task->argc;i++) {
	      task->argv[i+3]=strdup(argv[i]);
	      if (!task->argv[i+3]) goto error;
	 }
	 task->argc+=3;
    } else {

	 for (i=1;i<task->argc;i++) {
	      task->argv[i]=strdup(argv[i]);
	      if (!task->argv[i]) goto error;
	 }
    }
    task->argv[task->argc] = NULL;

    task->environ = dumpPSIEnv();
    if (!task->environ) {
	PSI_log(-1, "%s: cannot dump environment.", __func__);
	goto error;
    }

    /* test if task can be send */
    PStask_encodeTask(msg.buf, sizeof(msg.buf), task, &offset);
    if (offset) hugeTask = 1;

    i = 0;
    PStask_encodeArgv(msg.buf, sizeof(msg.buf), task->argv, &i, &offset);
    if (i != -1) hugeArgv = 1;

    for (i=0; i<count && !error; i++) {
	/* check if dstnode is ok */
	if (dstnodes[i] < 0 || dstnodes[i] >= PSC_getNrOfNodes()) {
	    errors[i] = ENETUNREACH;
	    if (tids) tids[i] = -1;
	    goto error;
	}
	if (hugeTask || hugeArgv) {
	    int proto = getProtoVersion(dstnodes[i]);

	    if (proto > 339) continue;

	    if (hugeTask) {
		PSI_log(-1, "%s: size of task too large.", __func__);
		PSI_log(-1, " Working directory '%s' too long?\n",
			task->workingdir);
		goto error;
	    }
	    if (hugeArgv) {
		PSI_log(-1, "%s: size of task too large.", __func__);
		PSI_log(-1, " Too many/too long arguments?\n");
		goto error;
	    }
	}
    }

    /* send actual requests */
    outstanding_answers=0;
    for (i=0; i<count && !error; i++) {
	size_t len = 0;

	msg.header.type = PSP_CD_SPAWNREQ;
	msg.header.dest = PSC_getTID(dstnodes[i],0);
	msg.header.sender = PSC_getMyTID();
	msg.header.len = sizeof(msg.header) + sizeof(msg.type);
	msg.type = PSP_SPAWN_TASK;

	/* set correct rank */
	task->rank = rank;

	/* send actual task */
	if (sendTask(&msg, task) < 0) goto error;

	msg.type = PSP_SPAWN_ARG;

	/* send argv stuff */
	if (sendArgv(&msg, task->argv) < 0) goto error;

	if (lastNode != dstnodes[i]) {
	    /* Send the static part of the environment */
	    msg.type = PSP_SPAWN_ENV;
	    if (sendEnv(&msg, task->environ, &len) < 0) goto error;
	} else {
	    /* Let the new rank use the environment of its sibling */
	    msg.type = PSP_SPAWN_ENV_CLONE;
	    msg.header.len = sizeof(msg.header) + sizeof(msg.type);
	    if (PSI_sendMsg(&msg)<0) {
		PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
		goto error;
	    }
	}
	lastNode = dstnodes[i];

	/* Maybe some variable stuff shall also be sent */
	if (extraEnvFunc) {
	    char **extraEnv = extraEnvFunc(rank, extraEnvInfo);

	    if (extraEnv) {
		if (len) {
		    if (PSI_sendMsg(&msg)<0) {
			PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
			goto error;
		    }
		    msg.header.len -= len;
		}

		msg.type = PSP_SPAWN_ENV;
		if (sendEnv(&msg, extraEnv, &len) < 0) goto error;
	    }
	}

	msg.type = PSP_SPAWN_END;
	if (PSI_sendMsg(&msg)<0) {
	    PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	    goto error;
	}
	msg.header.len -= len;

	rank++;
	outstanding_answers++;

	while (PSI_availMsg() > 0 && outstanding_answers) {
	    int r = handleAnswer(firstRank, count, dstnodes, errors, tids);
	    switch (r) {
	    case -1:
		goto error;
		break;
	    case 0:
		error = 1;
	    case 1:
		outstanding_answers--;
		ret++;
		break;
	    case 2:
		/* just ignore */
		break;
	    default:
		PSI_log(-1, "%s: unknown return %d, from handleAnswer()\n",
			__func__, r);
	    }
	}
    }/* for all new processes */

    PStask_delete(task);

    /* collect outstanding answers */
    while (outstanding_answers) {
	int r = handleAnswer(firstRank, count, dstnodes, errors, tids);
	switch (r) {
	case -1:
	    return -1;
	    break;
	case 0:
	    error = 1;
	case 1:
	    outstanding_answers--;
	    ret++;
	    break;
	case 2:
	    /* just ignore */
	    break;
	default:
	    PSI_log(-1, "%s: unknown return %d, from handleAnswer()\n",
		    __func__, r);
	}
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
    return PSI_spawnStrict(count, workdir, argc, argv, 0, errors, tids);
}

int PSI_spawnStrict(int count, char *workdir, int argc, char **argv,
		    int strictArgv, int *errors, PStask_ID_t *tids)
{
    return PSI_spawnStrictHW(count, 0/*hwType*/, 1/*tpp*/, 0/*options*/,
			     workdir, argc, argv, strictArgv, errors, tids);
}

int PSI_spawnStrictHW(int count, uint32_t hwType, uint16_t tpp,
		      PSpart_option_t options, char *workdir,
		      int argc, char **argv, int strictArgv,
		      int *errors, PStask_ID_t *tids)
{
    int total = 0;
    PSnodes_ID_t *nodes;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, count);

    if (!errors) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    if (count<=0) return 0;

    nodes = malloc(sizeof(*nodes) * NODES_CHUNK);
    if (!nodes) {
	*errors = ENOMEM;
	return -1;
    }

    while (count>0) {
	int chunk = (count>NODES_CHUNK) ? NODES_CHUNK : count;
	int rank = PSI_getNodes(chunk, hwType, tpp, options, nodes);
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

	ret = dospawn(chunk, nodes, workdir, argc, argv, strictArgv,
		      TG_ANY, rank, errors+total, tids ? tids+total : NULL);
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

int PSI_spawnRsrvtn(int count, PSrsrvtn_ID_t resID, char *workdir,
		    int argc, char **argv, int strictArgv,
		    int *errors, PStask_ID_t *tids)
{
    int total = 0, ret = -1;
    PSnodes_ID_t *nodes = NULL;

    PSI_log(PSI_LOG_VERB, "%s(%d, %#x)\n", __func__, count, resID);

    if (!errors) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	goto exit;
    }

    if (!(count > 0)) return 0;

    nodes = malloc(sizeof(*nodes) * NODES_CHUNK);
    if (!nodes) {
	*errors = ENOMEM;
	goto exit;
    }

    while (count>0) {
	int chunk = (count>NODES_CHUNK) ? NODES_CHUNK : count;
	int rank = PSI_getSlots(chunk, resID, nodes);
	int i, num;

	if (rank < 0) {
	    errors[total] = ENXIO;
	    goto exit;
	}

	PSI_log(PSI_LOG_SPAWN, "%s: will spawn to:", __func__);
	for (i=0; i<chunk; i++) {
	    PSI_log(PSI_LOG_SPAWN, " %2d", nodes[i]);
	}
	PSI_log(PSI_LOG_SPAWN, ".\n");
	PSI_log(PSI_LOG_SPAWN, "%s: first rank: %d\n", __func__, rank);

	num = dospawn(chunk, nodes, workdir, argc, argv, strictArgv,
		      TG_ANY, rank, errors+total, tids ? tids+total : NULL);
	if (num != chunk) goto exit;

	count -= chunk;
	total += chunk;
    }
    ret = total;

exit:
    if (nodes) free(nodes);
    return ret;
}

int PSI_spawnSingle(char *workdir, int argc, char **argv,
		    int *error, PStask_ID_t *tid)
{
    /* @todo Here we should get the node to spawn to and test if this
     * is corret */
    int ret, rank;
    PSnodes_ID_t node;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    rank = PSI_getNodes(1, 0 /*hwType*/, 1 /*tpp*/, 0/*options*/, &node);
    if (rank < 0) {
	*error = ENXIO;
	return -1;
    }

    PSI_log(PSI_LOG_SPAWN, "%s: will spawn to: %d  rank %d\n",
	    __func__, node, rank);

    ret = dospawn(1, &node, workdir, argc, argv, 0, TG_ANY, rank, error, tid);
    if (ret != 1) return -1;

    return rank;
}

int PSI_spawnAdmin(PSnodes_ID_t node, char *workdir, int argc, char **argv,
		   int strictArgv, unsigned int rank,
		   int *error, PStask_ID_t *tid)
{
    int ret;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, node);

    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    if (node == -1) node = PSC_getMyID();
    ret = dospawn(1, &node, workdir, argc, argv, strictArgv,
		  TG_ADMINTASK, rank, error, tid);
    if (ret != 1) return -1;

    return 1;
}

int PSI_spawnService(PSnodes_ID_t node, PStask_group_t taskGroup, char *wDir,
		     int argc, char **argv, int *error, PStask_ID_t *tid,
		     int rank)
{
    int ret;
    char *envStr = getenv(ENV_NUM_SERVICE_PROCS);

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, node);

    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    /* tell logger about service process */
    if (envStr) {
	char *end, newStr[32];
	long oldNum = strtol(envStr, &end, 10);

	if (*end) {
	    PSI_log(-1, "%s: unknown value '%s' in environment '%s'\n",
		    __func__, envStr, ENV_NUM_SERVICE_PROCS);
	    oldNum = 0;
	}

	snprintf(newStr, sizeof(newStr), "%ld", oldNum+1);
	setenv(ENV_NUM_SERVICE_PROCS, newStr, 1);
    } else {
	setenv(ENV_NUM_SERVICE_PROCS, "1", 1);
    }

    if (node == -1) node = PSC_getMyID();

    if (rank >= -1) rank = -2;

    ret = dospawn(1, &node, wDir, argc, argv, 0, taskGroup, rank, error, tid);
    if (ret != 1) return -1;

    return 1;
}

PStask_ID_t PSI_spawnRank(int rank, char *workdir, int argc, char **argv,
			  int *error)
{
    PSnodes_ID_t node;
    int ret, rankGot = PSI_getRankNode(rank, &node);
    PStask_ID_t tid;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, rank);

    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    if (rankGot != rank) {
	*error = ENXIO;
	return 0;
    }

    PSI_log(PSI_LOG_SPAWN, "%s: will spawn to: %d  rank %d\n",
	    __func__, node, rank);

    ret = dospawn(1, &node, workdir, argc, argv, 0, TG_ANY, rank, error, &tid);
    if (ret != 1) return 0;

    return tid;
}

PStask_ID_t PSI_spawnGMSpawner(int np, char *workdir, int argc, char **argv,
			       int *error)
{
    PSnodes_ID_t node;
    int ret, rankGot = PSI_getRankNode(0, &node);
    PStask_ID_t tid;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, np);

    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    if (rankGot) {
	*error = ENXIO;
	return 0;
    }

    PSI_log(PSI_LOG_SPAWN, "%s: will spawn to: %d", __func__, node);

    ret = dospawn(1, &node, workdir, argc, argv, 0, TG_ANY, np, error, &tid);
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
	PIfilename = PSC_concat(home, "/", filename, NULL);

	PIfile = fopen(PIfilename, "w+");
	/* File open failed finally */
	if (!PIfile) {
	    PSI_warn(-1, errno, "%s: fopen", __func__);
	    free(PIfilename);
	    free(myprog);
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
		free(myprog);
		return NULL;
	    }
	    PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr.s_addr, 0);
	}
	fprintf(PIfile, "%s %d %s\n", inet_ntoa(hostaddr), (i != 0), myprog);
    }
    fclose(PIfile);
    free(myprog);

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
	MPIhostsFilename = PSC_concat(home, "/", filename, NULL);

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

int PSI_kill(PStask_ID_t tid, short signal, int async)
{
    DDSignalMsg_t msg;
    DDErrorMsg_t answer;

    PSI_log(PSI_LOG_VERB, "%s(%s, %d)\n", __func__, PSC_printTID(tid), signal);

    msg.header.type = PSP_CD_SIGNAL;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.header.len = sizeof(msg);
    msg.signal = signal;
    msg.param = getuid();
    msg.pervasive = 0;
    msg.answer = 1;

    if (PSI_sendMsg(&msg)<0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    if (!async) {
	if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer))<0) {
	    PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	    return -1;
	}

	if (answer.request != tid) {
	    PSI_log(-1, "%s: answer from wrong task (%s/",
		    __func__, PSC_printTID(answer.request));
	    PSI_log(-1, "%s)\n", PSC_printTID(tid));
	    return -2;
	}

	return answer.error;
    }

    return 0;
}
