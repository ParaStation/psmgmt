/*
 * ParaStation
 *
 * Copyright (C) 2007-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pmiclient.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "list.h"
#include "pscommon.h"
#include "pscio.h"
#include "psenv.h"
#include "pslog.h"
#include "psprotocol.h"
#include "psserial.h"
#include "psstrv.h"
#include "selector.h"

#include "psilog.h"
#include "psispawn.h"

#include "kvs.h"
#include "kvscommon.h"
#include "pluginmalloc.h"
#include "pluginspawn.h"

#include "psidcomm.h"
#include "psidforwarder.h"
#include "psidhook.h"

#include "psaccounthandles.h"

#include "pmilog.h"

#define SOCKET int
#define PMI_VERSION 1
#define PMI_SUBVERSION 1
#define UPDATE_HEAD sizeof(uint8_t) + sizeof(uint32_t)

/* Set this to 1 to enable additional debug output describing the environment */
#define DEBUG_ENV 0
#if DEBUG_ENV
extern char **environ;
#endif

typedef struct {
    list_t next;         /**< Used to put into uBufferList */
    char *msg;           /**< Actual KVS update message */
    size_t len;          /**< Size of the update message */
    bool forwarded;      /**< Flag if message was forwarded */
    bool handled;        /**< Flag if message was locally handled */
    bool lastUpdate;     /**< Flag if the update is complete (last msg) */
    int updateIndex;     /**< Index to distinguish update messages */
} Update_Buffer_t;

/** A list of buffers holding KVS update data */
static LIST_HEAD(uBufferList);

/** Flag to check if the pmi_init() was called successful */
static bool initialized = false;

/** Flag to check if initialisation between us and client was ok */
static bool clientIsInitialized = false;

/** Flag (INIT) registration to the KVS provider -- shall be done only once */
static bool initToProvider = false;

/** Counter of the next KVS name */
static int kvs_next = 0;

/** My KVS name */
static char myKVSname[PMI_KVSNAME_MAX+12];

/** If set debug output is generated */
static bool debug = false;

/** If set KVS debug output is generated */
static bool debug_kvs = false;

/** The size of the PMI universe set from mpiexec */
static int universe_size = 0;

/** Task structure describing the connected client to serve */
static PStask_t *cTask = NULL;

/** PS rank of the connected client. This is redundant to cTask->rank */
static int rank = -1;

/** The PMI rank of the connected client */
static int pmiRank = -1;

/** The PMI appnum parameter of the connected client */
static int appnum = -1;

/** Count update KVS msg from provider to make sure all msg were received */
static int32_t updateMsgCount;

/** Suffix of the KVS name */
static char kvs_name_prefix[PMI_KVSNAME_MAX];

/** The socket which is connected to the PMI client */
static SOCKET pmisock = -1;

/** The KVS provider's task ID within the current job */
static PStask_ID_t kvsProvTID = -1;

/** Socket to the local KVS provider, if any */
static PStask_ID_t kvsProvSock = -1;

/** The predecessor task ID of the current job */
static PStask_ID_t predtid = -1;

/** The successor task ID of the current job */
static PStask_ID_t succtid = -1;

/** Flag to indicate if the successor is ready to receive update messages */
static bool succReady = false;

/** Flag to check if we got the local barrier_in msg */
static bool gotBarrierIn = false;

/** Flag to check if we got the daisy barrier_in msg */
static bool gotDaisyBarrierIn = false;

/** Flag to indicate if we should start the daisy barrier */
static bool startDaisyBarrier = false;

/** Generic message buffer */
static char buffer[1024];

/** The number of children which we try to spawn */
static int spawnChildCount = 0;

/** The number of successful spawned children */
static int spawnChildSuccess = 0;

/** Flag pending spawn request */
static bool pendReq = false;

/** Buffer to collect multiple spawn requests */
static SpawnRequest_t *spawnBuffer = NULL;

/** SpawnRequest to be handled when logger returns service rank */
static SpawnRequest_t *pendSpawn = NULL;

/** Count the number of kvs_put messages from mpi client */
static int32_t putCount = 0;

static int32_t barrierCount = 0;

static int32_t globalPutCount = 0;

/* spawn functions */
static fillerFunc_t *fillTaskFunction = NULL;

/**
 * @brief Send a KVS message to a task id
 *
 * @param func Pointer to name name of the calling function
 *
 * @param msg Pointer to the message to send
 *
 * @param len The size of the message to send
 *
 * @return No return value
 */
static void sendKvsMsg(const char *func, PStask_ID_t tid, char *msg, size_t len)
{
    if (debug_kvs) {
	PSKVS_cmd_t cmd = *(PSKVS_cmd_t *) msg;
	if (cmd == PUT) {
	    elog("%s(r%i): pslog_write cmd %s len %zu dest: %s\n", func,
		 rank, PSKVScmdToString(cmd), len, PSC_printTID(tid));
	}
    }
    if (tid == -1) {
	PSKVS_cmd_t cmd = *(PSKVS_cmd_t *) msg;
	mlog("%s(%s, %s, %zd)\n", func, PSC_printTID(tid),
	     PSKVScmdToString(cmd), len);
	return;
    }

    PSLog_write(tid, KVS, msg, len);
}

/**
 * @brief Send a KVS message to provider
 *
 * @param msg Pointer to the message to send
 *
 * @param len The size of the message to send
 *
 * @return No return value
 */
static void sendKvstoProvider(char *msg, size_t len)
{
    sendKvsMsg(__func__, kvsProvTID, msg, len);
}

/**
 * @brief Send KVS messages to successor
 *
 * Send a KVS messages to the successor, i.e. the process with the
 * next rank. If the current process is the one with the highest rank,
 * the message will be send to the provider in order to signal successful
 * delivery to all clients.
 *
 * @param msg Pointer to the message to send
 *
 * @param len The size of the message to send
 *
 * @return No return value
 */
static void sendKvstoSucc(char *msg, size_t len)
{
    sendKvsMsg(__func__, succtid, msg, len);
}

/**
 * @brief Terminate the Job
 *
 * Send first TERM and then KILL signal to all the job's processes.
 *
 * @return No return value
 */
static void terminateJob(void)
{
    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_SIGNAL,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.signal = -1,
	.param = getuid(),
	.pervasive = 1,
	.answer = 0 };

    sendDaemonMsg((DDMsg_t *)&msg);
}

/**
 * @brief Handle critical error
 *
 * To handle a critical error close the connection and kill the child.
 * If something goes wrong in the startup phase with PMI, the child
 * and therefore the whole job can hang infinite. So we have to kill it.
 *
 * @return Always return 1
 */
static int critErr(void)
{
    /* close connection */
    if (pmisock > -1) {
	if (Selector_isRegistered(pmisock)) Selector_remove(pmisock);
	close(pmisock);
	pmisock = -1;
    }

    terminateJob();

    return 1;
}

/**
 * @brief Send a PMI message to the client
 *
 * Send a message to the connected client.
 *
 * @param msg Buffer with the PMI message to send
 *
 * @return Returns 1 on error, 0 on success
 */
#define PMI_send(msg) __PMI_send(msg, __func__, __LINE__)
static int __PMI_send(char *msg, const char *caller, const int line)
{
    if (!msg) {
	/* assert */
	elog("%s(r%i): missing msg from %s:%i\n", __func__, rank, caller, line);
	return critErr();
    }
    size_t len = strlen(msg);
    if (!len || msg[len - 1] != '\n') {
	/* assert */
	elog("%s(r%i): missing '\\n' in PMI msg '%s' from %s:%i\n",
	     __func__, rank, msg, caller, line);
	return critErr();
    }

    if (debug) elog("%s(r%i): %s", __func__, rank, msg);

    /* try to send msg */
    size_t sent;
    ssize_t ret = PSCio_sendPProg(pmisock, msg, len, &sent);
    if (ret < 0) {
	char *errStr = strerror(errno);
	elog("%s(r%i): got error %d on PMI socket: %s\n", __func__,
	     rank, errno, errStr ? errStr : "UNKNOWN");
    }
    if (sent < len) {
	elog("%s(r%i): failed sending %s from %s:%i\n", __func__, rank, msg,
	     caller, line);
	return critErr();
    }

    return 0;
}

/**
 * @brief Handle spawn result message
 *
 * Handle the spawn results message contained in @a msg. Such message
 * is expected upon the creation of a new service process used to
 * actually realize the PMI spawn that was triggered by the PMI
 * client.
 *
 * @param msg Message to handle
 *
 * @return Return true if the message was fully handled or false otherwise
 */
static bool msgSPAWNRES(DDBufferMsg_t *msg)
{
    if (!pendReq) return false;

    switch (msg->header.type) {
    case PSP_CD_SPAWNFAILED:
	elog("%s(r%i): spawning service proccess failed\n", __func__, rank);
	PMI_send("cmd=spawn_result rc=-1\n");
	break;
    case PSP_CD_SPAWNSUCCESS:
	/* wait for result of the spawner process */
	if (debug) elog("%s(r%i): spawning service process successful\n",
			__func__, rank);
	break;
    default:
	return false;
    }

    pendReq = false;
    return true;
}

/**
 * @brief Delete a update buffer entry
 *
 * @param uBuf The update buffer entry to delete
 *
 * @return No return value
 */
static void deluBufferEntry(Update_Buffer_t *uBuf)
{
    ufree(uBuf->msg);
    list_del(&uBuf->next);
    ufree(uBuf);
}

/**
 * @brief Return the PMI universe size
 *
 * Return the size of the PMI universe.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Always returns 0
 */
static int p_Get_Universe_Size(char *msg)
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply), "cmd=universe_size size=%i\n", universe_size);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Return the application number
 *
 * Returns the application number which defines the order the
 * application was started. This appnum parameter is set by mpiexec
 * and then forwarded to here via the PMI_APPNUM environment variable.
 * The appnum number is increased by mpiexec when different executables
 * are started by a single call to mpiexec (see MPI_APPNUM).
 *
 * @return Application number (MPI_APPNUM) as set by mpiexec
 */
static int p_Get_Appnum(void)
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply), "cmd=appnum appnum=%i\n", appnum);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Check if we can forward the daisy barrier
 *
 * Check if we can forward the daisy barrier. We have to wait for the
 * daisy barrier from the previous rank and from the local PMI client.
 *
 * If we are the first in the daisy chain we start the barrier.
 *
 * @return No return value
 */
static void checkDaisyBarrier(void)
{
    if (gotBarrierIn && succReady) {
	if (startDaisyBarrier) {
	    barrierCount = 1;
	    globalPutCount = putCount;
	    startDaisyBarrier = false;
	} else if (gotDaisyBarrierIn) {
	    barrierCount++;
	    globalPutCount += putCount;
	    gotDaisyBarrierIn = false;
	} else return;

	char *ptr = buffer;
	size_t len = 0;
	setKVSCmd(&ptr, &len, DAISY_BARRIER_IN);
	addKVSInt32(&ptr, &len, &barrierCount);
	addKVSInt32(&ptr, &len, &globalPutCount);
	sendKvstoSucc(buffer, len);
    }
}

/**
 * @brief Parse a KVS update message from ther provider
 *
 * Parse a KVS update message from the provider. Save all received key-value
 * pairs into the local KVS space. All get requests from the local PMI client
 * will be answered using the local KVS space. If we got all update message
 * we can release the waiting PMI client via barrier_out.
 *
 * If we are the last client in the chain, we need to tell the provider that the
 * update was successful completed.
 *
 * @param pmiLine The update message to parse
 *
 * @param lastUpdate Flag to indicate this message completes the update
 *
 * @param updateIdx Integer to identify the update phase
 *
 * @return No return value
 */
static void handleUpdate(char *pmiLine, bool lastUpdate, int updateIdx)
{
    char vname[PMI_KEYLEN_MAX];

    /* parse the update message */
    char *saveptr;
    const char delimiters[] =" \n";
    char *nextvalue = strtok_r(pmiLine, delimiters, &saveptr);
    while (nextvalue) {
	/* extract next key/value pair */
	char *value = strchr(nextvalue, '=') + 1;
	if (!value) {
	    elog("%s(r%i): invalid kvs update" " received\n", __func__, rank);
	    critErr();
	    return;
	}
	size_t len = MIN(strlen(nextvalue)-strlen(value)-1, sizeof(vname) - 1);
	memcpy(vname, nextvalue, len);
	vname[len] = '\0';

	/* save key/value to kvs */
	if (!kvs_set(myKVSname, vname, value)) {
	    elog("%s(r%i): error saving kvs update: kvsname:%s, key:%s,"
		    " value:%s\n", __func__, rank, myKVSname, vname, value);
	    critErr();
	    return;
	}
	nextvalue = strtok_r(NULL, delimiters, &saveptr);
    }
    updateMsgCount++;

    if (lastUpdate) {
	/* we got all update msg, so we can release the waiting PMI client */
	gotBarrierIn = false;
	PMI_send("cmd=barrier_out\n");

	/* if we are the last in the chain we acknowledge the provider */
	if (succtid == kvsProvTID) {
	    char *ptr = buffer;
	    size_t len = 0;

	    setKVSCmd(&ptr, &len, UPDATE_CACHE_FINISH);
	    addKVSInt32(&ptr, &len, &updateMsgCount);
	    addKVSInt32(&ptr, &len, &updateIdx);
	    sendKvstoProvider(buffer, len);
	}
	updateMsgCount = 0;
    }
}

/**
 * @brief Handle a PMI barrier_in request from the local PMI client
 *
 * The PMI client has to wait until all clients have entered barrier.
 * A barrier is typically used to syncronize the local KVS space.
 *
 * We can forwarder a pending daisy chain barrier_in message now.
 * If we are the client in the chain we have to start the daisy message.
 *
 * Also we can start updating the local KVS space with received KVS update
 * messages.
 *
 * @param msg Buffer containing the PMI barrier_in msg
 *
 * @return Always returns 0
 */
static int p_Barrier_In(char *msg)
{
    gotBarrierIn = true;

    /* if we are the first in chain, send starting barrier msg */
    if (!pmiRank) startDaisyBarrier = true;
    checkDaisyBarrier();

    /* update local KVS cache with buffered update */
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &uBufferList) {
	Update_Buffer_t *uBuf = list_entry(pos, Update_Buffer_t, next);
	if (uBuf->handled) continue;

	/* skip cmd and index*/
	char *ptr = uBuf->msg + UPDATE_HEAD;

	char pmiLine[PMIU_MAXLINE];
	getKVSString(&ptr, pmiLine, sizeof(pmiLine));

	handleUpdate(pmiLine, uBuf->lastUpdate, uBuf->updateIndex);
	uBuf->handled = true;

	if (uBuf->forwarded) deluBufferEntry(uBuf);
    }

    return 0;
}

void leaveKVS(void)
{
    if (kvsProvTID == -1) return;
    if (!initialized) return;

    /* inform the provider that we are leaving the KVS space */
    char *ptr = buffer;
    size_t len = 0;
    setKVSCmd(&ptr, &len, LEAVE);
    sendKvstoProvider(buffer, len);
}

/**
 * @brief Finalize the PMI usage
 *
 * Return PMI_FINALIZED to notice the forwarder that the child has
 * signed out from PMI. The forwarder will prepare for receiving
 * SIGCHLD and release the child by calling @ref ackFinalize().
 *
 * @return Returns PMI_FINALIZED
 */
static int p_Finalize(void)
{
    return PMI_FINALIZED;
}

/**
 * @brief Return the default KVS name
 *
 * Returns the default KVS name.
 *
 * @return Always returns 0
 */
static int p_Get_My_Kvsname(void)
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply), "cmd=my_kvsname kvsname=%s\n", myKVSname);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Abort the job
 *
 * Abort the current job. No PMI answer is required.
 *
 * @return Always returns 0
 */
static int p_Abort(void)
{
    elog("%s(r%i): aborting on users request\n", __func__, rank);
    terminateJob();

    return 0;
}

/**
 * @brief Creates a new KVS
 *
 * Not supported by hydra anymore. Seems nobody using it. Disabled to make new
 * KVS implementation more straighforward.
 *
 * Creates a new key value space.
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Create_Kvs(void)
{
    char kvsmsg[PMIU_MAXLINE], kvsname[PMI_KVSNAME_MAX];

    /* disable creation of new KVS */
    PMI_send("cmd=newkvs rc=-1\n");
    return 0;

    /* create new KVS name */
    snprintf(kvsname, sizeof(kvsname), "%s_%i\n", kvs_name_prefix, kvs_next++);

    /* create local KVS */
    if (!kvs_create(kvsname)) {
	elog("%s(r%i): create kvs %s failed\n", __func__, rank, kvsname);
	PMI_send("cmd=newkvs rc=-1\n");
	return 1;
    }

    /* return succes to client */
    snprintf(kvsmsg, sizeof(kvsmsg), "cmd=newkvs kvsname=%s\n",	kvsname);
    PMI_send(kvsmsg);
    return 0;
}

/**
 * @brief Delete a KVS
 *
 * Not supported by hydra anymore. Seems nobody using it. Disabled to make new
 * KVS implementation more straighforward.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Destroy_Kvs(char *msg)
{
    char kvsname[PMI_KVSNAME_MAX];

    /* disable it */
    PMI_send("cmd=kvs_destroyed rc=-1\n");
    return 0;

    /* get parameter from msg */
    getpmiv("kvsname", msg, kvsname, sizeof(kvsname));

    if (!kvsname[0]) {
	elog("%s(r%i): got invalid msg\n", __func__, rank);
	PMI_send("cmd=kvs_destroyed rc=-1\n");
	return 1;
    }

    /* destroy KVS */
    if (!kvs_destroy(kvsname)) {
	elog("%s(r%i): error destroying kvs %s\n", __func__, rank, kvsname);
	PMI_send("cmd=kvs_destroyed rc=-1\n");
	return 1;
    }

    /* return succes to client */
    PMI_send("cmd=kvs_destroyed rc=0\n");

    /* destroy kvs in provider */

    return 0;
}

/**
 * @brief Put a key/value pair into the KVS
 *
 * The key is save into the local KVS and instantly send to provider
 * to be saved in the global KVS.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Put(char *msg)
{
    char kvsname[PMI_KVSNAME_MAX], key[PMI_KEYLEN_MAX], value[PMI_VALLEN_MAX];
    char *ptr = buffer;
    size_t len = 0;

    getpmiv("kvsname", msg, kvsname, sizeof(kvsname));
    getpmiv("key", msg, key, sizeof(key));
    getpmiv("value", msg, value, sizeof(value));

    /* check msg */
    if (!kvsname[0] || !key[0] || !value[0]) {
	if (debug_kvs) elog( "%s(r%i): received invalid PMI put msg\n",
			     __func__, rank);
	PMI_send("cmd=put_result rc=-1 msg=error_invalid_put_msg\n");
	return 1;
    }

    putCount++;

    /* save to local KVS */
    if (!kvs_set(kvsname, key, value)) {
	if (debug_kvs) {
	    elog("%s(r%i): error while put key:%s value:%s to kvs:%s \n",
		 __func__, rank, key, value, kvsname);
	}
	PMI_send("cmd=put_result rc=-1 msg=error_in_kvs\n");
	return 1;
    }

    /* forward put request to the global KVS in provider */
    setKVSCmd(&ptr, &len, PUT);
    addKVSString(&ptr, &len, key);
    addKVSString(&ptr, &len, value);
    sendKvstoProvider(buffer, len);

    /* return succes to client */
    PMI_send("cmd=put_result rc=0\n");

    return 0;
}

/**
 * @brief The PMI client reads a key/value pair from local KVS
 *
 * Read a value from the local KVS.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Get(char *msg)
{
    char reply[PMIU_MAXLINE], kvsname[PMI_KVSNAME_MAX], key[PMI_KEYLEN_MAX];
    char *value;

    /* extract parameters */
    getpmiv("kvsname", msg, kvsname, sizeof(kvsname));
    getpmiv("key", msg, key, sizeof(key));

    /* check msg */
    if (!kvsname[0] || !key[0]) {
	if (debug_kvs) elog("%s(r%i): received invalid PMI get cmd\n",
			    __func__, rank);
	PMI_send("cmd=get_result rc=-1 msg=error_invalid_get_msg\n");
	return 1;
    }

    /* get value from KVS */
    value = kvs_get(kvsname, key);
    if (!value) {
	/* check for mvapich process mapping */
	char *env = getenv("__PMI_PROCESS_MAPPING");
	if (env && !strcmp(key, "PMI_process_mapping")) {
	    snprintf(reply, sizeof(reply), "cmd=get_result rc=%i value=%s\n",
		     PMI_SUCCESS, env);
	    PMI_send(reply);
	    return 0;
	}

	if (debug_kvs) {
	    elog("%s(r%i): get on non exsisting kvs key:%s\n", __func__,
		 rank, key);
	}
	snprintf(reply, sizeof(reply),
		 "cmd=get_result rc=%i msg=error_value_not_found\n", PMI_ERROR);
	PMI_send(reply);
	return 1;
    }

    /* send PMI to client */
    snprintf(reply, sizeof(reply), "cmd=get_result rc=%i value=%s\n",
	     PMI_SUCCESS, value);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Publish a service
 *
 * Make a service public even for processes which are not
 * connected over PMI.
 *
 * Not implemented (yet).
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Publish_Name(char *msg)
{
    char service[PMI_VALLEN_MAX], port[PMI_VALLEN_MAX];
    getpmiv("service", msg, service, sizeof(service));
    getpmiv("port", msg, port, sizeof(port));

    /* check msg */
    if (!port[0] || !service[0]) {
	elog("%s(r%i): received invalid publish_name msg\n", __func__, rank);
	return 1;
    }

    if (debug) elog("%s(r%i): received publish name request for service:%s, "
		    "port:%s\n", __func__, rank, service, port);

    PMI_send("cmd=publish_result info=not_implemented_yet\n");

    return 0;
}

/**
 * @brief Unpublish a service
 *
 * Not implemented (yet).
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Unpublish_Name(char *msg)
{
    char service[PMI_VALLEN_MAX];
    getpmiv("service", msg, service, sizeof(service));

    /* check msg*/
    if (!service[0]) {
	elog("%s(r%i): received invalid unpublish_name msg\n", __func__, rank);
    }

    if (debug) elog("%s(r%i): received unpublish name request for service:%s\n",
		    __func__, rank, service);
    PMI_send("cmd=unpublish_result info=not_implemented_yet\n");

    return 0;
}

/**
 * @brief Lookup a service name
 *
 * Not implemented (yet).
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Lookup_Name(char *msg)
{
    char service[PMI_VALLEN_MAX];
    getpmiv("service", msg, service, sizeof(service));

    /* check msg*/
    if (!service[0]) {
	elog("%s(r%i): received invalid lookup_name msg\n", __func__, rank);
    }

    if (debug) elog("%s(r%i): received lookup name request for service:%s\n",
		    __func__, rank, service);
    PMI_send("cmd=lookup_result info=not_implemented_yet\n");

    return 0;
}

/**
 * @brief Get a key-value pair by index
 *
 * Get a key-value pair by specific index from key value space.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_GetByIdx(char *msg)
{
    char reply[PMIU_MAXLINE];
    char idx[PMI_VALLEN_MAX], kvsname[PMI_KVSNAME_MAX];
    char *ret, name[PMI_KEYLEN_MAX];
    int index;

    getpmiv("idx", msg, idx, sizeof(msg));
    getpmiv("kvsname", msg, kvsname, sizeof(msg));

    /* check msg */
    if (!idx[0] || !kvsname[0]) {
	if (debug_kvs) elog("%s(r%i): received invalid PMI getbiyidx msg\n",
			    __func__, rank);
	PMI_send("getbyidx_results rc=-1 reason=invalid_getbyidx_msg\n");
	return 1;
    }

    index = atoi(idx);
    /* find and return the value */
    ret = kvs_getbyidx(kvsname, index);
    if (!ret) {
	PMI_send("getbyidx_results rc=-2 reason=no_more_keyvals\n");
	return 0;
    }

    char *value = strchr(ret, '=') + 1;
    if (!value) {
	elog("%s(r%i): error in local key value space\n", __func__, rank);
	return critErr();
    }
    size_t len = MIN(strlen(ret) - strlen(value) - 1, sizeof(name) - 1);
    memcpy(name, ret, len);
    name[len] = '\0';
    snprintf(reply, sizeof(reply),
	     "getbyidx_results rc=0 nextidx=%d key=%s val=%s\n",
	     ++index, name, value);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Init the local PMI communication
 *
 * Init the PMI communication, mainly to be sure both sides
 * are using the same protocol versions.
 *
 * The init process is monitored by the provider to make sure all PMI clients
 * are started successfully in time.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Init(char *msg)
{
    char reply[PMIU_MAXLINE], pmiversion[20], pmisubversion[20];

    getpmiv("pmi_version", msg, pmiversion, sizeof(pmiversion));
    getpmiv("pmi_subversion", msg, pmisubversion, sizeof(pmisubversion));

    /* check msg */
    if (!pmiversion[0] || !pmisubversion[0]) {
	elog("%s(r%i): received invalid PMI init cmd\n", __func__, rank);
	return critErr();
    }

    if (atoi(pmiversion) == PMI_VERSION
	&& atoi(pmisubversion) == PMI_SUBVERSION) {
	snprintf(reply, sizeof(reply), "cmd=response_to_init"
		 " pmi_version=%i pmi_subversion=%i rc=%i\n",
		 PMI_VERSION, PMI_SUBVERSION, PMI_SUCCESS);
	PMI_send(reply);
    } else {
	snprintf(reply, sizeof(reply), "cmd=response_to_init"
		 " pmi_version=%i pmi_subversion=%i rc=%i\n",
		 PMI_VERSION, PMI_SUBVERSION, PMI_ERROR);
	PMI_send(reply);
	elog("%s(r%i): unsupported PMI version received: version=%i,"
		" subversion=%i\n", __func__, rank, atoi(pmiversion),
		atoi(pmisubversion));
	return 1;
    }
    clientIsInitialized = true;

    if (!initToProvider) {
	if (psAccountSwitchAccounting)
	    psAccountSwitchAccounting(cTask->tid, false);

	/* tell provider that the PMI client was initialized */
	char *ptr = buffer;
	size_t len = 0;
	setKVSCmd(&ptr, &len, INIT);
	sendKvstoProvider(buffer, len);

	initToProvider = true;
    }

    return 0;
}

/**
 * @brief Get KVS maxes values
 *
 * Get the maximal size of the kvsname, keylen and values.
 *
 * @return Always returns 0
 */
static int p_Get_Maxes(void)
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply),
	     "cmd=maxes kvsname_max=%i keylen_max=%i vallen_max=%i\n",
	     PMI_KVSNAME_MAX, PMI_KEYLEN_MAX, PMI_VALLEN_MAX);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Intel MPI 3.0 Extension
 *
 * PMI extension in Intel MPI since version 3.0, just to recognize it.
 *
 * response msg put_ranks2hosts:
 *
 * 1. put_ranks2hosts <msglen> <num_of_hosts>
 *	msglen: the number of characters in the next msg + 1
 *	num_of_hosts: total number of the non-recurring
 *	    host names that are in the set
 *
 * 2. <hnlen> <hostname> <rank1,rank2,...,rankN,>
 *	hnlen: number of characters in the next <hostname> field
 *	hostname: the node name
 *	rank1,rank2,..,rankN: a comma separated list of ranks executed on the
 *	    <hostname> node; if the list is the last in the response, it must be
 *	    followed a blank space
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Get_Rank2Hosts(void)
{
    if (debug) elog("%s(r%i): got get_rank2hosts request\n", __func__, rank);
    PMI_send("cmd=put_ranks2hosts 0 0\n");

    return 0;
}

/**
 * @brief Authenticate the client
 *
 * Use a handshake to authenticate the PMI client. Note that it is not
 * mandatory for the PMI client to authenticate itself. Hydra continous even
 * with incorrect authentification.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_InitAck(char *msg)
{
    char *pmi_id, client_id[PMI_KEYLEN_MAX], reply[PMIU_MAXLINE];

    if (debug) elog("%s(r%i): received PMI initack msg:%s\n", __func__,
		    rank, msg);

    pmi_id = getenv("PMI_ID");
    if (!pmi_id) {
	elog("%s(r%i): no PMI_ID is set\n", __func__, rank);
	return critErr();
    }

    getpmiv("pmiid", msg, client_id, sizeof(client_id));

    if (!client_id[0]) {
	elog("%s(r%i): empty pmiid from PMI client\n", __func__, rank);
	return critErr();
    }

    if (strcmp(pmi_id, client_id)) {
	elog("%s(r%i): invalid pmi_id '%s' from PMI client should be '%s'\n",
	     __func__, rank, client_id, pmi_id);
	return critErr();
    }

    PMI_send("cmd=initack rc=0\n");
    snprintf(reply, sizeof(reply), "cmd=set size=%i\n", universe_size);
    PMI_send(reply);
    snprintf(reply, sizeof(reply), "cmd=set rank=%i\n", pmiRank);
    PMI_send(reply);
    snprintf(reply, sizeof(reply), "cmd=set debug=%i\n", debug);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Handle a execution problem message
 *
 * The execution problem message is sent BEFORE client actually
 * starts, so even before PMI init.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Execution_Problem(char *msg)
{
    char exec[PMI_VALLEN_MAX], reason[PMI_VALLEN_MAX];

    getpmiv("reason", msg, exec, sizeof(exec));
    getpmiv("exec", msg, reason, sizeof(reason));

    if (!exec[0] || !reason[0]) {
	elog("%s(r%i): received invalid PMI execution problem msg\n",
	     __func__, rank);
	return 1;
    }

    elog("%s(r%i): execution problem: exec=%s, reason=%s\n", __func__,
	 rank, exec, reason);

    critErr();
    return 0;
}

static int setPreputValues(void)
{
    char *env = getenv("__PMI_preput_num");
    int i, preNum;

    if (!env) return 0;

    preNum = atoi(env);

    for (i = 0; i < preNum; i++) {
	char *key, *value;
	snprintf(buffer, sizeof(buffer), "__PMI_preput_key_%i", i);
	key = getenv(buffer);
	if (!key) {
	    elog("%s(r%i): invalid preput key %i\n", __func__, rank, i);
	    return critErr();
	}

	snprintf(buffer, sizeof(buffer), "__PMI_preput_val_%i", i);
	value = getenv(buffer);
	if (!value) {
	    elog("%s(r%i): invalid preput value %i\n", __func__, rank, i);
	    return critErr();
	}

	if (!kvs_set(myKVSname, key, value)) {
	    elog("%s(r%i): error saving preput value kvsname:%s, key:%s,"
		    " value:%s\n", __func__, rank, myKVSname, key, value);
	    return critErr();
	}
    }

    return 0;
}

int pmi_init(int pmisocket, PStask_t *childTask)
{
#if DEBUG_ENV
    for (int i = 0; environ[i]; i++) mlog("%d: %s\n", i, environ[i++]);
#endif

    cTask = childTask;
    rank = cTask->rank;
    char *env = getenv("PMI_RANK");
    if (!env) {
	elog("%s(r%i): invalid PMI rank environment\n", __func__, rank);
	return 1;
    }
    pmiRank = atoi(env);
    pmisock = pmisocket;

    env = getenv("PMI_APPNUM");
    if (!env) {
	elog("%s(r%i): invalid PMI APPNUM environment\n", __func__, rank);
	return 1;
    }
    appnum = atoi(env);
    if (appnum < 0) {
	elog("%s(r%i): invalid PMI APPNUM parameter %s\n", __func__, rank, env);
	return 1;
    }

    if (pmisocket < 1) {
	elog("%s(r%i): invalid PMI socket %i\n", __func__, rank, pmisocket);
	return 1;
    }

    /* set debug mode */
    env = getenv("PMI_DEBUG");
    if (env && atoi(env) > 0) {
	debug_kvs = debug = true;
    } else {
	env = getenv("PMI_DEBUG_CLIENT");
	if (env) debug = (atoi(env) > 0);
    }
    env = getenv("PMI_DEBUG_KVS");
    if (env) debug_kvs = (atoi(env) > 0);
    if (debug) {
	elog("%s(r%i): new connection on %d\n", __func__, rank, pmisocket);
	elog("%s(r%i): debug(_kvs) %d/%d\n", __func__, rank, debug, debug_kvs);
	elog("%s(r%i): logger %s pmiRank %i spawned '%s'\n", __func__, rank,
	     PSC_printTID(cTask->loggertid), pmiRank, getenv("PMI_SPAWNED"));
    }

    if (initialized) return 0; // this is a re-connect

    INIT_LIST_HEAD(&uBufferList);

    /* set the PMI universe size */
    env = getenv("PMI_UNIVERSE_SIZE");
    if (env) {
	universe_size = atoi(env);
    } else {
	universe_size = 1;
    }

    /* set the name of the KVS space */
    env = getenv("PMI_KVS_TMP");
    if (env) {
	snprintf(kvs_name_prefix, sizeof(kvs_name_prefix), "kvs_%s", env);
    } else {
	strncpy(kvs_name_prefix, "kvs_root", sizeof(kvs_name_prefix) -1);
    }

    if (!PSI_logInitialized()) PSI_initLog(NULL);

    initialized = true;
    updateMsgCount = 0;

    /* set my KVS name */
    env = getenv("PMI_KVSNAME");
    if (env) {
	strcpy(myKVSname, env);
    } else {
	snprintf(myKVSname, sizeof(myKVSname), "%s_%i", kvs_name_prefix,
		 kvs_next++);
    }

    /* create local KVS space */
    if (!kvs_create(myKVSname)) {
	elog("%s(r%i): error creating local kvs\n", __func__, rank);
	return critErr();
    }

    /* join global KVS space */
    char *ptr = buffer;
    size_t len = 0;
    setKVSCmd(&ptr, &len, JOIN);
    addKVSString(&ptr, &len, myKVSname);
    addKVSInt32(&ptr, &len, &rank);
    addKVSInt32(&ptr, &len, &pmiRank);
    sendKvstoProvider(buffer, len);

    /* add preput values from PMI spawn */
    if (setPreputValues()) return 1;

    /* tell my PMI parent I am alive */
    if (getenv("PMI_SPAWNED")) {
	PStask_ID_t parent;
	int32_t res = 1;

	mdbg(PSPMI_LOG_VERBOSE, "PMI_SPAWNED is set, contact parents.\n");

	env = getenv("__PMI_SPAWN_PARENT");
	if (!env) {
	    elog("%s(r%i): error getting my PMI parent\n", __func__, rank);
	    return critErr();
	}
	parent = atoi(env);

	ptr = buffer;
	len = 0;
	setKVSCmd(&ptr, &len, CHILD_SPAWN_RES);
	addKVSInt32(&ptr, &len, &res);
	addKVSInt32(&ptr, &len, &rank);
	addKVSInt32(&ptr, &len, &pmiRank);
	sendKvsMsg(__func__, parent, buffer, len);
    }

    /* set default spawn handler */
    mdbg(PSPMI_LOG_VERBOSE, "Setting PMI default fill spawn task function to"
	 " fillWithMpiexec()\n");
    if (!fillTaskFunction) psPmiResetFillSpawnTaskFunction();

    return 0;
}

void ackFinalize(void)
{
    PMI_send("cmd=finalize_ack\n");
    /* prepare for re-connecting client */
    clientIsInitialized = false;
}

/**
 * @brief Buffer an update message until both the local PMI client and our
 * successor are ready
 *
 * @param msg The message to buffer
 *
 * @param msgLen The len of the message
 *
 * @param lastUpdate Flag to indicate this message completes the update
 *
 * @param strLen The length of the update payload
 *
 * @return No return value
 */
static void bufferCacheUpdate(char *msg, size_t msgLen,
			      bool lastUpdate, int updateIndex)
{
    Update_Buffer_t *uBuf;

    uBuf = (Update_Buffer_t *) umalloc(sizeof(Update_Buffer_t));
    uBuf->msg = umalloc(msgLen);

    memcpy(uBuf->msg, msg, msgLen);
    uBuf->len = msgLen;
    uBuf->forwarded = succReady;
    uBuf->handled = gotBarrierIn;
    uBuf->lastUpdate = lastUpdate;
    uBuf->updateIndex = updateIndex;

    list_add_tail(&(uBuf->next), &uBufferList);
}

/**
 * @brief Handle a KVS update message from the provider
 *
 * @param msg The message to handle
 *
 * @param ptr Pointer to the update payload
 *
 * @param lastUpdate Flag to indicate this message completes the update
 *
 * @return No return value
 */
static void handleKVScacheUpdate(PSLog_Msg_t *msg, char *ptr, bool lastUpdate)
{
    int updateIndex = getKVSInt32(&ptr);

    /* get the PMI update message */
    char pmiLine[PMIU_MAXLINE];
    ssize_t len = getKVSString(&ptr, pmiLine, sizeof(pmiLine));
    if (len < 0) {
	elog("%s(%i): invalid update len %zi index %i last %i\n", __func__,
	     rank, len, updateIndex, lastUpdate);
	critErr();
	return;
    }
    size_t msgSize = msg->header.len - PSLog_headerSize;
    ssize_t pLen = msgSize - (UPDATE_HEAD) - sizeof(uint16_t);

    /* sanity check */
    if (strlen(pmiLine) != (size_t)len || len != pLen) {
	elog("%s(r%i): invalid update msg len:%zu strlen:%zu bufLen:%zi\n",
	     __func__, rank, len, strlen(pmiLine), pLen);
	critErr();
	return;
    }
    /* sanity check */
    if (lastUpdate && !gotBarrierIn) {
	elog("%s:(r%i): got last update message, but I have no barrier_in\n",
	     __func__, rank);
	critErr();
	return;
    }

    /* we might need to buffer the message for later */
    if (!succReady || !gotBarrierIn) {
	bufferCacheUpdate(msg->buf, msgSize, lastUpdate, updateIndex);
    }

    /* forward to successor */ // uBufferList
    if (succReady && succtid != kvsProvTID) sendKvstoSucc(msg->buf, msgSize);

    /* update local KVS if barrier_in from local PMI client already received */
    if (gotBarrierIn) handleUpdate(pmiLine, lastUpdate, updateIndex);

    if (lastUpdate && psAccountSwitchAccounting) {
	psAccountSwitchAccounting(cTask->tid, true);
    }
}

/**
 * @brief Hanlde a KVS barrier out message
 *
 * Release the waiting PMI client from the barrier.
 *
 * @return No return value
 */
static void handleDaisyBarrierOut(PSLog_Msg_t *msg)
{
    /* forward to next client */
    if (succtid != kvsProvTID) sendKvstoSucc(msg->buf, sizeof(uint8_t));

    /* Release local PMI client from barrier */
    gotBarrierIn = false;
    PMI_send("cmd=barrier_out\n");
}

/**
 * @brief Set a new daisy barrier
 *
 * @return No return value
 */
static void handleDaisyBarrierIn(char *ptr)
{
    if (predtid == kvsProvTID) {
	elog("%s(r%i): received daisy_barrier_in from provider\n", __func__,
	     rank);
	return;
    }

    barrierCount = getKVSInt32(&ptr);
    globalPutCount = getKVSInt32(&ptr);
    gotDaisyBarrierIn = true;
    checkDaisyBarrier();
}

/**
 * @brief Our successor is now ready to receive messages
 *
 * The successor finished its initialize phase with the provider and
 * is now ready to receive PMI messages from us. We can now forward
 * all buffered barrier/update messages.
 *
 * @return No return value
 */
static void handleSuccReady(char *mbuf)
{
    succtid = getKVSInt32(&mbuf);
    //elog("s(r%i): succ:%i pmiRank:%i providertid:%i\n", rank, succtid,
    //	    pmiRank, kvsProvTID);
    succReady = true;
    checkDaisyBarrier();

    /* now forward buffered messages */
    list_t *ub, *tmp;
    list_for_each_safe(ub, tmp, &uBufferList) {
	Update_Buffer_t *uBuf = list_entry(ub, Update_Buffer_t, next);
	if (uBuf->forwarded) continue;

	if (succtid != kvsProvTID) sendKvstoSucc(uBuf->msg, uBuf->len);

	uBuf->forwarded = true;

	if (uBuf->handled) deluBufferEntry(uBuf);
    }
}


/**
 * @brief Handle a CC_ERROR message
 *
 * Since the actual KVS lives within its own service process, sending
 * changes to the key-value store might fail. This function handles
 * the resulting CC_ERROR messages to be passed in @a msg.
 *
 * @param msg Message to handle
 *
 * @return Return true if the message was fully handled, i.e. was sent
 * by the KVS provider, or false otherwise
 */
static bool msgCCError(DDBufferMsg_t *msg)
{
    if (msg->header.sender == kvsProvTID) return true;

    return false;
}

void setKVSProviderTID(PStask_ID_t tid)
{
    kvsProvTID = tid;
}

void setKVSProviderSock(int fd)
{
    kvsProvSock = fd;
}

/* ************************************************************************* *
 *                           MPI Spawn Handling                              *
 * ************************************************************************* */

/**
 * @brief Extracts the arguments from PMI spawn request
 *
 * @param msg Buffer containing the PMI spawn message
 *
 * @return Return a string vector holding the extracted arguments on
 * success or NULL on error
 */
static strv_t getSpawnArgs(char *msg)
{
    char numArgsStr[32];
    if (!getpmiv("argcnt", msg, numArgsStr, sizeof(numArgsStr))) {
	mlog("%s(r%i): missing argc argument\n", __func__, rank);
	return false;
    }

    int numArgs = atoi(numArgsStr);
    if (numArgs > PMI_SPAWN_MAX_ARGUMENTS) {
	mlog("%s(r%i): too many arguments (%i)\n", __func__, rank, numArgs);
	return NULL;
    } else if (numArgs < 0) {
	mlog("%s(r%i): invalid argument count (%i)\n", __func__, rank, numArgs);
	return NULL;
    }

    /* add the executable as argv[0] */
    char *execname = getpmivm("execname", msg);
    if (!execname) {
	mlog("%s(r%i): invalid executable name\n", __func__, rank);
	return NULL;
    }
    strv_t argV = strvNew(NULL);
    strvLink(argV, execname);

    /* add additional arguments */
    for (int i = 1; i <= numArgs; i++) {
	char *nextval;
	snprintf(buffer, sizeof(buffer), "arg%i", i);
	nextval = getpmivm(buffer, msg);
	if (nextval) {
	    strvLink(argV, nextval);
	} else {
	    mlog("%s(r%i): extracting arguments failed\n", __func__, rank);
	    strvDestroy(argV);
	    return NULL;
	}
    }

    return argV;
}

/**
 * @brief Extract key-value pairs from PMI spawn request
 *
 * @param msg Buffer containing the PMI spawn message
 *
 * @param name Identifier for the key-value pairs to extract
 *
 * @param kvpc Where to store the number key-value pairs
 *
 * @param kvpv Where to store the array of key-value pairs
 *
 * @return Returns true on success, false on error
 */
static bool getSpawnKVPs(char *msg, char *name, int *kvpc, KVP_t **kvpv)
{
    char numKVP[50];
    int count, i;

    snprintf(buffer, sizeof(buffer), "%s_num", name);
    if (!getpmiv(buffer, msg, numKVP, sizeof(numKVP))) {
	mlog("%s(r%i): missing %s count\n", __func__, rank, name);
	return false;
    }
    *kvpc = atoi(numKVP);

    if (!*kvpc) return true;

    *kvpv = umalloc(*kvpc * sizeof(KVP_t));

    for (i = 0; i < *kvpc; i++) {
	char nextkey[PMI_KEYLEN_MAX], nextvalue[PMI_VALLEN_MAX];
	snprintf(buffer, sizeof(buffer), "%s_key_%i", name, i);
	if (!getpmiv(buffer, msg, nextkey, sizeof(nextkey))) {
	    mlog("%s(r%i): invalid %s key %s\n", __func__, rank, name, buffer);
	    goto kvp_error;
	}

	snprintf(buffer, sizeof(buffer), "%s_val_%i", name, i);
	if (!getpmiv(buffer, msg, nextvalue, sizeof(nextvalue))) {
	    mlog("%s(r%i): invalid %s val %s\n", __func__, rank, name, buffer);
	    goto kvp_error;
	}

	(*kvpv)[i].key = ustrdup(nextkey);
	(*kvpv)[i].value = ustrdup(nextvalue);
    }
    return true;

kvp_error:
    count = i;
    for (i = 0; i < count; i++) {
	ufree((*kvpv)[i].key);
	ufree((*kvpv)[i].value);
    }
    ufree(*kvpv);
    return false;
}

/**
 * @brief Parse spawn request messages
 *
 * @param msg Buffer containing the PMI spawn request message
 *
 * @param spawn Pointer to the struct to be filled with spawn data
 *
 * @return Returns true on success, false on error
 */
static bool parseSpawnReq(char *msg, SingleSpawn_t *spawn)
{
    const char delm[] = "\n";
    char *tmpStr;

    if (!msg) return false;

    setPMIDelim(delm);

    /* get the number of processes to spawn */
    tmpStr = getpmivm("nprocs", msg);
    if (!tmpStr) {
	mlog("%s(r%i): getting number of processes to spawn failed\n",
	     __func__, rank);
	goto parse_error;
    }
    spawn->np = atoi(tmpStr);
    ufree(tmpStr);

    /* setup argv for processes to spawn */
    spawn->argV = getSpawnArgs(msg);
    if (!spawn->argV) goto parse_error;

    /* extract preput keys and values */
    if (!getSpawnKVPs(msg, "preput", &spawn->preputc, &spawn->preputv)) {
	goto parse_error;
    }

    /* extract info keys and values */
    if (!getSpawnKVPs(msg, "info", &spawn->infoc, &spawn->infov)) {
	goto parse_error;
    }

    setPMIDelim(NULL);
    return true;

parse_error:
    setPMIDelim(NULL);
    return false;
}

/**
 * @brief Spawn one or more processes
 *
 * We first need the next rank for the new service process to
 * start. Only the logger knows that. So we ask it and buffer
 * the spawn request to wait for an answer.
 *
 * @param req Spawn request data structure
 *
 * @return Returns true on success and false on error
 */
static bool doSpawn(SpawnRequest_t *req)
{
    if (pendSpawn) {
	mlog("%s(r%i): anoter spawn is pending\n", __func__, rank);
	return false;
    }

    pendSpawn = copySpawnRequest(req);

    mlog("%s(r%i): trying to do %d spawns\n", __func__, rank, pendSpawn->num);

    /* get next service rank from logger */
    if (PSLog_write(cTask->loggertid, SERV_RNK, NULL, 0) < 0) {
	mlog("%s(r%i): Writing to logger failed.\n", __func__, rank);
	return false;
    }

    return true;
}

/**
 * @brief Spawn one or more processes
 *
 * Parses the spawn message and calls doSpawn().
 *
 * In case of spawn_multi, all spawn messages are collected here
 * and doSpawn() is called after the collection is completed.
 *
 * @param msg Buffer containing the PMI spawn message
 *
 * @return Returns 0 for success, 1 on normal error, 2 on critical
 * error, and 3 on fatal error
 */
static int handleSpawnRequest(char *msg)
{
    char buf[50];
    int totSpawns, spawnsSoFar;
    bool ret;

    static int s_total = 0;
    static int s_count = 0;

    if (!getpmiv("totspawns", msg, buf, sizeof(buf))) {
	mlog("%s(r%i): invalid totspawns argument\n", __func__, rank);
	return 1;
    }
    totSpawns = atoi(buf);

    if (!getpmiv("spawnssofar", msg, buf, sizeof(buf))) {
	mlog("%s(r%i): invalid spawnssofar argument\n", __func__, rank);
	return 1;
    }
    spawnsSoFar = atoi(buf);

    if (spawnsSoFar == 1) {
	s_total = totSpawns;

	/* check if spawn buffer is already in use */
	if (spawnBuffer) {
	    mlog("%s(r%i): spawn buffer should be empty, another spawn in"
		 " progress?\n", __func__, rank);
	    return 2;
	}

	/* create spawn buffer */
	spawnBuffer = initSpawnRequest(s_total);
	if (!spawnBuffer) {
	    mlog("%s(r%i): out of memory\n", __func__, rank);
	    return 3;
	}
    } else if (s_total != totSpawns) {
	mlog("%s(r%i): totalspawns argument does not match previous message\n",
	     __func__, rank);
	return 2;
    }

    if (debug) elog("%s(r%i): Adding spawn %d/%d to buffer\n", __func__, rank,
		    spawnsSoFar, totSpawns);

    if (!parseSpawnReq(msg, &(spawnBuffer->spawns[s_count]))) {
	mlog("%s(r%i): failed to parse spawn message '%s'\n",  __func__, rank,
	     msg);
	return 1;
    }

    if (!spawnBuffer->spawns[s_count].np) {
	mlog("%s(r%i): spawn %d/%d has (np == 0) set, cancel spawning.\n",
	     __func__, rank, s_count, spawnBuffer->num);
	elog("Rank %i: Spawn %d/%d has (np == 0) set, cancel spawning.\n",
	     rank, s_count, spawnBuffer->num);
    }

    s_count++;

    if (s_count < s_total) {
	/* another part of the multi spawn request is missing */
	return 0;
    }

    /* collection complete */
    s_total = 0;
    s_count = 0;

    /* do spawn */
    ret = doSpawn(spawnBuffer);

    freeSpawnRequest(spawnBuffer);
    spawnBuffer = NULL;

    if (!ret) {
	mlog("%s(r%i): doSpawn() failed\n",  __func__, rank);
	return 1;
    }

    /* wait for logger to answer */
    return 0;
}

/**
 * @brief Spawn one or more processes
 *
 * This function is only a wrapper around handleSpawnRequest to unify
 * error handling.
 *
 * @param msg Buffer containing the PMI spawn message
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Spawn(char *msg)
{
    int ret;

    ret = handleSpawnRequest(msg);

    switch(ret) {
	case 1:
	    PMI_send("cmd=spawn_result rc=-1\n");
	    return 1;
	case 2:
	    PMI_send("cmd=spawn_result rc=-1\n");
	    critErr();
	    return 1;
	case 3:
	    PMI_send("cmd=spawn_result rc=-1\n");
	    critErr();
	    exit(1);
    }

    return 0;
}

/* *
 * @brief TODO soft spawn
 *
static int getSoftArgList(char *soft, int **softList)
{
    const char delimiters[] =", \n";
    char *triplet, *saveptr;
    int start, end, mult, tcount, ecount = 0;

    triplet = strtok_r(soft, delimiters, &saveptr);

    while (triplet != NULL) {
	tcount = sscanf(triplet, "%i:%i:%i", &start, &end, &mult);

	switch (tcount) {
	    case 1:
		/ add start /

	    case 2:
		/ add range start - end /

	    case 3:
		/ add start + mult until end /

	    default:
		/ error /
		return 0;
	}

	triplet = strtok_r(NULL, delimiters, &saveptr);
    }

    / sort the array /
    //qsort(sList, ecount, sizeof(int), compInt);

    return ecount;
}
*/

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
static void addPreputToEnv(int preputc, KVP_t *preputv, env_t env)
{
    snprintf(buffer, sizeof(buffer), "__PMI_preput_num=%i", preputc);
    envAdd(env, buffer);

    for (int i = 0; i < preputc; i++) {
	snprintf(buffer, sizeof(buffer), "__PMI_preput_key_%i", i);
	char *tmpStr = PSC_concat(buffer, "=",  preputv[i].key);
	envPut(env, tmpStr);

	snprintf(buffer, sizeof(buffer), "__PMI_preput_val_%i", i);
	tmpStr = PSC_concat(buffer, "=",  preputv[i].value);
	envPut(env, tmpStr);
    }
}

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

    /* put preput key-value-pairs into environment
     *
     * We will transport this kv-pairs using the spawn environment. When
     * our children are starting the pmi_init() call will add them to their
     * local KVS.
     *
     * Only the values of the first single spawn are used. */
    SingleSpawn_t *spawn = &(req->spawns[0]);

    addPreputToEnv(spawn->preputc, spawn->preputv, task->env);

    /* build arguments:
     * mpiexec -u <UNIVERSE_SIZE> -np <NP> -d <WDIR> -p <PATH> \
     *  --nodetype=<NODETYPE> --tpp=<TPP> <BINARY> ... */
    strv_t args = strvNew(NULL);

    char *tmpStr = getenv("__PSI_MPIEXEC_KVSPROVIDER");
    if (tmpStr) {
	strvAdd(args, tmpStr);
    } else {
	strvAdd(args, PKGLIBEXECDIR "/kvsprovider");
    }
    strvAdd(args, "-u");

    snprintf(buffer, sizeof(buffer), "%d", usize);
    strvAdd(args, buffer);

    for (int i = 0; i < req->num; i++) {
	spawn = &(req->spawns[i]);

	/* add separating colon */
	if (i) strvAdd(args, ":");

	/* set the number of processes to spawn */
	strvAdd(args, "-np");
	snprintf(buffer, sizeof(buffer), "%d", spawn->np);
	strvAdd(args, buffer);

	/* extract info values and keys
	 *
	 * These info variables are implementation dependend and can
	 * be used for e.g. process placement. All unsupported values
	 * will be silently ignored.
	 *
	 * ParaStation will support:
	 *
	 *  - wdir: The working directory of the spawned processes
	 *  - arch/nodetype: The type of nodes to be used
	 *  - path: The directory were to search for the executable
	 *  - tpp: Threads per process
	 *  - parricide: Flag to not kill process upon relative's unexpected
	 *               death.
	 *
	 * TODO:
	 *
	 *  - soft:
	 *	    - if not all processes could be started the spawn is still
	 *		considered successful
	 *	    - ignore negative values and >maxproc
	 *	    - format = list of triplets
	 *		+ list 1,3,4,5,8
	 *		+ triplets a:b:c where a:b range c= multiplicator
	 *		+ 0:8:2 means 0,2,4,6,8
	 *		+ 0:8:2,7 means 0,2,4,6,7,8
	 *		+ 0:2 means 0,1,2
	 */
	for (int j = 0; j < spawn->infoc; j++) {
	    KVP_t *info = &(spawn->infov[j]);

	    if (!strcmp(info->key, "wdir")) {
		strvAdd(args, "-d");
		strvAdd(args, info->value);
	    }
	    if (!strcmp(info->key, "tpp")) {
		size_t len = strlen(info->value) + 7;
		tmpStr = umalloc(len);
		snprintf(tmpStr, len, "--tpp=%s", info->value);
		strvLink(args, tmpStr);
	    }
	    if (!strcmp(info->key, "nodetype") || !strcmp(info->key, "arch")) {
		size_t len = strlen(info->value) + 12;
		tmpStr = umalloc(len);
		snprintf(tmpStr, len, "--nodetype=%s", info->value);
		strvLink(args, tmpStr);
	    }
	    if (!strcmp(info->key, "path")) {
		strvAdd(args, "-p");
		strvAdd(args, info->value);
	    }

	    /* TODO soft spawn
	    if (!strcmp(info->key, "soft")) {
		char *soft;
		int *count, *sList;

		soft = info->value;
		sList = getSoftArgList(soft, &count);

		ufree(soft);
	    }
	    */

	    if (!strcmp(info->key, "parricide")) {
		if (!strcmp(info->value, "disabled")) {
		    noParricide = true;
		} else if (!strcmp(info->value, "enabled")) {
		    noParricide = false;
		}
	    }
	}

	/* add binary and argument from spawn request */
	strvAppend(args, spawn->argV);
    }
    task->argV = args;

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
 * @brief Parse the spawn request and start a new service process
 *
 * The service process will spawn itself again to start a spawner process
 * which will then spawn the requested executable. The first service process
 * will then turn into a new KVS provider for the new spawned PMI group.
 *
 * The new spawned children will have their own MPI_COMM_WORLD and
 * therefore a separate KVS, separate PMI barriers and a separate
 * daisy chain to communicate.
 *
 * @param req The spawn request from the MPI client
 *
 * @param universeSize MPI universe size
 *
 * @param serviceRank The next valid rank for a new service process
 *
 * @param totalProcs Pointer where to store the total number of
 * processes to be spawned
 *
 * @return Returns true on success and false on error
 */
static bool tryPMISpawn(SpawnRequest_t *req, int universeSize,
			int serviceRank, int *totalProcs)
{
    if (!req) {
	mlog("%s: no spawn request (THIS SHOULD NEVER HAPPEN!!!)\n", __func__);
	return false;
    }

    if (!cTask) {
	mlog("%s: cannot find my child's task structure\n", __func__);
	return false;
    }

    PStask_t *task = initSpawnTask(cTask, spawnEnvFilter);
    if (!task) {
	mlog("%s: cannot create a new task\n", __func__);
	return false;
    }

    task->rank = serviceRank;

    /* calc totalProcs */
    *totalProcs = 0;
    for (int i = 0; i < req->num; i++) {
	*totalProcs += req->spawns[i].np;
    }

    /* interchangable function to fill actual spawn command into task */
    int rc = fillTaskFunction(req, universeSize, task);

    if (rc == -1) {
	/* function to fill the spawn task tells us not to be responsible */
	mlog("%s(r%i): Falling back to default PMI fill spawn function.\n",
	     __func__, rank);
	rc = fillWithMpiexec(req, universeSize, task);
    }

    if (rc != 1) {
	elog("Error with spawning processes.\n");
	mlog("%s(r%i): Error in PMI fill spawn function.\n", __func__, rank);
	PStask_delete(task);
	return 0;
    }

    /* add additional env vars */
    snprintf(buffer, sizeof(buffer), "PMI_KVS_TMP=pshost_%i_%i",
	     PSC_getMyTID(), kvs_next++);  /* setup new KVS name */
    envAdd(task->env, buffer);
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    snprintf(buffer, sizeof(buffer), "__PMI_SPAWN_PARENT=%i", PSC_getMyTID());
    envAdd(task->env, buffer);
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    envAdd(task->env, "PMI_SPAWNED=1");

    snprintf(buffer, sizeof(buffer), "PMI_SIZE=%d", *totalProcs);
    envAdd(task->env, buffer);
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    snprintf(buffer, sizeof(buffer), "__PMI_NO_PARRICIDE=%i",task->noParricide);
    envAdd(task->env, buffer);
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    if (debug) {
	elog("%s(r%i): Executing '", __func__, rank);
	for (char **a = strvGetArray(task->argV); a && *a; a++) elog(" %s", *a);
	elog("'\n");
    }
    mlog("%s(r%i): Executing '", __func__, rank);
    for (char **a = strvGetArray(task->argV); a && *a; a++) mlog(" %s", *a);
    mlog("'\n");

#if 0
    mlog("Task environment:\n");
    int cnt = 0;
    for (char **e = envGetArray(task->env); e && *e; e++, cnt++) {
	mlog(" %d: %s\n", cnt, *a);
    }
#endif

    PSnodes_ID_t dest = PSC_getMyID();
    int num = PSI_sendSpawnReq(task, &dest, 1);

    PStask_delete(task);

    return num == 1;
}

/**
 * @brief Extract the next service rank and try to continue spawning
 *
 * @param msg Logger message to handle
 *
 * @return Return true if the message was consumed or false to pass it
 * to the next handler
 */
static bool handleServiceInfo(PSLog_Msg_t *msg)
{
    int totalProcs, serviceRank = *(int32_t *)msg->buf;

    /* message might be for other handler (e.g. pspmix) */
    if (!pendSpawn) return false;

    /* try to do the spawn */
    if (tryPMISpawn(pendSpawn, universe_size, serviceRank, &totalProcs)) {
	/* reset tracking */
	spawnChildSuccess = 0;
	spawnChildCount = totalProcs;
	pendReq = true;
    } else {
	PMI_send("cmd=spawn_result rc=-1\n");
    }

    /* cleanup */
    freeSpawnRequest(pendSpawn);
    pendSpawn = NULL;

    return true;
}

/**
 * @brief Handle a spawn result message from my children
 *
 * New spawned processes will send this success message when pmi_init() is
 * called in their forwarder.
 *
 * @param msg Message to handle
 *
 * @return No return value
 */
static void handleChildSpawnRes(PSLog_Msg_t *msg, char *ptr)
{
    int cRank, cPmiRank;

    spawnChildSuccess++;

    if (!getKVSInt32(&ptr)) {
	mlog("%s(r%i): spawning processes failed\n", __func__, rank);
	elog("Rank %i: Spawning processes failed\n", rank);

	spawnChildSuccess = 0;
	PMI_send("cmd=spawn_result rc=-1\n");
	return;
    }

    cRank = getKVSInt32(&ptr);
    cPmiRank = getKVSInt32(&ptr);

    if (debug) mlog("%s(r%i): success from %s, rank %i pmiRank %i\n", __func__,
		    rank, PSC_printTID(msg->sender), cRank, cPmiRank);

    if (spawnChildSuccess == spawnChildCount) {
	spawnChildSuccess = spawnChildCount = 0;
	PMI_send("cmd=spawn_result rc=0\n");
    }
}

/**
 * @brief Handle a KVS message
 *
 * @param msg Message to handle
 *
 * @return No return value
 */
static void handleKVSMessage(PSLog_Msg_t *msg)
{
    char *ptr = msg->buf;

    /* handle KVS messages, extract cmd from msg */
    uint8_t cmd = getKVSCmd(&ptr);

    if (debug_kvs) {
	elog("%s(r%i): cmd %s\n", __func__, rank, PSKVScmdToString(cmd));
    }

    switch(cmd) {
	case NOT_AVAILABLE:
	    elog("%s(r%i): global KVS is not available, exiting\n",
		 __func__, rank);
	    critErr();
	    break;
	case UPDATE_CACHE:
	    handleKVScacheUpdate(msg, ptr, false);
	    break;
	case UPDATE_CACHE_FINISH:
	    handleKVScacheUpdate(msg, ptr, true);
	    break;
	case DAISY_SUCC_READY:
	    handleSuccReady(ptr);
	    break;
	case DAISY_BARRIER_IN:
	    handleDaisyBarrierIn(ptr);
	    break;
	case DAISY_BARRIER_OUT:
	    handleDaisyBarrierOut(msg);
	    break;
	case CHILD_SPAWN_RES:
	    handleChildSpawnRes(msg, ptr);
	    break;
	default:
	    elog("%s(r%i): got unknown KVS msg %s:%i from %s\n", __func__, rank,
		 PSKVScmdToString(cmd), cmd, PSC_printTID(msg->header.sender));
	    critErr();
    }
}

/**
 * @brief Extract the PMI command
 *
 * Parse a PMI message and return the command.
 *
 * @param msg Message to parse
 *
 * @param cmdbuf Buffer which receives the extracted command
 *
 * @param bufsize Size of @ref cmdbuf
 *
 * @return Returns true for success, false on errors
 */
static bool extractPMIcmd(char *msg, char *cmdbuf, size_t bufsize)
{
    const char delimiters[] =" \n";

    if (!msg || strlen(msg) < 5) {
	elog("%s(r%i): invalid PMI msg '%s' received\n", __func__, rank, msg);
	return !critErr();
    }

    char *msgCopy = ustrdup(msg);
    char *saveptr;
    char *cmd = strtok_r(msgCopy, delimiters, &saveptr);

    while (cmd) {
	size_t offset = 0;
	if (!strncmp(cmd, "cmd=", 4)) offset = 4;
	if (!strncmp(cmd, "mcmd=", 5)) offset = 5;
	if (offset) {
	    cmd += offset;
	    size_t len = MIN(strlen(cmd), bufsize - 1);
	    memcpy(cmdbuf, cmd, len);
	    cmdbuf[len] = '\0';
	    ufree(msgCopy);
	    return true;
	}
	cmd = strtok_r(NULL, delimiters, &saveptr);
    }

    ufree(msgCopy);
    return false;
}

static const struct {
    char *Name;
    int (*fpFunc)(char *msg);
}  pmi_commands[] = {
    { "put",                p_Put },
    { "get",                p_Get },
    { "barrier_in",         p_Barrier_In },
    { "init",               p_Init },
    { "spawn",              p_Spawn },
    { "get_universe_size",  p_Get_Universe_Size },
    { "initack",            p_InitAck },
    { "lookup_name",        p_Lookup_Name },
    { "execution_problem",  p_Execution_Problem },
    { "getbyidx",           p_GetByIdx },
    { "publish_name",       p_Publish_Name },
    { "unpublish_name",     p_Unpublish_Name },
    { "destroy_kvs",        p_Destroy_Kvs },
    { NULL,                 NULL }
};

static const struct {
    char *Name;
    int (*fpFunc)(void);
} pmi_short_commands[] = {
    { "get_maxes",       p_Get_Maxes },
    { "get_appnum",      p_Get_Appnum },
    { "finalize",        p_Finalize },
    { "get_my_kvsname",  p_Get_My_Kvsname },
    { "get_ranks2hosts", p_Get_Rank2Hosts },
    { "create_kvs",      p_Create_Kvs },
    { "abort",		 p_Abort },
    { NULL,              NULL }
};

int handlePMIclientMsg(char *msg)
{
    if (!initialized) {
	elog("%s(r%i): you must call pmi_init first, msg '%s'\n", __func__,
	     rank, msg);
	return critErr();
    }

    if (!msg) {
	elog("%s(r%i): invalid PMI msg\n", __func__, rank);
	return critErr();
    }

    char cmd[PMI_VALLEN_MAX];
    if (!extractPMIcmd(msg, cmd, sizeof(cmd)) || strlen(cmd) <2) {
	elog("%s(r%i): invalid PMI cmd received, msg was '%s'\n", __func__,
	     rank, msg);
	return critErr();
    }

    if (debug) elog("%s(r%i): got '%s'\n", __func__, rank, msg);

    /* find PMI cmd */
    for (int i = 0; pmi_commands[i].Name; i++) {
	if (!strcmp(cmd, pmi_commands[i].Name)) {
	    return pmi_commands[i].fpFunc(msg);
	}
    }

    /* find short PMI cmd */
    for (int i = 0; pmi_short_commands[i].Name; i++) {
	if (!strcmp(cmd, pmi_short_commands[i].Name)) {
	    return pmi_short_commands[i].fpFunc();
	}
    }

    /* cmd not found */
    char reply[PMIU_MAXLINE];

    elog("%s(r%i): unsupported PMI cmd received '%s'\n", __func__, rank, cmd);

    snprintf(reply, sizeof(reply), "cmd=%.512s rc=%i info=not_supported_cmd\n",
	     cmd, PMI_ERROR);
    PMI_send(reply);

    return critErr();
}

static void handleServiceExit(PSLog_Msg_t *msg)
{
    if (kvsProvSock == -1) return;

    /* closing of the fd will trigger the kvsprovider to exit */
    close(kvsProvSock);
    kvsProvSock = -1;
}

/**
 * @brief Handle a KVS message
 *
 * Handle the KVS message @a msg within the forwarder. The message is
 * received from the job's logger within a (pslog) message of type
 * PSP_CC_MSG.
 *
 * @param msg KVS message to handle in a pslog container
 *
 * @return If the message is fully handled, true is returned; or false
 * if further handlers shall inspect this message
 */
static bool msgCC(DDBufferMsg_t *msg)
{
    PSLog_Msg_t *lmsg = (PSLog_Msg_t *)msg;
    switch (lmsg->type) {
	case KVS:
	    handleKVSMessage(lmsg);
	    return true;
	case SERV_RNK:
	    return handleServiceInfo(lmsg);
	case SERV_EXT:
	    handleServiceExit(lmsg);
	    return true;
	default:
	    return false; // pass message to next handler if any
    }
}


void psPmiSetFillSpawnTaskFunction(fillerFunc_t spawnFunc)
{
    mdbg(PSPMI_LOG_VERBOSE, "Set specific PMI fill spawn task function\n");
    fillTaskFunction = spawnFunc;
}

void psPmiResetFillSpawnTaskFunction(void)
{
    mdbg(PSPMI_LOG_VERBOSE, "Reset PMI fill spawn task function\n");
    fillTaskFunction = fillWithMpiexec;
}

static int setupMsgHandlers(void *data)
{
    /* initialize fragmentation layer */
    initSerial(0, sendDaemonMsg);

    if (!PSID_registerMsg(PSP_CC_MSG, msgCC))
	mlog("%s: failed to register PSP_CC_MSG handler\n", __func__);
    if (!PSID_registerMsg(PSP_CD_SPAWNSUCCESS, msgSPAWNRES))
	mlog("%s: failed to register PSP_CD_SPAWNSUCCESS handler\n", __func__);
    if (!PSID_registerMsg(PSP_CD_SPAWNFAILED, msgSPAWNRES))
	mlog("%s: failed to register PSP_CD_SPAWNFAILED handler\n", __func__);
    if (!PSID_registerMsg(PSP_CC_ERROR, msgCCError))
	mlog("%s: failed to register PSP_CC_ERROR handler\n", __func__);

    return 0;
}

static int clearMsgHandlers(void *unused)
{
    PSID_clearMsg(PSP_CC_MSG, msgCC);
    PSID_clearMsg(PSP_CD_SPAWNSUCCESS, msgSPAWNRES);
    PSID_clearMsg(PSP_CD_SPAWNFAILED, msgSPAWNRES);
    PSID_clearMsg(PSP_CC_ERROR, msgCCError);

    finalizeSerial();

    return 0;
}

void initClient(void)
{
    PSIDhook_add(PSIDHOOK_FRWRD_SETUP, setupMsgHandlers);
    PSIDhook_add(PSIDHOOK_FRWRD_EXIT, clearMsgHandlers);
}

void finalizeClient(void)
{
    PSIDhook_del(PSIDHOOK_FRWRD_SETUP, setupMsgHandlers);
    PSIDhook_del(PSIDHOOK_FRWRD_EXIT, clearMsgHandlers);
}
