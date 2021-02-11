/*
 * ParaStation
 *
 * Copyright (C) 2007-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

#include "pscio.h"
#include "pscommon.h"
#include "psilog.h"
#include "psispawn.h"
#include "pluginmalloc.h"
#include "pluginstrv.h"
#include "kvs.h"
#include "kvscommon.h"
#include "pslog.h"
#include "list.h"
#include "selector.h"
#include "psidforwarder.h"
#include "psidhook.h"
#include "psaccounthandles.h"

#include "pmilog.h"
#include "pmiforwarder.h"
#include "pmiclientspawn.h"

#include "pmiclient.h"

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
    bool isSuccReady;    /**< Flag if the successor was ready */
    bool gotBarrierIn;   /**< Flag if we got the barrier from the MPI client */
    bool lastUpdate;     /**< Flag if the update is complete (last msg) */
    int updateIndex;     /**< Index to distinguish update messages */
} Update_Buffer_t;

/** A list of buffers holding KVS update data */
static LIST_HEAD(uBufferList);

/** Flag to check if the pmi_init() was called successful */
static bool initialized = false;

/** Flag to check if initialisation between us and client was ok */
static bool pmi_init_client = false;

/** Counter of the next KVS name */
static int kvs_next = 0;

/** My KVS name */
static char myKVSname[PMI_KVSNAME_MAX+12];

/** If set debug output is generated */
static bool debug = false;

/** If set KVS debug output is generated */
static bool debug_kvs = false;

/** The size of the MPI universe set from mpiexec */
static int universe_size = 0;

/** Task structure describing the connected MPI client to serve */
static PStask_t *cTask = NULL;

/** PS rank of the connected MPI client. This is redundant to cTask->rank */
static int rank = -1;

/** The PMI rank of the connected MPI client */
static int pmiRank = -1;

/** The PMI appnum parameter of the connected MPI client */
static int appnum = -1;

/** Count update KVS msg from provider to make sure all msg were received */
static int32_t updateMsgCount;

/** Suffix of the KVS name */
static char kvs_name_prefix[PMI_KVSNAME_MAX];

/** The socket which is connected to the MPI client */
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
static bool isSuccReady = false;

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
	int8_t cmd;

	cmd = *(int8_t *) msg;
	if (cmd == PUT) {
	    elog("%s(r%i): pslog_write cmd %s len %zu dest: %s\n",
		 func, rank, PSKVScmdToString(cmd), len, PSC_printTID(tid));
	}
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
 * @return No return value.
 */
static void terminateJob(void)
{
    DDSignalMsg_t msg;

    msg.header.type = PSP_CD_SIGNAL;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getMyTID();
    msg.header.len = sizeof(msg);
    msg.signal = -1;
    msg.param = getuid();
    msg.pervasive = 1;
    msg.answer = 0;

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
 * @brief Send a PMI message to the MPI client
 *
 * Send a message to the connected MPI client.
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
 * Handle the spawn results message contained in @a vmsg. Such message
 * is expected upon the creation of a new service process used to
 * actually realize the PMI spawn that was triggered by the PMI
 * client.
 *
 * @param vmsg Message to handle
 *
 * @return Always returns 0
 */
static int handleSpawnRes(void *vmsg)
{
    DDBufferMsg_t *answer = vmsg;

    switch (answer->header.type) {
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
	    elog("%s(r%i): unexpect answer %s\n", __func__, rank,
		 PSP_printMsg(answer->header.type));
    }

    return 0;
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
 * @brief Return the MPI universe size
 *
 * Return the size of the MPI universe.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Always returns 0
 */
static int p_Get_Universe_Size(char *msg)
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply), "cmd=universe_size size=%i\n",
	     universe_size);

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
 * daisy barrier from the previous rank and from the local MPI client.
 *
 * If we are the first in the daisy chain we start the barrier.
 *
 * @return No return value
 */
static void checkDaisyBarrier(void)
{
    char *ptr = buffer;
    size_t len = 0;

    if (gotBarrierIn && isSuccReady && gotDaisyBarrierIn) {
	gotDaisyBarrierIn = false;
	barrierCount++;
	globalPutCount += putCount;

	setKVSCmd(&ptr, &len, DAISY_BARRIER_IN);
	addKVSInt32(&ptr, &len, &barrierCount);
	addKVSInt32(&ptr, &len, &globalPutCount);
	sendKvstoSucc(buffer, len);
    }

    if (gotBarrierIn && isSuccReady && startDaisyBarrier) {
	barrierCount  = 1;
	globalPutCount = putCount;
	startDaisyBarrier = false;

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
 * pairs into the local KVS space. All get requests from the local MPI client
 * will be answered using the local KVS space. If we got all update message
 * we can release the waiting MPI client via barrier-out.
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
static void parseUpdateMessage(char *pmiLine, bool lastUpdate, int updateIdx)
{
    char vname[PMI_KEYLEN_MAX];
    char *nextvalue, *saveptr;
    const char delimiters[] =" \n";

    /* parse the update message */
    nextvalue = strtok_r(pmiLine, delimiters, &saveptr);

    while (nextvalue != NULL) {
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
	if (kvs_put(myKVSname, vname, value)) {
	    elog("%s(r%i): error saving kvs update: kvsname:%s, key:%s,"
		    " value:%s\n", __func__, rank, myKVSname, vname, value);
	    critErr();
	    return;
	}
	nextvalue = strtok_r(NULL, delimiters, &saveptr);
    }
    updateMsgCount++;

    if (lastUpdate) {
	char *ptr = buffer;
	size_t len = 0;

	/* we got all update msg, so we can release the waiting MPI client */
	gotBarrierIn = false;
	snprintf(buffer, sizeof(buffer), "cmd=barrier_out\n");
	PMI_send(buffer);

	/* if we are the last in the chain we acknowledge the provider */
	if (succtid == kvsProvTID) {
	    setKVSCmd(&ptr, &len, UPDATE_CACHE_FINISH);
	    addKVSInt32(&ptr, &len, &updateMsgCount);
	    addKVSInt32(&ptr, &len, &updateIdx);
	    sendKvstoProvider(buffer, len);
	}
	updateMsgCount = 0;
    }
}

/**
 * @brief Hanlde a PMI barrier_in request from the local MPI client
 *
 * The MPI client has to wait until all clients have entered barrier.
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
    list_t *pos, *tmp;

    gotBarrierIn = true;

    /* if we are the first in chain, send starting barrier msg */
    if (!pmiRank) startDaisyBarrier = true;
    checkDaisyBarrier();

    /* update local KVS cache with buffered update */
    list_for_each_safe(pos, tmp, &uBufferList) {
	Update_Buffer_t *uBuf = list_entry(pos, Update_Buffer_t, next);
	if (!uBuf->gotBarrierIn) {
	    char pmiLine[PMIU_MAXLINE];
	    size_t len, pLen;

	    /* skip cmd */
	    char *ptr = uBuf->msg + UPDATE_HEAD;

	    len = getKVSString(&ptr, pmiLine, sizeof(pmiLine));
	    pLen = uBuf->len - (UPDATE_HEAD) - sizeof(uint16_t);

	    if (strlen(pmiLine) != len || len != pLen) {
		elog("%s(r%i): invalid update len:%zu strlen:%zu bufLen:%zu\n",
		     __func__, rank, len, strlen(pmiLine), pLen);
		critErr();
	    }

	    parseUpdateMessage(pmiLine, uBuf->lastUpdate, uBuf->updateIndex);

	    uBuf->gotBarrierIn = true;
	    if (uBuf->isSuccReady) deluBufferEntry(uBuf);
	}
    }

    return 0;
}

void leaveKVS(int used)
{
    char *ptr = buffer;
    size_t len = 0;

    if (!used || !isSuccReady) {
	/* inform the provider we are leaving the KVS space */
	setKVSCmd(&ptr, &len, LEAVE);
	sendKvstoProvider(buffer, len);
    }
}

/**
 * @brief Finalize the PMI
 *
 * Returns PMI_FINALIZED to notice the forwarder that the
 * child has successful finished execution. The forwarder will release
 * the child and then call pmi_finalize() to allow the child to exit.
 *
 * @return Returns PMI_FINALIZED
 * */
static int p_Finalize(void)
{
    leaveKVS(0);

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
    if (kvs_put(kvsname, key, value)) {
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
 * @brief The MPI client reads a key/value pair from local KVS
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
    char reply[PMIU_MAXLINE];

    getpmiv("service", msg, service, sizeof(service));
    getpmiv("port", msg, port, sizeof(port));

    /* check msg */
    if (!port[0] || !service[0]) {
	elog("%s(r%i): received invalid publish_name msg\n", __func__, rank);
	return 1;
    }

    if (debug) elog("%s(r%i): received publish name request for service:%s, "
		    "port:%s\n", __func__, rank, service, port);

    snprintf(reply, sizeof(reply), "cmd=publish_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

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
    char reply[PMIU_MAXLINE];

    getpmiv("service", msg, service, sizeof(service));

    /* check msg*/
    if (!service[0]) {
	elog("%s(r%i): received invalid unpublish_name msg\n", __func__, rank);
    }

    if (debug) elog("%s(r%i): received unpublish name request for service:%s\n",
		    __func__, rank, service);
    snprintf(reply, sizeof(reply), "cmd=unpublish_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

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
    char reply[PMIU_MAXLINE];

    getpmiv("service", msg, service, sizeof(service));

    /* check msg*/
    if (!service[0]) {
	elog("%s(r%i): received invalid lookup_name msg\n", __func__, rank);
    }

    if (debug) elog("%s(r%i): received lookup name request for service:%s\n",
		    __func__, rank, service);
    snprintf(reply, sizeof(reply), "cmd=lookup_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

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
	snprintf(reply, sizeof(reply),
		 "getbyidx_results rc=-1 reason=invalid_getbyidx_msg\n");
	PMI_send(reply);
	return 1;
    }

    index = atoi(idx);
    /* find and return the value */
    ret = kvs_getbyidx(kvsname, index);
    if (ret) {
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
    } else {
	snprintf(reply, sizeof(reply),
		 "getbyidx_results rc=-2 reason=no_more_keyvals\n");
    }

    PMI_send(reply);

    return 0;
}

/**
 * @brief Init the local PMI communication
 *
 * Init the PMI communication, mainly to be sure both sides
 * are using the same protocol versions.
 *
 * The init process is monitored by the provider to make sure all MPI clients
 * are started successfully in time.
 *
 * @param msg Buffer containing the PMI msg to handle
 *
 * @return Returns 0 for success and 1 on error
 */
static int p_Init(char *msg)
{
    char reply[PMIU_MAXLINE], pmiversion[20], pmisubversion[20];
    char *ptr;
    size_t len;

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
    pmi_init_client = true;

    if (psAccountSwitchAccounting) psAccountSwitchAccounting(cTask->tid, false);

    /* tell provider that the MPI client was initialized */
    ptr = buffer;
    len = 0;
    setKVSCmd(&ptr, &len, INIT);
    sendKvstoProvider(buffer, len);

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
    char reply[PMIU_MAXLINE];
    if (debug) elog("%s(r%i): got get_rank2hosts request\n", __func__, rank);

    snprintf(reply, sizeof(reply), "cmd=put_ranks2hosts 0 0\n");
    PMI_send(reply);

    return 0;
}

/**
 * @brief Authenticate the client
 *
 * Use a handshake to authenticate the MPI client. Note that it is not
 * mandatory for the MPI client to authenticate itself. Hydra continous even
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
	elog("%s(r%i): empty pmiid from MPI client\n", __func__, rank);
	return critErr();
    }

    if (!!strcmp(pmi_id, client_id)) {
	elog("%s(r%i): invalid pmi_id '%s' from MPI client should be '%s'\n",
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

	if (kvs_put(myKVSname, key, value)) {
	    elog("%s(r%i): error saving preput value kvsname:%s, key:%s,"
		    " value:%s\n", __func__, rank, myKVSname, key, value);
	    return critErr();
	}
    }

    return 0;
}

int pmi_init(int pmisocket, PStask_t *childTask)
{
    char *ptr, *env;
    size_t len;

#if DEBUG_ENV
    int i = 0;
    while(environ[i]) mlog("%d: %s\n", i, environ[i++]);
#endif

    cTask = childTask;
    rank = cTask->rank;
    env = getenv("PMI_RANK");
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

    mdbg(PSPMI_LOG_VERBOSE, "%s:(r%i): pmiRank %i pmisock %i logger %s",
	 __func__, rank, pmiRank, pmisock, PSC_printTID(cTask->loggertid));
    mdbg(PSPMI_LOG_VERBOSE, " spawned '%s' myTid %s\n",
	 getenv("PMI_SPAWNED"), PSC_printTID(PSC_getMyTID()));

    INIT_LIST_HEAD(&uBufferList);

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

    /* set the MPI universe size */
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
    ptr = buffer;
    len = 0;
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

/**
* @brief Release the PMI client
*
* Finalize the PMI connection and release the MPI client.
*
* @return No return value
*/
void pmi_finalize(void)
{
    if (pmi_init_client) PMI_send("cmd=finalize_ack\n");
}

/**
 * @brief Buffer an update message until both the local MPI client and our
 * successor is ready
 *
 * @param msg The message to buffer
 *
 * @param msgLen The len of the message
 *
 * @param barrierIn Flag to indicate if this update has to be sent to
 * the local MPI client
 *
 * @param lastUpdate Flag to indicate this message completes the update
 *
 * @param strLen The length of the update payload
 *
 * @return No return value
 */
static void bufferCacheUpdate(char *msg, size_t msgLen, bool barrierIn,
			      bool lastUpdate, size_t strLen, int updateIndex)
{
    Update_Buffer_t *uBuf;

    uBuf = (Update_Buffer_t *) umalloc(sizeof(Update_Buffer_t));
    uBuf->msg = umalloc(msgLen);

    memcpy(uBuf->msg, msg, msgLen);
    uBuf->len = msgLen;
    uBuf->isSuccReady = isSuccReady;
    uBuf->gotBarrierIn = barrierIn;
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
    char pmiLine[PMIU_MAXLINE];
    int len, msgSize, updateIndex;

    updateIndex = getKVSInt32(&ptr);

    /* get the PMI update message */
    len = getKVSString(&ptr, pmiLine, sizeof(pmiLine));
    if (len < 0) {
	elog("%s(%i): invalid update len %i index %i last %i\n", __func__,
	     rank, len, updateIndex, lastUpdate);
	critErr();
	return;
    }
    msgSize = msg->header.len - PSLog_headerSize;

    /* sanity check */
    if ((int) (PMIUPDATE_HEADER_LEN + sizeof(int32_t) + len) != msgSize) {
	elog("%s(r%i): got invalid update message\n", __func__, rank);
	elog("%s(r%i): msg.header.len:%i, len:%i msgSize:%i pslog_header:%zi\n",
	     __func__, rank, msg->header.len, len, msgSize, PSLog_headerSize);
	critErr();
	return;
    }

    /* we need to buffer the message for later */
    if (len > 0 && (!isSuccReady || !gotBarrierIn)) {
	bufferCacheUpdate(msg->buf, msgSize, gotBarrierIn,
			  lastUpdate, len, updateIndex);
    }

    /* sanity check */
    if (lastUpdate && !gotBarrierIn) {
	elog("%s:(r%i): got last update message, but I have no barrier_in\n",
	     __func__, rank);
	critErr();
    }

    /* forward to successor */
    if (isSuccReady && succtid != kvsProvTID) sendKvstoSucc(msg->buf, msgSize);

    if (lastUpdate && psAccountSwitchAccounting) {
	psAccountSwitchAccounting(cTask->tid, true);
    }

    /* wait with the update until we got the barrier_in from local MPI client */
    if (!gotBarrierIn) return;

    /* update the local KVS */
    parseUpdateMessage(pmiLine, lastUpdate, updateIndex);
}

/**
 * @brief Hanlde a KVS barrier out message
 *
 * Release the waiting MPI client from the barrier.
 *
 * @return No return value
 */
static void handleDaisyBarrierOut(PSLog_Msg_t *msg)
{
    if (succtid != kvsProvTID) {
	/* forward to next client */
	sendKvstoSucc(msg->buf, sizeof(uint8_t));
    }

    /* Forward msg from provider to client */
    gotBarrierIn = false;
    snprintf(buffer, sizeof(buffer), "cmd=barrier_out\n");
    PMI_send(buffer);
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
    list_t *pos, *tmp;

    succtid = getKVSInt32(&mbuf);
    //elog("s(r%i): succ:%i pmiRank:%i providertid:%i\n", rank, succtid,
    //	    pmiRank, kvsProvTID);
    isSuccReady = true;
    checkDaisyBarrier();

    /* forward buffered messages */
    list_for_each_safe(pos, tmp, &uBufferList) {
	Update_Buffer_t *uBuf = list_entry(pos, Update_Buffer_t, next);
	if (!uBuf->isSuccReady) {

	    if (succtid != kvsProvTID) {
		char *ptr;
		char pmiLine[PMIU_MAXLINE];
		size_t len, pLen;

		ptr = uBuf->msg + UPDATE_HEAD;
		len = getKVSString(&ptr, pmiLine, sizeof(pmiLine));
		pLen = uBuf->len - (UPDATE_HEAD) - sizeof(uint16_t);

		/* sanity check */
		if (strlen(pmiLine) != len || len != pLen) {
		    elog("%s(r%i): invalid update msg len:%zu strlen:%zu "
			 "bufLen:%zu\n", __func__, rank, len,
			 strlen(pmiLine), pLen);
		    critErr();
		}
		sendKvstoSucc(uBuf->msg, uBuf->len);
	    }

	    uBuf->isSuccReady = true;

	    if (uBuf->gotBarrierIn) deluBufferEntry(uBuf);
	}
    }
}


/**
 * @brief Handle a CC_ERROR message
 *
 * Since the actual KVS lives within its own service process, sending
 * changes to the key-value store might fail. This function handles
 * the resulting CC_ERROR messages to be passed in @a data.
 *
 * @param data Message to handle
 *
 * @return Always returns 0
 */
static int handleCCError(void *data)
{
    PSLog_Msg_t *msg = data;

    if (msg->header.sender == kvsProvTID) return 1;

    /*
    mlog("%s(r%i): got cc error message from %s type %s\n", __func__, rank,
	    PSC_printTID(msg->header.sender), PSLog_printMsgType(msg->type));
    */

    return 0;
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
 * @param argv String vector to add the extracted arguments to
 *
 * @return Returns true on success and false on error
 */
static bool getSpawnArgs(char *msg, strv_t *args)
{
    char *execname;
    char numArgs[50];
    int addArgs = 0, i;

    /* setup argv */
    if (!getpmiv("argcnt", msg, numArgs, sizeof(numArgs))) {
	mlog("%s(r%i): missing argc argument\n", __func__, rank);
	return false;
    }

    addArgs = atoi(numArgs);
    if (addArgs > PMI_SPAWN_MAX_ARGUMENTS) {
	mlog("%s(r%i): too many arguments (%i)\n", __func__, rank, addArgs);
	return false;
    } else if (addArgs < 0) {
	mlog("%s(r%i): invalid argument count (%i)\n", __func__, rank, addArgs);
	return false;
    }

    /* add the executable as argv[0] */
    execname = getpmivm("execname", msg);
    if (!execname) {
	mlog("%s(r%i): invalid executable name\n", __func__, rank);
	return false;
    }
    strvAdd(args, execname);

    /* add additional arguments */
    for (i = 1; i <= addArgs; i++) {
	char *nextval;
	snprintf(buffer, sizeof(buffer), "arg%i", i);
	nextval = getpmivm(buffer, msg);
	if (nextval) {
	    strvAdd(args, nextval);
	} else {
	    size_t j;
	    for (j = 0; j < args->count; j++) ufree(args->strings[j]);
	    mlog("%s(r%i): extracting arguments failed\n", __func__, rank);
	    return false;
	}
    }

    return true;
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
    strv_t args;
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
    strvInit(&args, NULL, 0);
    if (!getSpawnArgs(msg, &args)) {
	strvDestroy(&args);
	goto parse_error;
    }
    spawn->argv = args.strings;
    spawn->argc = args.count;

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
    if (PSLog_write(cTask->loggertid, SERV_TID, NULL, 0) < 0) {
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
static void addPreputToEnv(int preputc, KVP_t *preputv, strv_t *env)
{
    int i;
    char *tmpstr;

    snprintf(buffer, sizeof(buffer), "__PMI_preput_num=%i", preputc);
    strvAdd(env, ustrdup(buffer));

    for (i = 0; i < preputc; i++) {
	int esize;

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
    SingleSpawn_t *spawn;
    KVP_t *info;
    strv_t args, env;
    bool noParricide = false;
    char *tmpStr;
    int i, j;

    spawn = &(req->spawns[0]);

    /* put preput key-value-pairs into environment
     *
     * We will transport this kv-pairs using the spawn environment. When
     * our children are starting the pmi_init() call will add them to their
     * local KVS.
     *
     * Only the values of the first single spawn are used. */
    strvInit(&env, task->environ, task->envSize);
    addPreputToEnv(spawn->preputc, spawn->preputv, &env);

    ufree(task->environ);
    task->environ = env.strings;
    task->envSize = env.count;

    /* build arguments:
     * mpiexec -u <UNIVERSE_SIZE> -np <NP> -d <WDIR> -p <PATH> \
     *  --nodetype=<NODETYPE> --tpp=<TPP> <BINARY> ... */
    strvInit(&args, NULL, 0);

    tmpStr = getenv("__PSI_MPIEXEC_KVSPROVIDER");
    if (tmpStr) {
	strvAdd(&args, ustrdup(tmpStr));
    } else {
	strvAdd(&args, ustrdup(PKGLIBEXECDIR "/kvsprovider"));
    }
    strvAdd(&args, ustrdup("-u"));

    snprintf(buffer, sizeof(buffer), "%d", usize);
    strvAdd(&args, ustrdup(buffer));

    for (i = 0; i < req->num; i++) {
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
	for (j = 0; j < spawn->infoc; j++) {
	    info = &(spawn->infov[j]);

	    if (!strcmp(info->key, "wdir")) {
		strvAdd(&args, ustrdup("-d"));
		strvAdd(&args, ustrdup(info->value));
	    }
	    if (!strcmp(info->key, "tpp")) {
		size_t len = strlen(info->value) + 7;
		tmpStr = umalloc(len);
		snprintf(tmpStr, len, "--tpp=%s", info->value);
		strvAdd(&args, tmpStr);
	    }
	    if (!strcmp(info->key, "nodetype") || !strcmp(info->key, "arch")) {
		size_t len = strlen(info->value) + 12;
		tmpStr = umalloc(len);
		snprintf(tmpStr, len, "--nodetype=%s", info->value);
		strvAdd(&args, tmpStr);
	    }
	    if (!strcmp(info->key, "path")) {
		strvAdd(&args, ustrdup("-p"));
		strvAdd(&args, ustrdup(info->value));
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
	for (j = 0; j < spawn->argc; j++) {
	    strvAdd(&args, ustrdup(spawn->argv[j]));
	}

	/* add separating colon */
	if (i < req->num - 1) {
	    strvAdd(&args, ustrdup(":"));
	}
    }

    task->argv = args.strings;
    task->argc = args.count;

    task->noParricide = noParricide;

    return 1;
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
    int i;

    if (!req) {
	mlog("%s: no spawn request (THIS SHOULD NEVER HAPPEN!!!)\n", __func__);
	return false;
    }

    if (!cTask) {
	mlog("%s: cannot find my child's task structure\n", __func__);
	return false;
    }

    PStask_t *task = PStask_new();
    if (!task) {
	mlog("%s: cannot create a new task\n", __func__);
	return false;
    }

    /* copy data from my task */
    task->uid = cTask->uid;
    task->gid = cTask->gid;
    task->aretty = cTask->aretty;
    task->loggertid = cTask->loggertid;
    task->ptid = cTask->tid;
    task->group = TG_KVS;
    task->rank = serviceRank -1;
    task->winsize = cTask->winsize;
    task->termios = cTask->termios;

    /* set work dir */
    if (cTask->workingdir) {
	task->workingdir = ustrdup(cTask->workingdir);
    } else {
	task->workingdir = NULL;
    }

    /* build environment */
    strv_t env;
    strvInit(&env, NULL, 0);
    for (i = 0; cTask->environ[i]; i++) {
	char *cur = cTask->environ[i];

	/* skip troublesome old env vars */
	if (!strncmp(cur, "__KVS_PROVIDER_TID=", 19)) continue;
	if (!strncmp(cur, "PMI_ENABLE_SOCKP=", 17)) continue;
	if (!strncmp(cur, "PMI_RANK=", 9)) continue;
	if (!strncmp(cur, "PMI_PORT=", 9)) continue;
	if (!strncmp(cur, "PMI_FD=", 7)) continue;
	if (!strncmp(cur, "PMI_KVS_TMP=", 12)) continue;
	if (!strncmp(cur, "OMP_NUM_THREADS=", 16)) continue;
#if 0
	if (path && !strncmp(cur, "PATH", 4)) {
	    setPath(cur, path, &task->environ[i++]);
	    continue;
	}
#endif

	strvAdd(&env, ustrdup(cur));
    }
    task->environ = env.strings;
    task->envSize = env.count;

    /* calc totalProcs */
    *totalProcs = 0;
    for (i = 0; i < req->num; i++) {
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
    strvInit(&env, task->environ, task->envSize);

    snprintf(buffer, sizeof(buffer), "PMI_KVS_TMP=pshost_%i_%i",
	     PSC_getMyTID(), kvs_next++);  /* setup new KVS name */
    strvAdd(&env, ustrdup(buffer));
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    snprintf(buffer, sizeof(buffer), "__PMI_SPAWN_SERVICE_RANK=%i",
	     serviceRank - 2);
    strvAdd(&env, ustrdup(buffer));
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    snprintf(buffer, sizeof(buffer), "__PMI_SPAWN_PARENT=%i", PSC_getMyTID());
    strvAdd(&env, ustrdup(buffer));
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    strvAdd(&env, ustrdup("PMI_SPAWNED=1"));

    snprintf(buffer, sizeof(buffer), "PMI_SIZE=%d", *totalProcs);
    strvAdd(&env, ustrdup(buffer));
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    snprintf(buffer, sizeof(buffer), "__PMI_NO_PARRICIDE=%i",task->noParricide);
    strvAdd(&env, ustrdup(buffer));
    if (debug) elog("%s(r%i): Set %s\n", __func__, rank, buffer);

    ufree(task->environ);
    task->environ = env.strings;
    task->envSize = env.count;

    uint32_t j;
    if (debug) {
	elog("%s(r%i): Executing '", __func__, rank);
	for (j = 0; j < task->argc; j++) elog(" %s", task->argv[j]);
	elog("'\n");
    }
    mlog("%s(r%i): Executing '", __func__, rank);
    for (j = 0; j < task->argc; j++) mlog(" %s", task->argv[j]);
    mlog("'\n");

#if 0
    mlog("Task environment:\n");
    for (i = 0; i < task->envSize; i++) {
	if (task->environ[i] == NULL) {
	    mlog("!!! NULL pointer in task environment !!!\n");
	    continue;
	}
	mlog(" %d: %s\n", i, task->environ[i]);
    }
#endif

    bool ret = PSI_sendSpawnMsg(task, false, PSC_getMyID(), sendDaemonMsg);

    PStask_delete(task);

    return ret;
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
    int totalProcs, serviceRank = *(int32_t *)msg->buf;

    if (!pendSpawn) {
	mlog("%s(r%i): spawn failed, no spawn request set\n", __func__, rank);
	PMI_send("cmd=spawn_result rc=-1\n");
	return;
    }

    /* try to do the spawn */
    if (tryPMISpawn(pendSpawn, universe_size, serviceRank, &totalProcs)) {
	/* reset tracking */
	spawnChildSuccess = 0;
	spawnChildCount = totalProcs;
    } else {
	PMI_send("cmd=spawn_result rc=-1\n");
    }

    /* cleanup */
    freeSpawnRequest(pendSpawn);
    pendSpawn = NULL;
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
    uint8_t cmd;

    /* handle KVS messages, extract cmd from msg */
    cmd = getKVSCmd(&ptr);

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
    char *msgCopy, *cmd, *saveptr;

    if (!msg || strlen(msg) < 5) {
	elog("%s(r%i): invalid PMI msg '%s' received\n", __func__, rank, msg);
	return !critErr();
    }

    msgCopy = ustrdup(msg);
    cmd = strtok_r(msgCopy, delimiters, &saveptr);

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
    int i;
    char cmd[PMI_VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    if (!initialized) {
	elog("%s(r%i): you must call pmi_init first, msg '%s'\n", __func__,
	     rank, msg);
	return critErr();
    }

    if (!msg) {
	elog("%s(r%i): invalid PMI msg\n", __func__, rank);
	return critErr();
    }

    if (!extractPMIcmd(msg, cmd, sizeof(cmd)) || strlen(cmd) <2) {
	elog("%s(r%i): invalid PMI cmd received, msg was '%s'\n", __func__,
	     rank, msg);
	return critErr();
    }

    if (debug) elog("%s(r%i): got %s\n", __func__, rank, msg);

    /* find PMI cmd */
    for (i = 0; pmi_commands[i].Name; i++) {
	if (!strcmp(cmd, pmi_commands[i].Name)) {
	    return pmi_commands[i].fpFunc(msg);
	}
    }

    /* find short PMI cmd */
    for (i = 0; pmi_short_commands[i].Name; i++) {
	if (!strcmp(cmd, pmi_short_commands[i].Name)) {
	    return pmi_short_commands[i].fpFunc();
	}
    }

    /* cmd not found */
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
 * @brief Handle a KVS message from logger
 *
 * Handle the KVS message @a vmsg. The message is received from the
 * job's logger within a pslog message.
 *
 * @param vmsg KVS message to handle in a pslog container
 *
 * @return Always returns 0
 */
static int handlePSlogMessage(void *vmsg)
{
    PSLog_Msg_t *msg = vmsg;

    switch (msg->type) {
	case KVS:
	    handleKVSMessage(msg);
	    break;
	case SERV_TID:
	    handleServiceInfo(msg);
	    break;
	case SERV_EXT:
	    handleServiceExit(msg);
	    break;
	default:
	    elog("%s(r%i): got unexpected PSLog message %s\n", __func__, rank,
		 PSLog_printMsgType(msg->type));
    }
    return 0;
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

void initClient(void)
{
    PSIDhook_add(PSIDHOOK_FRWRD_KVS, handlePSlogMessage);
    PSIDhook_add(PSIDHOOK_FRWRD_SPAWNRES, handleSpawnRes);
    PSIDhook_add(PSIDHOOK_FRWRD_CC_ERROR, handleCCError);
}

void finalizeClient(void)
{
    PSIDhook_del(PSIDHOOK_FRWRD_KVS, handlePSlogMessage);
    PSIDhook_del(PSIDHOOK_FRWRD_SPAWNRES, handleSpawnRes);
    PSIDhook_del(PSIDHOOK_FRWRD_CC_ERROR, handleCCError);
}
