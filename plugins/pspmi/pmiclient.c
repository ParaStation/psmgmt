/*
 * ParaStation
 *
 * Copyright (C) 2007-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

#include "pscommon.h"
#include "pluginmalloc.h"
#include "kvs.h"
#include "kvscommon.h"
#include "pslog.h"
#include "list.h"
#include "selector.h"
#include "psidforwarder.h"
#include "pmilog.h"
#include "pmiservice.h"
#include "pmiprovider.h"

#include "pmiclient.h"

#define SOCKET int
#define PMI_RESEND 5
#define PMI_VERSION 1
#define PMI_SUBVERSION 1
#define UPDATE_HEAD sizeof(uint8_t) + sizeof(uint32_t)

typedef struct {
    char *Name;
    int (*fpFunc)(char *msgBuffer);
} PMI_Msg;

typedef struct {
    char *Name;
    int (*fpFunc)(void);
} PMI_shortMsg;

typedef struct {
    char *msg;		    /* The KVS update message */
    size_t len;		    /* The size of the message */
    int isSuccReady;	    /* Flag if the successor was ready */
    int gotBarrierIn;	    /* Flag if we got the barrier from the MPI client */
    int lastUpdate;	    /* Flag if the update is complete (last msg) */
    int updateIndex;	    /* Index to distinguish update messages */
    struct list_head list;
} Update_Buffer_t;

/** A list of buffers holding KVS update data */
Update_Buffer_t uBufferList;

/** Flag to check if the pmi_init() was called successful */
static int is_init = 0;

/** Flag to check if initialisation between us and client was ok */
static int pmi_init_client = 0;

/** Counter of the next KVS name */
static int kvs_next = 0;

/** My KVS name */
static char myKVSname[PMI_KVSNAME_MAX];

/** If set debug output is generated */
static int debug = 0;

/** If set KVS debug output is generated */
static int debug_kvs = 0;

/** The size of the MPI universe set from mpiexec */
static int universe_size = 0;

/** The parastation rank of the connected MPI client */
static int rank = -1;

/** The PMI rank of the connected MPI client */
static int pmiRank = -1;

/** Count update KVS msg from provider to make sure all msg were received */
static int32_t updateMsgCount;

/** Suffix of the KVS name */
static char kvs_name_prefix[PMI_KVSNAME_MAX];

/** The socket which is connected to the MPI client */
static SOCKET pmisock = -1;

/** The logger task ID of the current job */
static PStask_ID_t loggertid = -1;

/** The provider task ID of the current job */
static PStask_ID_t providertid = -1;

/** The predecessor task ID of the current job */
static PStask_ID_t predtid = -1;

/** The successor task ID of the current job */
static PStask_ID_t succtid = -1;

/** Flag to indicate if the successor is ready to receive update messages */
static int isSuccReady = 0;

/** Flag to check if we got the local barrier_in msg */
static int gotBarrierIn = 0;

/** Flag to check if we got the daisy barrier_in msg */
static int gotDaisyBarrierIn = 0;

/** Flag to indicate if we should start the daisy barrier */
static int startDaisyBarrier = 0;

/** Generic message buffer */
static char buffer[1024];

/** The number of children which we try to spawn */
static int spawnChildCount = 0;

/** The number of successful spawned children */
static int spawnChildSuccess = 0;

/** Buffer to store the spawn request for later processing */
static char *spawnBuffer = NULL;

/** Count the number of kvs_put messages from mpi client */
static int32_t putCount = 0;

static int32_t barrierCount = 0;

static int32_t globalPutCount = 0;


/**
 * @brief Send a KVS message to a task id.
 *
 * @param func Pointer to name name of the calling function.
 *
 * @param msg Pointer to the message to send.
 *
 * @param len The size of the message to send.
 *
 * @return No return value.
 */
static void sendKvsMsg(const char *func, PStask_ID_t tid, char *msg, size_t len)
{
    if (debug_kvs) {
	int8_t cmd;

	cmd = *(int8_t *) msg;
	if (cmd == PUT) {
	    elog("%s(r%i): pslog_write cmd '%s' len '%zu' dest:'%s'\n",
		    func, rank, PSKVScmdToString(cmd), len, PSC_printTID(tid));
	}
    }

    PSLog_write(tid, KVS, msg, len);
}

/**
 * @brief Send a KVS message to provider.
 *
 * @param msg Pointer to the message to send.
 *
 * @param len The size of the message to send.
 *
 * @return No return value.
 */
static void sendKvstoProvider(char *msg, size_t len)
{
    sendKvsMsg(__func__, providertid, msg, len);
}

/**
 * @brief Send KVS messages to successor
 *
 * Send a KVS messages to the successor, i.e. the process with the
 * next rank. If the current process is the one with the highest rank,
 * the message will be send to the provider in order to signal successful
 * delivery to all clients.
 *
 * @param msg Pointer to the message to send.
 *
 * @param len The size of the message to send.
 *
 * @return No return value.
 */
static void sendKvstoSucc(char *msg, size_t len)
{
    sendKvsMsg(__func__, succtid, msg, len);
}

/**
 * @brief Handle critical error.
 *
 * To handle a critical error close the connection and kill the child.
 * If something goes wrong in the startup phase with PMI, the child
 * and therefore the whole job can hang infinite. So we have to kill it.
 *
 * @return No return value.
 */
static int critErr()
{
    /* close connection */
    if (pmisock > -1) {
	if ((Selector_isRegistered(pmisock))) {
	    Selector_remove(pmisock);
	}
	close(pmisock);
	pmisock = -1;
    }

    /* kill the child */
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
    return 1;
}

/**
 * @brief Write to the PMI socket.
 *
 * Write the message @a msg to the PMI socket file-descriptor,
 * starting at by @a offset of the message. It is expected that the
 * previos parts of the message were sent in earlier calls to this
 * function.
 *
 * @param msg The message to transmit.
 *
 * @param offset Number of bytes sent in earlier calls.
 *
 * @param len The size of the message to transmit.
 *
 * @return On success, the total number of bytes written is returned,
 * i.e. usually this is the length of @a msg. If the @ref stdinSock
 * file-descriptor blocks, this might also be smaller. In this case
 * the total number of bytes sent in this and all previous calls is
 * returned. If an error occurs, -1 or 0 is returned and errno is set
 * appropriately.
 */
static int do_send(char *msg, int offset, int len)
{
    int n, i;

    if (!len || !msg) {
	return 0;
    }

    for (n=offset, i=1; (n<len) && (i>0);) {
	i = send(pmisock, &msg[n], len-n, 0);
	if (i<=0) {
	    switch (errno) {
	    case EINTR:
		break;
	    case EAGAIN:
		return n;
		break;
	    default:
		{
		char *errstr = strerror(errno);
		elog("%s(r%i): got error %d on PMI socket: %s\n",
			  __func__, rank, errno, errstr ? errstr : "UNKNOWN");
	    return i;
		}
	    }
	} else
	    n+=i;
    }
    return n;
}

/**
 * @brief Send a PMI message to the MPI client.
 *
 * Send a message to the connected MPI client.
 *
 * @param msg Buffer with the PMI message to send.
 *
 * @return Returns 1 on error, 0 on success.
 */
#define PMI_send(msg) __PMI_send(msg, __func__, __LINE__)
static int __PMI_send(char *msg, const char *caller, const int line)
{
    ssize_t len;
    int i, written = 0;

    len = strlen(msg);
    if (!(len && msg[len - 1] == '\n')) {
	/* assert */
	elog("%s(r%i): missing '\\n' in PMI msg '%s' from %s:%i\n",
		__func__, rank, msg, caller, line);
	return critErr();
    }

    if (debug) elog("%s(r%i): %s", __func__, rank, msg);

    /* try to send msg, repeat it PMI_RESEND times */
    for (i =0; (written < len) && (i< PMI_RESEND); i++) {
	written = do_send(msg, written, len);
    }

    if (written < len) {
	elog("%s(r%i): failed sending %s from %s:%i\n", __func__, rank, msg,
	    caller, line);
	return critErr();
    }

    return 0;
}

int handleSpawnRes(void *vmsg)
{
    DDBufferMsg_t *answer = vmsg;

    switch (answer->header.type) {
	case PSP_CD_SPAWNFAILED:
	    elog("%s(r%i): spawning service proccess failed\n", __func__, rank);
	    PMI_send("cmd=spawn_result rc=-1\n");
	    break;
	case PSP_CD_SPAWNSUCCESS:
	    /* wait for result of the spawner process */
	    if (debug) {
		elog("%s(r%i): spawning service process successful\n",
			__func__, rank);
	    }
	    break;
	default:
	    elog("%s(r%i): unexpect answer '%s'\n", __func__, rank,
		PSP_printMsg(answer->header.type));
    }

    return 0;
}

/**
 * @brief Delete a update buffer entry.
 *
 * @param uBuf The update buffer entry to delete.
 *
 * @return No return value.
 */
static void deluBufferEntry(Update_Buffer_t *uBuf)
{
    ufree(uBuf->msg);
    list_del(&uBuf->list);
    ufree(uBuf);
}

/**
 * @brief Return the MPI universe size.
 *
 * Return the size of the MPI universe.
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Always returns 0.
 */
static int p_Get_Universe_Size(char *msgBuffer)
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply), "cmd=universe_size size=%i\n",
	   universe_size);

    PMI_send(reply);

    return 0;
}

/**
 * @brief Return the application number.
 *
 * Returns the application number which defines the order the
 * application was started. In other implementations the appnum is set by
 * mpiexec. The number is increased when different executables are started by
 * a single call to mpiexec. This is not supported in parastation, yet.
 * Therefore this will always be set to 0.
 *
 * @return Always returns 0.
 */
static int p_Get_Appnum(void)
{
    char reply[PMIU_MAXLINE];

    snprintf(reply, sizeof(reply), "cmd=appnum appnum=0\n");
    PMI_send(reply);

    return 0;
}

/**
 * @brief Check if we can forward the daisy barrier.
 *
 * Check if we can forward the daisy barrier. We have to wait for the
 * daisy barrier from the previous rank and from the local MPI client.
 *
 * If we are the first in the daisy chain we start the barrier.
 *
 * @return No return value.
 */
static void checkDaisyBarrier()
{
    char *ptr = buffer;
    size_t len = 0;

    if (gotBarrierIn && isSuccReady && gotDaisyBarrierIn) {
	gotDaisyBarrierIn = 0;
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
	startDaisyBarrier = 0;

	setKVSCmd(&ptr, &len, DAISY_BARRIER_IN);
	addKVSInt32(&ptr, &len, &barrierCount);
	addKVSInt32(&ptr, &len, &globalPutCount);
	sendKvstoSucc(buffer, len);
    }
}

/**
 * @brief Parse a KVS update message from ther provider.
 *
 * Parse a KVS update message from the provider. Save all received key-value
 * pairs into the local KVS space. All get requests from the local MPI client
 * will be answered using the local KVS space. If we got all update message
 * we can release the waiting MPI client via barrier-out.
 *
 * If we are the last client in the chain, we need to tell the provider that the
 * update was successful completed.
 *
 * @param pmiLine The update message to parse.
 *
 * @param lastUpdateMsg Flag to indicate if this is the last update message.
 *
 * @param updateIdx Integer to identify the update phase.
 *
 * @return No return value.
 */
static void parseUpdateMessage(char *pmiLine, int lastUpdateMsg, int updateIdx)
{
    char vname[PMI_KEYLEN_MAX];
    char *nextvalue, *saveptr, *value;
    const char delimiters[] =" \n";
    size_t len;

    /* parse the update message */
    nextvalue = strtok_r(pmiLine, delimiters, &saveptr);

    while (nextvalue != NULL) {
	/* extract next key/value pair */
	if (!(value = strchr(nextvalue, '=') +1)) {
	    elog("%s(r%i): invalid kvs update" " received\n", __func__, rank);
	    critErr();
	    return;
	}
	len = strlen(nextvalue) - strlen(value) - 1;
	strncpy(vname, nextvalue, len);
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

    if (lastUpdateMsg) {
	char *ptr = buffer;
	size_t len = 0;

	/* we got all update msg, so we can release the waiting MPI client */
	gotBarrierIn = 0;
	snprintf(buffer, sizeof(buffer), "cmd=barrier_out\n");
	PMI_send(buffer);

	/* if we are the last in the chain we acknowledge the provider */
	if (succtid == providertid) {
	    setKVSCmd(&ptr, &len, UPDATE_CACHE_FINISH);
	    addKVSInt32(&ptr, &len, &updateMsgCount);
	    addKVSInt32(&ptr, &len, &updateIdx);
	    sendKvstoProvider(buffer, len);
	}
	updateMsgCount = 0;
    }
}

/**
 * @brief Hanlde a PMI barrier_in request from the local MPI client.
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
 * @param msgBuffer The buffer which contains the PMI barrier_in msg.
 *
 * @return Always returns 0.
 */
static int p_Barrier_In(char *msgBuffer)
{
    Update_Buffer_t *uBuf;
    list_t *pos, *tmp;
    char *ptr = buffer;

    gotBarrierIn = 1;

    /* if we are the first in chain, send starting barrier msg */
    if (pmiRank == 0) startDaisyBarrier = 1;
    checkDaisyBarrier();

    /* update local KVS cache with buffered update */
    if (list_empty(&uBufferList.list)) return 0;

    list_for_each_safe(pos, tmp, &uBufferList.list) {
	if ((uBuf = list_entry(pos, Update_Buffer_t, list)) == NULL) return 0;
	if (!uBuf->gotBarrierIn) {

	    char pmiLine[PMIU_MAXLINE];
	    size_t len, pLen;

	    /* skip cmd */
	    ptr = uBuf->msg + UPDATE_HEAD;

	    len = getKVSString(&ptr, pmiLine, sizeof(pmiLine));
	    pLen = uBuf->len - (UPDATE_HEAD) - sizeof(uint16_t);

	    if (strlen(pmiLine) != len || len != pLen) {
		elog("%s(r%i): invalid update len:%zu strlen:%zu bufLen:%zu\n",
			__func__, rank, len, strlen(pmiLine), pLen);
		critErr();
	    }

	    parseUpdateMessage(pmiLine, uBuf->lastUpdate, uBuf->updateIndex);

	    uBuf->gotBarrierIn = 1;
	    if (uBuf->isSuccReady) deluBufferEntry(uBuf);
	}
    }

    return 0;
}

void leaveKVS()
{
    char *ptr = buffer;
    size_t len = 0;

    /* inform the provider we are leaving the KVS space */
    setKVSCmd(&ptr, &len, LEAVE);
    sendKvstoProvider(buffer, len);
}

/**
 * @brief Finalize the PMI.
 *
 * Returns PMI_FINALIZED to notice the forwarder that the
 * child has successful finished execution. The forwarder will release
 * the child and then call pmi_finalize() to allow the child to exit.
 *
 * @return Returns PMI_FINALIZED.
 * */
static int p_Finalize()
{
    leaveKVS();

    return PMI_FINALIZED;
}

/**
 * @brief Return the default KVS name.
 *
 * Returns the default KVS name.
 *
 * @return Always returns 0.
 */
static int p_Get_My_Kvsname()
{
    char reply[PMIU_MAXLINE];

    snprintf(reply, sizeof(reply), "cmd=my_kvsname kvsname=%s\n", myKVSname);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Creates a new KVS.
 *
 * Not supported by hydra anymore. Seems nobody using it. Disabled to make new
 * KVS implementation more straighforward.
 *
 * Creates a new key value space.
 *
 * @return Returns 0 for success and 1 on error.
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
    snprintf(kvsmsg, sizeof(kvsmsg), "cmd=newkvs kvsname=%s\n",
		kvsname);
    PMI_send(kvsmsg);
    return 0;
}

/**
 * @brief Delete a KVS.
 *
 * Not supported by hydra anymore. Seems nobody using it. Disabled to make new
 * KVS implementation more straighforward.
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Destroy_Kvs(char *msgBuffer)
{
    char kvsname[PMI_KVSNAME_MAX];

    /* disable it */
    PMI_send("cmd=kvs_destroyed rc=-1\n");
    return 0;

    /* get parameter from msg */
    getpmiv("kvsname", msgBuffer, kvsname, sizeof(kvsname));

    if (kvsname[0] == 0) {
	elog("%s(r%i): got invalid msg\n", __func__, rank);
	PMI_send("cmd=kvs_destroyed rc=-1\n");
	return 1;
    }

    /* destroy KVS */
    if (kvs_destroy(kvsname)) {
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
 * @brief Put a key/value pair into the KVS.
 *
 * The key is save into the local KVS and instantly send to provider
 * to be saved in the global KVS.
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Put(char *msgBuffer)
{
    char kvsname[PMI_KVSNAME_MAX], key[PMI_KEYLEN_MAX], value[PMI_VALLEN_MAX];
    char *ptr = buffer;
    size_t len = 0;

    getpmiv("kvsname", msgBuffer, kvsname, sizeof(kvsname));
    getpmiv("key", msgBuffer, key, sizeof(key));
    getpmiv("value", msgBuffer, value, sizeof(value));

    /* check msg */
    if (kvsname[0] == 0 || key[0] == 0 || value[0] == 0) {
	if (debug_kvs) {
	    elog( "%s(r%i): received invalid PMI put msg\n",
		    __func__, rank);
	}
	PMI_send("cmd=put_result rc=-1 msg=error_invalid_put_msg\n");
	return 1;
    }

    putCount++;

    /* save to local KVS */
    if ((kvs_put(kvsname, key, value))) {
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
 * @brief The MPI client reads a key/value pair from local KVS.
 *
 * Read a value from the local KVS.
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Get(char *msgBuffer)
{
    char reply[PMIU_MAXLINE], kvsname[PMI_KVSNAME_MAX], key[PMI_KEYLEN_MAX];
    char *value;

    /* extract parameters */
    getpmiv("kvsname", msgBuffer, kvsname, sizeof(kvsname));
    getpmiv("key", msgBuffer, key, sizeof(key));

    /* check msg */
    if (kvsname[0] == 0 || key[0] == 0) {
	if (debug_kvs) {
	    elog("%s(r%i): received invalid PMI get cmd\n", __func__, rank);
	}
	PMI_send("cmd=get_result rc=-1 msg=error_invalid_get_msg\n");
	return 1;
    }

    /* get value from KVS */
    if (!(value = kvs_get(kvsname, key))) {
	char *env;

	/* check for mvapich process mapping */
	env = getenv("__PMI_PROCESS_MAPPING");
	if (env && !(strcmp(key, "PMI_process_mapping"))) {
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
		 "cmd=get_result rc=%i msg=error_value_not_found\n",
		 PMI_ERROR);
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
 * @brief Publish a service.
 *
 * Make a service public even for processes which are not
 * connected over PMI.
 *
 * Not implemented (yet).
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Publish_Name(char *msgBuffer)
{
    char service[PMI_VALLEN_MAX], port[PMI_VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    getpmiv("service", msgBuffer, service, sizeof(service));
    getpmiv("port", msgBuffer, port, sizeof(port));

    /* check msg */
    if (port[0] == 0 || service[0] == 0) {
	elog("%s(r%i): received invalid publish_name msg\n",
		__func__, rank);
	return 1;
    }

    if (debug) {
	elog("%s(r%i): received publish name request for service:%s, "
		"port:%s\n", __func__, rank, service, port);
    }

    snprintf(reply, sizeof(reply), "cmd=publish_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

    return 0;
}

/**
 * @brief Unpublish a service.
 *
 * Not implemented (yet).
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Unpublish_Name(char *msgBuffer)
{
    char service[PMI_VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    getpmiv("service", msgBuffer, service, sizeof(service));

    /* check msg*/
    if (service[0] == 0) {
	elog("%s(r%i): received invalid unpublish_name msg\n",
		__func__, rank);
    }

    if (debug) {
	elog("%s(r%i): received unpublish name request for service:%s\n",
		__func__, rank, service);
    }
    snprintf(reply, sizeof(reply), "cmd=unpublish_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

    return 0;
}

/**
 * @brief Lookup a service name.
 *
 * Not implemented (yet).
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Lookup_Name(char *msgBuffer)
{
    char service[PMI_VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    getpmiv("service",msgBuffer,service,sizeof(service));

    /* check msg*/
    if (service[0] == 0) {
	elog("%s(r%i): received invalid lookup_name msg\n", __func__, rank);
    }

    if (debug) {
	elog("%s(r%i): received lookup name request for service:%s\n",
		__func__, rank, service);
    }
    snprintf(reply, sizeof(reply), "cmd=lookup_result info=%s\n",
	     "not_implemented_yet\n" );
    PMI_send(reply);

    return 0;
}

/**
 * @brief Make sure the spawn request comes from MPI_Comm_spawn().
 *
 * We are currently not supporting the MPI_Comm_spawn_multiple() call.
 *
 * @param msgBuffer Buffer which holds the PMI spawn message.
 *
 * @return Returns 0 on success and 1 on error.
 */
static int checkSpawnCompatibility(char *msgBuffer)
{
    int spawnTotal, spawnSoFar;
    char totspawns[50], spawnssofar[50];

    /* check for unsupported MPI_Comm_spawn_multiple() calls */
    if (!(getpmiv("totspawns", msgBuffer, totspawns, sizeof(totspawns)))) {
	elog("%s(r%i): invalid totspawns argument\n", __func__, rank);
	PMI_send("cmd=spawn_result rc=-1\n");
	return 1;
    }
    spawnTotal = atoi(totspawns);

    if (!(getpmiv("spawnssofar", msgBuffer, spawnssofar, sizeof(spawnssofar)))) {
	elog("%s(r%i): invalid spawnssofar argument\n", __func__, rank);
	PMI_send("cmd=spawn_result rc=-1\n");
	return 1;
    }
    spawnSoFar = atoi(spawnssofar);

    if (spawnTotal != 1 || spawnSoFar != 1) {
	elog("%s(r%i): unsupported spawn multiple request\n", __func__, rank);
	PMI_send("cmd=spawn_result rc=-1\n");
	return 1;
    }

    return 0;
}

/**
 * @brief Build up argv and argc from a PMI spawn request.
 *
 * @param msgBuffer The buffer with the PMI spawn message.
 *
 * @param argc Pointer which will receive the argument count.
 *
 * @return Returns a pointer to the build argv or NULL on error.
 */
static char **getSpawnArgs(char *msgBuffer, int *argc)
{
    char *execname, *nextval, **argv;
    char numArgs[50];
    int addArgs = 0, maxargc = 2, i;

    /* setup argv */
    if (!(getpmiv("argcnt", msgBuffer, numArgs, sizeof(numArgs)))) {
	elog("%s(r%i): missing argc argument\n", __func__, rank);
	return NULL;
    }

    if ((addArgs = atoi(numArgs)) > PMI_SPAWN_MAX_ARGUMENTS) {
	elog("%s(r%i): too many arguments (%i)\n", __func__, rank, addArgs);
	return NULL;
    }

    if (addArgs < 0) {
	elog("%s(r%i): invalid argument count (%i)\n", __func__, rank, addArgs);
	return NULL;
    }

    maxargc += addArgs;
    argv = umalloc((maxargc) * sizeof(char *));

    for (i=0; i<maxargc; i++) argv[i] = NULL;

    /* add the executalbe as argv[0] */
    if (!(execname = getpmivm("execname", msgBuffer))) {
	ufree(argv);
	elog("%s(r%i): invalid executable name\n", __func__, rank);
	return NULL;
    }
    argv[(*argc)++] = execname;

    /* add additional arguments */
    for (i=1; i<=addArgs; i++) {
	snprintf(buffer, sizeof(buffer), "arg%i", i);
	if ((nextval = getpmivm(buffer, msgBuffer))) {
	    argv[(*argc)++] = nextval;
	} else {
	    for (i=0; i<*argc; i++) ufree(argv[i]);
	    ufree(argv);
	    *argc = 0;
	    elog("%s(r%i): extracting arguments failed\n", __func__, rank);
	    return NULL;
	}
    }

    return argv;
}

/**
 * @brief Spawn one or more processes.
 *
 * We first need the next rank for the new service process to
 * start. Only the logger knows that. So we ask it and buffer
 * the spawn request to wait for an answer.
 *
 * @param msgBuffer Buffer which holds the PMI spawn message.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Spawn(char *msgBuffer)
{
    /* get next service rank from logger */
    PSLog_write(loggertid, SERV_TID, NULL, 0);

    if (spawnBuffer) {
	elog("%s(r%i): spawn buffer should be empty, another spawn in"
		" progress?\n", __func__, rank);
	critErr();
	PMI_send("cmd=spawn_result rc=-1\n");
	return 0;
    }

    /* save spawn buffer for later */
    if (!(spawnBuffer = ustrdup(msgBuffer))) {
	elog("%s(r%i): out of memory\n", __func__, rank);
	critErr();
	PMI_send("cmd=spawn_result rc=-1\n");
	exit(1);
    }

    /* wait for logger to answer */
    return 0;
}

/**
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
 * @brief Parse the spawn request and start a new service process.
 *
 * The service process will spawn itself again to start a spawner process
 * which will then spawn the requested executable. The first service process
 * will then turn into a new KVS provider for the new spawned PMI group.
 *
 * The new spawned children will have their own MPI_COMM_WOLRD and
 * therefore a separate KVS, separate PMI barriers and a separate
 * daisy chain to communicate.
 *
 * @param spawnBuffer The buffered spawn request from the MPI client.
 *
 * @param serviceRank The next valid rank for a new service process.
 *
 * @return Returns 0 on success and 1 on error.
 */
static int tryPMISpawn(char *spawnBuffer, int serviceRank)
{
    char nextkey[PMI_KEYLEN_MAX], nextvalue[PMI_VALLEN_MAX];
    char numPreput[50], numInfo[50], np[50];
    char *pmiWdir = NULL, *nodeType = NULL, *machinefile = NULL, *hosts = NULL;
    char **argv = NULL, **envv = NULL;
    int argc = 0, envc = 0, i, infos, count;

    //mlog("%s: dump: %s\n", __func__, spawnBuffer);

    /* check for correct arguments */
    if ((checkSpawnCompatibility(spawnBuffer))) return 1;

    /* get the number of processes to spawn */
    if (!(getpmiv("nprocs", spawnBuffer, np, sizeof(np)))) {
	elog("%s(r%i): getting number of processes to spawn failed\n",
		__func__, rank);
	goto spawn_error;
    }

    /* setup argv and argc for processes to spawn */
    if (!(argv = getSpawnArgs(spawnBuffer, &argc))) goto spawn_error;

    /* extract preput values and keys
     *
     * We will transport this kv-pairs using the spawn environment. When our
     * children are starting the pmi_init() call will add them to their local
     * KVS.
     * */
    if (!(getpmiv("preput_num", spawnBuffer, numPreput, sizeof(numPreput)))) {
	elog("%s(r%i): missing preput count\n", __func__, rank);
	goto spawn_error;
    }

    count = 0;
    envc = atoi(numPreput);
    envv = umalloc(sizeof(char *) * ( envc + 2));

    snprintf(buffer, sizeof(buffer), "__PMI_preput_num=%i", envc);
    envv[count++] = ustrdup(buffer);

    for (i=0; i< envc; i++) {
	int esize;

	snprintf(buffer, sizeof(buffer), "preput_key_%i", i);
	if (!(getpmiv(buffer, spawnBuffer, nextkey, sizeof(nextkey)))) {
	    elog("%s(r%i): invalid preput key '%s'\n", __func__, rank, buffer);
	    goto spawn_error;
	}
	esize = 6 + strlen(buffer) + 1 + strlen(nextkey) + 1;
	envv[count] = umalloc(esize);
	snprintf(envv[count++], esize, "__PMI_%s=%s", buffer, nextkey);

	snprintf(buffer, sizeof(buffer), "preput_val_%i", i);
	if (!(getpmiv(buffer, spawnBuffer, nextvalue, sizeof(nextvalue)))) {
	    elog("%s(r%i): invalid preput value '%s'\n", __func__, rank,
		    buffer);
	    goto spawn_error;
	}
	esize = 6 + strlen(buffer) + 1 + strlen(nextvalue) + 1;
	envv[count] = umalloc(esize);
	snprintf(envv[count++], esize, "__PMI_%s=%s", buffer, nextvalue);
    }
    envc = count;

    /* extract info values and keys
     *
     * These info variables are implementation dependend and can be used for
     * e.g. process placement. All unsupported values will be silently ignored.
     *
     * ParaStation will support:
     *
     *  - wdir:	The working directory of the new spawned processes
     *  - host: A single host to use
     *  - hosts: A list of hosts to use
     *  - hostfile: A hostfile to use
     *  - machinefile: equals hostfile
     *
     * TODO:
     *
     *  - nodetype: The type of nodes requested
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
    if (!(getpmiv("info_num", spawnBuffer, numInfo, sizeof(numInfo)))) {
	elog("%s(r%i): missing info count\n", __func__, rank);
	goto spawn_error;
    }
    infos = atoi(numInfo);

    for (i=0; i<infos; i++) {
	snprintf(buffer, sizeof(buffer), "info_key_%i", i);
	if (!(getpmiv(buffer, spawnBuffer, nextkey, sizeof(nextkey)))) {
	    elog("%s(r%i): invalid info variable\n", __func__, rank);
	    goto spawn_error;
	}

	snprintf(buffer, sizeof(buffer), "info_val_%i", i);
	if (!strcmp(nextkey, "wdir")) {
	    pmiWdir = getpmivm(buffer, spawnBuffer);
	}
	if (!strcmp(nextkey, "nodetype")) {
	    nodeType = getpmivm(buffer, spawnBuffer);
	}
	if (!hosts && !strcmp(nextkey, "host")) {
	    hosts = getpmivm(buffer, spawnBuffer);
	}
	if (!hosts && !strcmp(nextkey, "hosts")) {
	    hosts = getpmivm(buffer, spawnBuffer);
	}
	if (!machinefile && !strcmp(nextkey, "machinefile")) {
	    machinefile = getpmivm(buffer, spawnBuffer);
	}
	if (!machinefile && !strcmp(nextkey, "hostfile")) {
	    machinefile = getpmivm(buffer, spawnBuffer);
	}

	/* TODO soft spawn
	if (!strcmp(nextkey, "soft")) {
	    char *soft;
	    int *count, *sList;

	    soft = getpmivm(buffer, spawnBuffer);
	    sList = getSoftArgList(soft, &count);

	    ufree(soft);
	}
	*/
    }

    if (hosts && machinefile) {
	elog("%s(r%i): host(s) and machinefile are mutal exclusive\n",
		__func__, rank);
	goto spawn_error;
    }

    /* reset tracking */
    spawnChildSuccess = 0;
    spawnChildCount = atoi(np);

    /* setup new KVS name */
    snprintf(buffer, sizeof(buffer), "PMI_KVS_TMP=pshost_%i_%i",
		PSC_getMyTID(), kvs_next++);

    /* spawn a service process which spawns the KVS provider and the new
     * children */
    if (!(spawnService(np, argv, argc, envv, envc, pmiWdir, nodeType,
			universe_size, machinefile, hosts, serviceRank,
			buffer))) {
	goto spawn_error;
    }

    return 0;

spawn_error:

    for (i=0; i<argc; i++) {
	if (argv && argv[i]) ufree(argv[i]);
    }
    ufree(argv);

    ufree(hosts);
    ufree(machinefile);
    ufree(nodeType);
    ufree(pmiWdir);

    PMI_send("cmd=spawn_result rc=-1\n");
    return 1;
}

/**
 * @brief Get a key-value pair by index.
 *
 * Get a key-value pair by specific index from key value space.
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_GetByIdx(char *msgBuffer)
{
    char reply[PMIU_MAXLINE];
    char idx[PMI_VALLEN_MAX], kvsname[PMI_KVSNAME_MAX];
    char *value, *ret, name[PMI_KEYLEN_MAX];
    int index, len;

    getpmiv("idx", msgBuffer, idx, sizeof(msgBuffer));
    getpmiv("kvsname", msgBuffer, kvsname, sizeof(msgBuffer));

    /* check msg */
    if (idx[0] == 0 || kvsname[0] == 0) {
	if (debug_kvs) {
	    elog("%s(r%i): received invalid PMI getbiyidx msg\n",
		    __func__, rank);
	}
	snprintf(reply, sizeof(reply),
		 "getbyidx_results rc=-1 reason=invalid_getbyidx_msg\n");
	PMI_send(reply);
	return 1;
    }

    index = atoi(idx);
    /* find and return the value */
    if ((ret = kvs_getbyidx(kvsname, index))) {
	if (!(value = strchr(ret,'=') + 1)) {
	    elog("%s(r%i): error in local key value space\n",
		    __func__, rank);
	    return critErr();
	}
	len = strlen(ret) - strlen(value) - 1;
	strncpy(name, ret, len);
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
 * @brief Init the local PMI communication.
 *
 * Init the PMI communication, mainly to be sure both sides
 * are using the same protocol versions.
 *
 * The init process is monitored by the provider to make sure all MPI clients
 * are started successfully in time.
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Init(char *msgBuffer)
{
    char reply[PMIU_MAXLINE], pmiversion[20], pmisubversion[20];
    char *ptr;
    size_t len;

    getpmiv("pmi_version", msgBuffer, pmiversion, sizeof(pmiversion));
    getpmiv("pmi_subversion", msgBuffer, pmisubversion, sizeof(pmisubversion));

    /* check msg */
    if (pmiversion[0] == 0 || pmisubversion[0] == 0) {
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
	return critErr();
    }
    pmi_init_client = 1;

    /* tell provider that the MPI client was initialized */
    ptr = buffer;
    len = 0;
    setKVSCmd(&ptr, &len, INIT);
    sendKvstoProvider(buffer, len);

    return 0;
}

/**
 * @brief Get KVS maxes values.
 *
 * Get the maximal size of the kvsname, keylen and values.
 *
 * @return Always returns 0.
 */
static int p_Get_Maxes()
{
    char reply[PMIU_MAXLINE];
    snprintf(reply, sizeof(reply),
	     "cmd=maxes kvsname_max=%i keylen_max=%i vallen_max=%i\n",
	     PMI_KVSNAME_MAX, PMI_KEYLEN_MAX, PMI_VALLEN_MAX);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Intel MPI 3.0 Extension.
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
 * @return Returns 0 for success and 1 on error.
 */
static int p_Get_Rank2Hosts(void)
{
    char reply[PMIU_MAXLINE];
    if (debug) {
	elog("%s(r%i): got get_rank2hosts request\n", __func__, rank);
    }

    snprintf(reply, sizeof(reply), "cmd=put_ranks2hosts 0 0\n");
    PMI_send(reply);

    return 0;
}

/**
 * @brief Authenticate the client.
 *
 * Use a handshake to authenticate the MPI client. Note that it is not
 * mandatory for the MPI client to authenticate itself. Hydra continous even
 * with incorrect authentification.
 *
 * @param msgBuffer The buffer which contains the PMI
 * msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_InitAck(char *msgBuffer)
{
    char *pmi_id, client_id[PMI_KEYLEN_MAX], reply[PMIU_MAXLINE];

    if (debug) {
	elog("%s(r%i): received PMI initack msg:%s\n",
		__func__, rank, msgBuffer);
    }

    if (!(pmi_id = getenv("PMI_ID"))) {
	elog("%s(r%i): no PMI_ID is set\n", __func__, rank);
	return critErr();
    }

    getpmiv("pmiid", msgBuffer, client_id, sizeof(client_id));

    if (client_id[0] == 0) {
	elog("%s(r%i): empty pmiid from MPI client\n", __func__, rank);
	return critErr();
    }

    if (!!(strcmp(pmi_id, client_id))) {
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
 * @brief Handle a execution problem message.
 *
 * The execution problem message is sent BEFORE client actually
 * starts, so even before PMI init.
 *
 * @param msgBuffer The buffer which contains the PMI msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Execution_Problem(char *msgBuffer)
{
    char exec[PMI_VALLEN_MAX], reason[PMI_VALLEN_MAX];

    getpmiv("reason",msgBuffer,exec,sizeof(exec));
    getpmiv("exec",msgBuffer,reason,sizeof(reason));

    if (exec[0] == 0 || reason[0] == 0) {
	elog("%s(r%i): received invalid PMI execution problem msg\n",
		__func__, rank);
	return 1;
    }

    elog("%s(r%i): execution problem: exec=%s, reason=%s\n",
	    __func__, rank, exec, reason);

    critErr();
    return 0;
}

const PMI_Msg pmi_commands[] =
{
	{ "put",			&p_Put			},
	{ "get",			&p_Get			},
	{ "barrier_in",			&p_Barrier_In		},
	{ "init",			&p_Init			},
	{ "spawn",			&p_Spawn		},
	{ "get_universe_size",		&p_Get_Universe_Size	},
	{ "initack",			&p_InitAck		},
	{ "lookup_name",		&p_Lookup_Name		},
	{ "execution_problem",		&p_Execution_Problem	},
	{ "getbyidx",			&p_GetByIdx		},
	{ "publish_name",		&p_Publish_Name		},
	{ "unpublish_name",		&p_Unpublish_Name	},
	{ "destroy_kvs",		&p_Destroy_Kvs		},
};

const PMI_shortMsg pmi_short_commands[] =
{
	{ "get_maxes",			&p_Get_Maxes		},
	{ "get_appnum",			&p_Get_Appnum		},
	{ "finalize",			&p_Finalize		},
	{ "get_my_kvsname",		&p_Get_My_Kvsname	},
	{ "get_ranks2hosts",		&p_Get_Rank2Hosts	},
	{ "create_kvs",			&p_Create_Kvs		},
};

const int pmi_com_count = sizeof(pmi_commands)/sizeof(pmi_commands[0]);
const int pmi_short_com_count =
    sizeof(pmi_short_commands)/sizeof(pmi_short_commands[0]);


static int setPreputValues()
{
    char *env, *key, *value;
    int i, preNum;

    if (!(env = getenv("__PMI_preput_num"))) return 0;

    preNum = atoi(env);

    for (i=0; i< preNum; i++) {
	snprintf(buffer, sizeof(buffer), "__PMI_preput_key_%i", i);
	if (!(key = getenv(buffer))) {
	    elog("%s(r%i): invalid preput key '%i'\n", __func__, rank, i);
	    return critErr();
	}

	snprintf(buffer, sizeof(buffer), "__PMI_preput_val_%i", i);
	if (!(value = getenv(buffer))) {
	    elog("%s(r%i): invalid preput value '%i'\n", __func__, rank, i);
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

int pmi_init(int pmisocket, int pRank, PStask_ID_t logger)
{
    char *envPtr, *env_kvs_name, *ptr;
    size_t len;

    rank = pRank;
    if (!(ptr = getenv("PMI_RANK"))) {
	elog("%s(r%i): invalid PMI rank environment\n", __func__, rank);
	return 1;
    }
    pmiRank = atoi(ptr);
    pmisock = pmisocket;
    loggertid = logger;

    /*
    mlog("%s:(r%i): pmiRank '%i' pmisock '%i' logger '%i' spawned '%s' myTid "
	    "'%s'\n", __func__, rank, pmiRank, pmisock, logger,
	    getenv("PMI_SPAWNED"), PSC_printTID(PSC_getMyTID()));
    */

    INIT_LIST_HEAD(&uBufferList.list);

    if (pmisocket < 1) {
	elog("%s(r%i): invalid PMI socket '%i'\n", __func__, rank, pmisocket);
	return 1;
    }

    /* set debug mode */
    if ((envPtr = getenv("PMI_DEBUG")) && atoi(envPtr) > 0) {
	debug = atoi(envPtr);
	debug_kvs = debug;
    } else if ((envPtr = getenv("PMI_DEBUG_CLIENT"))) {
	debug = atoi(envPtr);
    }
    if ((envPtr = getenv("PMI_DEBUG_KVS"))) {
	debug_kvs = atoi(envPtr);
    }

    /* set the MPI universe size */
    if ((envPtr = getenv("PMI_UNIVERSE_SIZE"))) {
	universe_size = atoi(envPtr);
    } else {
	universe_size = 1;
    }

    /* set the name of the KVS space */
    if (!(env_kvs_name = getenv("PMI_KVS_TMP"))) {
	strncpy(kvs_name_prefix, "kvs_root", sizeof(kvs_name_prefix) -1);
    } else {
	snprintf(kvs_name_prefix, sizeof(kvs_name_prefix), "kvs_%s",
		    env_kvs_name);
    }

    is_init = 1;
    updateMsgCount = 0;

    /* set my KVS name */
    if (!(env_kvs_name = getenv("PMI_KVSNAME"))) {
	snprintf(myKVSname, sizeof(myKVSname), "%s_%i", kvs_name_prefix,
		    kvs_next++);
    } else {
	strcpy(myKVSname, env_kvs_name);
    }

    /* create local KVS space */
    if (kvs_create(myKVSname)) {
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
    if ((setPreputValues())) return 1;

    /* tell my PMI parent I am alive */
    if ((getenv("PMI_SPAWNED"))) {
	PStask_ID_t parent;
	int32_t res = 1;

	if (!(ptr = getenv("__PMI_SPAWN_PARENT"))) {
	    elog("%s(r%i): error getting my PMI parent\n", __func__, rank);
	    return critErr();
	}
	parent = atoi(ptr);

	ptr = buffer;
	len = 0;
	setKVSCmd(&ptr, &len, CHILD_SPAWN_RES);
	addKVSInt32(&ptr, &len, &res);
	addKVSInt32(&ptr, &len, &rank);
	addKVSInt32(&ptr, &len, &pmiRank);
	sendKvsMsg(__func__, parent, buffer, len);
    }

    return 0;
}

/**
 * @brief Extract the PMI command.
 *
 * Parse a PMI message and return the command.
 *
 * @param msg The message to parse.
 *
 * @param cmdbuf The buffer which receives the extracted command.
 *
 * @param bufsize The size of the cmd buffer.
 *
 * @return Returns 1 for success, 0 on errors.
 */
static int extractPMIcmd(char *msg, char *cmdbuf, int bufsize)
{
    const char delimiters[] =" \n";
    char *msgCopy, *cmd, *saveptr;

    if (!msg || strlen(msg) < 5) {
	elog("%s(r%i): invalid PMI msg '%s' received\n", __func__, rank, msg);
	return !critErr();
    }

    msgCopy = ustrdup(msg);
    cmd = strtok_r(msgCopy, delimiters, &saveptr);

    while ( cmd != NULL ) {
	if (!strncmp(cmd,"cmd=", 4)) {
	    cmd += 4;
	    strncpy(cmdbuf, cmd, bufsize);
	    ufree(msgCopy);
	    return 1;
	}
	if (!strncmp(cmd,"mcmd=", 5)) {
	    cmd += 5;
	    strncpy(cmdbuf, cmd, bufsize);
	    ufree(msgCopy);
	    return 1;
	}
	cmd = strtok_r(NULL, delimiters, &saveptr);
    }

    ufree(msgCopy);
    return 0;
}

int handlePMIclientMsg(char *msg)
{
    int i;
    char cmd[PMI_VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    if (is_init != 1) {
	elog("%s(r%i): you must call pmi_init first, msg '%s'\n",
		__func__, rank, msg);
	return critErr();
    }

    if (!msg) {
	elog("%s(r%i): invalid PMI msg\n", __func__, rank);
	return critErr();
    }

    if (strlen(msg) > PMIU_MAXLINE ) {
	elog("%s(r%i): PMI msg to long, msg_size=%i and allowed_size=%i\n",
	    __func__, rank, (int)strlen(msg), PMIU_MAXLINE);
	return critErr();
    }

    if (!extractPMIcmd(msg, cmd, sizeof(cmd)) || strlen(cmd) <2) {
	elog("%s(r%i): invalid PMI cmd received, msg was '%s'\n",
		__func__, rank, msg);
	return critErr();
    }

    if (debug) {
	elog("%s(r%i): got %s\n", __func__, rank, msg);
    }

    /* find PMI cmd */
    for (i=0; i< pmi_com_count; i++) {
	if (!strcmp(cmd,pmi_commands[i].Name)) {
		return pmi_commands[i].fpFunc(msg);
	}
    }

    /* find short PMI cmd */
    for (i=0; i< pmi_short_com_count; i++) {
	if (!strcmp(cmd,pmi_short_commands[i].Name)) {
		return pmi_short_commands[i].fpFunc();
	}
    }

    /* cmd not found */
    elog("%s(r%i): unsupported PMI cmd received '%s'\n", __func__, rank, cmd);

    snprintf(reply, sizeof(reply),
	     "cmd=%s rc=%i info=not_supported_cmd\n", cmd, PMI_ERROR);
    PMI_send(reply);

    return critErr();
}

/**
* @brief Release the PMI client.
*
* Finalize the PMI connection and release the MPI client.
*
* @return No return value.
*/
void pmi_finalize(void)
{
    if (pmi_init_client) {
	PMI_send("cmd=finalize_ack\n");
    }
}

/**
 * @brief Buffer an update message until both the local MPI client and our
 * successor is ready.
 *
 * @param msg The message to buffer.
 *
 * @param msgLen The len of the message.
 *
 * @param isSuccReady Flag to indicate if we need to forward the message to our
 * successor.
 *
 * @param barrierIn Flag to indicate if we need to send this update to the local
 * MPI client.
 *
 * @param lastUpdate If this flag is set to 1 then the update is complete
 * with this message.
 *
 * @param strLen The lenght of the update payload.
 *
 * @return No return value.
 */
static void bufferCacheUpdate(char *msg, size_t msgLen, int isSuccReady,
				int barrierIn, int lastUpdate, size_t strLen,
				int updateIndex)
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

    list_add_tail(&(uBuf->list), &uBufferList.list);
}

/**
 * @brief Handle a KVS update message from the provider.
 *
 * @param msg The message to handle.
 *
 * @param ptr Pointer to the update payload.
 *
 * @param lastUpdateMsg If this flag is set to 1 then the update is complete
 * with this message.
 *
 * @return No return value.
 */
static void handleKVScacheUpdate(PSLog_Msg_t *msg, char *ptr, int lastUpdateMsg)
{
    char pmiLine[PMIU_MAXLINE];
    int len, msgSize, updateIndex;

    updateIndex = getKVSInt32(&ptr);

    /* get the PMI update message */
    if ((len = getKVSString(&ptr, pmiLine, sizeof(pmiLine))) < 0) {
	elog("%s(%i): invalid update len '%i' index '%i' last'%i'\n", __func__,
		rank, len, updateIndex, lastUpdateMsg);
	critErr();
	return;
    }
    msgSize = msg->header.len - PSLog_headerSize;

    /* sanity check */
    if ((int) (PMIUPDATE_HEADER_LEN + sizeof(int32_t) + len) != msgSize) {
	elog("%s(r%i): got invalid update message\n", __func__, rank);
	elog("%s(r%i): msg.header.size:%i, len:%i msgSize:%i pslog_header:%i\n",
		__func__, rank, msg->header.len, len, msgSize, PSLog_headerSize);
	critErr();
	return;
    }

    /* we need to buffer the message for later */
    if (len > 0 && (!isSuccReady || !gotBarrierIn)) {
	bufferCacheUpdate(msg->buf, msgSize, isSuccReady, gotBarrierIn,
			    lastUpdateMsg, len, updateIndex);
    }

    /* sanity check */
    if (lastUpdateMsg && !gotBarrierIn) {
	elog("%s:(r%i): got last update message, but I have no barrier_in\n",
		__func__, rank);
	critErr();
    }

    /* forward to successor */
    if (isSuccReady && succtid != providertid) sendKvstoSucc(msg->buf, msgSize);

    /* wait with the update until we got the barrier_in from local MPI client */
    if (!gotBarrierIn) return;

    /* update the local KVS */
    parseUpdateMessage(pmiLine, lastUpdateMsg, updateIndex);
}

/**
 * @brief Hanlde a KVS barrier out message.
 *
 * Release the waiting MPI client from the barrier.
 *
 * @return No return value.
 */
static void handleDaisyBarrierOut(PSLog_Msg_t *msg)
{
    if (succtid != providertid) {
	/* forward to next client */
	sendKvstoSucc(msg->buf, sizeof(uint8_t));
    }

    /* Forward msg from provider to client */
    gotBarrierIn = 0;
    snprintf(buffer, sizeof(buffer), "cmd=barrier_out\n");
    PMI_send(buffer);
}

/**
 * @brief Set a new daisy barrier.
 *
 * @return No return value.
 */
static void handleDaisyBarrierIn(char *ptr)
{
    if (predtid == providertid) {
	elog("%s(r%i): received daisy_barrier_in from provider\n",
		__func__, rank);
	return;
    }

    barrierCount = getKVSInt32(&ptr);
    globalPutCount = getKVSInt32(&ptr);
    gotDaisyBarrierIn = 1;
    checkDaisyBarrier();
}

/**
 * @brief Our successor is now ready to receive messages.
 *
 * The successor finished its initialize phase with the provider and
 * is now ready to receive PMI messages from us. We can now forward
 * all buffered barrier/update messages.
 *
 * @return No return value.
 */
static void handleSuccReady(char *mbuf)
{
    list_t *pos, *tmp;
    Update_Buffer_t *uBuf;

    succtid = getKVSInt32(&mbuf);
    //elog("s(r%i): succ:%i pmiRank:%i providertid:%i\n", rank, succtid,
    //	    pmiRank, providertid);
    isSuccReady = 1;
    checkDaisyBarrier();

    /* forward buffered messages */
    if (list_empty(&uBufferList.list)) return;

    list_for_each_safe(pos, tmp, &uBufferList.list) {
	if ((uBuf = list_entry(pos, Update_Buffer_t, list)) == NULL) return;
	if (!uBuf->isSuccReady) {

	    if (succtid != providertid) {
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
	    }

	    if (succtid != providertid) sendKvstoSucc(uBuf->msg, uBuf->len);
	    uBuf->isSuccReady = 1;

	    if (uBuf->gotBarrierIn) deluBufferEntry(uBuf);
	}
    }
}

int handleCCError(void *data)
{
    PSLog_Msg_t *msg = data;

    if (msg->header.sender == providertid) return 1;

    /*
    mlog("%s(r%i): got cc error message from '%s' type '%s'\n", __func__, rank,
	    PSC_printTID(msg->header.sender), PSLog_printMsgType(msg->type));
    */

    return 0;
}

void setKVSProviderTID(PStask_ID_t ptid)
{
    providertid = ptid;
}

/**
 * @brief Handle a spawn result message from my children.
 *
 * New spawned processes will send this success message when pmi_init() is
 * called in their forwarder.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleChildSpawnRes(PSLog_Msg_t *msg, char *ptr)
{
    int cRank, cPmiRank;

    spawnChildSuccess++;

    if (!(getKVSInt32(&ptr))) {
	elog("%s(r%i): spawning processes failed\n", __func__, rank);

	spawnChildSuccess = 0;
	PMI_send("cmd=spawn_result rc=-1\n");
	return;
    }

    cRank = getKVSInt32(&ptr);
    cPmiRank = getKVSInt32(&ptr);

    if (debug) {
	elog("%s(r%i): success from %s, rank '%i' pmiRank '%i'\n", __func__,
	    rank, PSC_printTID(msg->sender), cRank, cPmiRank);
    }

    if (spawnChildSuccess == spawnChildCount) {
	spawnChildSuccess = spawnChildCount = 0;
	PMI_send("cmd=spawn_result rc=0\n");
    }
}

/**
 * @brief Extract the next service rank and try to continue spawning.
 *
 * @param msg The message from the logger to handle.
 *
 * @return No return value.
 */
static void handleServiceInfo(PSLog_Msg_t *msg)
{
    char *ptr = msg->buf;
    int serviceRank;

    serviceRank = *(int32_t *) ptr;
    //ptr += sizeof(int32_t);

    if (!spawnBuffer) {
	elog("%s(%i): spawning failed, no spawn buffer set\n", __func__, rank);
	PMI_send("cmd=spawn_result rc=-1\n");
	return;
    }

    /* try to do the spawn */
    tryPMISpawn(spawnBuffer, serviceRank);

    /* cleanup */
    ufree(spawnBuffer);
    spawnBuffer = NULL;
}

/**
 * @brief Handle a KVS message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleKVSMessage(PSLog_Msg_t *msg)
{
    char *ptr = msg->buf;
    uint8_t cmd;

    /* handle KVS messages, extract cmd from msg */
    cmd = getKVSCmd(&ptr);

    if (debug_kvs) {
	elog("%s(r%i): cmd '%s'\n", __func__, rank, PSKVScmdToString(cmd));
    }

    switch(cmd) {
	case NOT_AVAILABLE:
	    elog("%s(r%i): global KVS is not available, exiting\n",
		    __func__, rank);
	    critErr();
	    break;
	case UPDATE_CACHE:
	    handleKVScacheUpdate(msg, ptr, 0);
	    break;
	case UPDATE_CACHE_FINISH:
	    handleKVScacheUpdate(msg, ptr, 1);
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
	    elog("%s(r%i): got unknown KVS msg '%s:%i' from '%s'\n", __func__,
		    rank, PSKVScmdToString(cmd), cmd,
		    PSC_printTID(msg->header.sender));
	    critErr();
    }
}

int handlePSlogMessage(void *vmsg)
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
	    elog("%s(%i): got unexpected PSLog message '%s'\n", __func__, rank,
		   PSLog_printMsgType(msg->type));
    }
    return 0;
}
