/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Implementation of all functions running in the forwarders
 *
 * Four jobs are done in the forwarders:
 * - Before forking the client, wait for the environment sent by the
 *   PMIx server and set it for the client.
 * - Manage the initialization state (in PMIx sense) of the client to correctly
 *   do the client release and thus failure handling.
 *   (This task is historically done in the psid forwarder and actually means
 *    an unnecessary indirection (PMIx server <-> PSID Forwarder <-> PSID).
 *    @todo Think about getting rid of that.)
 * - Handling spawn requests initiated by the forwarder's client by calling
 *   PMIx_Spawn().
 * - Inform the PMIx server about the successful spawn of the client if it
 *   results from a call to PMIx_Spawn().
 */
#include "pspmixforwarder.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pmix.h>
#include <pmix_common.h>
#include <syslog.h>

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
#include "psenv.h"
#include "pslog.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "psserial.h"
#include "psstrv.h"
#include "psispawn.h"

#include "psidcomm.h"
#include "psidforwarder.h"
#include "psidhook.h"

#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "pluginspawn.h"

#include "pspmixcommon.h"
#include "pspmixconfig.h"
#include "pspmixdaemon.h"
#include "pspmixlog.h"
#include "pspmixtypes.h"
#include "pspmixutil.h"

/* psid rank of this forwarder and child */
static int32_t rank = -1;

/* PMIx process identifier for the child */
static pmix_proc_t myproc;

/* task structure of this forwarders child */
static PStask_t *childTask = NULL;

/* PMIx initialization status of the child */
PSIDhook_ClntRls_t pmixStatus = IDLE;

typedef struct {
    list_t next;               /**< used to put into pendSpawns */
    PStask_ID_t pmixServer;    /**< PMIx user-server requesting this spawn */
    uint16_t spawnID;          /**< spawn ID as provided by user-server */
    SpawnRequest_t *req;       /**< corresponding request itself */
    char *pnspace;             /**< parent namespace */
    uint32_t prank;            /**< parent rank */
    uint32_t opts;             /**< additional options flags */
    int32_t servRank;          /**< service rank to use (0 none yet assigned) */
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
 *          SpawnReqData_t helper functions               *
 * ****************************************************** */

/** list of pending spawn requests of type SpawnReqData_t */
static LIST_HEAD(pendSpawns);

static SpawnReqData_t *newSpawnReqData(void)
{
    SpawnReqData_t *srdata = umalloc(sizeof(*srdata));
    srdata->pnspace = NULL;
    srdata->servRank = 0;

    return srdata;
}

/**
 * @brief Find spawn request data by service rank
 *
 * Identity spawn request data struct by its service rank @a
 * servRank. If @a servRank is 0 (i.e. no service rank was yet
 * assigned to this spawn request data struct) the first item with
 * service rank 0 is returned.
 *
 * @param servRank Service rank to search for
 *
 * @return Return the spawn request data struct or NULL
 */
static SpawnReqData_t *findSpawnReqData(int32_t servRank)
{
    list_t *s;
    list_for_each(s, &pendSpawns) {
	SpawnReqData_t *srdata = list_entry(s, SpawnReqData_t, next);
	if (srdata->servRank == servRank) return srdata;
    }
    return NULL;
}

static void freeSpawnReqData(SpawnReqData_t *srdata)
{
    if (!srdata) return;
    ufree(srdata->pnspace);
    ufree(srdata);
}

/* ****************************************************** *
 *                Spawn handling functions                *
 * ****************************************************** */

/* spawn functions */
static fillerFunc_t *fillTaskFunction = NULL;

/** Generic static buffer */
static char buffer[1024];

/**
 * @brief Spawn one or more processes
 *
 * We first need the next service rank for the new service process to
 * start. Only the logger knows that. So we ask it and enqueue the
 * spawn request to wait for an answer.
 *
 * Note: This function is only called if the forwarder's client has
 * called PMIx_Spawn() and thus we are handling this respawn as
 * leading forwarder.
 *
 * @param srdata Spawn request plus additional data (takes ownership)
 *
 * @return Returns true on success and false on error
 *
 * @see handleServiceInfo()
 * @see handleClientSpawn()
 */
static bool doSpawn(SpawnReqData_t *srdata)
{
    list_add_tail(&srdata->next, &pendSpawns);

    plog("trying to spawn job with %d apps\n", srdata->req->num);

    pdbg(PSPMIX_LOG_SPAWN, "Requesting service rank for spawn %s:%d triggered"
	 " by nspace %s rank %u\n", PSC_printTID(srdata->pmixServer),
	 srdata->spawnID, srdata->pnspace, srdata->prank);

    /* get next service rank from logger */
    if (PSLog_write(childTask->loggertid, SERV_RNK, NULL, 0) < 0) {
	plog("writing to logger failed\n");

	list_del(&srdata->next);
	freeSpawnRequest(srdata->req);
	freeSpawnReqData(srdata);
	return false;
    }

    return true;
}

/**
 * Fill the passed task structure to spawn processes using kvsprovider
 *
 * Note: This function is only called if the forwarder's client has called
 * PMIx_Spawn() and thus we are handling this respawn as leading forwarder.
 *
 * @see handleClientSpawn()
 *
 * @param req spawn request
 * @param usize universe size (ignored)
 * @param task task structure to adjust
 *
 * @return 1 on success, 0 on error (currently unused)
 */
static int fillWithMpiexec(SpawnRequest_t *req, int usize, PStask_t *task)
{
    bool noParricide = false;

    /* build arguments:
     * kvsprovider --pmix --gnodetype <typelist> <mpiexecopts(job)>
     *             -np <NP> -d <WDIR> --nodetype <typeslist>
     *			    -E <key> <mpiexecopts(app) <value> <BINARY> : ... */
    strv_t args = strvNew(NULL);
    /* @todo change to not need to start a kvsprovider any longer */
    char *tmpStr = getenv("__PSI_MPIEXEC_KVSPROVIDER");
    if (tmpStr) {
	strvAdd(args, tmpStr);
    } else {
	strvAdd(args, PKGLIBEXECDIR "/kvsprovider");
    }

    /* set PMIx mode */
    strvAdd(args, "--pmix");

    char *info = envGet(req->infos, "nodetypes");
    if (info) {
	strvAdd(args, "--gnodetype");
	strvAdd(args, info);
    }

    /* "mpiexecopts" info field is extremely dangerous and only meant for
     * easier testing purposes during development. It is officially
     * undocumented and can be removed at any time in the future.
     * Every option that is found useful using this mechanism should
     * get it's own info key for in production use. */
    info = envGet(req->infos, "mpiexecopts");
    if (info) {
	flog("WARNING: Undocumented feature 'mpiexecopts' used (job): '%s'\n",
	     info);
	/* simply split at blanks */
	char *mpiexecopts = ustrdup(info);
	char *ptr = strtok(mpiexecopts, " ");
	while (ptr) {
	    strvAdd(args, ptr);
	    ptr = strtok(NULL, " ");
	}
	ufree(mpiexecopts);
    }

    size_t jobsize = 0;

    for (int r = 0; r < req->num; r++) {
	SingleSpawn_t *spawn = &req->spawns[r];

	/* add separating colon */
	if (r) strvAdd(args, ":");

	jobsize += spawn->np;

	/* set the number of processes to spawn */
	strvAdd(args, "-np");
	snprintf(buffer, sizeof(buffer), "%d", spawn->np);
	strvAdd(args, buffer);

	/* extract info values and keys
	 *
	 * These info variables are set in handleClientSpawn() depending on the
	 * information received. These information are originally taken from
	 * info fields set by the user in the PMIx_Spawn() call.
	 *
	 * Plain pspmix supports:
	 *
	 *  - PMIX_WDIR (wdir): Working directory for spawned processes
	 *  - pspmix.nodetypes (nodetypes): Comma separated list of nodetypes to
	 *                                  be used
	 *  - parricide: Flag to not kill process upon relative's unexpected
	 *               death.
	 */
	char * mpiexecopts = NULL;

	char *info = envGet(spawn->infos, "wdir");
	if (info) {
	    strvAdd(args, "-d");
	    strvAdd(args, info);
	}

	info = envGet(spawn->infos, "nodetypes");
	if (info) {
	    strvAdd(args, "--nodetype");
	    strvAdd(args, info);
	}

	info = envGet(spawn->infos, "parricide");
	if (info) {
	    if (!strcmp(info, "disabled")) {
		noParricide = true;
	    } else if (!strcmp(info, "enabled")) {
		noParricide = false;
	    }
	}

	/* "mpiexecopts" info field is extremely dangerous (see above) */
	info = envGet(spawn->infos, "mpiexecopts");
	if (info) {
	    flog("WARNING: Undocumented feature 'mpiexecopts' used (app %d):"
		 " '%s'\n", r, info);
	    mpiexecopts = ustrdup(info);
	}

	/* add user defined environment */
	for (char **env = envGetArray(spawn->env); env && *env; env++) {
	    strvAdd(args, "-E");
	    char *tmp = ustrdup(*env);
	    char *stringp = tmp;
	    strvAdd(args, strsep(&stringp, "="));
	    strvAdd(args, stringp);
	    ufree(tmp);
	}

	/* append developer options if passed as last options */
	if (mpiexecopts) {
	    /* simply split at blanks */
	    char *ptr = strtok(mpiexecopts, " ");
	    while (ptr) {
		strvAdd(args, ptr);
		ptr = strtok(NULL, " ");
	    }
	    ufree(mpiexecopts);
	}

	/* add binary and argument from spawn request */
	strvAppend(args, spawn->argV);
    }

    task->argV = args;

    task->noParricide = noParricide;

    /* update environment */
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%zu", jobsize);
    envSet(task->env, "PMIX_JOB_SIZE", tmp);

    return 1;
}

/**
 * @brief Filter spawner environment for the spawnee
 *
 * Filter out some of the spawners environment variables when copying
 * the task for the spawnees.
 *
 * Note: This function is only called if the forwarder's client has
 * called PMIx_Spawn() and thus we are handling this respawn as
 * leading forwarder.
 *
 * @param envent Single entry of the task environment in "k=v" notation
 *
 * @return Returns true to include and false to exclude @a envent
 *
 * @see handleClientSpawn()
 */
static bool spawnEnvFilter(const char *envent)
{
    /* skip troublesome old env vars */
    if (!strncmp(envent, "__KVS_PROVIDER_TID=", 19)
	|| !strncmp(envent, "PMI_ENABLE_SOCKP=", 17)
	|| !strncmp(envent, "PMI_RANK=", 9)
	|| !strncmp(envent, "PMI_PORT=", 9)
	|| !strncmp(envent, "PMI_FD=", 7)
	|| !strncmp(envent, "PMI_KVS_TMP=", 12)
	|| !strncmp(envent, "OMP_NUM_THREADS=", 16)
	|| !strncmp(envent, "PMIX_", 5)
	|| !strncmp(envent, "__PMIX_", 7)) {
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
 * This function is called once we received a service rank to use from
 * the job's logger. It triggers the actual spawn.
 *
 * It uses @ref fillTaskFunction to setup a command, that will lead to
 * a new job containing the given applications. Usually this will call
 * kvsprovider directly to start a new spawner or use a different
 * resource manager command like Slurm's @a srun which will later
 * trigger psslurm to execute kvsprovider.
 *
 * Note: This function is only called if the forwarder's client has
 * called PMIx_Spawn() and thus we are handling this respawn as
 * leading forwarder.
 *
 * @param srdata  spawn request to execute plus additional data
 *
 * @return True on success, false on error
 *
 * @see handleClientSpawn()
 */
static bool tryPMIxSpawn(SpawnReqData_t *srdata)
{
    char *str = getenv("PMIX_DEBUG");
    bool debug = (str && atoi(str) > 0);

    PStask_t *task = initSpawnTask(childTask, spawnEnvFilter);
    if (!task) {
	mlog("%s: cannot create a new task\n", __func__);
	return false;
    }

    task->rank = srdata->servRank;

    /* fill the command of the task */
    int rc = fillTaskFunction(srdata->req, 0 /* ignored */, task);

    if (rc == -1) {
	/* function to fill the spawn task tells us not to be responsible */
	mlog("%s(r%i): Falling back to default PMIx fill spawn function.\n",
	     __func__, rank);
	rc = fillWithMpiexec(srdata->req, 0 /* ignored */, task);
    }

    if (!rc) {
	elog("Error with spawning processes.\n");
	mlog("%s(r%i): Error in PMIx fill spawn function.\n", __func__, rank);
	PStask_delete(task);
	return false;
    }

    /* add additional env vars */

    /* tell the spawnees the spawn id */
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", srdata->spawnID);
    envSet(task->env, "PMIX_SPAWNID", tmp);

    /* tell the spawnees our tid (that of the forwarder) */
    snprintf(tmp, sizeof(tmp), "%#.12lx", PSC_getMyTID());
    envSet(task->env, "__PMIX_SPAWN_PARENT_FWTID", tmp);

    /* tell the spawnees the namespace of the spawner (=> PMIX_PARENT_ID) */
    envSet(task->env, "__PMIX_SPAWN_PARENT_NSPACE", srdata->pnspace);

    /* tell the spawnees the rank of the spawner (=> PMIX_PARENT_ID) */
    snprintf(tmp, sizeof(tmp), "%d", srdata->prank);
    envSet(task->env, "__PMIX_SPAWN_PARENT_RANK", tmp);

    /* tell the spawnees the options of the spawn */
    snprintf(tmp, sizeof(tmp), "0x%08x", srdata->opts);
    envSet(task->env, "__PMIX_SPAWN_OPTS", tmp);

    /* tell the responsible PMIx server to the spawner for fail reports */
    snprintf(tmp, sizeof(tmp), "%#.12lx", srdata->pmixServer);
    envSet(task->env,  "__PMIX_SPAWN_SERVERTID", tmp);

    /* tell the message type to be used for fails to the spawner */
    snprintf(tmp, sizeof(tmp), "0x%08x", PSPMIX_SPAWNER_FAILED);
    envSet(task->env, "__PMIX_SPAWN_FAILMSG_TYPE", tmp);

    if (debug) {
	elog("%s(r%i): Executing:", __func__, rank);
	for (char **a = strvGetArray(task->argV); a && *a; a++) elog(" %s", *a);
	elog("\n");
    }
    mlog("%s(r%i): Executing:", __func__, rank);
    for (char **a = strvGetArray(task->argV); a && *a; a++) mlog(" %s", *a);
    mlog("\n");
    if (mset(PSPMIX_LOG_ENV)) {
	int cnt = 0;
	for (char **e = envGetArray(task->env); e && *e; e++, cnt++) {
	    rlog("%d: %s\n", cnt, *e);
	}
    }

    /* spawn task */
    PSnodes_ID_t dest = PSC_getMyID();
    int num = PSI_sendSpawnReq(task, &dest, 1);

    PStask_delete(task);

    return num == 1;
}

/**
 * @brief Send PSPMIX_CLIENT_SPAWN_RES message back to the PMIx server
 *
 * Note: This function is only called if the forwarder's client has
 * called PMIx_Spawn() and thus we are handling this respawn as
 * leading forwarder.
 *
 * @param dest Task ID of the destination PMIx server
 *
 * @param spawnID ID of the spawn sent by the PMIx server along with the
 * spawn request
 *
 * @param result status: 1 means success, 0 means fail
 *
 * @return Returns true on success, false on error
 */
static bool sendSpawnResp(PStask_ID_t dest, uint16_t spawnID, uint8_t result)
{
    rdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s spawnID %hu result %d\n",
	 PSC_printTID(dest), spawnID, result);

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_CLIENT_SPAWN_RES);
    setFragDest(&msg, dest);

    addUint16ToMsg(spawnID, &msg);
    addUint8ToMsg(result, &msg);

    if (sendFragMsg(&msg) < 0) {
	plog("Sending PSPMIX_CLIENT_SPAWN_RES (spawnID %hu result %d)"
	     " to %s failed\n", spawnID, result, PSC_printTID(dest));
	return false;
    }
    return true;
}

/**
 * @brief Extract the next service rank and try to continue spawning
 *
 * Note: This function is only called if the forwarder's client has
 * called PMIx_Spawn() and thus we are handling this respawn as
 * leading forwarder.
 *
 * @param msg Logger message to handle
 *
 * @return true if message handled, false to pass it to the next handler
 *
 * @see handleClientSpawn()
 */
static bool handleServiceInfo(PSLog_Msg_t *msg)
{
    SpawnReqData_t *srdata = findSpawnReqData(0);
    /* message might be for other handler (e.g. pspmi) */
    if (!srdata) return false;

    srdata->servRank = *(int32_t *)msg->buf;
    pdbg(PSPMIX_LOG_SPAWN, "Trying spawn %s:%d with service rank %d\n",
	 PSC_printTID(srdata->pmixServer), srdata->spawnID, srdata->servRank);

    /* try to do the spawn */
    if (tryPMIxSpawn(srdata)) return true;

    /* spawn already failed */
    plog("spawn failed\n");
    sendSpawnResp(srdata->pmixServer, srdata->spawnID, 0);

    /* cleanup */
    list_del(&srdata->next);
    freeSpawnRequest(srdata->req);
    freeSpawnReqData(srdata);

    return true;
}

/* function to be used by other plugins (e.g. psslurm) */
void psPmixSetFillSpawnTaskFunction(fillerFunc_t spawnFunc)
{
    mdbg(PSPMIX_LOG_VERBOSE, "Set specific PMIx fill spawn task function\n");
    fillTaskFunction = spawnFunc;
}

/* function to be used by other plugins (e.g. psslurm) */
void psPmixResetFillSpawnTaskFunction(void)
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
 * @param clientTask Client task to register
 *
 * @return Returns true on success, false on error
 *
 * @see waitForClientEnv()
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

/**
 * @brief Compose and send a spawn success message to the PMIx server
 *
 * This function reports the successful spawn of the client to the local
 * PMIx server. In addition it includes the client's TID to enable the
 * PMIx server to terminate the job.
 *
 * If the spawn results from a call to PMIx_Spawn(), the ID of that spawn is
 * included as well. This is identified by peeking into the client's
 * environment for PMIX_SPAWNID.
 *
 * Only one message is ever sent, no matter how ofter the function is called.
 *
 * @param success    success state to report
 *
 * @return Returns true on success, false on error
 */
static bool sendSpawnSuccess(bool success)
{
    rdbg(PSPMIX_LOG_CALL, "success %s\n", success ? "true" : "false");

    /* Each forwarder should only send this message once in a lifetime */
    static bool alreadySent = false;
    if (alreadySent) return true;
    alreadySent = true;

    /* get the namespace of the client from the environment
     * it is always in included in the additional environment received from the
     * PMIx server in hookExecForwarder() and set in handleClientPMIxEnv() */
    char *nspace = getenv("PMIX_NAMESPACE");
    if (!nspace) {
	rlog("UNEXPECTED: PMIX_NAMESPACE not found in client environment\n");
	return false;
    }

    /* try to get the spawn ID which should only be there for respawns */
    uint16_t spawnID = 0; /* no respawn */
    char *spawnIDstr = envGet(childTask->env, "PMIX_SPAWNID");
    if (spawnIDstr) {
	char *end;
	long res = strtol(spawnIDstr, &end, 10);
	if (*end != '\0' || res <= 0) {
	    rlog("invalid PMIX_SPAWNID: %s\n", spawnIDstr);
	    success = false;
	} else {
	    spawnID = res;
	}
    }

    PStask_ID_t serverTID = pspmix_daemon_getServerTID(childTask->uid);
    if (serverTID < 0) {
	rlog("Failed to get PMIx server TID (uid %d)\n", childTask->uid);
	return false; /* cannot send a message without a target */
    }

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_CLIENT_STATUS);
    setFragDest(&msg, serverTID);

    addStringToMsg(nspace, &msg);
    addUint16ToMsg(spawnID, &msg);
    addInt32ToMsg(childTask->jobRank, &msg);
    addBoolToMsg(success, &msg);
    addTaskIdToMsg(childTask->tid, &msg);  /* avail. in PSIDHOOK_FRWRD_INIT */

    if (mset(PSPMIX_LOG_COMM)) {
	rlog("Send message to %s (nspace '%s' spawnID %hu jobRank %d",
	     PSC_printTID(serverTID), nspace, spawnID, childTask->jobRank);
	mlog(" success %s clientTID %s\n", success ? "true" : "false",
	     PSC_printTID(childTask->tid));
    }

    if (sendFragMsg(&msg) < 0) {
	rlog("Sending spawn success message to %s failed\n",
	     PSC_printTID(serverTID));
	return false;
    }
    return true;
}

/* indicates that env has been received */
static bool environmentReady = false;
/* indicates that fail message has been received */
static bool jobsetupFailed = false;

/**
 * @brief Handle PSPMIX_CLIENT_PMIX_ENV message
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 *
 * @return No return value
 *
 * @see waitForClientEnv()
 */
static void handleClientPMIxEnv(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    env_t env;
    getEnv(data, env);

    rdbg(PSPMIX_LOG_COMM, "Setting environment:\n");
    int cnt = 0;
    for (char **e = envGetArray(env); e && *e; e++, cnt++) {
	if (putenv(*e) != 0) {
	    rwarn(errno, "set env '%s'", *e);
	    continue;
	}
	rdbg(PSPMIX_LOG_COMM, "%d %s\n", cnt, *e);
    }
    envSteal(env);

    environmentReady = true;
}

/**
 * @brief Handle PSP_PLUG_PSPMIX message arriving early
 *
 * At the very beginning of forwarder's setup, we are requesting the
 * environment for our client from the PMIx server. This function
 * handles the answer to that request and is not meant to be used
 * later again.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 *
 * @return No return value
 *
 * @see waitForClientEnv()
 */
static void handleEarlyMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    if (msg->header.type != PSP_PLUG_PSPMIX) {
	rlog("Dropping unexpected message of type %d/%d from %s\n", msg->type,
	     msg->header.type, PSC_printTID(msg->header.sender));
	return;
    }

    switch (msg->type) {
	case PSPMIX_CLIENT_PMIX_ENV:
	    handleClientPMIxEnv(msg, data);
	    break;
	case PSPMIX_JOBSETUP_FAILED:
	    jobsetupFailed = true;
	    break;
	default:
	    rlog("Dropping unexpected message of type %d/%d from %s\n",
		 msg->type, msg->header.type, PSC_printTID(msg->header.sender));
    }
}

/**
 * @brief Block until the PMIx environment is set or a failure is reported
 *
 * This function waits for the message of type PSPMIX_CLIENT_PMIX_ENV expected
 * from the local PMIx server as response to our PSPMIX_REGISTER_CLIENT message.
 * It would containing the environment additions. If a problem occurs on the
 * PMIx server side, PSPMIX_JOBSETUP_FAILED might be received instead.
 *
 * @param timeout  maximum time in microseconds to wait
 *
 * @return Returns true on success, false on timeout
 */
static bool waitForClientEnv(int daemonfd, struct timeval timeout)
{
    rdbg(PSPMIX_LOG_CALL, "timeout %lu us\n",
	 (unsigned long)(timeout.tv_sec * 1000 * 1000 + timeout.tv_usec));

    while (!environmentReady && !jobsetupFailed) {
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

	recvFragMsg(&msg, handleEarlyMsg);
    }

    return !jobsetupFailed;
}

static bool sendNotificationResp(PStask_ID_t targetTID, PSP_PSPMIX_t type,
				 const char *nspace, pmix_rank_t pmixrank)
{
    rdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "targetTID %s type %s nspace %s"
	 " rank %u\n", PSC_printTID(targetTID), pspmix_getMsgTypeString(type),
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
 * This message is sent by the local PMIx server after the forwarder's client
 * has called PMIx_Init().
 *
 * This function marks the client as connected so exiting becomes erroneous.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 *
 * @return No return value
 */
static void handleClientInit(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    rdbg(PSPMIX_LOG_CALL, "msg %p data %p\n", msg, data);

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
 * This message is sent by the local PMIx server after the forwarder's client
 * has called PMIx_Finalize().
 *
 * This function marks the client as released so exiting becomes non erroneous.
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 *
 * @return No return value
 */
static void handleClientFinalize(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    rdbg(PSPMIX_LOG_CALL, "msg %p data %p\n", msg, data);

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

/* Macro to put info key value pair from msg into environment */
#define GET_STRING_INFO(name, infos)		\
    do {					\
	size_t len;				\
	char *name = getStringML(data, &len);	\
	if (len) envSet(infos, #name, name);	\
	ufree(name);				\
    } while(false)

/**
 * @brief Handle messages of type PSPMIX_CLIENT_SPAWN
 *
 * Handle request to spawn new processes received from the user
 * server. Such a request is sent if the child of this forwarder has
 * called PMIx_Spawn().
 *
 * This function creates a new spawn request and triggers the actual
 * spawn by requesting a new service rank from the logger. After this
 * service rank has been received, the spawn request is further
 * handled in @a tryPMIxSpawn()
 *
 * @param msg Last fragment of the message to handle
 *
 * @param data Defragmented data received
 *
 * @return No return value
 *
 * @see tryPMIxSpawn()
 */
static void handleClientSpawn(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data)
{
    rdbg(PSPMIX_LOG_CALL, "msg %p data %p\n", msg, data);

    if (pmixStatus != CONNECTED) {
	rlog("ERROR: client not connected\n");
	return;
    }

    SpawnReqData_t *srdata = newSpawnReqData();
    srdata->pmixServer = msg->header.sender;

    getUint16(data, &srdata->spawnID);
    srdata->pnspace = getStringM(data);
    getUint32(data, &srdata->prank);
    getUint32(data, &srdata->opts);

    /* get additional job level info */
    env_t infos = envNew(NULL);

    GET_STRING_INFO(nodetypes, infos);
    GET_STRING_INFO(mpiexecopts, infos);
    GET_STRING_INFO(srunopts, infos);

    /* get number of apps and initialize request accordingly */
    uint16_t napps;
    getUint16(data, &napps);
    srdata->req = initSpawnRequest(napps);

    /* fill additional job level info into request */
    srdata->req->infos = infos;

    for (size_t a = 0; a < napps; a++) {
	SingleSpawn_t *spawn = srdata->req->spawns + a;

	getArgV(data, spawn->argV);

	/* read and fill np (maxprocs) */
	getInt32(data, &spawn->np);

	/* read and fill environment */
	getEnv(data, spawn->env);

	/* get and fill additional info */
	spawn->infos = envNew(NULL);

	GET_STRING_INFO(wdir, spawn->infos);
	GET_STRING_INFO(hosts, spawn->infos);
	GET_STRING_INFO(hostfile, spawn->infos);
	GET_STRING_INFO(nodetypes, spawn->infos);
	GET_STRING_INFO(mpiexecopts, spawn->infos);
	GET_STRING_INFO(srunopts, spawn->infos);
	GET_STRING_INFO(srunconstraint, spawn->infos);
    }

    pdbg(PSPMIX_LOG_COMM, "received %s with napps %hu.\n",
	 pspmix_getMsgTypeString(msg->type), napps);


    if (!doSpawn(srdata /* transfers ownership */)) plog("spawn failed");
}

static bool sendClientLogResp(PStask_ID_t dest,
			      uint16_t callID, uint16_t reqID, bool success)
{
    fdbg(PSPMIX_LOG_CALL|PSPMIX_LOG_COMM, "dest %s success %s\n",
	 PSC_printTID(dest), success ? "true" : "false");

    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PSPMIX, PSPMIX_CLIENT_LOG_RES);
    setFragDest(&msg, dest);

    addUint16ToMsg(callID, &msg);
    addUint16ToMsg(reqID, &msg);
    addBoolToMsg(success, &msg);

    if (sendFragMsg(&msg) < 0) {
	flog("sending log response to %s failed\n", PSC_printTID(dest));
	return false;
    }

    return true;
}

/**
 * @brief Handle messages of type PSPMIX_CLIENT_LOG_REQ
 *
 * Handle request to log to stdout and stderr. Such a request can be
 * sent if the child of this forwarder has called PMIx_LOG().
 *
 * @param msg  The last fragment of the message to handle
 * @param data The defragmented data received
 *
 * @return No return value
 */
static void handleClientLogReq(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data) {
    rdbg(PSPMIX_LOG_CALL, "msg %p data %p\n", msg, data);

    uint16_t callID;
    getUint16(data, &callID);

    uint16_t reqID;
    getUint16(data, &reqID);

    int32_t channel_i;
    getInt32(data, &channel_i);
    PspmixLogChannel_t channel = channel_i;

    int32_t priority = LOG_ERR;
    if (channel == PSPMIX_LC_SYSLOG) getInt32(data, &priority);

    time_t time;
    getTime(data, &time);
    char timeStr[32] = { '\0' };
    if (time) {
	ctime_r(&time, timeStr);
	// evict trailing newline if any
	size_t len = strlen(timeStr);
	if (len && timeStr[len-1] == '\n') timeStr[len-1] = '\0';
    }

    char *str = getStringM(data);

    rdbg(PSPMIX_LOG_LOGGING, "Logging to %s '%s%s%s'\n",
	 pspmix_getChannelName(channel), timeStr, time ? ": " : "", str);

    int ret = 0;
    switch (channel) {
    case PSPMIX_LC_STDOUT:
	if (time) {
	    ret = PSIDfwd_printMsg(STDOUT, timeStr);
	    if (ret != -1) ret = PSIDfwd_printMsg(STDOUT, ": ");
	}
	if (ret != -1) ret = PSIDfwd_printMsg(STDOUT, str);
	break;
    case PSPMIX_LC_STDERR:
	ret = (getenv("__PMIX_BREAK_STDERR")) ? -1 : 0;
	if (time) {
	    if (ret != -1) ret = PSIDfwd_printMsg(STDERR, timeStr);
	    if (ret != -1) ret = PSIDfwd_printMsg(STDERR, ": ");
	}
	if (ret != -1) ret = PSIDfwd_printMsg(STDERR, str);
	break;
    case PSPMIX_LC_SYSLOG:
	syslog(priority, "%s%s%s\n", timeStr, time ? ": " : "", str);
	ret = 0;
	break;
    default:
	ret = -1; // unsupported channel
	break;
    }

    if (callID) sendClientLogResp(msg->header.sender, callID, reqID, ret != -1);
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
    rdbg(PSPMIX_LOG_CALL, "vmsg %p\n", vmsg);

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
    case PSPMIX_CLIENT_LOG_REQ:
	recvFragMsg(msg, handleClientLogReq);
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
	case SERV_RNK:
	    return handleServiceInfo(lmsg);
#if 0
	case SERV_EXT:
	    handleServiceExit(lmsg);
	    return true;
#endif
	default:
	    return false; // pass message to next handler if any
    }
}

/**
 * @brief Handle spawn result message
 *
 * Handle the spawn results message contained in @a msg. Such message
 * of type PSP_CD_SPAWNSUCCESS or PSP_CD_SPAWNFAILED is only expected
 * upon the creation of a new service process used to actually realize
 * the PMIx spawn that was triggered by this forwarder's client.
 *
 * @see handleClientSpawn()
 *
 * @param msg Message to handle
 *
 * @return Return true if the message was fully handled or false otherwise
 */
static bool msgSPAWNRES(DDBufferMsg_t *msg)
{
    int type = msg->header.type;
    if (type != PSP_CD_SPAWNFAILED && type != PSP_CD_SPAWNSUCCESS) return false;

    DDErrorMsg_t *eMsg = (DDErrorMsg_t *)msg;
    SpawnReqData_t *srdata = findSpawnReqData(eMsg->request);
    if (!srdata) {
	/* might happen if PMI is actually used */
	pdbg(PSPMIX_LOG_SPAWN, "no pending spawn found (type %s from %s)\n",
	     PSP_printMsg(type), PSC_printTID(msg->header.sender));
	return false;
    }

    switch (type) {
    case PSP_CD_SPAWNFAILED:
	plog("spawn %s:%d: spawning service process failed\n",
	     PSC_printTID(srdata->pmixServer), srdata->spawnID);
	sendSpawnResp(srdata->pmixServer, srdata->spawnID, 0);
	break;
    case PSP_CD_SPAWNSUCCESS:
	/* wait for result of the spawner process */
	pdbg(PSPMIX_LOG_SPAWN, "spawn %s:%d: service process spawned successful",
	     PSC_printTID(srdata->pmixServer), srdata->spawnID);
	mdbg(PSPMIX_LOG_SPAWN, " to %s\n", PSC_printTID(msg->header.sender));
	sendSpawnResp(srdata->pmixServer, srdata->spawnID, 1);
	break;
    }

    /* cleanup */
    list_del(&srdata->next);
    freeSpawnRequest(srdata->req);
    freeSpawnReqData(srdata);

    return true;
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
 * - Set the environment in the child's task structure
 *
 * @param data Pointer to the child's task structure
 *
 * @return Returns 0 on success and -1 on error.
 */
static int hookExecForwarder(void *data)
{
    rdbg(PSPMIX_LOG_CALL, "data %p\n", data);

    /* pointer is assumed to be valid for the life time of the forwarder */
    childTask = data;

    /* Remember my rank for debugging and error output */
    rank = childTask->rank;

    if (childTask->group != TG_ANY) {
	childTask = NULL;
	return 0;
    }

    /* continue only if PMIx support is requested
     * or singleton support is configured and np == 1 */
    bool usePMIx = pspmix_common_usePMIx(childTask->env);
    if (!usePMIx && (!getConfValueI(config, "SUPPORT_MPI_SINGLETON"))
	/* @todo do we need to check np == 1 here? */) {
	childTask = NULL;
	return 0;
    }

    /* initialize fragmentation layer only to receive environment */
    initSerial(0, NULL);

    /* Send client registration request to the PMIx server */
    if (!sendRegisterClientMsg(childTask)) {
	rlog("Failed to send register message\n");
	return -1;
    }

    /* block until PMIx environment is set with some timeout */
    uint32_t tmout = 3;
    char *tmoutStr = envGet(childTask->env, "PSPMIX_ENV_TMOUT");
    if (tmoutStr && *tmoutStr) {
	char *end;
	long tmp = strtol(tmoutStr, &end, 0);
	if (! *end && tmp >= 0) tmout = tmp;
	rlog("timeout is %d\n", tmout);
    }
    struct timeval timeout = { .tv_sec = tmout, .tv_usec = 0 };
    bool success = waitForClientEnv(childTask->fd, timeout);

    finalizeSerial();
    return success ? 0 : -1;
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

    rdbg(PSPMIX_LOG_CALL, "data %p\n", data);

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
    if (!PSID_registerMsg(PSP_CD_SPAWNSUCCESS, msgSPAWNRES))
	mlog("%s: failed to register PSP_CD_SPAWNSUCCESS handler\n", __func__);
    if (!PSID_registerMsg(PSP_CD_SPAWNFAILED, msgSPAWNRES))
	mlog("%s: failed to register PSP_CD_SPAWNFAILED handler\n", __func__);

    return 0;
}

/**
 * @brief Hook function for PSIDHOOK_FRWRD_INIT
 *
 * Send PSPMIX_SPAWN_SUCCESS message to the local PMIx server if the
 * forwarder's client is the result of a call to PMIx_Spawn().
 *
 * @param data Pointer to the child's task structure
 *
 * @return Return 0 or -1 in case of error
 */
static int hookForwarderInit(void *data)
{
    /* break if this is not a PMIx job and no PMIx singleton */
    if (!childTask) return 0;

    rdbg(PSPMIX_LOG_CALL, "data %p\n", data);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	rlog("Unexpected child task\n");
	childTask = data; /* in doubt take the new one */
    }

    /* inform PMIx server about success of the spawn */
    if (!sendSpawnSuccess(true)) {
	rlog("Failed to send spawn success message\n");
	return -1;
    }

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

    rdbg(PSPMIX_LOG_CALL, "data %p\n", data);

    /* pointer is assumed to be valid for the life time of the forwarder */
    if (childTask != data) {
	rlog("Unexpected child task\n");
	return -1;
    }

    /* if this is a singleton case, call PMIx_Init() to prevent the PMIx server
     * lib from deleting the namespace after first use */
    if (pspmix_common_usePMIx(childTask->env)) return 0;

    rlog("Calling PMIx_Init() for singleton support.\n");
    /* need to call with proc != NULL since this is buggy until in 4.2.0
     * see https://github.com/openpmix/openpmix/issues/2707
     * @todo subject to change when dropping support for PMIx < 4.2.1 */
    pmix_proc_t proc;
    PMIX_PROC_CONSTRUCT(&proc);
    pmix_status_t status = PMIx_Init(&proc, NULL, 0);
    if (status != PMIX_SUCCESS) {
	rlog("PMIx_Init() failed: %s\n", PMIx_Error_string(status));
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

    rdbg(PSPMIX_LOG_CALL, "data %p\n", data);

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
    rdbg(PSPMIX_LOG_CALL, "data %p\n", data);

    /* un-register handler for notification messages from the PMIx userserver */
    PSID_clearMsg(PSP_PLUG_PSPMIX, handlePspmixMsg);


    /* un-register handlers for messages needed for spawn handling */
    PSID_clearMsg(PSP_CC_MSG, msgCC);
    PSID_clearMsg(PSP_CD_SPAWNSUCCESS, msgSPAWNRES);
    PSID_clearMsg(PSP_CD_SPAWNFAILED, msgSPAWNRES);

    finalizeSerial();

    return 0;
}

void pspmix_initForwarderModule(void)
{
    /* set spawn handler
     * as long as this function is only called in pspmix init function,
     * it will always be unset before */
    mdbg(PSPMIX_LOG_VERBOSE, "Setting PMIx default fill spawn task function"
	 " to fillWithMpiexec()\n");
    psPmixResetFillSpawnTaskFunction();

    PSIDhook_add(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
    PSIDhook_add(PSIDHOOK_FRWRD_SETUP, hookForwarderSetup);
    PSIDhook_add(PSIDHOOK_EXEC_CLIENT_USER, hookExecClientUser);
    PSIDhook_add(PSIDHOOK_FRWRD_INIT, hookForwarderInit);
    PSIDhook_add(PSIDHOOK_FRWRD_CLNT_RLS, hookForwarderClientRelease);
    PSIDhook_add(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

void pspmix_finalizeForwarderModule(void)
{
    PSIDhook_del(PSIDHOOK_EXEC_FORWARDER, hookExecForwarder);
    PSIDhook_del(PSIDHOOK_FRWRD_SETUP, hookForwarderSetup);
    PSIDhook_del(PSIDHOOK_FRWRD_INIT, hookForwarderInit);
    PSIDhook_del(PSIDHOOK_EXEC_CLIENT_USER, hookExecClientUser);
    PSIDhook_del(PSIDHOOK_FRWRD_CLNT_RLS, hookForwarderClientRelease);
    PSIDhook_del(PSIDHOOK_FRWRD_EXIT, hookForwarderExit);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
