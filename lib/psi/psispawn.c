/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psispawn.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "pscommon.h"
#include "psserial.h"
#include "psstrv.h"

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
	    setPSIEnv(env_name, new_argv[i]);
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

/** Function called to create per rank environment */
static char **(*extraEnvFunc)(int, void *) = NULL;

static void *extraEnvInfo = NULL;

void PSI_registerRankEnvFunc(char **(*func)(int, void *), void *info)
{
    PSI_log(PSI_LOG_SPAWN, "%s(%p)\n", __func__, func);

    extraEnvFunc = func;
    extraEnvInfo = info;
}

/** Structure containing all information to handle an answer */
typedef struct {
    int first;     /**< Rank offset */
    int num;       /**< Total number of spawn requests sent */
    int expected;  /**< Count outstanding answers to spawn requests */
    int *errors;   /**< Array to collect error codes */
} AnswerBucket_t;

/**
 * @brief Handle answer on spawn requests
 *
 * Handle a pending answer contained in the message @a msg sent by the
 * remote side on a spawn requests. The structure @a bucket contains
 * all information to handle this request including the:
 * - rank offset of the tasks to be spawned (and thus offset to errors)
 * - total number of spawn requests sent
 * - count of answers still expected to be received
 * - array to store possible error codes to
 *
 * @param msg Message to handle
 *
 * @param bucket Structure containing all information to handle answer
 *
 * @return Return might be one of four different values:
 * -2: ignore message; from unknown node, answer to "who died" question, etc.
 * -1: fatal error
 *  0: legitimate error while spawning; permission denied, down node, etc.
 *  1: no error
 */
static int handleAnswer(DDBufferMsg_t *msg, AnswerBucket_t *bucket)
{
    if (!bucket || !bucket->errors) return -1;

    DDErrorMsg_t *errMsg = (DDErrorMsg_t *)msg;
    int localRank = errMsg->request - bucket->first;

    switch (msg->header.type) {
    case PSP_CD_SPAWNSUCCESS:
	if (localRank < 0 || localRank >= bucket->num) {
	    PSI_log(-1, "%s: %s from illegal rank %ld at node %d\n", __func__,
		    PSP_printMsg(errMsg->header.type), errMsg->request,
		    PSC_getID(errMsg->header.sender));
	    return -2; /* Ignore answer */
	}
	bucket->expected--;
	return 1;
    case PSP_CD_SPAWNFAILED:
	if (localRank < 0 || localRank >= bucket->num) {
	    PSI_log(-1, "%s: %s from illegal rank %ld at node %d\n", __func__,
		    PSP_printMsg(errMsg->header.type), errMsg->request,
		    PSC_getID(errMsg->header.sender));
	    return -2; /* Ignore answer */
	}
	bucket->errors[localRank] = errMsg->error;
	bucket->expected--;

	if (errMsg->header.len > sizeof(*errMsg)) {
	    /* trailing note in message */
	    size_t bufUsed = sizeof(*errMsg) - DDBufferMsgOffset;
	    char *note = msg->buf + bufUsed;

	    /* Ensure to only use valid buffer data as string */
	    if (msg->header.len == DDBufferMsgOffset + sizeof(msg->buf)) {
		msg->buf[sizeof(msg->buf) - 1] = '\0';
	    } else {
		msg->buf[msg->header.len - DDBufferMsgOffset] = '\0';
	    }

	    if (note[strlen(note)-1] == '\n') note[strlen(note)-1] = '\0';
	    if (note[strlen(note)-1] == '\r') note[strlen(note)-1] = '\0';

	    PSI_log(-1, "%s: spawn to node %d failed: \"%s\"\n",
		    __func__, PSC_getID(errMsg->header.sender), note);
	} else {
	    PSI_warn(-1, errMsg->error, "%s: spawn to node %d failed",
		     __func__, PSC_getID(errMsg->header.sender));
	}
	return 0;
    case PSP_CD_WHODIED:
	;
	DDSignalMsg_t *sigMsg = (DDSignalMsg_t *)msg;
	PSI_log(-1, "%s: got signal %d from %s\n", __func__, sigMsg->signal,
		PSC_printTID(sigMsg->header.sender));
	__attribute__((fallthrough));
    case PSP_CD_SENDSTOP:
    case PSP_CD_SENDCONT:
	return -2; /* Ignore answer */
    default:
	PSI_log(-1, "%s: unexpected answer %s from %s\n", __func__,
		PSP_printMsg(msg->header.type),
		PSC_printTID(msg->header.sender));
	return -2; /* Ignore answer */
    }
    return 1;
}

int PSI_sendSpawnReq(PStask_t *task, PSnodes_ID_t *dstnodes, uint32_t max)
{
    PS_SendDB_t msg;
    PStask_ID_t dest = PSC_getTID(dstnodes[0], 0);

    initFragBuffer(&msg, PSP_CD_SPAWNREQUEST, -1);
    setFragDest(&msg, dest);

    uint32_t num = 0;
    while (num < max && dstnodes[num] == dstnodes[0]) num++;

    PSI_log(PSI_LOG_SPAWN, "%s: %d tasks to %d at rank %d\n", __func__, num,
	    dstnodes[0], task->rank);

    addUint32ToMsg(num, &msg);
    if (PSI_protocolVersion(dstnodes[0]) < 345) {
	if (!PStask_addToMsg_old(task, &msg)) return -1;
    } else {
	if (!PStask_addToMsg(task, &msg)) return -1;
    }
    if (!addStringArrayToMsg(strvGetArray(task->argV), &msg)) return -1;
    if (!addStringArrayToMsg(envGetArray(task->env), &msg)) return -1;

    for (uint32_t r = 0; r < num; r++) {
	char **extraEnv = NULL;
	if (extraEnvFunc) extraEnv = extraEnvFunc(task->rank + r, extraEnvInfo);

	if (!addStringArrayToMsg(extraEnv, &msg)) return -1;
    }

    if (sendFragMsg(&msg) == -1) {
	PSI_log(-1, "%s: send to %d failed\n", __func__,  dstnodes[0]);
	return -1;
    }

    return num;
}

/**
 * @brief Create spawn helper task structure
 *
 * Create and prepare a task structure to be passed to @ref doSpawn()
 * in order to actually spawn tasks.
 *
 * The task structure is prepared in a way that the resulting tasks
 * will reside in the working directory @a wDir, are of group type @a
 * taskGroup and belong to the reservation identified by @a resID.
 *
 * The task structure's argument vector will be created from @a argv of
 * size @a argc. These arguments might be prefixed by additional
 * entries depending on the environment variables PSI_USE_VALGRIND and
 * PSI_USE_CALLGRIND.
 *
 * Further members of the task structure will be filled by the
 * corresponding properties of the calling process, like UID, GID,
 * logger TID, etc. including a copy of the environment. This
 * environment will be expanded by the content of @a env.
 *
 * @param wDir Initial working directory of the spawned tasks
 *
 * @param taskGroup Task-group under which the spawned tasks shall be
 * created, e.g. TG_ANY or TG_ADMINTASK; the latter is used for
 * admin-tasks, i.e. unaccounted tasks
 *
 * @param resID ID of the reservation holding resources for spawning;
 * -1 if none
 *
 * @param argc Number of arguments in @a argv
 *
 * @param argv Arguments vector used to spawn the tasks
 *
 * @param strictArgv Flag to prevent "smart" replacement of argv[0]
 *
 * @param env Additional environment for the spawned tasks
 *
 * @return Upon success a prepare task structure is returned or NULL
 * in case of failure
 */
static PStask_t * createSpawnTask(char *wDir, PStask_group_t taskGroup,
				  PSrsrvtn_ID_t resID, int argc, char **argv,
				  bool strictArgv, env_t env)
{
    /* setup task structure to store information of tasks to be spawned */
    PStask_t *task = PStask_new();
    if (!task) {
	PSI_log(-1, "%s: cannot create task structure\n", __func__);
	return NULL;
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

    char *myWD = PSC_getwd(wDir);
    if (!myWD) {
	PSI_warn(-1, errno, "%s: unable to get working directory", __func__);
	goto cleanup;
    }
    task->workingdir = myWD;

    task->argV = strvNew(NULL);
    if (!strvInitialized(task->argV)) {
	PSI_warn(-1, errno, "%s: unable to store argument vector", __func__);
	goto cleanup;
    }

    /* check for valgrind support and whether this is the actual executable: */
    char *valgrind = getenv("PSI_USE_VALGRIND");
    if (valgrind && strcmp(basename(argv[0]), "mpiexec")
	&& strcmp(basename(argv[0]), "kvsprovider")
	&& strcmp(basename(argv[0]), "spawner")
	&& strcmp(argv[0], "valgrind") ) {
	/* add 'valgrind' and its parameters before the executable name: */
	strvAdd(task->argV, "valgrind");
	strvAdd(task->argV, "--quiet");

	if (getenv("PSI_USE_CALLGRIND")) {
	    strvAdd(task->argV, "--tool=callgrind");
	} else {
	    if (!strcmp(valgrind, "1")) {
		strvAdd(task->argV, "--leak-check=no");
	    } else {
		strvAdd(task->argV, "--leak-check=full");
	    }
	}
    }

    /* add the executable name */
    struct stat statbuf;
    if (stat(argv[0], &statbuf) && !strictArgv) {
	char myexec[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", myexec, sizeof(myexec) - 1);
	if (len < 0) {
	    PSI_warn(-1, errno, "%s: readlink", __func__);
	} else {
	    myexec[len]='\0';
	}
	strvAdd(task->argV, myexec);
    } else {
	strvAdd(task->argV, argv[0]);
    }

    /* copy arguments */
    for (int a = 1; a < argc; a++) strvAdd(task->argV, argv[a]);

    task->env = dumpPSIEnv();
    /* add the content of env */
    envMerge(task->env, env, NULL);

    return task;

 cleanup:
    PStask_delete(task);

    return NULL;
}

/**
 * @brief Actually spawn tasks
 *
 * Spawn @a count tasks on the nodes in @a dstNodes. The spawned tasks
 * are created according to the description within the structure @a
 * task. It is advised to create @a task by utilizing the @ref
 * createSpawnTask() function.
 *
 * The first task spawned will get the unique rank @a first, all
 * further tasks will get successive ranks. Only a single service-task
 * is allowed to be spawned per call!
 *
 * Upon return the array @a errors will hold @a count error codes
 * indicating if the corresponding spawn failed and what caused this
 * failure. Alternatively a pointer to the AnswerBucket_t @a bucket
 * might be provided. It will contain all information for a whole
 * series of calls to this function necessary to handle answers
 * including the @a errors array.
 *
 * Both, @a errors and @a bucket are mutually exclusive and the other
 * shall be NULL when called. @a errors will be ignored when @a bucket
 * is given.
 *
 * If the flags @a awaitAllAnswers is set, this function will wait for
 * all outstanding answers before returning, unless a fatal error
 * occurred before. Otherwise this function will handle all answers
 * ready for receive but return as soon as all spawn requests are
 * sent.
 *
 * @param count Number of tasks to spawn
 *
 * @param first Rank of the first tasks spawn
 *
 * @param dstNodes Array of node IDs acting as tasks destination nodes
 *
 * @param task Task structure acting as a skeleton for the tasks to spawn
 *
 * @param errors Array holding error codes upon return
 *
 * @param bucket Bucket of information used to handle answers
 *
 * @param awaitAllAnswers Flag waiting for all outstanding answers
 * before return
 *
 * @return Upon success, the number of received ansers is returned.
 * Usually this is @a count if @a awaitAllAnswers is set or the number
 * of answers received during this call. In case of detecting an error
 * -1 is returned.
 */
static int doSpawn(int count, int first, PSnodes_ID_t *dstNodes, PStask_t *task,
		   int *errors, AnswerBucket_t *bucket, bool awaitAllAnswers)
{
    AnswerBucket_t myBucket = {
	.first = first,
	.num = 0,
	.expected = 0,
	.errors = errors };

    if (!task) {
	PSI_log(-1, "%s: no task\n", __func__);
	return -1;
    }

    if ((task->group == TG_SERVICE || task->group == TG_SERVICE_SIG
	|| task->group == TG_KVS) && count != 1) {
	PSI_log(-1, "%s: spawn %d SERVICE tasks not allowed\n", __func__, count);
	return -1;
    }

    if ((!bucket || !bucket->errors) && !errors) {
	PSI_log(-1, "%s: unable to report errors\n", __func__);
	return -1;
    }

    if (!initSerial(0, PSI_sendMsg)) {
	PSI_log(-1, "%s: initSerial() failed\n", __func__);
	return -1;
    }

    AnswerBucket_t *thisBucket = bucket;
    if (!thisBucket) {
	thisBucket = &myBucket;
	for (int i = 0; i < count; i++) myBucket.errors[i] = 0;
    }

    int ret = 0;
    for (int i = 0; i < count; i++) {
	if (i && dstNodes[i] == dstNodes[i-1]) continue; // do not check twice
	/* check if dstnode is ok */
	if (!PSC_validNode(dstNodes[i])) {
	    thisBucket->errors[first - thisBucket->first + i] = ENETUNREACH;
	    return -1;
	}
    }

    /* send actual requests */
    bool error = false;
    for (int i = 0; i < count && !error;) {
	task->rank = first + i;
	int sent = PSI_sendSpawnReq(task, &dstNodes[i], count - i);
	if (sent < 0) return -1;

	i += sent;
	thisBucket->num += sent;
	thisBucket->expected += sent;

	while (PSI_availMsg() > 0 && thisBucket->expected) {
	    DDBufferMsg_t msg;
	    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg)) < 0) {
		PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
		return -1;
	    }
	    int r = handleAnswer(&msg, thisBucket);
	    switch (r) {
	    case -2:  /* just ignore */
		break;
	    case -1:  /* fatal error */
		return -1;
	    case 0:  /* spawn failed */
		error = true;
		__attribute__((fallthrough));
	    case 1:  /* spawn success */
		ret++;
		break;
	    default:
		PSI_log(-1, "%s: handleAnswer() returns %d\n", __func__, r);
	    }
	}
    }/* for all new tasks */

    if (!awaitAllAnswers) return error ? -1 : ret;

    /* collect expected answers */
    while (thisBucket->expected > 0) {
	DDBufferMsg_t msg;
	if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg)) < 0) {
	    PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
	    return -1;
	}
	int r = handleAnswer(&msg, thisBucket);
	switch (r) {
	case -2:  /* just ignore */
	    break;
	case -1:  /* fatal error */
	    return -1;
	case 0:   /* spawn failed */
	    error = true;
	    __attribute__((fallthrough));
	case 1:   /* spawn success */
	    ret++;
	    break;
	default:
	    PSI_log(-1, "%s: handleAnswer() returns %d\n", __func__, r);
	}
    }

    return error ? -1 : ret;
}

int PSI_spawn(int count, char *wDir, int argc, char **argv, int *errors)
{
    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, count);
    if (!errors) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    if (count <= 0) return 0;

    PStask_t *task = createSpawnTask(wDir, TG_ANY, -1 /* resID */,
				     argc, argv, false, NULL);
    if (!task) {
	PSI_log(-1, "%s: unable to create helper task\n", __func__);
	return -1;
    }

    PSnodes_ID_t *nodes = malloc(sizeof(*nodes) * NODES_CHUNK);
    if (!nodes) {
	*errors = ENOMEM;
	PStask_delete(task);
	return -1;
    }

    int total = 0;
    while (count > 0) {
	int chunk = (count > NODES_CHUNK) ? NODES_CHUNK : count;
	int rank = PSI_getNodes(chunk, 0/*hwType*/, 1/*tpp*/, 0/*options*/, nodes);
	if (rank < 0) {
	    errors[total] = ENXIO;
	    total = -1;
	    break;
	}

	PSI_log(PSI_LOG_SPAWN, "%s: spawn to:", __func__);
	for (int i = 0; i < chunk; i++) PSI_log(PSI_LOG_SPAWN, " %2d", nodes[i]);
	PSI_log(PSI_LOG_SPAWN, "\n");
	PSI_log(PSI_LOG_SPAWN, "%s: first rank: %d\n", __func__, rank);

	int ret = doSpawn(chunk, rank, nodes, task, errors + total, NULL, true);
	if (ret != chunk) {
	    total = -1;
	    break;
	}

	count -= chunk;
	total += chunk;
    }
    free(nodes);
    PStask_delete(task);
    return total;
}

int PSI_spawnRsrvtn(int count, PSrsrvtn_ID_t resID, char *wDir,
		    int argc, char **argv, bool strictArgv, env_t env,
		    int *errors)
{
    PSI_log(PSI_LOG_VERB, "%s(%d, %#x)\n", __func__, count, resID);
    if (!errors) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return -1;
    }

    if (count <= 0) return 0;

    PStask_t *task = createSpawnTask(wDir, TG_ANY, resID, argc, argv,
				     strictArgv, env);
    if (!task) {
	PSI_log(-1, "%s: unable to create helper task\n", __func__);
	return -1;
    }

    PSnodes_ID_t *nodes = malloc(sizeof(*nodes) * NODES_CHUNK);
    if (!nodes) {
	*errors = ENOMEM;
	PStask_delete(task);
	return -1;
    }

    AnswerBucket_t bucket = {
	.first = 0,
	.num = 0,
	.expected = 0,
	.errors = errors };

    bool error = false; /* flag fatal error */
    while (count > 0 && !error) {
	int chunk = (count > NODES_CHUNK) ? NODES_CHUNK : count;
	if (PSI_requestSlots(chunk, resID) < 0) {
	    errors[bucket.num] = ENXIO;
	    error = true;
	    break;
	}

	/* wait to receive nodes and first rank while handling answers */
	int rank = -1;
	while (rank < 0 && !error) {
	    DDBufferMsg_t msg;
	    if (PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg)) < 0) {
		PSI_warn(-1, errno, "%s: PSI_recvMsg", __func__);
		error = true;
		break;
	    }
	    switch (msg.header.type) {
	    case PSP_CD_SLOTSRES:
		rank = PSI_extractSlots(&msg, chunk, nodes);
		if (rank < 0) {
		    errors[bucket.num] = ENXIO;
		    error = true;
		}
		break;
	    default:
		;
		int r = handleAnswer(&msg, &bucket);
		switch (r) {
		case -2:
		    /* just ignore */
		    break;
		case -1:  /* fatal error */
		case 0:   /* spawn failed */
		    error = true;
		    break;
		case 1:   /* spawn success, just continue */
		    break;
		default:
		    PSI_log(-1, "%s: handleAnswer() returns %d\n", __func__, r);
		}
	    }
	}
	if (error) break; /* propagate error to next level */

	if (!bucket.num) {
	    bucket.first = rank;
	}
	if (bucket.first + bucket.num != rank) {
	    PSI_log(-1, "%s: unexpected gap in ranks (%d/%d)\n", __func__,
		    bucket.first + bucket.num, rank);
	    error = true;
	    break;
	}

	PSI_log(PSI_LOG_SPAWN, "%s: spawn to:", __func__);
	for (int i = 0; i < chunk; i++) PSI_log(PSI_LOG_SPAWN, " %2d", nodes[i]);
	PSI_log(PSI_LOG_SPAWN, "\n");
	PSI_log(PSI_LOG_SPAWN, "%s: first rank: %d\n", __func__, rank);

	int ret = doSpawn(chunk, rank, nodes, task,
			  NULL, &bucket, chunk == count);
	if (ret < 0) {
	    error = true;
	    break;
	}

	count -= chunk;
    }
    free(nodes);
    PStask_delete(task);
    return error ? -1 : bucket.num;
}

bool PSI_spawnAdmin(PSnodes_ID_t node, char *wDir, int argc, char **argv,
		    bool strictArgv, unsigned int rank, int *error)
{
    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, node);
    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return false;
    }

    PStask_t *task = createSpawnTask(wDir, TG_ADMINTASK, -1 /* resID */,
				     argc, argv, strictArgv, NULL);
    if (!task) {
	PSI_log(-1, "%s: unable to create helper task\n", __func__);
	return false;
    }

    if (node == -1) node = PSC_getMyID();
    int ret = doSpawn(1, rank, &node, task, error, NULL, true);
    PStask_delete(task);
    return ret == 1;
}

bool PSI_spawnService(PSnodes_ID_t node, PStask_group_t taskGroup, char *wDir,
		      int argc, char **argv, int *error, int rank)
{
    PSI_log(PSI_LOG_VERB, "%s(%d)\n", __func__, node);
    if (!error) {
	PSI_log(-1, "%s: unable to reports errors\n", __func__);
	return false;
    }

    if (rank >= -1) {
	PSI_log(-1, "%s: unexpected service rank %d\n", __func__, rank);
	return false;
    }

    PStask_t *task = createSpawnTask(wDir, taskGroup, -1, argc, argv, false, NULL);
    if (!task) {
	PSI_log(-1, "%s: unable to create helper task\n", __func__);
	return false;
    }

    if (node == -1) node = PSC_getMyID();
    int ret = doSpawn(1, rank, &node, task, error, NULL, true);
    PStask_delete(task);
    return ret == 1;
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
