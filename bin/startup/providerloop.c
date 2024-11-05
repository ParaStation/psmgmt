/*
 * ParaStation
 *
 * Copyright (C) 2013-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "providerloop.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "pscommon.h"
#include "kvs.h"
#include "kvscommon.h"
#include "timer.h"
#include "kvslog.h"
#include "psi.h"
#include "selector.h"
#include "pslog.h"

/* PSLog buffer size - PMIHEADER */
#define PMIUPDATE_PAYLOAD (1048 - sizeof(uint8_t) - sizeof(uint32_t)	\
			   - sizeof(uint16_t))

/* Ring buffer to keep track of KVS update messages */
#define KVS_UPDATE_FIELDS 100

typedef enum {
    PMI_CLIENT_JOINED = 0x001,
    PMI_CLIENT_INIT   = 0x002,
    PMI_CLIENT_GONE   = 0x004,
} Clients_Flags_t;

typedef struct {
    PStask_ID_t tid;	    /**< PStask ID of the forwarder */
    Clients_Flags_t flags;  /**< Track the client state */
    int init;		    /**< Flag to mark the successful PMI init */
    int rank;		    /**< The parastation rank of the PMI client */
    int pmiRank;	    /**< The PMI (MPI WORLD) rank of the PMI client */
} PMI_Clients_t;

/** Id of the init timer */
static int timerid = -1;

/** Counter of client init rounds */
static int initRoundsCount = 0;

/** Set the timeout of the client init phase */
static int initTimeout = 0;

/** Set the number of rounds for the client init phase */
static int initRounds = 2;

/** The number of received PMI client init msgs */
static int initCount = 0;

/** Track the total length of new KVS updates */
static size_t kvsUpdateLen = 0;

/** Track which cache updates still have to be sent */
static char * *kvsUpdateCache = NULL;

/** Size of @ref kvsUpdateCache */
static size_t kvsCacheSize = 0;

/** Next entry of @ref kvsUpdateCache to use */
static size_t nextCacheEntry = 0;

/** Index to the next update element to use */
static int nextUpdateField = 0;

/** Track KVS update message count, so we can distinguish them */
static int kvsUpdateTrack[KVS_UPDATE_FIELDS];

/** Array to store infos about forwarders joined our KVS */
static PMI_Clients_t *clients;

/** Maximum number of PMI clients we will handle */
static int maxClients;

/** The job's unique KVS name */
static char kvsname[PMI_KVSNAME_MAX];

/** Total count of all KVS clients we know */
static int totalKVSclients = 0;

/** FD which is connected to our local forwarder */
static int forwarderFD = -1;

/** FD which is connected to our local daemon */
static int daemonFD = -1;

/** Flag to set the verbosity level */
static bool verbose = false;

/** The PStask ID of our logger */
static PStask_ID_t loggertid = -1;

/** Flag to be set if kill signals should not be forwarded to parents */
static char noParricide = false;

/** Number of received kvs_put messages */
static int putCount = 0;

/** Counter indicating the need to wait for late arriving kvs_put messages */
static int waitForPuts = 0;

/** Flag to enable measurement output */
static bool measure = false;

/** Timer to measure the kvs phases */
static struct timeval time_start;

/** Timer to measure the kvs phases */
static struct timeval time_now;

/** Timer to measure the kvs phases */
static struct timeval time_diff;


/**
 * @brief Send a release message to the logger.
 *
 * @return No return value.
 */
static void releaseMySelf(const char *func)
{
    if (Selector_isRegistered(daemonFD)) Selector_remove(daemonFD);
    if (Selector_isRegistered(forwarderFD)) Selector_remove(forwarderFD);

    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_RELEASE,
	    .dest = PSC_getMyTID(),
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.signal = -1,
	.answer = 1, };
    if (PSI_sendMsg(&msg) == -1) {
	mwarn(errno, "%s: sending msg failed", __func__);
	return;
    }

    while (true) {
	PSLog_Msg_t answer;
	int ret = PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer));

	if (ret <= 0) {
	    if (!ret) {
		mlog("%s: unexpected message length 0\n", __func__);
	    } else {
		mwarn(errno, "%s: PSI_recvMsg", __func__);
	    }
	    return;
	}

	switch (answer.header.type) {
	case PSP_CD_RELEASERES:
	    break;
	case PSP_CC_ERROR:
	    if (answer.header.sender != loggertid) continue;
	    mlog("%s: logger already died\n", __func__);
	    break;
	case PSP_CD_WHODIED:
	case PSP_CC_MSG:
	    /* ignore late arriving messages */
	    continue;
	default:
	    mlog("%s: wrong message type %d (%s)\n", __func__,
		 answer.header.type, PSP_printMsg(answer.header.type));
	}
	break;
    }

    if (verbose) printf("(%s:) KVS process %s finished\n", func,
			PSC_printTID(PSC_getMyTID()));
}

/**
 * @brief Send a term to all processes and release myself.
 *
 * @return Never returns.
 */
__attribute__ ((noreturn))
static void terminateJob(const char *func)
{
    if (verbose) {
	mlog("%s: Terminating the job.\n", func);
    }

    /* send kill signal to all children */
    DDSignalMsg_t msg = {
	.header = {
	    .type = PSP_CD_SIGNAL,
	    .dest = (loggertid == -1 || noParricide) ?
	    PSC_getMyTID() : loggertid,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(msg) },
	.signal = -1,
	.param = getuid(),
	.pervasive = 1,
	.answer = 1, };

    PSI_sendMsg((DDMsg_t *)&msg);

    releaseMySelf(__func__);
    exit(1);
}

/**
 * @brief Grow a KVS update cache
 *
 * @param minSize Minimum size of the KVS update cache upon return
 *
 * @return No return value
 */
static void growKvsUpdateCache(size_t minSize)
{
    mdbg(KVS_LOG_PROVIDER, "%s(%zd)\n", __func__, minSize);

    size_t newSize = kvsCacheSize + maxClients;
    if (newSize < minSize) newSize = minSize;

    kvsUpdateCache = realloc(kvsUpdateCache, sizeof(*kvsUpdateCache) * newSize);
    if (!kvsUpdateCache) {
	mwarn(errno, "%s: realloc()", __func__);
	terminateJob(__func__);
    }

    for (size_t i = kvsCacheSize; i < newSize; i++) kvsUpdateCache[i] = NULL;
    kvsCacheSize = newSize;
}

/**
 * @brief Initialize the KVS.
 *
 * @return No return value.
 */
static void initKVS(void)
{
    clients = malloc(sizeof(*clients) * maxClients);
    if (!clients) {
	mwarn(errno, "%s", __func__);
	terminateJob(__func__);
    }

    for (int i = 0; i < maxClients; i++) {
	clients[i].tid = -1;
	clients[i].flags = 0;
	clients[i].rank = -1;
	clients[i].pmiRank = -1;
    }

    growKvsUpdateCache(maxClients + 10);

    memset(kvsUpdateTrack, 0, sizeof(kvsUpdateTrack));
}

/**
 * @brief Find a PMI client from a PSLog message.
 *
 * @param msg The PSLog message to identify the client.
 *
 * @return Returns a pointer to the requested client or terminates
 * the Job on error.
 */
#define findClient(msg, term) __findClient(msg, term, __func__)
static PMI_Clients_t *__findClient(PSLog_Msg_t *msg, bool term, const char *func)
{
    PStask_ID_t tid = msg->header.sender;

    for (int i = 0; i < maxClients; i++) {
	if (clients[i].tid == tid) return &clients[i];
    }

    if (term) {
	mlog("%s(%s): invalid client with rank %i tid %s connected to me\n",
	     __func__, func, msg->sender, PSC_printTID(tid));
	terminateJob(__func__);
    }

    return NULL;
}

/**
 * @brief Test incoming messages
 *
 * Test, if incoming message @a msg is consistent with internal KVS
 * settings. Tests are consistency of task-ID and rank fitting into
 * @ref clients array.
 *
 * @param fName Name of the calling function
 *
 * @param client The client or NULL; in the latter case try to find it
 *
 * @param msg The message to test
 *
 * @return No return value
 */
static void testMsg(const char fName[], PMI_Clients_t *client, PSLog_Msg_t *msg)
{
    if (!client) client = findClient(msg, true);

    /* check for invalid ranks */
    if (client->pmiRank < 0 || client->pmiRank > maxClients -1) {
	mlog("%s: invalid PMI rank index %i for rank %i\n", __func__,
	     client->rank, msg->sender);
	terminateJob(__func__);
    }

    /* check for false clients */
    if (client->tid != msg->header.sender) {
	mlog("%s: rank %d pmiRank %i from %s", fName, client->rank,
	     client->pmiRank, PSC_printTID(msg->header.sender));
	mlog(" should come from %s\n", PSC_printTID(client->tid));
	terminateJob(__func__);
    }
}

/**
 * @brief Send KVS message to a PMI client
 *
 * Send a KVS message to the PMI client with task ID @a tid. The
 * payload of size @a len is provided within the buffer @a msgBuf.
 *
 * @param tid Task ID to send the KVS message to.
 *
 * @param msgBuf Message payload to send
 *
 * @param len Length of the message payload to send
 *
 * @return No return value.
 */
static void sendKvsMsg(PStask_ID_t tid, char *msgBuf, size_t len)
{
    PSLog_Msg_t msg;

    msg.header.type = PSP_CC_MSG;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = tid;
    msg.version = 2;
    msg.type = KVS;
    msg.sender = -1;
    msg.header.len = (sizeof(msg) - sizeof(msg.buf)) + len;

    if (len > sizeof(msg.buf)) {
	mlog("%s: payload too large\n", __func__);
	return;
    }

    memcpy(msg.buf, msgBuf, len);
    if (PSI_sendMsg(&msg) == -1) {
	mwarn(errno, "%s: sending msg failed", __func__);
    }
}

/**
 * @brief Send a KVS message to first client in the daisy chain.
 *
 * @param msg The message to send.
 *
 * @param len The length of the message to send.
 *
 * @return No return value.
 */
static void sendMsgToKvsSucc(char *msgBuf, size_t len)
{
    if (!msgBuf) {
	mlog("%s: no payload\n", __func__);
	return;
    }
    if (clients[0].tid == -1) {
	mlog("%s: daisy-chain unusable: invalid client[0] tid\n", __func__);
	return;
    }

    sendKvsMsg(clients[0].tid, msgBuf, len);
}

/**
 * @brief Send a KVS update to all clients in a PMI group
 *
 * @param finish Flag sending of finish message when the update is
 * complete. Otherwise only update messages are sent.
 *
 * @return No return value
 */
static void sendKvsUpdateToClients(bool finish)
{
    const size_t limit = MIN(PMIUPDATE_PAYLOAD, PMIU_MAXLINE);
    size_t ent;
    for (ent = 0; ent < nextCacheEntry
	     && (finish || kvsUpdateLen > limit); ent++) {
	char kvsmsg[PMIU_MAXLINE] = { '\0' };

	/* add key-value pairs to the msg */
	for (; ent < nextCacheEntry; ent++) {
	    char *nextEnt = kvsUpdateCache[ent];

	    if (!nextEnt) {
		mlog("%s: invalid KVS entry %zi (putCount %i)\n", __func__,
		     ent, putCount);
		terminateJob(__func__);
	    }

	    mdbg(KVS_LOG_PROVIDER, "%s: inspect ent %zd len %zd\n", __func__,
		 ent, strlen(nextEnt));
	    size_t newLen = strlen(kvsmsg) + strlen(nextEnt) + 2;
	    if (newLen > limit) break;  /* message full, send right now */

	    mdbg(KVS_LOG_PROVIDER, "%s: add ent %zd len %zd to msg\n", __func__,
		 ent, strlen(nextEnt));
	    strcat(kvsmsg, " ");
	    kvsUpdateLen -= 1;
	    strcat(kvsmsg, nextEnt);
	    kvsUpdateLen -= strlen(nextEnt) + 1;
	}

	int pmiCmd = UPDATE_CACHE;
	if (ent >= nextCacheEntry && finish) pmiCmd = UPDATE_CACHE_FINISH;

	ent--; // retry to sent in the next round if necessary

	mdbg(KVS_LOG_PROVIDER, "%s: sending KVS update: %s len:%lu finish:%i,"
	     " ent:%zi putCount:%i\n", __func__, PSKVScmdToString(pmiCmd),
	     strlen(kvsmsg), finish, ent, putCount);

	PSLog_Msg_t msg;   // abused just to get a buffer of according size
	char *bufPtr = msg.buf;
	size_t bufLen = 0;

	setKVSCmd(&bufPtr, &bufLen, pmiCmd);
	addKVSInt32(&bufPtr, &bufLen, &nextUpdateField);
	addKVSString(&bufPtr, &bufLen, kvsmsg);
	sendMsgToKvsSucc(msg.buf, bufLen);

	kvsUpdateTrack[nextUpdateField]++;
	if (pmiCmd == UPDATE_CACHE_FINISH) {
	    nextUpdateField++;
	    nextUpdateField %= KVS_UPDATE_FIELDS;
	}
	mdbg(KVS_LOG_PROVIDER, "%s: ent %zd nextCacheEntry %zd kvsUpdateLen %zd\n",
	     __func__, ent, nextCacheEntry, kvsUpdateLen);
    }

    /* Eliminate now obsolete cache entries and reorder remaining ones */
    mdbg(KVS_LOG_PROVIDER, "%s: before cleanup ent %zd nextCacheEntry %zd\n",
	 __func__, ent, nextCacheEntry);
    for (size_t c = 0; c < nextCacheEntry; c++) {
	if (c < ent) free(kvsUpdateCache[c]);
	if (ent + c < nextCacheEntry) {
	    kvsUpdateCache[c] = kvsUpdateCache[ent + c];
	} else {
	    kvsUpdateCache[c] = NULL;
	}
    }
    nextCacheEntry -= ent;
    mdbg(KVS_LOG_PROVIDER, "%s: after cleanup ent %zd nextCacheEntry %zd\n",
	 __func__, ent, nextCacheEntry);
}

/**
 * Handle a KVS put message.
 *
 * @param msg The message to handle.
 *
 * @param ptr Pointer to the payload of the message.
 *
 * @return No return value.
 */
static void handleKVS_Put(PSLog_Msg_t *msg, char *ptr)
{
    /* extract key and value */
    char key[PMI_KEYLEN_MAX];
    size_t keyLen = getKVSString(&ptr, key, sizeof(key));
    if (keyLen < 1) goto PUT_ERROR;

    char value[PMI_VALLEN_MAX];
    size_t valLen = getKVSString(&ptr, value, sizeof(value));
    if (valLen < 1) goto PUT_ERROR;

    size_t envStrLen = keyLen + valLen + 2;
    char *envStr = malloc(envStrLen);
    sprintf(envStr, "%s=%s", key, value);

    /* save in global KVS */
    if (!kvs_set(kvsname, key, value)) goto PUT_ERROR;

    putCount++;

    /* add envStr to send-cache */
    if (nextCacheEntry >= kvsCacheSize) growKvsUpdateCache(0);
    kvsUpdateCache[nextCacheEntry++] = envStr;

    kvsUpdateLen += envStrLen + 1 /* extra separator in message to send */;

    /* check if we can start sending update messages */
    if (clients[0].tid != -1) {
	if (waitForPuts && waitForPuts == putCount) {
	    waitForPuts = 0;
	    sendKvsUpdateToClients(true);
	} else if (kvsUpdateLen + 2 >= PMIUPDATE_PAYLOAD) {
	    sendKvsUpdateToClients(false);
	}
    }
    return;

PUT_ERROR:
    mlog("%s: error saving value to kvs\n", __func__);
    terminateJob(__func__);
}

/**
 * @brief Handle a daisy-barrier-in message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleKVS_Daisy_Barrier_In(PSLog_Msg_t *msg, char *ptr)
{
    PMI_Clients_t *client = findClient(msg, true);
    testMsg(__func__, client, msg);

    /* debugging output */
    mdbg(KVS_LOG_PROVIDER, "%s\n", __func__);

    /* check if we got the msg from the last client in chain */
    if (client->pmiRank != maxClients -1) {
	mlog("%s: barrier from wrong rank %i on %s\n", __func__,
	     client->pmiRank, PSC_printTID(msg->header.sender));
	terminateJob(__func__);
    }

    int32_t barrierCount = getKVSInt32(&ptr);
    int32_t globalPutCount = getKVSInt32(&ptr);
    if (barrierCount != maxClients) {
	mlog("%s: not all clients in the barrier\n", __func__);
	terminateJob(__func__);
    }

    if (measure) {
	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_start, &time_diff);
	mlog("%s: barrier complete: bcount %i time %f diff %f\n", __func__,
	     barrierCount, time_now.tv_sec + 1e-6 * time_now.tv_usec,
	     time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
    }

    if (putCount != globalPutCount) {
	mdbg(KVS_LOG_PROVIDER, "%s: missing put messages got %i global %i\n",
	     __func__, putCount, globalPutCount);
	waitForPuts = globalPutCount;
	return;
    }

    if (kvsUpdateCache[0]) {
	/* distribute KVS update */
	sendKvsUpdateToClients(true);
    } else {
	/* send all Clients barrier_out */
	char buffer[sizeof(uint8_t)];
	char *bufPtr = buffer;
	size_t bufLen = 0;

	setKVSCmd(&bufPtr, &bufLen, DAISY_BARRIER_OUT);
	sendMsgToKvsSucc(buffer, bufLen);
    }
}

/**
 * @brief Handle a KVS update-cache-finish message.
 *
 * @param msg The message to handle.
 *
 * @param ptr Pointer to the payload of the message.
 *
 * @return No return value.
 */
static void handleKVS_Update_Cache_Finish(PSLog_Msg_t *msg, char *ptr)
{
    PMI_Clients_t *client = findClient(msg, true);
    testMsg(__func__, client, msg);

    /* parse arguments */
    int mc = getKVSInt32(&ptr);
    int updateIndex = getKVSInt32(&ptr);

    if (updateIndex > KVS_UPDATE_FIELDS - 1) {
	mlog("%s: invalid update index %i from %s\n", __func__,
	     updateIndex, PSC_printTID(msg->header.sender));
	terminateJob(__func__);
    }

    /* check if the result msg came from the last client in chain */
    if (client->pmiRank != maxClients -1) {
	mlog("%s: update from wrong rank %i on %s\n", __func__,
	     msg->sender, PSC_printTID(msg->header.sender));
	terminateJob(__func__);
    }

    /* check if clients got all the updates */
    if (mc != kvsUpdateTrack[updateIndex]) {
	mlog("%s: clients did not get all KVS update msgs %i : %i\n", __func__,
	     mc, kvsUpdateTrack[updateIndex]);
	terminateJob(__func__);
    }

    /* last forward send us an reply, so everything is ok */
    kvsUpdateTrack[updateIndex] = 0;

    if (measure) {
	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_start, &time_diff);
	mlog("%s: cache update complete: time %f diff %f\n", __func__,
	     time_now.tv_sec + 1e-6 * time_now.tv_usec,
	     time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
    }
}

/**
 * @brief Inform a PMI client that its successor became ready.
 *
 * @param tid The Task ID to send the message to.
 *
 * @param succ The Task ID of the successor which became ready.
 *
 * @return No return value.
 */
static void sendDaisyReady(PStask_ID_t tid, PStask_ID_t succ)
{
    char buffer[sizeof(uint8_t) + sizeof(uint32_t)];
    char *bufPtr = buffer;
    size_t bufLen = 0;

    setKVSCmd(&bufPtr, &bufLen, DAISY_SUCC_READY);
    addKVSInt32(&bufPtr, &bufLen, &succ);
    sendKvsMsg(tid, buffer, bufLen);
}

/**
 * @brief Handle PMI init timeouts.
 *
 * Callback function to handle PMI init timeouts.
 *
 * Terminate the job, send all children term signal, to avoid that the
 * job hangs infinite.
 *
 * @return No return value.
 */
static void handleInitTimeout(int dummy, void *ptr)
{
    mlog("Timeout: Not all clients called pmi_init(): "
	 "init=%i left=%i round=%i\n", initCount, maxClients - initCount,
	 initRounds - initRoundsCount+1);

    if (--initRoundsCount) return;

    mlog("Missing clients:\n");
    for (int i = 0; i < maxClients; i++) {
	if (!clients[i].init) {
	    mlog("%s rank %d\n", (clients[i].tid == -1) ?
		 "unconnected" : PSC_printTID(clients[i].tid), i);
	}
    }

    /* kill all children */
    terminateJob(__func__);
}

#define USEC_PER_CLIENT 500
/**
 * @brief Set the timeout for the KVS init phase.
 *
 * @return No return value.
 */
static void setInitTimeout(void)
{
    struct timeval timer;

    if (initTimeout == -1) return;

    if (initTimeout) {
	/* timeout from user */
	timer.tv_sec = initTimeout;
	timer.tv_usec = 0;
    } else {
	/* timeout after 1 min + n * USEC_PER_CLIENT ms */
	timer.tv_sec = 60 + maxClients/(1000000/USEC_PER_CLIENT);
	timer.tv_usec = maxClients%(1000000/USEC_PER_CLIENT)*USEC_PER_CLIENT;
    }

    initRoundsCount = initRounds;
    timerid = Timer_registerEnhanced(&timer, handleInitTimeout, NULL);

    if (timerid == -1) mlog("%s: failed to set init timer\n", __func__);
}

/**
 * @brief Handle a KVS init from the PMI client.
 *
 * The init process is monitored to make sure all PMI clients are
 * started successfully in time. The KVS init message is sent when
 * the corresponding PMI client calls PMI init.
 *
 * @param msg The message to handle
 *
 * @return No return value
 */
static void handleKVS_Init(PSLog_Msg_t *msg)
{
    PMI_Clients_t *client = findClient(msg, true);
    testMsg(__func__, client, msg);

    client->flags |= PMI_CLIENT_INIT;

    if (!initCount) {
	setInitTimeout();
	if (measure) {
	    gettimeofday(&time_now, NULL);
	    timersub(&time_now, &time_start, &time_diff);
	    mlog("%s: kvs init start: expected %i time %f diff %f\n", __func__,
		 maxClients, time_now.tv_sec + 1e-6 * time_now.tv_usec,
		 time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	}
    }
    initCount++;

    if (initCount == maxClients) {
	if (measure) {
	    gettimeofday(&time_now, NULL);
	    timersub(&time_now, &time_start, &time_diff);
	    mlog("%s: kvs init complete: %i:%i time %f diff %f\n", __func__,
		 initCount, maxClients,
		 time_now.tv_sec + 1e-6 * time_now.tv_usec,
		 time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	}
	Timer_remove(timerid);
	timerid = -1;
    }
}

/**
 * @brief Handle a KVS join message.
 *
 * @param msg The message to handle.
 *
 * @param ptr Pointer to the message payload.
 *
 * @return No return value.
 */
static void handleKVS_Join(PSLog_Msg_t *msg, char *ptr)
{
    int rank = msg->sender;

    /* verify that the client has the same kvsname */
    char client_kvs[PMI_KEYLEN_MAX];
    getKVSString(&ptr, client_kvs, sizeof(client_kvs));
    if (strcmp(client_kvs, kvsname)) {
	mlog("%s: got invalid default KVS name '%s' from rank %i\n", __func__,
	     client_kvs, rank);
	terminateJob(__func__);
    }

    int rRank = getKVSInt32(&ptr);
    if (rRank != rank) {
	mlog("%s: mismatching ranks %i - %i.\n", __func__, rank, rRank);
	terminateJob(__func__);
    }
    int pmiRank = getKVSInt32(&ptr);

    if (!totalKVSclients) {
	if (measure) {
	    gettimeofday(&time_now, NULL);
	    timersub(&time_now, &time_start, &time_diff);
	    mlog("%s: kvs join start: expected %i time %f diff %f\n", __func__,
		 maxClients, time_now.tv_sec + 1e-6 * time_now.tv_usec,
		 time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	}
    }
    totalKVSclients++;

    if (maxClients == totalKVSclients) {
	if (measure) {
	    gettimeofday(&time_now, NULL);
	    timersub(&time_now, &time_start, &time_diff);
	    mlog("%s: kvs join complete: %i:%i time %f diff %f\n", __func__,
		 maxClients, totalKVSclients,
		 time_now.tv_sec + 1e-6 * time_now.tv_usec,
		 time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	}
    }

    clients[pmiRank].pmiRank = pmiRank;
    clients[pmiRank].flags |= PMI_CLIENT_JOINED;
    clients[pmiRank].rank = rank;

    /*
    mlog("%s(%i): %s rank:%i pmiRank:%i\n", __func__, getpid(),
	    PSC_printTID(msg->header.sender), rank, pmiRank);
    */

    if (clients[pmiRank].tid != -1) {
	mlog("%s(%i): %s (rank %d) pmiRank %i already in KVS.\n", __func__,
	     getpid(), PSC_printTID(msg->header.sender), msg->sender, pmiRank);
    } else {
	clients[pmiRank].tid = msg->header.sender;

	/* inform the predecessor that the successor is ready */
	if (pmiRank+1 < maxClients && clients[pmiRank +1].tid != -1) {
	    sendDaisyReady(msg->header.sender, clients[pmiRank +1].tid);
	}
	if (pmiRank-1>= 0 && clients[pmiRank -1].tid != -1) {
	    sendDaisyReady(clients[pmiRank-1].tid, clients[pmiRank].tid);
	}
	if (pmiRank == maxClients -1) {
	    sendDaisyReady(msg->header.sender, PSC_getMyTID());
	}
    }
}

/**
 * @brief Handle a KVS leave message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleKVS_Leave(PSLog_Msg_t *msg)
{
    PMI_Clients_t *client = findClient(msg, true);

    if (client->flags & PMI_CLIENT_GONE) {
	mlog("%s: rank %i pmiRank %i already left\n", __func__,
	     client->rank, client->pmiRank);
	return;
    }
    totalKVSclients--;
    if (totalKVSclients <= 0) {
	if (measure) {
	    gettimeofday(&time_now, NULL);
	    timersub(&time_now, &time_start, &time_diff);
	    mlog("%s: kvs leave complete: time %f diff %f\n", __func__,
		 time_now.tv_sec + 1e-6 * time_now.tv_usec,
		 time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
	}
	releaseMySelf(__func__);
	exit(0);
    }

    testMsg(__func__, client, msg);
    client->flags |= PMI_CLIENT_GONE;
}

/**
 * @brief Handle messages from forwarder.
 *
 * Used to handle the "service exit" message which is send by the logger.
 *
 * @param fd Not used.
 *
 * @param data Not used.
 *
 * @return Always returns 0.
 */
static int handleFWMessage(int fd, void *data)
{
    releaseMySelf(__func__);
    exit(0);

    return 0;
}

/**
 * @brief Parse and handle a PMI KVS message.
 *
 * @param msg The received KVS message to handle.
 *
 * @return No return value.
 */
static void handleKvsMsg(PSLog_Msg_t *msg)
{
    uint8_t cmd;
    char *ptr;

    if (msg->version < 3) {
	mlog("%s: unsupported PSLog msg version %i from %s\n", __func__,
	     msg->version, PSC_printTID(msg->header.sender));
	terminateJob(__func__);
    }

    ptr = msg->buf;
    cmd = getKVSCmd(&ptr);

    mdbg(KVS_LOG_PROVIDER, "%s: cmd %s from %s rank %i\n", __func__,
	 PSKVScmdToString(cmd), PSC_printTID(msg->header.sender), msg->sender);

    switch (cmd) {
	case JOIN:
	    handleKVS_Join(msg, ptr);
	    break;
	case INIT:
	    handleKVS_Init(msg);
	    break;
	case PUT:
	    handleKVS_Put(msg, ptr);
	    break;
	case DAISY_BARRIER_IN:
	    handleKVS_Daisy_Barrier_In(msg, ptr);
	    break;
	case UPDATE_CACHE_FINISH:
	    handleKVS_Update_Cache_Finish(msg, ptr);
	    break;
	case LEAVE:
	    handleKVS_Leave(msg);
	    break;
	default:
	    mlog("%s: unsupported PMI KVS cmd %i from %s rank %i\n", __func__,
		 cmd, PSC_printTID(msg->header.sender), msg->sender);
	    terminateJob(__func__);
    }
}

/**
 * @brief Handle a CC message.
 *
 * @param msg The message to handle.
 *
 * @return No return value.
 */
static void handleCCMsg(PSLog_Msg_t *msg)
{
    switch (msg->type) {
	case KVS:
	    handleKvsMsg(msg);
	    break;
	default:
	    mlog("%s: unexpected CC message type %s from %s\n", __func__,
		 PSLog_printMsgType(msg->type),
		 PSC_printTID(msg->header.sender));
    }
}

/**
 * @brief Handle a new PSI message.
 *
 * @param fd Not used.
 *
 * @param data Not used.
 *
 * @return Always returns 0.
 */
static int handlePSIMessage(int fd, void *data)
{
    PSLog_Msg_t msg;
    DDSignalMsg_t *sigMsg = (DDSignalMsg_t *)&msg;
    int ret;
    PMI_Clients_t *client;

    ret = PSI_recvMsg((DDMsg_t *)&msg, sizeof(msg));

    if (ret<=0) {
	if (!ret) {
	    mlog("%s: unexpected message length 0\n", __func__);
	} else {
	    mwarn(errno, "%s: PSI_recvMsg", __func__);
	}
	terminateJob(__func__);
    }

    switch (msg.header.type) {
    case PSP_CC_MSG:
	handleCCMsg(&msg);
	break;
    case PSP_CD_WHODIED:
	if (verbose) {
	    mlog("%s: got signal %i from %s\n", __func__, sigMsg->signal,
		 PSC_printTID(msg.header.sender));
	}

	if (sigMsg->signal == SIGTERM) {
	    releaseMySelf(__func__);
	    exit(0);
	}
	terminateJob(__func__);
    case PSP_CC_ERROR:
	if (msg.header.sender == loggertid) {
	    /* logger died, nothing left for me to do here */
	    exit(0);
	}
	if ((client = findClient(&msg, false))) {
	    if (!(client->flags & PMI_CLIENT_GONE)) {
		mdbg(KVS_LOG_VERBOSE, "%s: client %s already gone\n",
		     __func__, PSC_printTID(msg.header.sender));
	    }
	} else {
	    mlog("%s: got CC_ERROR from unknown source %s\n", __func__,
		 PSC_printTID(msg.header.sender));
	}
	break;
    default:
	mlog("%s: unexpected PSLog message type %s from %s\n", __func__,
	     PSP_printMsg(msg.header.type), PSC_printTID(msg.header.sender));
    }

    return 0;
}

/**
 * @brief Handle signals.
 *
 * Handle the signal @a sig sent to me. For the time being
 * only SIGTERM is handled.
 *
 * @param sig Signal to handle.
 *
 * @return No return value.
 */
static void sighandler(int sig)
{
    switch(sig) {
    case SIGTERM:
	terminateJob(__func__);
    default:
	if (verbose) mlog("Got signal %s\n", strsignal(sig));
    }
}

/**
 * @brief Init the KVS provider process.
 *
 * Initialize the key-value space. This function must be called
 * before call to @ref handleKvsMsg().
 *
 * @return No return value.
 */
static void initKvsProvider(void)
{
    char tmp[100];
    snprintf(tmp, sizeof(tmp), "kvsprovider[%i]", getpid());
    initKVSLogger(tmp, stderr);

    /* install sig handlers */
    PSC_setSigHandler(SIGTERM, sighandler);

    /* set KVS debug mode */
    char *envstr = getenv("PMI_DEBUG");
    if (!envstr) envstr = getenv("PMI_DEBUG_KVS");
    if (!envstr) envstr = getenv("PMI_DEBUG_PROVIDER");
    if (envstr && atoi(envstr)) {
	maskKVSLogger(getKVSLoggerMask() | KVS_LOG_PROVIDER);
    }

    /* set the starting size of the job */
    envstr = getenv("PMI_SIZE");
    if (envstr) {
	maxClients = atoi(envstr);
	if (maxClients < 1) {
	    mlog("%s: PMI_SIZE %i is invalid\n", __func__, maxClients);
	    terminateJob(__func__);
	}
    } else {
	mlog("%s: PMI_SIZE is not set.\n", __func__);
	terminateJob(__func__);
    }
    initKVS();

    /* set the name of the KVS */
    envstr = getenv("PMI_KVS_TMP");
    if (!envstr) {
	strncpy(kvsname, "kvs_localhost_0", sizeof(kvsname) - 1);
    } else {
	snprintf(kvsname, sizeof(kvsname), "kvs_%s_0", envstr);
    }

    /* create global KVS */
    if(!kvs_create(kvsname)) {
	mlog("%s: Failed to create default KVS\n", __func__);
	terminateJob(__func__);
    }

    envstr = getenv("__PSI_LOGGER_TID");
    if (!envstr) {
	mlog("%s: cannot find logger tid\n", __func__);
	terminateJob(__func__);
    }
    if (sscanf(envstr, "%d", &loggertid) != 1) {
	mlog("%s: cannot determine logger from '%s'\n", __func__, envstr);
	terminateJob(__func__);
    }

    envstr = getenv("__PMI_NO_PARRICIDE");
    if (envstr) noParricide = atoi(envstr);

    /* init the timer structure, if necessary */
    if (!Timer_isInitialized()) Timer_init(stderr);

    /* set the timeout for client init phase */
    envstr = getenv("PMI_BARRIER_TMOUT");
    if (envstr) {
	initTimeout = atoi(envstr);
	mdbg(KVS_LOG_VERBOSE, "PMI init timeout");
	if (initTimeout == -1) {
	    mdbg(KVS_LOG_VERBOSE,	" disabled\n");
	} else {
	    mdbg(KVS_LOG_VERBOSE,	": %i\n", initTimeout);
	}
    }

    /* identify number of rounds for the init timeout */
    envstr = getenv("PMI_BARRIER_ROUNDS");
    if (envstr) {
	initRounds = atoi(envstr);
	if (initRounds < 1) initRounds = 1;
	mdbg(KVS_LOG_VERBOSE, "PMI init rounds: %i\n", initRounds);
    }

    if (!Selector_isInitialized()) Selector_init(NULL);

    daemonFD = PSI_getDaemonFD();
    if (daemonFD == -1) {
	mlog("%s: Connection to local daemon is broken\n", __func__);
	terminateJob(__func__);
    }
    Selector_register(daemonFD, handlePSIMessage, NULL);

    /* listen to message of my forwarder */
    envstr = getenv("__PMI_PROVIDER_FD");
    if (!envstr) {
	if (verbose) {
	    mlog("%s: Failed to init connection to my forwarder, "
		 "pspmi plugin loaded?\n", __func__);
	}
	releaseMySelf(__func__);
	exit(0);
    }
    forwarderFD = atoi(envstr);
    Selector_register(forwarderFD, handleFWMessage, NULL);

    envstr = getenv("MEASURE_KVS_PROVIDER");
    if (envstr) measure = atoi(envstr);
}

void kvsProviderLoop(bool kvsverbose)
{
    verbose = kvsverbose;

    initKvsProvider();

    if (measure) {
	gettimeofday(&time_start, NULL);
	mlog("%s: kvs provider ready, time %f\n", __func__,
	     time_start.tv_sec + 1e-6 * time_start.tv_usec);
    }

    while (1) {
	if (Swait(-1) < 0) {
	    if (errno && errno != EINTR) mwarn(errno, "%s: Swait()", __func__);
	}
    }

    /* never reached */
    releaseMySelf(__func__);
    exit(0);
}
