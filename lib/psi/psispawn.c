/*
 * ParaStation
 *
 * Copyright (C) 1999-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
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
    PSI_fdbg(PSI_LOG_VERB, "\n");

    int cnt = 0;
    for (;;) {
	char env[sizeof(ENV_NODE_RARG) + 20];
	snprintf(env, sizeof(env), ENV_NODE_RARG, cnt);
	if (getenv(env)) {
	    cnt++;
	} else {
	    break;
	}
    }

    if (cnt) {
	int new_argc = cnt + Argc;
	char **new_argv = malloc(sizeof(char*)*(new_argc + 1));
	new_argv[new_argc]=NULL;

	for (int i = 0; i < cnt; i++) {
	    char env[sizeof(ENV_NODE_RARG) + 20];
	    snprintf(env, sizeof(env), ENV_NODE_RARG, i);
	    new_argv[i] = getenv(env);
	    /* Propagate the environment */
	    setPSIEnv(env, new_argv[i]);
	}
	for (int i = 0; i < Argc; i++) {
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
    PSI_fdbg(PSI_LOG_SPAWN, "%p\n", func);

    extraEnvFunc = func;
    extraEnvInfo = info;
}

int PSI_sendSpawnReq(PStask_t *task, PSnodes_ID_t *dstnodes, uint32_t max)
{
    PS_SendDB_t msg;
    PStask_ID_t dest = PSC_getTID(dstnodes[0], 0);

    initFragBuffer(&msg, PSP_CD_SPAWNREQUEST, -1);
    setFragDest(&msg, dest);

    uint32_t num = 0;
    while (num < max && dstnodes[num] == dstnodes[0]) num++;

    PSI_fdbg(PSI_LOG_SPAWN, "%d tasks to %d at rank %d\n", num,
	    dstnodes[0], task->rank);

    addUint32ToMsg(num, &msg);
    if (!PStask_addToMsg(task, &msg)) return -1;
    if (!addStringArrayToMsg(strvGetArray(task->argV), &msg)) return -1;
    if (!addStringArrayToMsg(envGetArray(task->env), &msg)) return -1;

    for (uint32_t r = 0; r < num; r++) {
	char **extraEnv = NULL;
	if (extraEnvFunc) extraEnv = extraEnvFunc(task->rank + r, extraEnvInfo);

	if (!addStringArrayToMsg(extraEnv, &msg)) return -1;
    }

    if (sendFragMsg(&msg) == -1) {
	PSI_flog("send to %d failed\n", dstnodes[0]);
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
	PSI_flog("cannot create task structure\n");
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
	PSI_fwarn(errno, "unable to determine logger's TID");
	goto cleanup;
    }
    task->resID = resID;

    char *myWD = PSC_getwd(wDir);
    if (!myWD) {
	PSI_fwarn(errno, "unable to get working directory");
	goto cleanup;
    }
    task->workingdir = myWD;

    task->argV = strvNew(NULL);
    if (!strvInitialized(task->argV)) {
	PSI_fwarn(errno, "unable to store argument vector");
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
	if (len == -1) {
	    PSI_fwarn(errno, "readlink");
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

/** Structure containing all information to handle an answer */
typedef struct {
    int first;     /**< Rank offset */
    int num;       /**< Total number of spawn requests sent */
    int expected;  /**< Count outstanding answers to spawn requests */
    bool errFlag;  /**< general error flag */
    int *errors;   /**< individual error codes for each rank */
} AnswerBucket_t;

static bool msg_SPAWNSUCCESS(DDBufferMsg_t *msg, const char *caller, void *info)
{
    AnswerBucket_t *bucket = info;
    if (!bucket || !bucket->errors) return false; // return from PSI_recvMsg()

    DDErrorMsg_t *errMsg = (DDErrorMsg_t *)msg;
    int localRank = errMsg->request - bucket->first;

    if (localRank < 0 || localRank >= bucket->num) {
	PSI_flog("%s: %s from illegal rank %ld at node %d\n", caller,
		 PSP_printMsg(errMsg->header.type), errMsg->request,
		 PSC_getID(errMsg->header.sender));
	return true;   // ignore answer and continue PSI_recvMsg()
    }
    bucket->expected--;

    return true;  // continue PSI_recvMsg()
}

static bool msg_SPAWNFAILED(DDBufferMsg_t *msg, const char *caller, void *info)
{
    AnswerBucket_t *bucket = info;
    if (!bucket || !bucket->errors) return false; // return from PSI_recvMsg()

    DDErrorMsg_t *errMsg = (DDErrorMsg_t *)msg;
    int localRank = errMsg->request - bucket->first;

    if (localRank < 0 || localRank >= bucket->num) {
	PSI_log("%s: %s from illegal rank %ld at node %d\n", caller,
		    PSP_printMsg(errMsg->header.type), errMsg->request,
		    PSC_getID(errMsg->header.sender));
	return true;   // ignore answer and continue PSI_recvMsg()
    }
    bucket->expected--;
    bucket->errFlag = true;
    bucket->errors[localRank] = errMsg->error;

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

	PSI_log("%s: spawn to node %d failed: \"%s\"\n", caller,
		PSC_getID(errMsg->header.sender), note);
    } else {
	PSI_warn(errMsg->error, "%s: spawn to node %d failed", caller,
		 PSC_getID(errMsg->header.sender));
    }

    return bucket->expected != 0;  // continue PSI_recvMsg() unless all received
}

void clrRecvHandlers(void)
{
    PSI_clrRecvHandler(PSP_CD_SPAWNSUCCESS, msg_SPAWNSUCCESS);
    PSI_clrRecvHandler(PSP_CD_SPAWNFAILED, msg_SPAWNFAILED);
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
 * @return Upon success true is returned; or false in case of
 * detecting an error
 */
static bool doSpawn(int count, int first, PSnodes_ID_t *dstNodes, PStask_t *task,
		    int *errors, AnswerBucket_t *bucket, bool awaitAllAnswers)
{
    AnswerBucket_t myBucket = {
	.first = first,
	.num = 0,
	.expected = 0,
	.errFlag = false,
	.errors = errors };

    if (!task) {
	PSI_flog("no task\n");
	return false;
    }

    if ((task->group == TG_SERVICE || task->group == TG_SERVICE_SIG
	|| task->group == TG_KVS) && count != 1) {
	PSI_flog("spawn %d SERVICE tasks not allowed\n", count);
	return false;
    }

    if ((!bucket || !bucket->errors) && !errors) {
	PSI_flog("unable to report errors\n");
	return false;
    }

    AnswerBucket_t *thisBucket = bucket;
    if (!thisBucket) {
	thisBucket = &myBucket;
	for (int i = 0; i < count; i++) myBucket.errors[i] = 0;
    }

    for (int i = 0; i < count; i++) {
	if (i && dstNodes[i] == dstNodes[i-1]) continue; // do not check twice
	/* check if dstnode is ok */
	if (!PSC_validNode(dstNodes[i])) {
	    thisBucket->errors[first - thisBucket->first + i] = ENETUNREACH;
	    return false;
	}
    }

    if (!thisBucket->num) {
	PSI_addRecvHandler(PSP_CD_SPAWNSUCCESS, msg_SPAWNSUCCESS, thisBucket);
	PSI_addRecvHandler(PSP_CD_SPAWNFAILED, msg_SPAWNFAILED, thisBucket);
    }

    /* send actual requests */
    for (int i = 0; i < count && !thisBucket->errFlag;) {
	task->rank = first + i;
	int sent = PSI_sendSpawnReq(task, &dstNodes[i], count - i);
	if (sent == -1) goto end;

	i += sent;
	thisBucket->num += sent;
	thisBucket->expected += sent;

	if (i < count) {
	    // before the last round, receive messages if any are available
	    DDBufferMsg_t msg;
	    ssize_t ret = PSI_tryRecvMsg(&msg, sizeof(msg), 0, true);
	    if (ret == -1 && errno != ENODATA && errno != ENOMSG) {
		PSI_fwarn(errno, "PSI_recvMsg");
		goto end;
	    }
	}
    }/* for all new tasks */

    if (!awaitAllAnswers) return !thisBucket->errFlag;

    /* collect expected answers */
    while (thisBucket->expected > 0) {
	DDBufferMsg_t msg;
	ssize_t ret = PSI_recvMsg(&msg, sizeof(msg), PSP_CD_SPAWNSUCCESS, true);
	if (ret == -1 && errno != ENOMSG) {
	    PSI_fwarn(errno, "PSI_recvMsg");
	    goto end;
	}
	if (ret != -1) msg_SPAWNSUCCESS(&msg, __func__, thisBucket);
    }

end:
    clrRecvHandlers();

    return !thisBucket->errFlag;
}

int PSI_spawnRsrvtn(int count, PSrsrvtn_ID_t resID, char *wDir,
		    strv_t argV, bool strictArgv, env_t env, int *errors)
{
    PSI_fdbg(PSI_LOG_VERB, "%d %#x\n", count, resID);
    if (!errors) {
	PSI_flog("unable to reports errors\n");
	return -1;
    }

    if (count <= 0) return 0;

    PStask_t *task = createSpawnTask(wDir, TG_ANY, resID, strvSize(argV),
				     strvGetArray(argV), strictArgv, env);
    if (!task) {
	PSI_flog("unable to create helper task\n");
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
	.errFlag = false,
	.errors = errors };

    while (count > 0 && !bucket.errFlag) {
	int chunk = (count > NODES_CHUNK) ? NODES_CHUNK : count;
	if (PSI_requestSlots(chunk, resID) < 0) {
	    errors[bucket.num] = ENXIO;
	    bucket.errFlag = true;
	    break;
	}

	/* wait to receive nodes and first rank while handling answers */
	int rank = -1;
	while (rank < 0 && !bucket.errFlag) {
	    DDBufferMsg_t msg;
	    ssize_t ret = PSI_recvMsg(&msg, sizeof(msg), PSP_CD_SLOTSRES, true);
	    if (ret == -1 && errno != ENOMSG) {
		PSI_fwarn(errno, "PSI_recvMsg");
		bucket.errFlag = true;
		break;
	    }
	    if (ret != -1) {
		rank = PSI_extractSlots(&msg, chunk, nodes);
		if (rank < 0) {
		    errors[bucket.num] = ENXIO;
		    bucket.errFlag = true;
		}
	    }
	}
	if (bucket.errFlag) break; /* propagate error to next level */

	if (!bucket.num) bucket.first = rank;
	if (bucket.first + bucket.num != rank) {
	    PSI_flog("unexpected gap in ranks (%d/%d)\n",
		     bucket.first + bucket.num, rank);
	    bucket.errFlag = true;
	    break;
	}

	PSI_fdbg(PSI_LOG_SPAWN, "spawn to:");
	for (int i = 0; i < chunk; i++) PSI_dbg(PSI_LOG_SPAWN, " %2d", nodes[i]);
	PSI_dbg(PSI_LOG_SPAWN, "\n");
	PSI_fdbg(PSI_LOG_SPAWN, "first rank: %d\n", rank);

	if (!doSpawn(chunk, rank, nodes, task, NULL, &bucket, chunk == count)) {
	    // doSpawn returns !bucket.errFlag
	    break;
	}

	count -= chunk;
    }
    free(nodes);
    PStask_delete(task);
    clrRecvHandlers();
    return bucket.errFlag ? -1 : bucket.num;
}

bool PSI_spawnAdmin(PSnodes_ID_t node, char *wDir, int argc, char **argv,
		    bool strictArgv, unsigned int rank, int *error)
{
    PSI_fdbg(PSI_LOG_VERB, "node %d\n", node);
    if (!error) {
	PSI_flog("unable to reports errors\n");
	return false;
    }

    PStask_t *task = createSpawnTask(wDir, TG_ADMINTASK, -1 /* resID */,
				     argc, argv, strictArgv, NULL);
    if (!task) {
	PSI_flog("unable to create helper task\n");
	return false;
    }

    if (node == -1) node = PSC_getMyID();
    bool ret = doSpawn(1, rank, &node, task, error, NULL, true);
    PStask_delete(task);
    clrRecvHandlers();
    return ret;
}

bool PSI_spawnService(PSnodes_ID_t node, PStask_group_t taskGroup, char *wDir,
		      int argc, char **argv, int *error, int rank)
{
    PSI_fdbg(PSI_LOG_VERB, "node %d\n", node);
    if (!error) {
	PSI_flog("unable to reports errors\n");
	return false;
    }

    if (rank >= -1) {
	PSI_flog("unexpected service rank %d\n", rank);
	return false;
    }

    PStask_t *task = createSpawnTask(wDir, taskGroup, -1, argc, argv, false, NULL);
    if (!task) {
	PSI_flog("unable to create helper task\n");
	return false;
    }

    if (node == -1) node = PSC_getMyID();
    bool ret = doSpawn(1, rank, &node, task, error, NULL, true);
    PStask_delete(task);
    clrRecvHandlers();
    return ret;
}

int PSI_kill(PStask_ID_t tid, short signal, bool async)
{
    DDSignalMsg_t request = {
	.header = {
	    .type = PSP_CD_SIGNAL,
	    .dest = tid,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(request) },
	.signal = signal,
	.param = getuid(),
	.pervasive = 0,
	.answer = 1 };

    PSI_fdbg(PSI_LOG_VERB, "%s  %d\n", PSC_printTID(tid), signal);

    if (PSI_sendMsg(&request) == -1) {
	PSI_fwarn(errno, "PSI_sendMsg");
	return -1;
    }

    if (async) return 0;

    DDBufferMsg_t msg;
    ssize_t ret = PSI_recvMsg(&msg, sizeof(msg), PSP_CD_SIGRES, true);
    if (ret == -1 && errno != ENOMSG) {
	PSI_fwarn(errno, "PSI_recvMsg");
	return -1;
    }

    if (msg.header.type != PSP_CD_SIGRES) return -2;

    DDErrorMsg_t *eMsg = (DDErrorMsg_t *)&msg;
    if (eMsg->request != tid) {
	PSI_flog("answer from wrong task (%s/", PSC_printTID(eMsg->request));
	PSI_log("%s)\n", PSC_printTID(tid));
	return -2;
    }

    return eMsg->error;
}
