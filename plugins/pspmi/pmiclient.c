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
    "$Id: psidpmiprotocol.c 8598 2012-05-02 11:12:04Z moschny $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

#include "pscommon.h"
#include "kvs.h"
#include "kvscommon.h"
#include "pslog.h"
#include "list.h"
#include "selector.h"
#include "psidforwarder.h"
#include "pmilog.h"

#include "pmiclient.h"

#define SOCKET int
#define PMI_RESEND 5
#define PMI_VERSION 1
#define PMI_SUBVERSION 1

typedef struct {
    char *Name;
    int (*fpFunc)(char *msgBuffer);
} PMI_Msg;

typedef struct {
    char *Name;
    int (*fpFunc)(void);
} PMI_shortMsg;

typedef struct {
    char *msg;		    /* The kvs update message */
    size_t len;		    /* The size of the message */
    int isSuccReady;	    /* Flag if the successor was ready */
    int gotBarrierIn;	    /* Flag if we got the barrier from the mpi client */
    int lastUpdate;	    /* Flag if the update is complete (last msg) */
    struct list_head list;
} Update_Buffer_t;

/** A list of buffers holding kvs update data */
Update_Buffer_t uBufferList;

/** Flag to check if the pmi_init() was called successful */
static int is_init = 0;

/** Flag to check if initialisation between us and client was ok */
static int pmi_init_client = 0;

/** Counter of the next kvs name */
static int kvs_next = 0;

/** my kvs name */
static char myKVSname[KVSNAME_MAX];

/** If set debug output is generated */
static int debug = 0;

/** If set kvs debug output is generated */
static int debug_kvs = 0;

/** The size of the mpi universe set from mpiexec */
static int universe_size = 0;

/** The parastation rank of the connected mpi client */
static int rank = 0;

/** The pmi rank of the connected mpi client */
static int pmiRank = -1;

/** Count update kvs msg from logger to make sure all msg were received */
static int32_t updateMsgCount;

/** Suffix of the kvs name */
static char kvs_name_prefix[KVSNAME_MAX];

/** The socket which is connected to the mpi client */
static SOCKET pmisock;

/** The logger task ID of the current job */
static PStask_ID_t loggertid;

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


/**
 * @brief Send a kvs message to a task id.
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
	elog("%s(r%i): cmd '%s'\n", func, rank, PSKVScmdToString(cmd));
    }

    PSLog_write(tid, KVS, msg, len);
}

/**
 * @brief Send a kvs message to logger.
 *
 * @param msg Pointer to the message to send.
 *
 * @param len The size of the message to send.
 *
 * @return No return value.
 */
static void sendKvstoLogger(char *msg, size_t len)
{
    sendKvsMsg(__func__, loggertid, msg, len);
}

/**
 * @brief Send KVS messages to successor
 *
 * Send a KVS messages to the successor, i.e. the process with the
 * next rank. If the current process is the one with the highest rank,
 * the message will be send to the logger in order to signal successful
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
 * If something goes wrong in the startup phase with pmi, the child
 * and therefore the whole job can hang infinite. So we have to kill it.
 *
 * @return No return value.
 */
static int critErr()
{
    /* close connection */
    if (pmisock > 0 ) {
	Selector_remove(pmisock);
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
 * @brief Write to the pmi socket.
 *
 * Write the message @a msg to the pmi socket file-descriptor,
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
		elog("%s(r%i): got error %d on pmi socket: %s\n",
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
 * @brief Send a pmi message to the mpi client.
 *
 * Send a message to the connected mpi client.
 *
 * @param msg Buffer with the pmi message to send.
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
	elog("%s(r%i): missing '\\n' in pmi msg '%s' from %s:%i\n",
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

/**
 * @brief Delete a update buffer entry.
 *
 * @param uBuf The update buffer entry to delete.
 *
 * @return No return value.
 */
static void deluBufferEntry(Update_Buffer_t *uBuf)
{
    free(uBuf->msg);
    list_del(&uBuf->list);
    free(uBuf);
}

/**
 * @brief Return the mpi universe size.
 *
 * Return the size of the mpi universe.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
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
 * daisy barrier from the previous rank and from the local mpi client.
 *
 * If we are the first in the daisy chain we start the barrier.
 *
 * @return No return value.
 */
static void checkDaisyBarrier()
{
    char *ptr = buffer;
    size_t len = 0;

    if (gotBarrierIn && gotDaisyBarrierIn && isSuccReady) {
	gotDaisyBarrierIn = 0;

	setKVSCmd(&ptr, &len, DAISY_BARRIER_IN);
	sendKvstoSucc(buffer, len);
    }

    if (isSuccReady && startDaisyBarrier) {
	startDaisyBarrier = 0;
	setKVSCmd(&ptr, &len, DAISY_BARRIER_IN);
	sendKvstoSucc(buffer, len);
    }
}

/**
 * @brief Parse a kvs update message from ther logger.
 *
 * Parse a kvs update message from the logger. Save all received key-value pairs
 * into the local kvs space. All get requests from the local mpi client will be
 * answered using the local kvs space. If we got all update message we can
 * release the waiting mpi client via barrier-out.
 *
 * If we are the last client in the chain, we need to tell the logger that the
 * update was successfull completed.
 *
 * @param pmiLine The update message to parse.
 *
 * @param lastUpdateMsg Flag to indicate if this is the last update message.
 *
 * @return No return value.
 */
static void parseUpdateMessage(char *pmiLine, int lastUpdateMsg)
{
    char vname[KEYLEN_MAX], kvsname[KEYLEN_MAX];
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
	/* kvsname */
	if (!strcmp(vname,"kvsname")) {
	    strncpy(kvsname, value, sizeof(kvsname));
	} else if (strlen(kvsname) > 1) {
	    /* save key/value to kvs */
	    if (kvs_put(kvsname, vname, value)) {
		elog("%s(r%i): error saving kvs update: kvsname:%s, key:%s,"
			" value:%s\n", __func__, rank, kvsname, vname, value);
		critErr();
		return;
	    }
	} else {
	    elog("%s(r%i): received invalid update kvs request from"
		    " logger\n", __func__, rank);
	    critErr();
	    return;
	}
	nextvalue = strtok_r(NULL, delimiters, &saveptr);
    }
    updateMsgCount++;

    if (lastUpdateMsg) {
	char *ptr = buffer;
	size_t len = 0;

	/* we got all update msg, so we can release the waiting mpi client */
	gotBarrierIn = 0;
	snprintf(buffer, sizeof(buffer), "cmd=barrier_out\n");
	PMI_send(buffer);

	/* if we are the last in the chain we acknowledge the logger */
	if (succtid == loggertid) {
	    setKVSCmd(&ptr, &len, UPDATE_CACHE_FINISH);
	    addKVSInt32(&ptr, &len, &updateMsgCount);
	    sendKvstoLogger(buffer, len);
	}
	updateMsgCount = 0;
    }
}

/**
 * @brief Hanlde a pmi barrier_in request from the local mpi client.
 *
 * The mpi client has to wait until all clients have entered barrier.
 * A barrier is typically used to syncronize the local kvs space.
 *
 * We can forwarder a pending daisy chain barrier_in message now.
 * If we are the client in the chain we have to start the daisy message.
 *
 * Also we can start updating the local kvs space with received kvs update
 * messages.
 *
 * @param msgBuffer The buffer which contains the pmi barrier_in msg.
 *
 * @return Always returns 0.
 */
static int p_Barrier_In(char *msgBuffer)
{
    Update_Buffer_t *uBuf;
    list_t *pos, *tmp;
    char *ptr = buffer;

    gotBarrierIn = 1;

    if (predtid == loggertid) {
	/* if we are the first in chain, send starting barrier msg */
	startDaisyBarrier = 1;
    }
    checkDaisyBarrier();

    /* update local kvs cache with buffered update */
    if (list_empty(&uBufferList.list)) return 0;

    list_for_each_safe(pos, tmp, &uBufferList.list) {
	if ((uBuf = list_entry(pos, Update_Buffer_t, list)) == NULL) return 0;
	if (!uBuf->gotBarrierIn) {

	    char pmiLine[PMIU_MAXLINE];
	    size_t len, pLen;

	    /* skip cmd */
	    ptr = uBuf->msg + sizeof(uint8_t);

	    len = getKVSString(&ptr, pmiLine, sizeof(pmiLine));
	    pLen = uBuf->len - sizeof(uint8_t) - sizeof(uint16_t);

	    if (strlen(pmiLine) != len || len != pLen) {
		elog("%s: invalid update msg len:%zu strlen:%zu bufLen:%zu\n",
			__func__, len, strlen(pmiLine), pLen);
		critErr();
	    }

	    parseUpdateMessage(pmiLine, uBuf->lastUpdate);

	    uBuf->gotBarrierIn = 1;
	    if (uBuf->isSuccReady) deluBufferEntry(uBuf);
	}
    }

    return 0;
}

/**
 * @brief Finalize the pmi.
 *
 * Returns PMI_FINALIZED to notice the forwarder that the
 * child has successfull finished execution. The forwarder will release
 * the child and then call pmi_finalize() to allow the child to exit.
 *
 * @return Returns PMI_FINALIZED.
 * */
static int p_Finalize()
{
    char *ptr = buffer;
    size_t len = 0;

    /* inform the logger we are leaving the kvs space */
    setKVSCmd(&ptr, &len, LEAVE);
    sendKvstoLogger(buffer, len);

    return PMI_FINALIZED;
}

/**
 * @brief Return the default kvs name.
 *
 * Returns the default kvs name.
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
 * @brief Creates a new kvs.
 *
 * Not supported by hydra anymore. Seems nobody using it. Disabled to make new
 * kvs implementation more straighforward.
 *
 * Creates a new key value space.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Create_Kvs(void)
{
    char kvsmsg[PMIU_MAXLINE], kvsname[KVSNAME_MAX];

    /* disable creation of new kvs */
    PMI_send("cmd=newkvs rc=-1\n");
    return 0;

    /* create new kvs name */
    snprintf(kvsname, sizeof(kvsname), "%s_%i\n", kvs_name_prefix, kvs_next++);

    /* create local kvs */
    if (!kvs_create(kvsname)) {
	elog("%s(r%i): create kvs %s failed\n", __func__, rank, kvsname);
	PMI_send("cmd=newkvs rc=-1\n");
	return 1;
    }

    /* create kvs in logger */

    /* return succes to client */
    snprintf(kvsmsg, sizeof(kvsmsg), "cmd=newkvs kvsname=%s\n",
		kvsname);
    PMI_send(kvsmsg);
    return 0;
}

/**
 * @brief Delete a kvs.
 *
 * Not supported by hydra anymore. Seems nobody using it. Disabled to make new
 * kvs implementation more straighforward.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Destroy_Kvs(char *msgBuffer)
{
    char kvsname[KVSNAME_MAX];

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

    /* destroy kvs */
    if (kvs_destroy(kvsname)) {
	elog("%s(r%i): error destroying kvs %s\n", __func__, rank, kvsname);
	PMI_send("cmd=kvs_destroyed rc=-1\n");
	return 1;
    }

    /* return succes to client */
    PMI_send("cmd=kvs_destroyed rc=0\n");

    /* destroy kvs in logger */

    return 0;
}

/**
 * @brief Put a key/value pair into the kvs.
 *
 * The key is save into the local kvs and instantly send to logger
 * to be saved in the global kvs.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Put(char *msgBuffer)
{
    char kvsname[KVSNAME_MAX], key[KEYLEN_MAX], value[VALLEN_MAX];
    char *ptr = buffer;
    size_t len = 0;

    getpmiv("kvsname", msgBuffer, kvsname, sizeof(kvsname));
    getpmiv("key", msgBuffer, key, sizeof(key));
    getpmiv("value", msgBuffer, value, sizeof(value));

    /* check msg */
    if (kvsname[0] == 0 || key[0] == 0 || value[0] == 0) {
	if (debug_kvs) {
	    elog( "%s(r%i): received invalid pmi put msg\n",
		    __func__, rank);
	}
	PMI_send("cmd=put_result rc=-1 msg=error_invalid_put_msg\n");
	return 1;
    }

    /* save to local kvs */
    if ((kvs_put(kvsname, key, value))) {
	if (debug_kvs) {
	    elog("%s(r%i): error while put key:%s value:%s to kvs:%s \n",
		    __func__, rank, key, value, kvsname);
	}
	PMI_send("cmd=put_result rc=-1 msg=error_in_kvs\n");
	return 1;
    }

    /* forward put request to the global kvs in logger */
    setKVSCmd(&ptr, &len, PUT);
    addKVSString(&ptr, &len, kvsname);
    addKVSString(&ptr, &len, key);
    addKVSString(&ptr, &len, value);
    sendKvstoLogger(buffer, len);

    /* return succes to client */
    PMI_send("cmd=put_result rc=0\n");

    return 0;
}

/**
 * @brief The mpi client reads a key/value pair from local kvs.
 *
 * Read a value from the local kvs.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Get(char *msgBuffer)
{
    char reply[PMIU_MAXLINE], kvsname[KVSNAME_MAX], key[KEYLEN_MAX];
    char *value;

    /* extract parameters */
    getpmiv("kvsname", msgBuffer, kvsname, sizeof(kvsname));
    getpmiv("key", msgBuffer, key, sizeof(key));

    /* check msg */
    if (kvsname[0] == 0 || key[0] == 0) {
	if (debug_kvs) {
	    elog("%s(r%i): received invalid pmi get cmd\n", __func__, rank);
	}
	PMI_send("cmd=get_result rc=-1 msg=error_invalid_get_msg\n");
	return 1;
    }

    /* get value from kvs */
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

    /* send pmi to client */
    snprintf(reply, sizeof(reply), "cmd=get_result rc=%i value=%s\n",
	     PMI_SUCCESS, value);
    PMI_send(reply);

    return 0;
}

/**
 * @brief Publish a service.
 *
 * Make a service public even for processes which are not
 * connected over pmi.
 *
 * Not implemented (yet).
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Publish_Name(char *msgBuffer)
{
    char service[VALLEN_MAX], port[VALLEN_MAX];
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
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Unpublish_Name(char *msgBuffer)
{
    char service[VALLEN_MAX];
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
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Lookup_Name(char *msgBuffer)
{
    char service[VALLEN_MAX];
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
 * @param msgBuffer Buffer which holds the pmi spawn message.
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
 * @brief Spawn one or more processes.
 *
 * Spawn one or more processes. The new spawned processes have their own MPI
 * mpi_comm_world and therefore separate kvs, pmi barriers and a separate
 * daisy chain. This will be established by putting them into a new pmi group in
 * the logger.
 *
 * The first step is to create a new pmi group, since we need
 * information about it before we can continue.
 *
 * @param msgBuffer Buffer which holds the pmi spawn message.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Spawn(char *msgBuffer)
{
    char kvsname[KEYLEN_MAX];
    char *ptr;
    size_t len;

    elog("%s(dump):%s\n", __func__, msgBuffer);

    /* check for correct arguments */
    if ((checkSpawnCompatibility(msgBuffer))) return 1;

    /* let the logger do the work */
    ptr = buffer;
    len = 0;
    setKVSCmd(&ptr, &len, SPAWN);
    snprintf(kvsname, sizeof(kvsname), "kvs_%i:%i_%i", PSC_getMyID(), getpid(),
		kvs_next++);
    addKVSString(&ptr, &len, kvsname);
    addKVSString(&ptr, &len, msgBuffer);
    sendKvstoLogger(buffer, len);

    return 0;
}

/**
 * @brief Get a key-value pair by index.
 *
 * Get a key-value pair by specific index from key value space.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_GetByIdx(char *msgBuffer)
{
    char reply[PMIU_MAXLINE];
    char idx[VALLEN_MAX], kvsname[KVSNAME_MAX];
    char *value, *ret, name[KEYLEN_MAX];
    int index, len;

    getpmiv("idx", msgBuffer, idx, sizeof(msgBuffer));
    getpmiv("kvsname", msgBuffer, kvsname, sizeof(msgBuffer));

    /* check msg */
    if (idx[0] == 0 || kvsname[0] == 0) {
	if (debug_kvs) {
	    elog("%s(r%i): received invalid pmi getbiyidx msg\n",
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
 * @brief Init the local pmi communication.
 *
 * Init the pmi communication, mainly to be sure both sides
 * are using the same protocol versions.
 *
 * The init process is monitored by the logger to make sure all mpi clients are
 * started successfully in time.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
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
	elog("%s(r%i): received invalid pmi init cmd\n", __func__, rank);
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
	elog("%s(r%i): unsupported pmi version received: version=%i,"
		" subversion=%i\n", __func__, rank, atoi(pmiversion),
		atoi(pmisubversion));
	return critErr();
    }
    pmi_init_client = 1;

    /* tell logger that the mpi client was initialized */
    ptr = buffer;
    len = 0;
    setKVSCmd(&ptr, &len, INIT);
    sendKvstoLogger(buffer, len);

    return 0;
}

/**
 * @brief Get kvs maxes values.
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
	     KVSNAME_MAX, KEYLEN_MAX, VALLEN_MAX);
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
 *	rank1,rank2,..,rankN: a comma seperated list of ranks executed on the
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
 * Use a handshake to authenticate the mpi client. Note that it is not
 * mandatory for the mpi client to authenticate itself. Hydra continous even
 * with incorrect authentification.
 *
 * @param msgBuffer The buffer which contains the pmi
 * msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_InitAck(char *msgBuffer)
{
    char *pmi_id, client_id[KEYLEN_MAX], reply[PMIU_MAXLINE];

    if (debug) {
	elog("%s(r%i): received pmi initack msg:%s\n",
		__func__, rank, msgBuffer);
    }

    if (!(pmi_id = getenv("PMI_ID"))) {
	elog("%s(r%i): no PMI_ID is set\n", __func__, rank);
	return critErr();
    }

    getpmiv("pmiid", msgBuffer, client_id, sizeof(client_id));

    if (client_id[0] == 0) {
	elog("%s(r%i): empty pmiid from mpi client\n", __func__, rank);
	return critErr();
    }

    if (!!(strcmp(pmi_id, client_id))) {
	elog("%s(r%i): invalid pmi_id '%s' from mpi client should be '%s'\n",
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
 * starts, so even before pmi init.
 *
 * @param msgBuffer The buffer which contains the pmi msg to handle.
 *
 * @return Returns 0 for success and 1 on error.
 */
static int p_Execution_Problem(char *msgBuffer)
{
    char exec[VALLEN_MAX], reason[VALLEN_MAX];

    getpmiv("reason",msgBuffer,exec,sizeof(exec));
    getpmiv("exec",msgBuffer,reason,sizeof(reason));

    if (exec[0] == 0 || reason[0] == 0) {
	elog("%s(r%i): received invalid pmi execution problem msg\n",
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

int pmi_init(int pmisocket, PStask_ID_t loggertaskid, int pRank)
{
    char *envPtr, *env_kvs_name, *ptr;
    size_t len;

    rank = pRank;
    loggertid = loggertaskid;
    pmisock = pmisocket;

    INIT_LIST_HEAD(&uBufferList.list);

    /* check if pred or succ client is already gone */
    if (predtid == -2 || succtid == -2) {
	return 0;
    }

    /* we need the pred and succ for the daisy chain */
    if (predtid < 0) {
	elog("%s(r%i): invalid predtid '%s'\n", __func__,
		rank, PSC_printTID(predtid));
	return critErr();
    }
    if (succtid < 0) {
	elog("%s(r%i): invalid succtid '%s'\n", __func__,
		rank, PSC_printTID(succtid));
	return critErr();
    }

    /* the logger is always ready */
    if (succtid == loggertid) isSuccReady = 1;

    if (pmisocket < 1) {
	elog("%s(r%i): invalid pmi socket '%i'\n", __func__, rank, pmisocket);
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

    /* set the mpi universe size */
    if ((envPtr = getenv("PMI_UNIVERSE_SIZE"))) {
	universe_size = atoi(envPtr);
    } else {
	universe_size = 1;
    }

    /* set the name of the kvs space */
    if (!(env_kvs_name = getenv("PMI_KVS_TMP"))) {
	strncpy(kvs_name_prefix, "kvs_root", sizeof(kvs_name_prefix) -1);
    } else {
	snprintf(kvs_name_prefix, sizeof(kvs_name_prefix), "kvs_%s",
		    env_kvs_name);
    }

    is_init = 1;
    updateMsgCount = 0;

    /* set my kvs name */
    if (!(env_kvs_name = getenv("PMI_KVSNAME"))) {
	snprintf(myKVSname, sizeof(myKVSname), "%s_%i", kvs_name_prefix,
		    kvs_next++);
    } else {
	strcpy(myKVSname, env_kvs_name);
    }

    /* create local kvs space */
    if (kvs_create(myKVSname)) {
	elog("%s(r%i): error creating local kvs\n", __func__, rank);
	return critErr();
    }

    /* join global kvs space */
    ptr = buffer;
    len = 0;
    setKVSCmd(&ptr, &len, JOIN);
    addKVSString(&ptr, &len, myKVSname);
    sendKvstoLogger(buffer, len);

    return 0;
}

int setPMIclientInfo(void *data)
{
    char *ptr = data;

    ptr = data;

    /* set predecessor tid */
    predtid = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    /* set successor tid */
    succtid = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    /* set the pmi rank */
    pmiRank = *(int *) ptr;
    ptr += sizeof(int);

    return 0;
}

/**
 * @brief Extract the pmi command.
 *
 * Parse a pmi message and return the command.
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
	elog("%s(r%i): invalid pmi msg '%s' received\n", __func__, rank, msg);
	return !critErr();
    }

    msgCopy = strdup(msg);
    cmd = strtok_r(msgCopy, delimiters, &saveptr);

    while ( cmd != NULL ) {
	if (!strncmp(cmd,"cmd=", 4)) {
	    cmd += 4;
	    strncpy(cmdbuf, cmd, bufsize);
	    free(msgCopy);
	    return 1;
	}
	if (!strncmp(cmd,"mcmd=", 5)) {
	    cmd += 5;
	    strncpy(cmdbuf, cmd, bufsize);
	    free(msgCopy);
	    return 1;
	}
	cmd = strtok_r(NULL, delimiters, &saveptr);
    }

    free(msgCopy);
    return 0;
}

int handlePMIclientMsg(char *msg)
{
    int i;
    char cmd[VALLEN_MAX];
    char reply[PMIU_MAXLINE];

    if (is_init != 1) {
	elog("%s(r%i): you must call pmi_init first, msg '%s'\n",
		__func__, rank, msg);
	return critErr();
    }

    if (!msg) {
	elog("%s(r%i): invalid pmi msg\n", __func__, rank);
	return critErr();
    }

    if (strlen(msg) > PMIU_MAXLINE ) {
	elog("%s(r%i): pmi msg to long, msg_size=%i and allowed_size=%i\n",
	    __func__, rank, (int)strlen(msg), PMIU_MAXLINE);
	return critErr();
    }

    if (!extractPMIcmd(msg, cmd, sizeof(cmd)) || strlen(cmd) <2) {
	elog("%s(r%i): invalid pmi cmd received, msg was '%s'\n",
		__func__, rank, msg);
	return critErr();
    }

    if (debug) {
	elog("%s(r%i): got %s\n", __func__, rank, msg);
    }

    /* find pmi cmd */
    for (i=0; i< pmi_com_count; i++) {
	if (!strcmp(cmd,pmi_commands[i].Name)) {
		return pmi_commands[i].fpFunc(msg);
	}
    }

    /* find short pmi cmd */
    for (i=0; i< pmi_short_com_count; i++) {
	if (!strcmp(cmd,pmi_short_commands[i].Name)) {
		return pmi_short_commands[i].fpFunc();
	}
    }

    /* cmd not found */
    elog("%s(r%i): unsupported pmi cmd received '%s'\n", __func__, rank, cmd);

    snprintf(reply, sizeof(reply),
	     "cmd=%s rc=%i info=not_supported_cmd\n", cmd, PMI_ERROR);
    PMI_send(reply);

    return critErr();
}

/**
* @brief Release the PMI client.
*
* Finalize the pmi connection and release the mpi client.
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
 * @brief Buffer an update message until both the local mpi client and our
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
 * mpi client.
 *
 * @param lastUpdate If this flag is set to 1 then the update is complete
 * with this message.
 *
 * @param strLen The lenght of the update payload.
 *
 * @return No return value.
 */
static void bufferCacheUpdate(char *msg, size_t msgLen, int isSuccReady,
				int barrierIn, int lastUpdate, size_t strLen)
{
    Update_Buffer_t *uBuf;

    if (!(uBuf = (Update_Buffer_t *) malloc(sizeof(Update_Buffer_t)))) {
	elog("%s(r%i): out of memory\n", __func__, rank);
	exit(1);
    }

    if (!(uBuf->msg = malloc(msgLen))) {
	elog("%s(r%i): out of memory\n", __func__, rank);
	exit(1);
    }

    memcpy(uBuf->msg, msg, msgLen);
    uBuf->len = msgLen;
    uBuf->isSuccReady = isSuccReady;
    uBuf->gotBarrierIn = barrierIn;
    uBuf->lastUpdate = lastUpdate;

    list_add_tail(&(uBuf->list), &uBufferList.list);
}

/**
 * @brief Handle a kvs update message from the logger.
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
    int len, msgSize;

    /* get the pmi update message */
    if ((len = getKVSString(&ptr, pmiLine, sizeof(pmiLine))) < 7) {
	elog("%s(%i): invalid update len '%i'\n", __func__, rank, len);
	critErr();
	return;
    }
    msgSize = msg->header.len - PSLog_headerSize;

    /* sanity check */
    if ((int) (PMIUPDATE_HEADER_LEN + len) != msgSize) {
	elog("%s(r%i): got invalid update message\n", __func__, rank);
	elog("%s(r%i): msg.header.size:%i, len:%i msgSize:%i pslog_header:%i\n",
		__func__, rank, msg->header.len, len, msgSize, PSLog_headerSize);
	critErr();
	return;
    }

    /* we need to buffer the message for later */
    if (!isSuccReady || !gotBarrierIn) {
	bufferCacheUpdate(msg->buf, msgSize, isSuccReady, gotBarrierIn,
			    lastUpdateMsg, len);
    }

    /* sanity check */
    if (lastUpdateMsg && !gotBarrierIn) {
	elog("%s:(r%i): got last update message, but I have no barrier_in\n",
		__func__, rank);
	critErr();
    }

    /* forward to successor */
    if (isSuccReady && succtid != loggertid) sendKvstoSucc(msg->buf, msgSize);

    /* wait with the update until we got the barrier_in from local mpi client */
    if (!gotBarrierIn) return;

    /* update the local kvs */
    parseUpdateMessage(pmiLine, lastUpdateMsg);
}

/**
 * @brief Hanlde a kvs barrier out message.
 *
 * Release the waiting mpi client from the barrier.
 *
 * @return No return value.
 */
static void handleDaisyBarrierOut(PSLog_Msg_t *msg)
{
    if (succtid != loggertid) {
	/* forward to next client */
	sendKvstoSucc(msg->buf, sizeof(uint8_t));
    }

    /* Forward msg from logger to client */
    snprintf(buffer, sizeof(buffer), "cmd=barrier_out\n");
    PMI_send(buffer);

    gotBarrierIn = 0;
}

/**
 * @brief Set a new daisy barrier.
 *
 * @return No return value.
 */
static void handleDaisyBarrierIn()
{
    if (predtid == loggertid) {
	elog("%s(r%i): received daisy_barrier_in from logger\n",
		__func__, rank);
	return;
    }

    gotDaisyBarrierIn = 1;
    checkDaisyBarrier();
}

static void handleSpawnResult(char *ptr)
{
    /* extract spawn result */
    /*
    np = getKVSInt32(&ptr);
    getKVSString(&ptr, kvsname, sizeof(kvsname));
    minRank = getKVSInt32(&ptr);
    maxRank = getKVSInt32(&ptr);

    elog("%s: np:%i kvs:%s min:%i max:%i\n", __func__,
	    np, kvsname, minRank, maxRank);
    */
    PMI_send("cmd=spawn_result rc=-1\n");
}

/**
 * @brief Our successor is now ready to receive messages.
 *
 * The successor finished its initialize phase with the logger and is now ready
 * to receive pmi messages from us. We can now forward all buffered
 * barrier/update messages.
 *
 * @return No return value.
 */
static void handleSuccReady()
{
    list_t *pos, *tmp;
    Update_Buffer_t *uBuf;

    isSuccReady = 1;
    checkDaisyBarrier();

    /* forward buffered messages */
    if (list_empty(&uBufferList.list)) return;

    list_for_each_safe(pos, tmp, &uBufferList.list) {
	if ((uBuf = list_entry(pos, Update_Buffer_t, list)) == NULL) return;
	if (!uBuf->isSuccReady) {

	    if (succtid != loggertid) {
		char *ptr;
		char pmiLine[PMIU_MAXLINE];
		size_t len, pLen;

		ptr = uBuf->msg + sizeof(uint8_t);
		len = getKVSString(&ptr, pmiLine, sizeof(pmiLine));
		pLen = uBuf->len - sizeof(uint8_t) - sizeof(uint16_t);

		/* sanity check */
		if (strlen(pmiLine) != len || len != pLen) {
		    elog("%s: invalid update msg len:%zu strlen:%zu "
			    "bufLen:%zu\n", __func__, len, strlen(pmiLine),
			    pLen);
		    critErr();
		}
	    }

	    if (succtid != loggertid) sendKvstoSucc(uBuf->msg, uBuf->len);
	    uBuf->isSuccReady = 1;

	    if (uBuf->gotBarrierIn) deluBufferEntry(uBuf);
	}
    }
}

int handleKVSMessage(void *vmsg)
{
    PSLog_Msg_t *msg = vmsg;
    uint8_t cmd;
    char *ptr = msg->buf;

    /* extract cmd from msg */
    cmd = getKVSCmd(&ptr);

    if (debug_kvs) {
	elog("%s(r%i): cmd '%s'\n", __func__, rank, PSKVScmdToString(cmd));
    }

    switch(cmd) {
	case NOT_AVAILABLE:
	    elog("%s(r%i): global kvs is not available, exiting\n",
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
	    handleSuccReady();
	    break;
	case DAISY_BARRIER_IN:
	    handleDaisyBarrierIn();
	    break;
	case DAISY_BARRIER_OUT:
	    handleDaisyBarrierOut(msg);
	    break;
	case SPAWN_RESULT:
	    handleSpawnResult(ptr);
	    break;
	default:
	    elog("%s(r%i): got unknown kvs msg '%s:%i'\n", __func__,
		    rank, PSKVScmdToString(cmd), cmd);
	    critErr();
    }
    return 0;
}
