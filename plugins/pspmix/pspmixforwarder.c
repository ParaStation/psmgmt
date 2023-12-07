/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Implementation of all functions running in the forwarders
 *
 * Three jobs are done in the forwarders:
 * - Before forking the client, wait for the environment sent by the
 *   PMIx server and set it for the client.
 * - Manage the initialization state (in PMIx sense) of the client to correctly
 *   do the client release and thus failure handling.
 *   (This task is historically done in the psid forwarder and actually means
 *    an unnecessary indirection (PMIx server <-> PSID Forwarder <-> PSID).
 *    @todo Think about getting rid of that.)
 * - Handling spawn requests initiated by the client of the forwarder by a call
 *   of PMIx_Spawn().
 */
#include "pspmixforwarder.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pmix.h>
#include <pmix_common.h>

#include "pscio.h"
#include "pscommon.h"
#include "psenv.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"
#include "pstask.h"
#include "psispawn.h"

#include "psidcomm.h"
#include "psidforwarder.h"
#include "psidhook.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "pluginstrv.h"

#include "pspmixconfig.h"
#include "pspmixcommon.h"
#include "pspmixlog.h"
#include "pspmixtypes.h"
#include "pspmixdaemon.h"

/* psid rank of this forwarder and child */
static int32_t rank = -1;

/* PMIx process identifier for the child */
static pmix_proc_t myproc;

/* task structure of this forwarders child */
static PStask_t *childTask = NULL;

/* PMIx initialization status of the child */
PSIDhook_ClntRls_t pmixStatus = IDLE;

typedef struct {
    PStask_ID_t pmixServer;
    uint16_t spawnID;
} SpawnReqData_t;


/* convenient logging functions */
/* These are only meaningful between the client init and finalize messages
 * so iff pmixStatus == CONNECTED, since myproc needs to be valid */
#if defined __GNUC__ && __GNUC__ < 8
#define pdbg(mask, format, ...)						     \
    mdbg(mask, "%s(%s:%d): " format, __func__, myproc.nspace, myproc.rank,   \
	 ##__VA_ARGS__)
#else
#define pdbg(mask, format, ...)						     \
    mdbg(mask, "%s(%s:%d): " format, __func__, myproc.nspace, myproc.rank    \
	  __VA_OPT__(,) __VA_ARGS__)
#endif

#define plog(...) pdbg(-1, __VA_ARGS__)

/* These can be used always */
#if defined __GNUC__ && __GNUC__ < 8
#define rdbg(mask, format, ...)						    \
    mdbg(mask, "%s(r%d): " format, __func__, rank, ##__VA_ARGS__)
#define rwarn(eno, format, ...)						    \
    mwarn(eno, "%s(r%d): " format, __func__, rank, ##__VA_ARGS__)
#else
#define rdbg(mask, format, ...)						    \
    mdbg(mask, "%s(r%d): " format, __func__, rank __VA_OPT__(,) __VA_ARGS__)
#define rwarn(eno, format, ...)						    \
    mwarn(eno, "%s(r%d): " format, __func__, rank __VA_OPT__(,) __VA_ARGS__)
#endif

#define rlog(...) rdbg(-1, __VA_ARGS__)

/* ****************************************************** *
 *                Spawn handling functions                *
 * ****************************************************** */

/* spawn functions */
static fillerFunc_t *fillTaskFunction = NULL;

/** Generic static buffer */
static char buffer[1024];

/** SpawnRequest to be handled when logger returns service rank */
static SpawnRequest_t *pendSpawn = NULL;

/**
 * @brief Spawn one or more processes
 *
 * We first need the next rank for the new service process to
 * start. Only the logger knows that. So we ask it and buffer
 * the spawn request to wait for an answer.
 *
 * @see handleServiceInfo()
 *
 * @param req Spawn request data structure (takes ownership)
 *
 * @return Returns true on success and false on error
 */
static bool doSpawn(SpawnRequest_t *req)
{
    if (pendSpawn) {
	plog("another spawn is pending\n");
	return false;
    }

    pendSpawn = req;

    plog("trying to spawn job with %d apps\n", pendSpawn->num);

    /* get next service rank from logger */
    if (PSLog_write(childTask->loggertid, SERV_TID, NULL, 0) < 0) {
	plog("writing to logger failed\n");
	return false;
    }

    return true;
}

#if 0 /* currently not used with PMIx */
/**
 * @brief Prepare preput keys and values to pass by environment
 *
 * Generates an array of strings representing the preput key-value-pairs in the
 * format "__PMI_<KEY>=<VALUE>" and one variable "__PMI_preput_num=<COUNT>"
 *
 * @param preputc Number of key value pairs
 *
 * @param preputv Array of key value pairs
 *
 * @param envv Where to store the pointer to the array of definitions
 *
 * @return No return value
 */
static void addPreputToEnv(int preputc, KVP_t *preputv, strv_t *env)
{

    snprintf(buffer, sizeof(buffer), "__PMI_preput_num=%i", preputc);
    strvAdd(env, ustrdup(buffer));

    for (int i = 0; i < preputc; i++) {
	int esize;
	char *tmpstr;

	snprintf(buffer, sizeof(buffer), "preput_key_%i", i);
	esize = 6 + strlen(buffer) + 1 + strlen(preputv[i].key) + 1;
	tmpstr = umalloc(esize);
	snprintf(tmpstr, esize, "__PMI_%s=%s", buffer, preputv[i].key);
	strvAdd(env, tmpstr);

	snprintf(buffer, sizeof(buffer), "preput_val_%i", i);
	esize = 6 + strlen(buffer) + 1 + strlen(preputv[i].value) + 1;
	tmpstr = umalloc(esize);
	snprintf(tmpstr, esize, "__PMI_%s=%s", buffer, preputv[i].value);
	strvAdd(env, tmpstr);
    }
}
#endif

/**
 *  fills the passed task structure to spawn processes using mpiexec
 *
 *  @param req spawn request
 *
 *  @param usize universe size
 *
 *  @param task task structure to adjust
 *
 *  @return 1 on success, 0 on error (currently unused)
 */
static int fillWithMpiexec(SpawnRequest_t *req, int usize, PStask_t *task)
{
    bool noParricide = false;

    SingleSpawn_t *spawn = &(req->spawns[0]);

#if 0 /* currently not used with PMIx */
    /* put preput key-value-pairs into environment
     *
     * We will transport this kv-pairs using the spawn environment. When
     * our children are starting the pmi_init() call will add them to their
     * local KVS.
     *
     * Only the values of the first single spawn are used. */
    strv_t env;
    strvInit(&env, task->environ, task->envSize);
    addPreputToEnv(spawn->preputc, spawn->preputv, &env);

    ufree(task->environ);
    task->environ = env.strings;
    task->envSize = env.count;
    strvSteal(&env, true);
#endif

    /* build arguments:
     * mpiexec -u <UNIVERSE_SIZE> -np <NP> -d <WDIR> -p <PATH> \
     *  --nodetype=<NODETYPE> --tpp=<TPP> <BINARY> ... */
    strv_t args;
    strvInit(&args, NULL, 0);

    /* @todo change to not need to start a kvsprovider any longer */
    char *tmpStr = getenv("__PSI_MPIEXEC_KVSPROVIDER");
    if (tmpStr) {
	strvAdd(&args, ustrdup(tmpStr));
    } else {
	strvAdd(&args, ustrdup(PKGLIBEXECDIR "/kvsprovider"));
    }
    strvAdd(&args, ustrdup("-u"));

    snprintf(buffer, sizeof(buffer), "%d", usize);
    strvAdd(&args, ustrdup(buffer));

    for (int i = 0; i < req->num; i++) {

	spawn = &(req->spawns[i]);

	/* set the number of processes to spawn */
	strvAdd(&args, ustrdup("-np"));
	snprintf(buffer, sizeof(buffer), "%d", spawn->np);
	strvAdd(&args, ustrdup(buffer));

	/* extract info values and keys
	 *
	 * These info variables are implementation dependend and can
	 * be used for e.g. process placement. All unsupported values
	 * will be silently ignored.
	 *
	 * ParaStation pspmix supports:
	 *
	 *  - wdir: The working directory of the spawned processes
	 *  - hosts: List of hosts to use
	 *  - hostfile: File containing the list of hosts to use
	 */
	for (int j = 0; j < spawn->infoc; j++) {
	    KVP_t *info = &(spawn->infov[j]);

	    if (!strcmp(info->key, "wdir")) {
		strvAdd(&args, ustrdup("-d"));
		strvAdd(&args, ustrdup(info->value));
	    }
	    if (!strcmp(info->key, "hosts")) {
		strvAdd(&args, ustrdup("-H"));
		char *val = ustrdup(info->value);
		/* replace all colons with whitespaces */
		for (char *p = val; (p = strchr(p, ',')) != NULL; *p = ' ');
		strvAdd(&args, val);
	    }
	    if (!strcmp(info->key, "hostfile")) {
		strvAdd(&args, ustrdup("-f"));
		strvAdd(&args, ustrdup(info->value));
	    }
	}

	/* add binary and argument from spawn request */
	for (int j = 0; j < spawn->argc; j++) {
	    strvAdd(&args, ustrdup(spawn->argv[j]));
	}

	/* add separating colon */
	if (i < req->num - 1) {
	    strvAdd(&args, ustrdup(":"));
	}
    }

    task->argv = args.strings;
    task->argc = args.count;
    strvSteal(&args, true);

    task->noParricide = noParricide;

    return 1;
}

/**
 * @brief Filter spawner environment for the spawnee
 *
 * Filter out some of the spawners environment variables
 * when copying the task for the spawnees.
 *
 * @param envent  one entry of the task environment in "k=v" notation
 *
 * @return Returns true to include and false to exclude @a envent
 */
static bool spawnEnvFilter(const char *envent)
{
    /* @todo just copied from pspmi, modify */

    /* skip troublesome old env vars */
    if (!strncmp(envent, "__KVS_PROVIDER_TID=", 19)
	    || !strncmp(envent, "PMI_ENABLE_SOCKP=", 17)
	    || !strncmp(envent, "PMI_RANK=", 9)
	    || !strncmp(envent, "PMI_PORT=", 9)
	    || !strncmp(envent, "PMI_FD=", 7)
	    || !strncmp(envent, "PMI_KVS_TMP=", 12)
	    || !strncmp(envent, "OMP_NUM_THREADS=", 16)) {
	return false;
    }

#if 0
    if (path && !strncmp(cur, "PATH", 4)) {
	setPath(cur, path, &task->environ[i++]);
	continue;
    }
#endif

    return true;
}

/**
 * @brief Actually try to execute the spawn
 *
 * This function is called once we received a service rank to use from the
 * job's logger. It triggers the actual spawn.
 *
 * It uses @a fillTaskFunction to setup a command, that will lead to a new
 * job containing the given applications. Usually this will call @a mpiexec
 * directly or use a different resource manager command like Slurm's @a srun
 * which will later trigger @a psslurm to execute @a mpiexec.
 *
 * @param req           spawn request to execute
 * @param serviceRank   service rank to use
 *
 * @return True on success, false on error
 */
static bool tryPMIxSpawn(SpawnRequest_t *req, int serviceRank)
{
    int usize = 1;
    char *str = getenv("PMI_UNIVERSE_SIZE");
    if (str) {
	usize = atoi(str);
    }

    bool debug = false;
    str = getenv("PMIX_DEBUG");
    if (str && atoi(str) > 0) {
	debug = true;
    }

    PStask_t *task = initSpawnTask(childTask, spawnEnvFilter);
    if (!task) {
	mlog("%s: cannot create a new task\n", __func__);
	return false;
    }

    task->rank = serviceRank - 1;

    /* fill the command of the task */
    int rc = fillTaskFunction(req, usize, task);

    if (rc == -1) {
	/* function to fill the spawn task tells us not to be responsible */
	mlog("%s(r%i): Falling back to default PMIx fill spawn function.\n",
	     __func__, rank);
	rc = fillWithMpiexec(req, usize, task);
    }

    if (!rc) {
	elog("Error with spawning processes.\n");
	mlog("%s(r%i): Error in PMIx fill spawn function.\n", __func__, rank);
	PStask_delete(task);
	return false;
    }

    /* add additional env vars */
    strv_t env;
    strvInit(&env, task->environ, task->envSize);

    strvAdd(&env, ustrdup("PMIX_SPAWNED=1"));

    /* PMI_SIZE should be set my mpiexec @todo right? */
#if 0
    /* calc totalProcs */
    int totalProcs = 0;
    for (int i = 0; i < req->num; i++) {
	totalProcs += req->spawns[i].np;
    }

    snprintf(buffer, sizeof(buffer), "PMIX_SIZE=%d", totalProcs);
    strvAdd(&env, ustrdup(buffer));
    if(debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);
#endif

    ufree(task->environ);
    task->environ = env.strings;
    task->envSize = env.count;
    strvSteal(&env, true);

    if (debug) {
	elog("%s(r%i): Executing '", __func__, rank);
	for (uint32_t j = 0; j < task->argc; j++) elog(" %s", task->argv[j]);
	elog("'\n");
    }
    mlog("%s(r%i): Executing '", __func__, rank);
    for (uint32_t j = 0; j < task->argc; j++) mlog(" %s", task->argv[j]);
    mlog("'\n");

    /* spawn task */
    bool ret = PSI_sendSpawnMsg(task, false, PSC_getMyID(), sendDaemonMsg);

    PStask_delete(task);

    return ret;
}

/**
 * @brief Send message of type PSPMIX_CLIENT_SPAWN_RES back to the PMIx server
 *
 * @param targetTID   TID of the PMIx server
 * @param result      status: 1 means success, 0 means fail
 * @param spawnID     ID of the spawn transmitted by the PMIx server with the
 *                    spawn request
 *
 * @return Returns true on success, false on error
 */
static bool sendSpawnResp(PStask_ID_t targetTID, uint8_t result, uint16_t spawnID)
{
    rdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM,
	 "(targetTID %s spawnid %hu result %d)\n",
	 PSC_printTID(targetTID), spawnID, result);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_CLIENT_SPAWN_RES);
    setFragDest(&msg, targetTID);

    addUint16ToMsg(spawnID, &msg);
    addUint8ToMsg(result, &msg);

    if (sendFragMsg(&msg) < 0) {
	plog("Sending PSPMIX_CLIENT_SPAWN_RES (spawnID %hu result %d)"
	     " to %s failed\n", spawnID, result, PSC_printTID(targetTID));
	return false;
    }
    return true;
}

/**
 * @brief Extract the next service rank and try to continue spawning
 *
 * @param msg Logger message to handle
 *
 * @return No return value
 */
static void handleServiceInfo(PSLog_Msg_t *msg)
{
    int serviceRank = *(int32_t *)msg->buf;

    /* @todo support multiple concurrent pendSpawn? */
    if (!pendSpawn) {
	plog("spawn failed, no pending spawn request\n");
	return;
    }

    /* uniquely identifies the spawn globally */
    SpawnReqData_t *srdata = pendSpawn->data;

    /* try to do the spawn */
    if (tryPMIxSpawn(pendSpawn, serviceRank)) {
	plog("spawn executed successfully\n");
	sendSpawnResp(srdata->pmixServer, 1, srdata->spawnID);
    } else {
	plog("spawn failed\n");
	sendSpawnResp(srdata->pmixServer, 0, srdata->spawnID);
    }

    /* cleanup */
    ufree(pendSpawn->data);
    freeSpawnRequest(pendSpawn);
    pendSpawn = NULL;
}

void pspmix_setFillSpawnTaskFunction(fillerFunc_t spawnFunc)
{
    mdbg(PSPMIX_LOG_VERBOSE, "Set specific PMIx fill spawn task function\n");
    fillTaskFunction = spawnFunc;
}

void pspmix_resetFillSpawnTaskFunction(void)
{
    mdbg(PSPMIX_LOG_VERBOSE, "Reset PMIx fill spawn task function\n");
    fillTaskFunction = fillWithMpiexec;
}

fillerFunc_t * pspmix_getFillTaskFunction(void) {
    return fillTaskFunction;
}


/* ****************************************************** *
 *                 Send/Receive functions                 *
 * ****************************************************** */

/**
 * @brief Compose and send a client registration message to the PMIx server
 *
 * @param clientTask the client task to register
 *
 * @return Returns true on success, false on error
 */
static bool sendRegisterClientMsg(PStask_t *clientTask)
{
    rdbg(PSPMIX_LOG_COMM, "Send register client message for rank %d\n",
	 clientTask->rank);

    PStask_ID_t myTID = PSC_getMyTID();

    PStask_ID_t serverTID = pspmix_daemon_getServerTID(clientTask->uid);
    if (serverTID < 0) {
	rlog("Failed to get PMIx server TID (uid %d)\n", clientTask->uid);
	return false;
    }

    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSPMIX,
	    .sender = myTID,
	    .dest = serverTID,
	    .len = 0, /* to be set by PSP_putTypedMsgBuf */ },
	.type = PSPMIX_REGISTER_CLIENT,
	.buf = {'\0'} };

    PSP_putTypedMsgBuf(&msg, "loggertid",
		       &clientTask->loggertid, sizeof(clientTask->loggertid));
    PSP_putTypedMsgBuf(&msg, "spawnertid",
		       &clientTask->spawnertid, sizeof(clientTask->spawnertid));
    PSP_putTypedMsgBuf(&msg, "resid",
		       &clientTask->resID, sizeof(clientTask->resID));
    PSP_putTypedMsgBuf(&msg, "rank",
		       &clientTask->rank, sizeof(clientTask->rank));
    PSP_putTypedMsgBuf(&msg, "uid", &clientTask->uid, sizeof(clientTask->uid));
    PSP_putTypedMsgBuf(&msg, "gid", &clientTask->gid, sizeof(clientTask->gid));

    rdbg(PSPMIX_LOG_COMM, "Send message for %s\n", PSC_printTID(serverTID));

    /* Do not use sendDaemonMsg() here since forwarder is not yet initialized */
    ssize_t ret = PSCio_sendF(clientTask->fd, &msg, msg.header.len);
    if (ret < 0) {
	rwarn(errno, "Send msg to %s", PSC_printTID(serverTID));
    } else if (!ret) {
	rlog("Lost connection to daemon\n");
    }
    return ret > 0;
}

static bool environmentReady = false;

/**
* @brief Handle PSPMIX_CLIENT_PMIX_ENV message
*
* @param msg  The last fragment of the message to handle
* @param data The defragmented data received
*/
static void handleClientPMIxEnv(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    char **envP = NULL;
    getStringArrayM(data, &envP, NULL);
    env_t env = envNew(envP);

    rdbg(PSPMIX_LOG_COMM, "Setting environment:\n");
    for (uint32_t i = 0; i < envSize(env); i++) {
	char *envStr = envDumpIndex(env, i);
	if (putenv(envStr) != 0) {
	    rwarn(errno, "set env '%s'", envStr);
	    continue;
	}
	rdbg(PSPMIX_LOG_COMM, "%d %s\n", i, envStr);
    }
    envSteal(env);

    environmentReady = true;
}

/**
 * @brief Block until the PMIx enviroment is set
 *
 * @param timeout  maximum time in microseconds to wait
 *
 * @return Returns true on success, false on timeout
 */
static bool readClientPMIxEnvironment(int daemonfd, struct timeval timeout)
{
    rdbg(PSPMIX_LOG_CALL, "(timeout %lu us)\n",
	 (unsigned long)(timeout.tv_sec * 1000 * 1000 + timeout.tv_usec));

    while (!environmentReady) {
	DDTypedBufferMsg_t msg;
	ssize_t ret = PSCio_recvMsgT(daemonfd, &msg, &timeout);
	if (ret < 0) {
	    rwarn(errno, "Error receiving environment message");
	    return false;
	}
	else if (ret == 0) {
	    rlog("Timeout while receiving environment message\n");
	    return false;
	}
	else if (ret != msg.header.len) {
	    rlog("Unknown error receiving environment message: read %ld"
		 " len %hu\n", ret, msg.header.len);
	    return false;
	}

	recvFragMsg(&msg, handleClientPMIxEnv);
    }

    return true;
}

static bool sendNotificationResp(PStask_ID_t targetTID, PSP_PSPMIX_t type,
				 const char *nspace, pmix_rank_t pmixrank)
{
    rdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "(targetTID %s type %s nspace %s"
	 " rank %u)\n", PSC_printTID(targetTID), pspmix_getMsgTypeString(type),
	 nspace, rank);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, type);
    setFragDest(&msg, targetTID);

    addUint8ToMsg(1, &msg);
    addStringToMsg(nspace, &msg);
    addUint32ToMsg(pmixrank, &msg);

    if (sendFragMsg(&msg) < 0) {
	plog("Sending %s (nspace %s rank %u) to %s failed\n",
	     pspmix_getMsgTypeString(type), nspace, pmixrank,
	     PSC_printTID(targetTID));
	return false;
    }
    return true;
}

/**
 * @brief Handle messages of type PSPMIX_CLIENT_INIT
 *
 * @param msg  The last fragment of the message to handle
 * @param data The defragmented data received
 */
static void handleClientInit(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    rdbg(PSPMIX_LOG_CALL, "(msg %p data %p)\n", msg, data);

    PMIX_PROC_CONSTRUCT(&myproc);
    getString(data, myproc.nspace, sizeof(myproc.nspace));
    getUint32(data, &myproc.rank);

    rdbg(PSPMIX_LOG_COMM, "Handling client initialized message for %s:%d\n",
	 myproc.nspace, myproc.rank);

    pmixStatus = CONNECTED;

    /* send response */
    sendNotificationResp(msg->header.sender, PSPMIX_CLIENT_INIT_RES,
			 myproc.nspace, myproc.rank);
}

/**
 * @brief Handle messages of type PSPMIX_CLIENT_FINALIZE
 *
 * Handle notification about finalization of forwarders client. This marks the
 * client as released so exiting becomes non erroneous.
 *
 * @param msg  The last fragment of the message to handle
 * @param data The defragmented data received
 *
 * @return No return value
 */
static void handleClientFinalize(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    rdbg(PSPMIX_LOG_CALL, "(msg %p data %p)\n", msg, data);

    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    getString(data, proc.nspace, sizeof(proc.nspace));
    getUint32(data, &proc.rank);

    pdbg(PSPMIX_LOG_COMM, "received %s from namespace %s rank %d\n",
	 pspmix_getMsgTypeString(msg->type), proc.nspace, proc.rank);

    if (strcmp(proc.nspace, myproc.nspace) || proc.rank != myproc.rank) {
	plog("proc info does not match (%s:%d != %s:%d)\n", proc.nspace,
	     proc.rank, myproc.nspace, myproc.rank);
	return;
    }

    pmixStatus = RELEASED;

    /* send response */
    sendNotificationResp(msg->header.sender, PSPMIX_CLIENT_FINALIZE_RES,
			 proc.nspace, proc.rank);
    PMIX_PROC_DESTRUCT(&proc);
    PMIX_PROC_DESTRUCT(&myproc);
}

/**
 * @brief Handle messages of type PSPMIX_CLIENT_SPAWN
 *
 * Handle request to spawn new processes received from the user server. This
 * function creates a new spawn request and triggers the actual spawn by
 * requesting a new service rank from the logger. After this service rank has
 * been received, the spawn request is further handled in @a tryPMIxSpawn()
 *
 * @see tryPMIxSpawn()
 *
 * @param msg  The last fragment of the message to handle
 * @param data The defragmented data received
 *
 * @return No return value
 */
static void handleClientSpawn(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    rdbg(PSPMIX_LOG_CALL, "(msg %p data %p\n", msg, data);

    if (pmixStatus != CONNECTED) {
	rlog("ERROR: client not connected\n");
	return;
    }

    SpawnReqData_t *srdata = umalloc(sizeof(*srdata));
    srdata->pmixServer = msg->header.sender;

    getUint16(data, &srdata->spawnID);

    uint16_t napps;
    getUint16(data, &napps);

    SpawnRequest_t *req = initSpawnRequest(napps);

    req->data = srdata;

    for (size_t a = 0; a < napps; a++) {
	SingleSpawn_t *spawn = req->spawns + a;

	size_t len;

	uint32_t alen;
	getStringArrayM(data, &spawn->argv, &alen);
	spawn->argc = alen;

	/* read and fill np (maxprocs) */
	getInt32(data, &spawn->np);

	/* read and fill environment */
	char **env;
	getStringArrayM(data, &env, &alen);
	for (size_t i = 0; i < alen; i++) envPut(spawn->env, env[i]);

	/* Note on spawn->preput:
	 * In PMI "preput" is used to pass KVPs along the PMI_Spawn to be
	 * prefilled into the KVS.
	 * This is something we do not need with PMIx, since here the spawn
	 * parent has to PMIx_put() such values directly to the PMIx servers.
	 */

	/* get and fill additional info */
	vector_t infos;
	vectorInit(&infos, 8, 8, KVP_t);

	KVP_t entry;
	char *wdir = getStringML(data, &len);
	if (len) {
	    entry.key = ustrdup("wdir");
	    entry.value = wdir;
	    vectorAdd(&infos, &entry);
	} else {
	    ufree(wdir);
	}

	char *host = getStringML(data, &len);
	if (len) {
	    entry.key = ustrdup("hosts");
	    entry.value = host; /* Comma-delimited list */
	    vectorAdd(&infos, &entry);
	} else {
	    ufree(host);
	}

	char *hostfile = getStringML(data, &len);
	if (len) {
	    entry.key = ustrdup("hostfile");
	    entry.value = hostfile;
	    vectorAdd(&infos, &entry);
	} else {
	    ufree(hostfile);
	}

	spawn->infov = infos.data;
	spawn->infoc = infos.len;
    }

    pdbg(PSPMIX_LOG_COMM, "received %s with napps %hu.\n",
	 pspmix_getMsgTypeString(msg->type), napps);

    if (!doSpawn(req)) {
	plog("spawn failed");
    }
}

/**
 * @brief Handle messages of type PSP_PLUG_PSPMIX
 *
 * This function is registered in the forwarder and used for messages coming
 * from the PMIx server.
 *
 * @param vmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool handlePspmixMsg(DDBufferMsg_t *vmsg)
{
    rdbg(PSPMIX_LOG_CALL, "(vmsg %p)\n", vmsg);

    DDTypedBufferMsg_t *msg = (DDTypedBufferMsg_t *)vmsg;

    rdbg(PSPMIX_LOG_COMM, "msg: type %s length %hu [%s",
	 pspmix_getMsgTypeString(msg->type), msg->header.len,
	 PSC_printTID(msg->header.sender));
    mdbg(PSPMIX_LOG_COMM, "->%s]\n", PSC_printTID(msg->header.dest));

    switch(msg->type) {
    case PSPMIX_CLIENT_INIT:
	recvFragMsg(msg, handleClientInit);
	break;
    case PSPMIX_CLIENT_FINALIZE:
	recvFragMsg(msg, handleClientFinalize);
	break;
    case PSPMIX_CLIENT_SPAWN:
	recvFragMsg(msg, handleClientSpawn);
	break;
    default:
	rlog("unexpected message (sender %s type %s)\n",
	     PSC_printTID(msg->header.sender),
	     pspmix_getMsgTypeString(msg->type));
    }
    return true;
}

/**
 * @brief Handle a message from the job's logger
 *
 * Handle the CC message @a msg within the forwarder. The message is
 * received from the job's logger within a (pslog) message of type
 * PSP_CC_MSG.
 *
 * @param msg message to handle in a pslog container
 *
 * @return If the message is fully handled, true is returned; or false
 * if further handlers shall inspect this message
 */
static bool msgCC(DDBufferMsg_t *msg)
{
    PSLog_Msg_t *lmsg = (PSLog_Msg_t *)msg;
    switch (lmsg->type) {
	case SERV_TID:
	    handleServiceInfo(lmsg);
	    return true;
#if 0
	case SERV_EXT:
	    handleServiceExit(lmsg);
	    return true;
#endif
	default:
	    return false; // pass message to next handler if any
    }
}

/* ****************************************************** *
 *                     Hook functions                     *
 * ****************************************************** */

/**
 * @brief Hook function for PSIDHOOK_EXEC_FORWARDER
 *
 * This hook is called right before the psid forwarder forks its child
 *
 * In this function we do:
 * - Wait for the environment sent by the PMIx server
 * - Set the environment in the childs task structure
 *
 * @param data Pointer to the child's task structure
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookExecForwarder(void *data)
{
    rdbg(PSPMIX_LOG_CALL, "(data %p)\n", data);

    /* pointer is assumed to be valid for the life time of the forwarder */
    childTask = data;

    /* Remember my rank for debugging and error output */
    rank = childTask->rank;

    if (childTask->group != TG_ANY) {
	childTask = NULL;
	return 0;
    }

    env_t env = envNew(childTask->environ); // use of env is read only

    /* continue only if PMIx support is requested
     * or singleton support is configured and np == 1 */
    bool usePMIx = pspmix_common_usePMIx(env);
    char *jobsize = envGet(env, "PMI_SIZE");
    if (!usePMIx && (!getConfValueI(config, "SUPPORT_MPI_SINGLETON")
		     || (jobsize ? atoi(jobsize) : 1) != 1)) {
	childTask = NULL;
	envStealArray(env);
	return 0;
    }

    /* initialize fragmentation layer only to receive environment */
    initSerial(0, NULL);

    /* Send client registration request to the PMIx server */
    if (!sendRegisterClientMsg(childTask)) {
	rlog("Failed to send register message\n");
	envStealArray(env);
	return -1;
    }

    /* block until PMIx environment is set with some timeout */
    uint32_t tmout = 3;
    char *tmoutStr = envGet(env, "PSPMIX_ENV_TMOUT");
    envStealArray(env);
    if (tmoutStr && *tmoutStr) {
	char *end;
	long tmp = strtol(tmoutStr, &end, 0);
	if (! *end && tmp >= 0) tmout = tmp;
	rlog("timeout is %d\n", tmout);
    }
    struct timeval timeout = { .tv_sec = tmout, .tv_usec = 0 };
    if (!readClientPMIxEnvironment(childTask->fd, timeout)) return -1;

    finalizeSerial();
    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_SETUP
 *
 * Register handler for PSP_PLUG_PSPMIX messages.
 *
 * @param data Pointer to the child's task structure
 *
 * @return Return 0 or -1 in case of error
 */
static int hookForwarderSetup(void *data)
{
    /* break if this is not a PMIx job and no PMIx singleton */
    if (!childTask) return 0;

    rdbg(PSPMIX_LOG_CALL, "(data %p)\n", data);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	rlog("Unexpected child task\n");
	return -1;
    }

    /* initialize fragmentation layer */
    initSerial(0, sendDaemonMsg);

    /* register handler for notification messages from the PMIx server */
    if (!PSID_registerMsg(PSP_PLUG_PSPMIX, handlePspmixMsg)) {
	rlog("Failed to register message handler\n");
	return -1;
    }

    /* register handlers for messages needed for spawn handling */
    if (!PSID_registerMsg(PSP_CC_MSG, msgCC))
	rlog("failed to register PSP_CC_MSG handler\n");
#if 0
    if (!PSID_registerMsg(PSP_CD_SPAWNSUCCESS, msgSPAWNRES))
	mlog("%s: failed to register PSP_CD_SPAWNSUCCESS handler\n", __func__);
    if (!PSID_registerMsg(PSP_CD_SPAWNFAILED, msgSPAWNRES))
	mlog("%s: failed to register PSP_CD_SPAWNFAILED handler\n", __func__);
    if (!PSID_registerMsg(PSP_CC_ERROR, msgCCError))
	mlog("%s: failed to register PSP_CC_ERROR handler\n", __func__);
#endif

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_EXEC_CLIENT_USER
 *
 * Call PMIx_Init() for singleton support
 *
 * @param data Pointer to the child's task structure
 *
 * @return Return 0 or -1 in case of error
 */
static int hookExecClientUser(void *data)
{
    /* break if this is not a PMIx job and no PMIx singleton */
    if (!childTask) return 0;

    rdbg(PSPMIX_LOG_CALL, "(data %p)\n", data);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	rlog("Unexpected child task\n");
	return -1;
    }

    /* if this is a singleton case, call PMIx_Init() to prevent the PMIx server
     * lib from deleting the namespace after first use */
    env_t env = envNew(childTask->environ); // use of env is read only
    bool usePMIx = pspmix_common_usePMIx(env);
    envStealArray(env);
    if (usePMIx) return 0;

    rlog("Calling PMIx_Init() for singleton support.\n");
    /* need to call with proc != NULL since this is buggy until in 4.2.0
     * see https://github.com/openpmix/openpmix/issues/2707
     * @todo subject to change when dropping support for PMIx < 4.2.1 */
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    pmix_status_t status = PMIx_Init(&proc, NULL, 0);
    if (status != PMIX_SUCCESS) {
	rlog("PMIX_Init() failed: %s\n", PMIx_Error_string(status));
	PMIX_PROC_DESTRUCT(&proc);
	return -1;
    }
    PMIX_PROC_DESTRUCT(&proc);
    return 0;
}

/**
 * @brief Return PMIx initialization status
 *
 * This is meant to decide the child release strategy to be used in
 * forwarder finalization. If this returns 0 the child will not be released
 * automatically, independent of its exit status.
 *
 * @param data Pointer to the child's task structure
 *
 * @return Returns the PMIx initialization status of the child
 */
static int hookForwarderClientRelease(void *data)
{
    /* break if this is not a PMIx job */
    if (!childTask) return IDLE;

    rdbg(PSPMIX_LOG_CALL, "(data %p)\n", data);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	rlog("Unexpected child task\n");
	return IDLE;
    }

    return pmixStatus;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_EXIT
 *
 * Remove message registration and finalize the serialization layer.
 * This is mostly needless since the forwarder will exit anyway and done only
 * for symmetry reasons.
 *
 * @param data Unused parameter
 *
 * @return Always returns 0
 */
static int hookForwarderExit(void *data)
{
    rdbg(PSPMIX_LOG_CALL, "(data %p)\n", data);

    /* un-register handler for notification messages from the PMIx userserver */
    PSID_clearMsg(PSP_PLUG_PSPMIX, handlePspmixMsg);


    /* un-register handlers for messages needed for spawn handling */
    PSID_clearMsg(PSP_CC_MSG, msgCC);
#if 0
    PSID_clearMsg(PSP_CD_SPAWNSUCCESS, msgSPAWNRES);
    PSID_clearMsg(PSP_CD_SPAWNFAILED, msgSPAWNRES);
    PSID_clearMsg(PSP_CC_ERROR, msgCCError);
#endif

    finalizeSerial();

    return 0;
}

void pspmix_initForwarderModule(void)
{
    /* set spawn handler if no other plugin (e.g. psslurm) already did */
    if (!pspmix_getFillTaskFunction()) {
	mdbg(PSPMIX_LOG_VERBOSE, "Setting PMIx default fill spawn task function"
	     " to fillWithMpiexec()\n");
	pspmix_resetFillSpawnTaskFunction();
    } else {
	mdbg(PSPMIX_LOG_VERBOSE, "PMIx fill spawn task function already set\n");
    }

    PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
    PSIDhook_add(PSIDHOOK_FRWRD_SETUP, hookForwarderSetup);
    PSIDhook_add(PSIDHOOK_EXEC_CLIENT_USER, hookExecClientUser);
    PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RLS, hookForwarderClientRelease);
    PSIDhook_add(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

void pspmix_finalizeForwarderModule(void)
{
    PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
    PSIDhook_del(PSIDHOOK_FRWRD_SETUP, hookForwarderSetup);
    PSIDhook_del(PSIDHOOK_EXEC_CLIENT_USER, hookExecClientUser);
    PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RLS, hookForwarderClientRelease);
    PSIDhook_del(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
