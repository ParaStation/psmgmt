/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psispawn.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "psserial.h"

#include "psi.h"
#include "psilog.h"
#include "psiinfo.h"
#include "psienv.h"
#include "psipartition.h"

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
    int new_argc = 0;
    char **new_argv;
    char env_name[ sizeof(ENV_NODE_RARG) + 20];
    int cnt = 0;
    int i;

    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

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

	for (i = 0; i < cnt; i++) {
	    snprintf(env_name, sizeof(env_name), ENV_NODE_RARG, i);
	    new_argv[i] = getenv(env_name);
	    /* Propagate the environment */
	    setPSIEnv(env_name, new_argv[i], 1);
	}
	for (i = 0; i < Argc; i++) {
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
 * @brief Send task-structure
 *
 * Send the actual task structure stored in @a task using the message
 * template @a msg and the sending function @a sendFunc. @a msg has to
 * have the destination address already filled in.
 *
 * This function might send -- besides the initial PSP_SPAWN_TASK
 * message -- more messages containing trailing part of the task's
 * working-directory. The latter will use messages of type
 * PSP_SPAWN_WDIRCNTD.
 *
 * @param msg Message template prepared to send the task-structure
 *
 * @param task The task to send
 *
 * @param sendFunc Actual function used to send out the message(s)
 *
 * @return On success this function returns true; or false if an error
 * occurred
 */
static bool sendTask(DDTypedBufferMsg_t *msg, PStask_t *task,
		     ssize_t (*sendFunc)(void *))
{
    /* pack the task information in the msg */
    char *offset = NULL;
    size_t len = PStask_encodeTask(msg->buf, sizeof(msg->buf), task, &offset);

    msg->header.len += len;
    if (sendFunc(msg) < 0) {
	PSI_warn(-1, errno, "%s: sending", __func__);
	return false;
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
	    /* buffer too small */
	    strncpy(msg->buf, offset, sizeof(msg->buf) - 1);
	    msg->buf[sizeof(msg->buf) - 1] = '\0';
	    len =  sizeof(msg->buf);
	    offset += sizeof(msg->buf) - 1;
	}

	msg->header.len += len;
	if (sendFunc(msg) < 0) {
	    PSI_warn(-1, errno, "%s: sending", __func__);
	    return false;
	}
	msg->header.len -= len;
    }

    return true;
}

/**
 * @brief Send argument-vector
 *
 * Send the argument-vector @a argv using the message template @a msg
 * and the sending function @a sendFunc. @a msg has to have the
 * destination address already filled in.
 *
 * This function might send several messages of both types,
 * PSP_SPAWN_ARG and PSP_SPAWN_ARGCNTD, depending on the size of @a
 * argv as a whole and the single arguments.
 *
 * @param msg Message template prepared to send the argument-vector
 *
 * @param argv The argument-vector to send
 *
 * @param sendFunc Actual function used to send out the message(s)
 *
 * @return On success this function returns true; or false if an error
 * occurred
 */
static bool sendArgv(DDTypedBufferMsg_t *msg, char **argv,
		     ssize_t (*sendFunc)(void *))
{
    char *off = NULL;
    int num = 0, len;

    if (!argv) return true;

    msg->header.len = offsetof(DDTypedBufferMsg_t, buf);

    while (num != -1) {
	msg->type = (off) ? PSP_SPAWN_ARGCNTD : PSP_SPAWN_ARG;

	len = PStask_encodeArgv(msg->buf, sizeof(msg->buf), argv, &num, &off);

	msg->header.len += len;
	if (sendFunc(msg) < 0) {
	    PSI_warn(-1, errno, "%s: sending", __func__);
	    return false;
	}
	msg->header.len -= len;
    }

    return true;
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
 * Send the environment @a env using the message template @a msg and
 * the sending function @a sendFunc. @a msg has to have the
 * destination address already filled in.
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
 * @param sendFunc Actual function used to send out the message(s)
 *
 * @return On success this function returns true; or false if an error
 * occurred. In the latter case, @a msg will point to the last message
 * still to be sent to the destination. The current length of the
 * message's buffer is given back in @a len.
 */
static bool sendEnv(DDTypedBufferMsg_t *msg, char **env, size_t *len,
		    ssize_t (*sendFunc)(void *))
{
    char *off = NULL;
    int num = 0;

    *len = 0;

    if (!env) return true;

    msg->header.len = offsetof(DDTypedBufferMsg_t, buf);

    while (true) {
	if (off) msg->type = PSP_SPAWN_ENVCNTD;

	*len = PStask_encodeEnv(msg->buf, sizeof(msg->buf), env, &num, &off);
	msg->header.len += *len;

	if (num == -1) {
	    /* last entry done */
	    if (msg->type == PSP_SPAWN_ENVCNTD) {
		if (sendFunc(msg) < 0) {
		    PSI_warn(-1, errno, "%s: sending(CNTD)", __func__);
		    return false;
		}
		msg->header.len -= *len;
		*len = 0;
	    }
	    return true;
	}

	if (sendFunc(msg) < 0) {
	    PSI_warn(-1, errno, "%s: sending", __func__);
	    return false;
	}
	msg->header.len -= *len;
	msg->type = PSP_SPAWN_ENV;
    }

    PSI_log(-1, "%s: Never be here\n", __func__);
    return false;
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
 * @param firstRank Rank of the first process to spawn
 *
 * @param count Number of processes to spawn
 *
 * @param dstnodes Array of node IDs used in order to spawn processes
 *
 * @param errors Array holding error codes upon return.
 *
 * @param tids Array holding unique task IDs upon return. If this is
 * NULL, no such information will be stored.
 *
 * @return Return might be one of four different values:
 * -2: ignore message; from unknown node, answer to "who died" question, etc.
 * -1: fatal error
 *  0: legitimate error while spawning; permission denied, down node, etc.
 *  1: no error
  */
static int handleAnswer(unsigned int firstRank, int count,
			PSnodes_ID_t *dstnodes, int *errors, PStask_ID_t *tids)
{
    DDBufferMsg_t answer;
    DDErrorMsg_t *errMsg = (DDErrorMsg_t *)&answer;
    DDSignalMsg_t *sigMsg = (DDSignalMsg_t *)&answer;
    int rank;

    if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer)) < 0) {
	PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	return -1;
    }
    switch (answer.header.type) {
    case PSP_CD_SPAWNSUCCESS:
	rank = errMsg->request - firstRank;
	if (rank >= 0 && rank < count) {
	    errors[rank] = 0; /* no error on success */
	    if (tids) tids[rank] = answer.header.sender;
	} else {
	    PSI_log(-1, "%s: %s from illegal rank %d at node %d\n", __func__,
		    PSP_printMsg(answer.header.type), errMsg->request,
		    PSC_getID(answer.header.sender));
	    return -2; /* Ignore answer */
	}
	return 1;
    case PSP_CD_SPAWNFAILED:
	rank = errMsg->request - firstRank;

	/* find the right task request */
	/* errMsg->request == 0 might be due to rank == 0 or unknown rank */
	bool found = false;
	if (errMsg->request == 0 && firstRank == 0
	    && dstnodes[0] == PSC_getID(answer.header.sender)
	    && (!tids || tids[0] == 0)
	    && !errors[0]) {
	    found = true;
	}

	if (errMsg->request == 0 && !found) {
	    for (rank = 0; rank < count; rank++) {
		if (dstnodes[rank] == PSC_getID(answer.header.sender)
		    && (!tids || tids[rank] == 0) && !errors[rank]) {
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

	if (rank == count) {
	    if (PSC_getID(answer.header.sender) == PSC_getMyID()
		&& errMsg->error == EACCES && count == 1) {
		/* This might be due to 'starting not allowed' here */
		/* @todo is this actually true? IMHO these are always
		 * sent with the correct (remote) sender */
		PSI_log(-1, "%s: Starting not allowed from node %d\n", __func__,
			PSC_getID(answer.header.sender));
		errors[0] = errMsg->error;
		if (tids) tids[0] = answer.header.sender;
	    } else {
		PSI_log(-1, "%s: %s from unknown node %d\n", __func__,
			PSP_printMsg(answer.header.type),
			PSC_getID(answer.header.sender));
		return -2; /* Ignore answer */
	    }
	}

	if (answer.header.len > sizeof(*errMsg)) {
	    size_t bufUsed = sizeof(*errMsg) - sizeof(answer.header);
	    char *note = answer.buf + bufUsed;

	    /* Ensure to only use valid buffer data as string */
	    if (answer.header.len == sizeof(answer)) {
		answer.buf[sizeof(answer.buf) - 1] = '\0';
	    } else {
		answer.buf[answer.header.len
			   - offsetof(DDBufferMsg_t, buf)] = '\0';
	    }

	    if (note[strlen(note)-1] == '\n') note[strlen(note)-1] = '\0';
	    if (note[strlen(note)-1] == '\r') note[strlen(note)-1] = '\0';

	    PSI_log(-1, "%s: spawn to node %d failed: \"%s\"\n",
		    __func__, PSC_getID(answer.header.sender), note);
	} else {
	    PSI_warn(-1, errMsg->error, "%s: spawn to node %d failed",
		     __func__, PSC_getID(answer.header.sender));
	}
	return 0;
    case PSP_CD_WHODIED:
	PSI_log(-1, "%s: got signal %d from %s\n", __func__, sigMsg->signal,
		PSC_printTID(sigMsg->header.sender));
	__attribute__((fallthrough));
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	return -2; /* Ignore answer */
    default:
	PSI_log(-1, "%s: unexpected answer %s\n", __func__,
		PSP_printMsg(answer.header.type));
	return -2; /* Ignore answer */
    }
    return 1;
}

/**
 * @brief Send spawn request
 *
 * Use the serialization layer in order to send the request to spawn
 * at most @a max processes. The request might be split into multiple
 * messages depending on the amount of information that needs to be
 * submitted. The task structure @a task describes the processes to be
 * spawned containing e.g. the argument vector or the environment. @a
 * dstnodes holds the ID of the destination node (in dstnodes[0]) and
 * the number of processes to spawn (encoded in the number of
 * consecutive elements identical to dstnodes[0]). Nevertheless, @a
 * max limits the number of processes anyhow.
 *
 * This function will consider the per rank environment characterized
 * through the function to be registered via @ref
 * PSI_registerRankEnvFunc().
 *
 * A single call to this function might initiate to spawn multiple
 * processes to a remote node. The actual number of processes spawned
 * is returned.
 *
 * On the long run this function shall obsolete PSI_sendSpawnMsg().
 *
 * @param task Task structure describing the processes to be spawned
 *
 * @param dstnodes Destination nodes of the spawn requests
 *
 * @param max Maximum number of processes to spawn -- might be less
 *
 * @return On success the number of spawned ranks is returned; or
 * -1 in case of an error
 */
static int sendSpawnReq(PStask_t* task, PSnodes_ID_t *dstnodes, uint32_t max)
{
    PS_SendDB_t msg;
    PStask_ID_t dest = PSC_getTID(dstnodes[0], 0);

    initFragBuffer(&msg, PSP_CD_SPAWNREQUEST, -1);
    setFragDest(&msg, dest);

    uint32_t num = 0;
    while (num < max && dstnodes[num] == dstnodes[0]) num++;

    PSI_log(PSI_LOG_SPAWN, "%s: %d proc to %d at rank %d\n", __func__, num,
	    dstnodes[0], task->rank);

    addUint32ToMsg(num, &msg);
    if (!PStask_addToMsg(task, &msg)) return -1;
    if (!addStringArrayToMsg(task->argv, &msg)) return -1;
    if (!addStringArrayToMsg(task->environ, &msg)) return -1;

    for (uint32_t r = 0; r < num; r++) {
	char **extraEnv;

	if (extraEnvFunc
	    && (extraEnv = extraEnvFunc(task->rank + r, extraEnvInfo))) {
	    if (!addStringArrayToMsg(extraEnv, &msg)) return -1;
	} else {
	    addUint32ToMsg(0, &msg);
	}
    }

    if (sendFragMsg(&msg) == -1) {
	PSI_log(-1, "%s: send to %d failed\n", __func__,  dstnodes[0]);
	return -1;
    }

    return num;
}

bool PSI_sendSpawnMsg(PStask_t* task, bool envClone, PSnodes_ID_t dest,
		      ssize_t (*sendFunc)(void *))
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_SPAWNREQ,
	    .dest = PSC_getTID(dest, 0),
	    .sender = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_SPAWN_TASK };
    static PSnodes_ID_t lastNode = -1;
    size_t len = 0;

    /* send actual task */
    if (!sendTask(&msg, task, sendFunc)) return false;

    /* send argv stuff */
    msg.type = PSP_SPAWN_ARG;
    if (!sendArgv(&msg, task->argv, sendFunc)) return false;

    if (!envClone || lastNode != dest) {
	/* Send the static part of the environment */
	msg.type = PSP_SPAWN_ENV;
	if (!sendEnv(&msg, task->environ, &len, sendFunc)) return false;
    } else {
	/* Let the new rank use the environment of its sibling */
	msg.type = PSP_SPAWN_ENV_CLONE;
	msg.header.len = offsetof(DDTypedBufferMsg_t, buf);
	if (sendFunc(&msg) < 0) {
	    PSI_warn(-1, errno, "%s: sending", __func__);
	    return false;
	}
    }
    lastNode = dest;

    /* Maybe some variable stuff shall also be sent */
    if (extraEnvFunc) {
	char **extraEnv = extraEnvFunc(task->rank, extraEnvInfo);

	if (extraEnv) {
	    if (len) {
		if (sendFunc(&msg) < 0) {
		    PSI_warn(-1, errno, "%s: sending", __func__);
		    return false;
		}
		msg.header.len -= len;
	    }

	    msg.type = PSP_SPAWN_ENV;
	    if (!sendEnv(&msg, extraEnv, &len, sendFunc)) return false;
	}
    }

    msg.type = PSP_SPAWN_END;
    if (sendFunc(&msg) < 0) {
	PSI_warn(-1, errno, "%s: sending", __func__);
	return false;
    }

    return true;
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
 * @param count Number of processes to spawn
 *
 * @param dstnodes Array of node IDs used in order to spawn processes
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
 * @param resID The ID of the reservation to spawn the task into. -1 if none.
 *
 * @param rank The rank of the first process spawned.
 *
 * @param errors Array holding error codes upon return.
 *
 * @param tids Array holding unique task IDs upon return. If this is
 * NULL, no such information will be stored.
 *
 * @return Upon success, the number of processes spawned is returned,
 * i.e. usually this is @a count. Otherwise a negative value is
 * returned which indicates the number of answer got from spawn
 * requests.
 */
static int dospawn(int count, PSnodes_ID_t *dstnodes, char *workingdir,
		   int argc, char **argv, bool strictArgv,
		   PStask_group_t taskGroup, PSrsrvtn_ID_t resID,
		   unsigned int rank, int *errors, PStask_ID_t *tids)
{
    int ret = 0;    /* return value */

    if (!errors) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    char *valgrind = getenv("PSI_USE_VALGRIND");
    char *callgrind = getenv("PSI_USE_CALLGRIND");

    if ((taskGroup == TG_SERVICE || taskGroup == TG_SERVICE_SIG
	|| taskGroup == TG_KVS) && count != 1) {
	PSI_log(-1, "%s: spawn %d SERVICE tasks not allowed\n",
		__func__, count);
	return -1;
    }

    for (int i = 0; i < count; i++) errors[i] = 0;
    if (tids) for (int i = 0; i < count; i++) tids[i] = 0;

    /* setup task structure to store information of the new processes */
    PStask_t* task = PStask_new();
    if (!task) {
	PSI_log(-1, "%s: Cannot create task structure\n", __func__);
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

    int fd = 0;
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
    if (PSI_infoTaskID(-1, PSP_INFO_LOGGERTID, NULL,
		       &(task->loggertid), false)) {
	PSI_warn(-1, errno, "%s: unable to determine logger's TID", __func__);
	goto cleanup;
    }
    task->resID = resID;

    char *myWD = PSC_getwd(workingdir);
    if (!myWD) {
	PSI_warn(-1, errno, "%s: unable to get working directory", __func__);
	goto cleanup;
    }

    task->workingdir = myWD;
    task->argc = argc;

    if (!valgrind) {
	 task->argv = malloc(sizeof(char*)*(task->argc+1));
    } else {
	 /* add 'valgrind' and its parameters before executable: (see below)*/
	 task->argv = malloc(sizeof(char*)*(task->argc+4));
    }

    if (!task->argv) {
	PSI_warn(-1, errno, "%s: unable to store argument vector", __func__);
	goto cleanup;
    }

    struct stat statbuf;
    if (stat(argv[0], &statbuf) && !strictArgv) {
	char myexec[PATH_MAX];
	int length;

	length = readlink("/proc/self/exe", myexec, sizeof(myexec)-1);
	if (length < 0) {
	    PSI_warn(-1, errno, "%s: readlink", __func__);
	} else {
	    myexec[length]='\0';
	}

	task->argv[0] = strdup(myexec);
    } else {
	task->argv[0] = strdup(argv[0]);
    }

    /* check for valgrind support and whether this is the actual executable: */
    if (valgrind && strcmp(basename(argv[0]), "mpiexec")
	&& strcmp(basename(argv[0]), "kvsprovider")
	&& strcmp(basename(argv[0]), "spawner")
	&& strcmp(argv[0], "valgrind") ) {
	/* add 'valgrind' and its parameters before the executable name: */
	task->argv[3] = strdup(argv[0]);
	task->argv[0] = strdup("valgrind");
	task->argv[1] = strdup("--quiet");

	if (!callgrind) {
	    if (!strcmp(valgrind, "1")) {
		task->argv[2]=strdup("--leak-check=no");
	    } else {
		task->argv[2]=strdup("--leak-check=full");
	    }
	} else {
	    task->argv[2]=strdup("--tool=callgrind");
	}

	for (uint32_t a = 1; a < task->argc; a++) {
	    task->argv[a+3]=strdup(argv[a]);
	    if (!task->argv[a+3]) goto cleanup;
	}
	task->argc+=3;
    } else {
	for (uint32_t a = 1; a < task->argc; a++) {
	    task->argv[a]=strdup(argv[a]);
	    if (!task->argv[a]) goto cleanup;
	}
    }
    task->argv[task->argc] = NULL;

    task->environ = dumpPSIEnv();
    if (!task->environ) {
	PSI_log(-1, "%s: cannot dump environment\n", __func__);
	goto cleanup;
    }

    for (int i = 0; i < count; i++) {
	if (i && dstnodes[i] == dstnodes[i-1]) continue; // do not check twice
	/* check if dstnode is ok */
	if (!PSC_validNode(dstnodes[i])) {
	    errors[i] = ENETUNREACH;
	    if (tids) tids[i] = -1;
	    goto cleanup;
	}
	/* Fill the protocol version cache before sending actual requests */
	if (PSI_protocolVersion(dstnodes[i]) == -1) {
	    PSI_log(-1, "%s: unable to get protocol version for node %d\n",
		    __func__, dstnodes[i]);
	    goto cleanup;
	}
    }

    if (!initSerial(0, PSI_sendMsg)) {
	PSI_log(-1, "%s: initSerial() failed\n", __func__);
	goto cleanup;
    }

    /* send actual requests */
    bool error = false;
    int expectedAnswers = 0;
    unsigned int firstRank = rank;
    for (int i = 0; i < count && !error;) {
	task->rank = rank;
	int protocolVersion = PSI_protocolVersion(dstnodes[i]);

	int num;
	if (protocolVersion == -1) {
	    PSI_log(-1, "%s: unable to get protocol version for node %d\n",
		    __func__, dstnodes[i]);
	    goto cleanup;
	} else if (protocolVersion > 340) {
	    num = sendSpawnReq(task, &dstnodes[i], count - i);
	    if (num < 0) goto cleanup;
	} else {
	    if (!PSI_sendSpawnMsg(task, true, dstnodes[i], PSI_sendMsg)) {
		goto cleanup;
	    }
	    num = 1;
	}

	i += num;
	rank += num;
	expectedAnswers += num;

	while (PSI_availMsg() > 0 && expectedAnswers) {
	    int r = handleAnswer(firstRank, count, dstnodes, errors, tids);
	    switch (r) {
	    case -2:
		/* just ignore */
		break;
	    case -1:
		goto cleanup;
		break;
	    case 0:
		error = true;
		__attribute__((fallthrough));
	    case 1:
		expectedAnswers--;
		ret++;
		break;
	    default:
		PSI_log(-1, "%s: unknown return %d, from handleAnswer()\n",
			__func__, r);
	    }
	}
    }/* for all new processes */

    PStask_delete(task);

    /* collect expected answers */
    while (expectedAnswers > 0) {
	int r = handleAnswer(firstRank, count, dstnodes, errors, tids);
	switch (r) {
	case -2:
	    /* just ignore */
	    break;
	case -1:
	    return -1;
	    break;
	case 0:
	    error = true;
	    __attribute__((fallthrough));
	case 1:
	    expectedAnswers--;
	    ret++;
	    break;
	default:
	    PSI_log(-1, "%s: unknown return %d, from handleAnswer()\n",
		    __func__, r);
	}
    }

    if (error) ret = -ret;
    return ret;

 cleanup:
    PStask_delete(task);

    return -1;
}

int PSI_spawn(int count, char *workdir, int argc, char **argv,
	      int *errors, PStask_ID_t *tids)
{
    return PSI_spawnStrict(count, workdir, argc, argv, false, errors, tids);
}

int PSI_spawnStrict(int count, char *workdir, int argc, char **argv,
		    bool strictArgv, int *errors, PStask_ID_t *tids)
{
    return PSI_spawnStrictHW(count, 0/*hwType*/, 1/*tpp*/, 0/*options*/,
			     workdir, argc, argv, strictArgv, errors, tids);
}

int PSI_spawnStrictHW(int count, uint32_t hwType, uint16_t tpp,
		      PSpart_option_t options, char *workdir,
		      int argc, char **argv, bool strictArgv,
		      int *errors, PStask_ID_t *tids)
{
    int total = 0;
    PSnodes_ID_t *nodes;

    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, count);

    if (!errors) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    if (count <= 0) return 0;

    nodes = malloc(sizeof(*nodes) * NODES_CHUNK);
    if (!nodes) {
	*errors = ENOMEM;
	return -1;
    }

    while (count > 0) {
	int chunk = (count > NODES_CHUNK) ? NODES_CHUNK : count;
	int rank = PSI_getNodes(chunk, hwType, tpp, options, nodes);
	int i, ret;

	if (rank < 0) {
	    errors[total] = ENXIO;
	    free(nodes);
	    return -1;
	}

	PSI_log(PSI_LOG_SPAWN, "%s: will spawn to:", __func__);
	for (i = 0; i < chunk; i++) {
	    PSI_log(PSI_LOG_SPAWN, " %2d", nodes[i]);
	}
	PSI_log(PSI_LOG_SPAWN, "\n");
	PSI_log(PSI_LOG_SPAWN, "%s: first rank: %d\n", __func__, rank);

	ret = dospawn(chunk, nodes, workdir, argc, argv, strictArgv, TG_ANY,
		-1, rank, errors+total, tids ? tids+total : NULL);
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
		    int argc, char **argv, bool strictArgv,
		    int *errors, PStask_ID_t *tids)
{
    int total = 0, ret = -1;
    PSnodes_ID_t *nodes = NULL;

    PSI_log(PSI_LOG_VERB, "%s(%d, %#x)\n", __func__, count, resID);

    if (!errors) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	goto exit;
    }

    if (count <= 0) return 0;

    nodes = malloc(sizeof(*nodes) * NODES_CHUNK);
    if (!nodes) {
	*errors = ENOMEM;
	goto exit;
    }

    while (count > 0) {
	int chunk = (count > NODES_CHUNK) ? NODES_CHUNK : count;
	int rank = PSI_getSlots(chunk, resID, nodes);
	int i, num;

	if (rank < 0) {
	    errors[total] = ENXIO;
	    goto exit;
	}

	PSI_log(PSI_LOG_SPAWN, "%s: will spawn to:", __func__);
	for (i = 0; i < chunk; i++) {
	    PSI_log(PSI_LOG_SPAWN, " %2d", nodes[i]);
	}
	PSI_log(PSI_LOG_SPAWN, "\n");
	PSI_log(PSI_LOG_SPAWN, "%s: first rank: %d\n", __func__, rank);

	num = dospawn(chunk, nodes, workdir, argc, argv, strictArgv, TG_ANY,
	       resID, rank, errors+total, tids ? tids+total : NULL);
	if (num != chunk) goto exit;

	count -= chunk;
	total += chunk;
    }
    ret = total;

exit:
    free(nodes);
    return ret;
}

int PSI_spawnSingle(char *workdir, int argc, char **argv,
		    int *error, PStask_ID_t *tid)
{
    PSI_log(PSI_LOG_VERB, "%s()\n", __func__);

    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    PSnodes_ID_t node;
    int rank = PSI_getNodes(1, 0 /*hwType*/, 1 /*tpp*/, 0 /*options*/, &node);
    if (rank < 0) {
	*error = ENXIO;
	return -1;
    }

    PSI_log(PSI_LOG_SPAWN, "%s: spawn rank %d to %d\n", __func__, rank, node);

    if (dospawn(1, &node, workdir, argc, argv, false, TG_ANY, -1, rank,
		error, tid) != 1) return -1;

    return rank;
}

int PSI_spawnAdmin(PSnodes_ID_t node, char *workdir, int argc, char **argv,
		   bool strictArgv, unsigned int rank,
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
		  TG_ADMINTASK, -1, rank, error, tid);
    if (ret != 1) return -1;

    return 1;
}

int PSI_spawnService(PSnodes_ID_t node, PStask_group_t taskGroup, char *wDir,
		     int argc, char **argv, int *error, PStask_ID_t *tid,
		     int rank)
{
    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, node);

    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    /* tell logger about service process */
    /* @todo remove this on the long run since the logger ignores it */
    char *envStr = getenv(ENV_NUM_SERVICE_PROCS);
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

    int ret = dospawn(1, &node, wDir, argc, argv, false, taskGroup, -1, rank,
		      error, tid);
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

    ret = dospawn(1, &node, workdir, argc, argv, false, TG_ANY, -1, rank,
		  error, &tid);
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

    ret = dospawn(1, &node, workdir, argc, argv, false, TG_ANY, -1, np,
		  error, &tid);
    if (ret != 1) return 0;

    return tid;
}

char *PSI_createPGfile(int num, const char *prog, int local)
{
    char *myprog = PSC_getwd(prog);
    if (!myprog) {
	PSI_warn(-1, errno, "%s: PSC_getwd", __func__);
	return NULL;
    }

    char filename[20];
    snprintf(filename, sizeof(filename), "PI%d", getpid());
    FILE *PIfile = fopen(filename, "w+");

    char *PIfilename;
    if (PIfile) {
	PIfilename = strdup(filename);
    } else {
	/* File open failed, lets try the user's home directory */
	char *home = getenv("HOME");
	PIfilename = PSC_concat(home, "/", filename);

	PIfile = fopen(PIfilename, "w+");
	/* File open failed finally */
	if (!PIfile) {
	    PSI_warn(-1, errno, "%s: fopen", __func__);
	    free(PIfilename);
	    free(myprog);
	    return NULL;
	}
    }

    for (int i = 0; i < num; i++) {
	PSnodes_ID_t node;
	static struct in_addr hostaddr;

	if (!local || !i) {
	    if (PSI_infoNodeID(-1, PSP_INFO_RANKID, &i, &node, true)
		|| (node < 0)) {
		fclose(PIfile);
		free(PIfilename);
		free(myprog);
		return NULL;
	    }
	    PSI_infoUInt(-1, PSP_INFO_NODE, &node, &hostaddr.s_addr, false);
	}
	fprintf(PIfile, "%s %d %s\n", inet_ntoa(hostaddr), (i != 0), myprog);
    }
    fclose(PIfile);
    free(myprog);

    return PIfilename;
}

int PSI_kill(PStask_ID_t tid, short signal, int async)
{
    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_SIGNAL,
	    .dest = tid,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	msg.signal = signal,
	msg.param = getuid(),
	msg.pervasive = 0,
	msg.answer = 1 };
    DDErrorMsg_t answer;

    PSI_log(PSI_LOG_VERB, "%s(%s, %d)\n", __func__, PSC_printTID(tid), signal);

    if (PSI_sendMsg(&msg) < 0) {
	PSI_warn(-1, errno, "%s: PSI_sendMsg", __func__);
	return -1;
    }

    if (!async) {
	if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer)) < 0) {
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
