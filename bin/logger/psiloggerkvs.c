/*
 * ParaStation
 *
 * Copyright (C) 2007-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "pscommon.h"
#include "pstask.h"
#include "kvs.h"
#include "kvscommon.h"
#include "timer.h"
#include "psilogger.h"

#include "psiloggerkvs.h"

/* PSLog buffer size - PMIHEADER */
#define PMIUPDATE_PAYLOAD (1024 - 3)

#define PMI_GROUP_GROW_SIZE 5

#define elog(...) PSIlog_log(-1, __VA_ARGS__)

typedef enum {
    PMI_CLIENT_JOINED = 0x001,
    PMI_CLIENT_INIT   = 0x002,
    PMI_CLIENT_GONE   = 0x004,
} Clients_Flags_t;

typedef struct {
    PStask_ID_t tid;	    /**< TID of the forwarder */
    Clients_Flags_t flags;
    int init;		    /**< flag to mark the successfull pmi-init */
    int rank;		    /**< The parastation rank of the pmi client */
} PMI_Clients_t;

typedef struct {
    int maxClients;	    /**< The actual size of clients in this group */
    int minRank;	    /**< The minimal rank in this group */
    int maxRank;	    /**< The maximal rank in this group */
    int initCount;	    /** The number of received pmi client init msgs */
    int initRoundsCount;    /** Counter of client init rounds */
    int timerid;	    /** Id of the init timer */
    int updateMsgCount;	    /**< The number of kvs update msgs sends */
    int kvsUpdateLen;	    /**< Track the total length of new kvs updates */
    int kvsChanged;	    /**< Track if we need to distribute an update */
    int *kvsUpdateIndex;    /**< Track which index was updated in the kvs */
    int kvsIndexSize;	    /**< The size of the kvs update index */
    char *kvsname;	    /**< The main kvs name of the pmi group */

    PMI_Clients_t *clients; /** Array to store info about forwarders
				joined to kvs. */
} PMI_Group_t;

/** The structure which holds all pmi groups */
PMI_Group_t *groups = NULL;

/** The current number of PMI groups */
static int pmiGroupSize = 0;

/** The maximal number of PMI groups */
static int maxPMIGroups = 0;

/** Set the timeout of the client init phase */
static int initTimeout = 0;

/** Set the number of rounds for the client init phase */
static int initRounds = 2;

/** Total count of all kvs clients we know */
static int totalKVSclients = 0;

/** Generic message buffer */
static char buffer[1024];


/**
 * @brief Add a new pmi group.
 *
 * @param kvsname The kvsname of the new pmi group.
 *
 * @param maxClients The maximal number of clients in the new pmi group.
 *
 * @return Returns a pointer to the new pmi group.
 */
static PMI_Group_t *addPMIGroup(char *kvsname, int maxClients)
{
    PMI_Group_t *gptr = NULL;
    int i;

    /* grow pmi groups */
    if (pmiGroupSize + 1 > maxPMIGroups) {
	int newsize = maxPMIGroups + PMI_GROUP_GROW_SIZE;

	if (!(groups = realloc(groups, sizeof(PMI_Group_t) * newsize))) {
	    PSIlog_warn(-1, errno, "%s", __func__);
	    terminateJob();
	    exit(0);
	}
	maxPMIGroups = newsize;
    }

    gptr = &groups[pmiGroupSize];
    gptr->minRank = (pmiGroupSize == 0) ?
			0 : groups[pmiGroupSize].maxRank +1;
    gptr->maxClients = maxClients;
    gptr->maxRank = gptr->minRank + maxClients -1;
    gptr->timerid = -1;
    gptr->initCount = 0;
    gptr->kvsname = strdup(kvsname);
    totalKVSclients += maxClients;
    pmiGroupSize++;

    /* init the kvs clients structure */
    if (!(gptr->clients = malloc(sizeof(*gptr->clients) * gptr->maxClients))) {
	PSIlog_warn(-1, errno, "%s", __func__);
	terminateJob();
	exit(0);
    }

    for (i=0; i<gptr->maxClients; i++) {
	gptr->clients[i].tid = -1;
	gptr->clients[i].flags = 0;
    }

    /* init the kvs update tracking */
    gptr->kvsIndexSize = gptr->maxClients + 10;

    if (!(gptr->kvsUpdateIndex = malloc(sizeof(int *) * gptr->kvsIndexSize))) {
	PSIlog_warn(-1, errno, "%s", __func__);
	terminateJob();
	exit(0);
    }

    for (i=0; i<gptr->kvsIndexSize; i++) {
	gptr->kvsUpdateIndex[i] = 0;
    }

    return gptr;
}

void initLoggerKvs(void)
{
    char kvsname[KVSNAME_MAX], *envstr;
    int debug_kvs = 0, maxClients = 0;


    /* set the starting size of the job */
    if ((envstr = getenv("PMI_SIZE"))) {
	if ((maxClients = atoi(envstr)) < 1) {
	    elog("%s: PMI_SIZE '%i' is invalid\n", __func__, maxClients);
	    terminateJob();
	}
    } else {
	elog("%s: PMI_SIZE is not set.\n", __func__);
	terminateJob();
    }

    /* set the name of the kvs */
    if (!(envstr = getenv("PMI_KVS_TMP"))) {
	strncpy(kvsname,"kvs_localhost_0",sizeof(kvsname) -1);
    } else {
	snprintf(kvsname,sizeof(kvsname),"kvs_%s_0", envstr);
    }

    /* init the pmi group */
    addPMIGroup(kvsname, maxClients);

    /* create global kvs */
    if((kvs_create(kvsname))) {
	elog("%s: Failed to create default kvs\n", __func__);
	terminateJob();
    }

    /* init the timer structure, if necessary */
    if (!Timer_isInitialized()) Timer_init(stderr);

    /* set the timeout for client init phase */
    if ((envstr = getenv("PMI_BARRIER_TMOUT"))) {
	initTimeout = atoi(envstr);
	PSIlog_log(PSILOG_LOG_VERB, "pmi init timeout");
	if (initTimeout == -1) {
	    PSIlog_log(PSILOG_LOG_VERB,	" disabled\n");
	} else {
	    PSIlog_log(PSILOG_LOG_VERB,	": %i\n", initTimeout);
	}
    }

    /* identify number of rounds for the init timeout */
    if ((envstr = getenv("PMI_BARRIER_ROUNDS"))) {
	initRounds = atoi(envstr);
	if (initRounds < 1) initRounds = 1;
	PSIlog_log(PSILOG_LOG_VERB, "pmi init rounds: %i\n", initRounds);
    }

    /* set kvs debug mode */
    if ((envstr = getenv("PMI_DEBUG"))) {
	debug_kvs = atoi(envstr);
    } else if ((envstr = getenv("PMI_DEBUG_KVS"))) {
	debug_kvs = atoi(envstr);
    }
    if (debug_kvs) PSIlog_setDebugMask(PSIlog_getDebugMask() | PSILOG_LOG_KVS);
}

/**
 * @brief Test incoming message
 *
 * Test, if incoming message @a msg is consistent with internal KVS
 * settings. Tests are consistency of task-ID and rank fitting into
 * @ref clients array.
 *
 * @param fName Name of the calling function
 *
 * @param msg The message to test
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @return No return value.
 */
static void testMsg(const char fName[], PSLog_Msg_t *msg, PMI_Group_t *gptr)
{
    int pmiRank;

    pmiRank = msg->sender - gptr->minRank;

    /* check for invalid ranks */
    if (pmiRank < gptr->minRank || pmiRank > gptr->maxRank) {
	elog("%s: invalid pmi rank index '%i' for rank '%i'\n", __func__,
		pmiRank, msg->sender);
	terminateJob();
	exit(1);
    }

    /* check for false clients */
    if (gptr->clients[pmiRank].tid != msg->header.sender) {
	elog("%s: rank '%d' pmiRank '%i' from '%s'", fName, msg->sender,
		pmiRank, PSC_printTID(msg->header.sender));
	elog(" should come from %s\n",
		PSC_printTID(gptr->clients[pmiRank].tid));
	terminateJob();
    }
}

int getNumKvsClients(void)
{
    return totalKVSclients;
}

/**
 * @brief Send a kvs message to a pmi client.
 *
 * @param tid The taskid to send the kvs message to.
 *
 * @param msg The message to send.
 *
 * @param len The lenght of the message to send.
 *
 * @return No return value.
 */
static void sendKvsMsg(PStask_ID_t tid, char *msg, size_t len)
{
    /* send the msg */
    sendMsg(tid, KVS, msg, len);
}

/**
 * @brief Send a kvs message to first client in the daisy chain.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @param msg The message to send.
 *
 * @param len The lenght of the message to send.
 *
 * @return No return value.
 */
static void sendMsgToKvsSucc(PMI_Group_t *gptr, char *msg, size_t len)
{
    if (!msg) {
	elog("%s: invalid kvs msg: 'null'\n", __func__);
	return;
    }

    if (gptr->clients[0].tid != -1) {
	sendKvsMsg(gptr->clients[0].tid, msg, len);
    } else {
	elog("%s: daisy-chain not unusable : invalid client[0] tid\n",
		    __func__);
	return;
    }
}

/**
 * @brief Grow a kvs update tracking index.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @param minNewSize The minimum size of the grown update index.
 *
 * @param caller The name of the calling function.
 *
 * @return No return value.
 */
static void growKvsUpdateIdx(PMI_Group_t *gptr, int minNewSize,
				const char *caller)
{
    int oldSize = gptr->kvsIndexSize, i;
    int newSize = oldSize + 2 * gptr->maxClients;

    if (newSize < minNewSize) newSize = minNewSize;
    gptr->kvsUpdateIndex =
		    realloc(gptr->kvsUpdateIndex, sizeof(int *) * newSize);

    if (!gptr->kvsUpdateIndex) {
	PSIlog_warn(-1, errno, "%s: realloc()", __func__);
	terminateJob();
	exit(0);
    }
    gptr->kvsIndexSize = newSize;

    for (i=oldSize + 1; i < gptr->kvsIndexSize; i++) {
	gptr->kvsUpdateIndex[i] = 0;
    }
}

/**
 * @brief Send a kvs update to all clients in a pmi group.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @param finish When set to 1 an finish message is send when the update is
 * complete. Otherwise only update messages are send.
 *
 * @return No return value.
 */
static void sendKvsUpdateToClients(PMI_Group_t *gptr, int finish)
{
    char kvsmsg[PMIU_MAXLINE], nextval[KEYLEN_MAX + VALLEN_MAX];
    char *kvsname = gptr->kvsname, *bufPtr = buffer, *valPtr;
    int kvsvalcount, valup, pmiCmd;
    size_t bufLen = 0, toAdd = 0;

    kvsvalcount = kvs_count_values(kvsname);
    valup = 0;
    while (kvsvalcount > valup) {
	snprintf(kvsmsg, sizeof(kvsmsg), "kvsname=%s", kvsname);

	/* add the values to the msg */
	while (kvsvalcount > valup) {

	    /* skip already send fields */
	    if (valup > gptr->kvsIndexSize) {
		growKvsUpdateIdx(gptr, valup, __func__);
	    }
	    if (gptr->kvsUpdateIndex[valup] == 0) {
		valup++;
		continue;
	    }

	    if (!(valPtr = kvs_getbyidx(kvsname, valup))) {
		elog("%s: invalid kvs index valup '%i' kvsvalcount '%i'\n",
			__func__, valup, kvsvalcount);
		terminateJob();
		return;
	    }
	    snprintf(nextval, sizeof(nextval), " %s", valPtr);

	    /* PSlog msg can hold 1024 buffer payload */
	    toAdd = strlen(nextval) + strlen(kvsmsg) + 1;

	    if (toAdd > PMIUPDATE_PAYLOAD || toAdd > sizeof(kvsmsg)) break;

	    strcat(kvsmsg, nextval);
	    gptr->kvsUpdateLen -= strlen(nextval);
	    gptr->kvsUpdateIndex[valup] = 0;
	    valup++;
	}

	if (!finish) {
	    pmiCmd = UPDATE_CACHE;
	} else {
	    pmiCmd = (kvsvalcount <= valup) ?
			UPDATE_CACHE_FINISH : UPDATE_CACHE;
	}

	bufPtr = buffer;
	bufLen = 0;
	setKVSCmd(&bufPtr, &bufLen, pmiCmd);
	addKVSString(&bufPtr, &bufLen, kvsmsg);
	sendMsgToKvsSucc(gptr, buffer, bufLen);

	elog("%s: 4 : len:%zu bufLen:%zu cmd:%s toAdd:%zu\n", __func__,
		strlen(kvsmsg), bufLen, PSKVScmdToString(pmiCmd), toAdd);
	gptr->updateMsgCount++;
	if (!finish) break;

    }

    /* we are up to date now */
    if (finish) gptr->kvsChanged = 0;
}

/**
 * Handle a kvs put message.
 *
 * @param msg The message to handle.
 *
 * @param ptr Pointer to the payload of the message.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @return No return value.
 */
static void handleKVS_Put(PSLog_Msg_t *msg, char *ptr, PMI_Group_t *gptr)
{
    char kvsname[KVSNAME_MAX], key[KEYLEN_MAX], value[VALLEN_MAX];
    size_t keyLen, valLen, kvsLen;
    int index;

    /* extract kvsname, key and value */
    if ((kvsLen = getKVSString(&ptr, kvsname, sizeof(kvsname))) < 1) {
	goto PUT_ERROR;
    }
    if ((keyLen = getKVSString(&ptr, key, sizeof(key))) < 1) goto PUT_ERROR;
    if ((valLen = getKVSString(&ptr, value, sizeof(value))) < 1) goto PUT_ERROR;

    /* save in global kvs */
    if (!(kvs_putIdx(kvsname, key, value, &index))) {
	if (index < 0) {
	    elog("%s: invalid kvs index from put\n", __func__);
	    terminateJob();
	}
	if (gptr->kvsIndexSize < index) growKvsUpdateIdx(gptr, index, __func__);
	gptr->kvsUpdateIndex[index] = 1;

	gptr->kvsChanged = 1;
	gptr->kvsUpdateLen += keyLen + valLen + 2;

	/* check if we can start sending update messages */
	if (gptr->clients[0].tid != -1) {
	    if (gptr->kvsUpdateLen + kvsLen + 2 >= PMIUPDATE_PAYLOAD) {
		sendKvsUpdateToClients(gptr, 0);
	    }
	}
	return;
    }

PUT_ERROR:
    elog("%s: error saving value to kvs\n", __func__);
    terminateJob();
}

/**
 * @brief Handle a daisy-barrier-in message.
 *
 * @param msg The message to handle.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @return No return value.
 */
static void handleKVS_Daisy_Barrier_In(PSLog_Msg_t *msg, PMI_Group_t *gptr)
{
    char *bufPtr = buffer;
    size_t bufLen = 0;

    testMsg(__func__, msg, gptr);

    /* debugging output */
    PSIlog_log(PSILOG_LOG_KVS, "%s\n", __func__);

    /* check if we got the msg from the last client in chain */
    if (msg->sender != gptr->maxRank) {
	elog("%s: barrier from wrong rank %i on %s\n", __func__,
		   msg->sender, PSC_printTID(msg->header.sender));
	terminateJob();
    }

    if (gptr->kvsChanged) {
	/* distribute kvs update */
	sendKvsUpdateToClients(gptr, 1);
    } else {
	/* send all Clients barrier_out */
	setKVSCmd(&bufPtr, &bufLen, DAISY_BARRIER_OUT);
	sendMsgToKvsSucc(gptr, buffer, bufLen);
    }
}

/**
 * @brief Handle a kvs-update-cache-finish message.
 *
 * @param msg The message to handle.
 *
 * @param ptr Pointer to the payload of the message.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @return No return value.
 */
static void handleKVS_Update_Cache_Finish(PSLog_Msg_t *msg, char *ptr,
					    PMI_Group_t *gptr)
{
    int32_t mc;

    testMsg(__func__, msg, gptr);

    /* parse arguments */
    mc = getKVSInt32(&ptr);

    /* check if clients got all the updates */
    if (mc != gptr->updateMsgCount) {
	elog("%s: kvs clients did not get all kvs update msgs\n",
		   __func__);
	terminateJob();
    }
    gptr->updateMsgCount = 0;

    /* last forward send us an reply, so everything is ok */
    /* check if the result msg came from the last client in chain */
    if (msg->sender != gptr->clients[gptr->maxRank].rank) {
	elog("%s: update from wrong rank %i on %s\n", __func__,
		msg->sender, PSC_printTID(msg->header.sender));
	terminateJob();
    }
}

/**
 * @brief Send a daisy-chain-ready message to a taskid.
 *
 * @param tid The taskid to send the message to.
 *
 * @return No return value.
 */
static void sendDaisyReady(PStask_ID_t tid)
{
    char *bufPtr = buffer;
    size_t bufLen = 0;

    setKVSCmd(&bufPtr, &bufLen, DAISY_SUCC_READY);
    sendKvsMsg(tid, buffer, bufLen);
}

/**
 * @brief Handle barrier timeouts.
 *
 * Callback function to handle barrier timeouts.
 *
 * Terminate the job, send all children term signal, to avoid that the
 * job hangs infinite.
 *
 * @return No return value.
 */
static void handleInitTimeout(int timerid, void *ptr)
{
    PMI_Group_t *gptr = ptr;
    int i;

    elog("Timeout: Not all clients called pmi_init(): "
	    "init=%i left=%i round=%i\n", gptr->initCount,
	    gptr->maxClients - gptr->initCount,
	    initRounds - gptr->initRoundsCount+1);

    if (--gptr->initRoundsCount) return;

    elog("Missing clients:\n");
    for (i=0; i<gptr->maxClients; i++) {
	if (!gptr->clients[i].init) {
	    elog("%s rank %d\n", (gptr->clients[i].tid == -1) ?
		       "unconnected" : PSC_printTID(gptr->clients[i].tid), i);
	}
    }

    /* kill all children */
    terminateJob();
}

#define USEC_PER_CLIENT 500
/**
 * @brief Set the timeout for the barrier/update msgs.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @return No return value.
 */
static void setInitTimeout(PMI_Group_t *gptr)
{
    struct timeval timer;

    if (initTimeout == -1) return;

    if (initTimeout) {
	/* timeout from user */
	timer.tv_sec = initTimeout;
	timer.tv_usec = 0;
    } else {
	/* timeout after 1 min + n * USEC_PER_CLIENT ms */
	timer.tv_sec = 60 + gptr->maxClients/(1000000/USEC_PER_CLIENT);
	timer.tv_usec =
		gptr->maxClients%(1000000/USEC_PER_CLIENT)*USEC_PER_CLIENT;
    }

    gptr->initRoundsCount = initRounds;
    gptr->timerid = Timer_registerEnhanced(&timer, handleInitTimeout, gptr);

    if (gptr->timerid == -1) elog("%s: failed to set init timer\n", __func__);
}

/**
 * @brief Handle a kvs init from the pmi client.
 *
 * The init process is monitored to make sure all mpi clients are
 * started successfully in time. The kvs init message is send when
 * the correspoding mpi client does the pmi init.
 *
 * @param msg The message to handle.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @return No return value.
 */
static void handleKVS_Init(PSLog_Msg_t *msg, PMI_Group_t *gptr)
{
    int pmiRank;

    testMsg(__func__, msg, gptr);

    pmiRank = msg->sender - gptr->minRank;
    gptr->clients[pmiRank].flags |= PMI_CLIENT_INIT;

    if (gptr->initCount == 0) {
	setInitTimeout(gptr);
    }
    gptr->initCount++;

    if (gptr->initCount == gptr->maxClients) {
	Timer_remove(gptr->timerid);
	gptr->timerid = -1;
	elog("%s: all clients did the init in time\n", __func__);
    }
}

/**
 * @brief Get the pmi group for a rank.
 *
 * @param rank The rank to get the pmi group for.
 *
 * @return Returns a pointer to the pmi group.
 */
static PMI_Group_t *getPMIgroup(int rank)
{
    int i;

    for (i=0; i< pmiGroupSize; i++) {
	if (rank >= groups[i].minRank && rank <= groups[i].maxRank) {
	    return &groups[i];
	}
    }

    elog("%s: pmi group for rank '%i' not found\n", __func__, rank);
    terminateJob();
    exit(0);
}

PStask_ID_t getPMIPred(int rank, PStask_ID_t pred)
{
    PMI_Group_t *gptr;

    gptr = getPMIgroup(rank);

    if (rank == gptr->minRank) {
	return PSC_getMyTID();
    }
    return pred;
}

PStask_ID_t getPMISucc(int rank, PStask_ID_t succ)
{
    PMI_Group_t *gptr;

    gptr = getPMIgroup(rank);

    if (rank == gptr->maxRank) {
	return PSC_getMyTID();
    }
    return succ;
}

int getPMIRank(int rank)
{
    PMI_Group_t *gptr;

    gptr = getPMIgroup(rank);
    return rank - gptr->minRank;
}

/**
 * @brief Handle a kvs join message.
 *
 * @param msg The message to handle.
 *
 * @param ptr Pointer to the message payload.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @return No return value.
 */
static void handleKVS_Join(PSLog_Msg_t *msg, char *ptr, PMI_Group_t *gptr)
{
    char kvsname[KEYLEN_MAX];
    int rank = msg->sender, pmiRank;
    PMI_Clients_t *clients;

    pmiRank = rank - gptr->minRank;
    clients = gptr->clients;
    clients[pmiRank].rank = rank;
    clients[pmiRank].flags |= PMI_CLIENT_JOINED;

    /* verify that the client has the same kvsname */
    getKVSString(&ptr, kvsname, sizeof(kvsname));
    if (!!(strcmp(kvsname, gptr->kvsname))) {
	elog("%s: got invalid default kvs name '%s' from rank '%i'\n", __func__,
		kvsname, rank);
	terminateJob();
	return;
    }

    if (clients[pmiRank].tid != -1) {
	elog("%s: %s (rank %d) already in kvs.\n", __func__,
		   PSC_printTID(msg->header.sender), msg->sender);
    } else {
	clients[pmiRank].tid = msg->header.sender;

	/* inform the predecessor that the successor is ready */
	if (pmiRank+1 < gptr->maxClients && clients[pmiRank +1].tid != -1) {
	    sendDaisyReady(msg->header.sender);
	}
	if (pmiRank-1>= 0 && clients[pmiRank -1].tid != -1) {
	    sendDaisyReady(clients[pmiRank-1].tid);
	}
    }
}

/**
 * @brief Handle a kvs leave message.
 *
 * @param msg The message to handle.
 *
 * @param gptr Pointer to the pmi group of the client.
 *
 * @return No return value.
 */
static void handleKVS_Leave(PSLog_Msg_t *msg, PMI_Group_t *gptr)
{
    int pmiRank;

    pmiRank = msg->sender - gptr->minRank;

    if (gptr->clients[pmiRank].flags & PMI_CLIENT_GONE) {
	elog("%s: rank '%i' pmiRank '%i' already left\n", __func__, msg->sender,
		pmiRank);
	return;
    }

    testMsg(__func__, msg, gptr);
    gptr->clients[pmiRank].flags |= PMI_CLIENT_GONE;
}

/**
 * @brief Build up argv and argc from a pmi spawn request.
 *
 * @param msgBuffer The buffer with the pmi spawn message.
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
	elog("%s: invalid argument count\n", __func__);
	return NULL;
    }

    if ((addArgs = atoi(numArgs)) > PMI_SPAWN_MAX_ARGUMENTS) {
	elog("%s: too many arguments (%i)\n", __func__, addArgs);
	return NULL;
    }

    if (addArgs < 0) {
	elog("%s: invalid argument count (%i)\n", __func__, addArgs);
	return NULL;
    }

    maxargc += addArgs;
    if (!(argv = malloc((maxargc) * sizeof(char *)))) {
	elog("%s: out of memory\n", __func__);
	terminateJob();
	exit(1);
    }
    for (i=0; i<maxargc; i++) argv[i] = NULL;

    /* add the executalbe as argv[0] */
    if (!(execname = getpmivm("execname", msgBuffer))) {
	free(argv);
	elog("%s: invalid executable name\n", __func__);
	return NULL;
    }
    argv[*argc++] = execname;

    /* add additional arguments */
    for (i=1; i<=addArgs; i++) {
	snprintf(buffer, sizeof(buffer), "arg%i", i);
	if ((nextval = getpmivm(buffer, msgBuffer))) {
	    elog("%s: save next argument:%s\n", __func__, nextval);
	    argv[*argc++] = nextval;
	} else {
	    for (i=0; i<*argc; i++) free(argv[i]);
	    free(argv);
	    elog("%s: extracting arguments failed\n", __func__);
	    return NULL;
	}
    }

    return argv;
}

/**
 * @brief Handle a kvs spawn message.
 *
 * @param msg The message to handle.
 *
 * @param ptr Pointer to the message payload.
 *
 * @return No return value.
 */
static void handleKVS_Spawn(PSLog_Msg_t *msg, char *ptr)
{
    char kvsname[KEYLEN_MAX], spawnBuffer[VALLEN_MAX], nextkey[KEYLEN_MAX];
    char numPreput[50], numInfo[50], numProcs[50];
    char *pmiWdir = NULL, *nodeType = NULL, **argv = NULL, *bufPtr;
    int infos = 0, argc = 0, i;
    int32_t np;
    //int32_t minRank, maxRank
    size_t bufLen;
    PMI_Group_t *gptr;

    // char *wdir = NULL;

    getKVSString(&ptr, kvsname, sizeof(kvsname));
    getKVSString(&ptr, spawnBuffer, sizeof(spawnBuffer));

    /* get the number of processes to spawn */
    if (!(getpmiv("nprocs", spawnBuffer, numProcs, sizeof(numProcs)))) {
	elog("%s: Getting number of processes to spawn failed\n", __func__);
	/*
	 * TODO
	PMI_send("cmd=spawn_result rc=-1\n");
	*/
	return;
    }
    np = atoi(numProcs);

    /* add a new pmi group */
    gptr = addPMIGroup(kvsname, np);

    /* init new kvs */
    if((kvs_create(kvsname))) {
	elog("%s: Failed to create default kvs\n", __func__);
	terminateJob();
	return;
    }

    /* setup argv and argc for processes to spawn */
    if (!(argv = getSpawnArgs(spawnBuffer, &argc))) return;

    /* extract preput values and keys */
    if (!(getpmiv("preput_num", spawnBuffer, numPreput, sizeof(numPreput)))) {
	elog("%s: invalid preput count\n", __func__);
	return;
    }

    /*
     * preput_num=1
     * preput_key_0=PARENT_ROOT_PORT_NAME
     * preput_val_0=tag#0$description#michi-ng$port#41925$ifname#192.168.13.30$

    int numpre, i;
    char *nextpreval, *nextprekey, prename[200];

    numpre = atoi(preput_num);
    for (i=1; i< numpre; i++) {
	snprintf(prename, sizeof(prename), "preput_key_%i", i);
	nextprekey = getpmivm(prename, spawnBuffer);
	snprintf(prename, sizeof(prename), "preput_val_%i", i);
	nextpreval = getpmivm(prename, spawnBuffer);

	if (nextpreval && nextprekey) {
	    elog("%s: save preput key:%s value:%s\n", __func__, nextprekey, nextpreval);
	    free(nextprekey);
	    free(nextpreval);
	} else {
	    elog("%s: extracting preput values "
				"failed\n", __func__);
	    PMI_send("cmd=spawn_result rc=-1\n");
	    return 1;
	}
    }
    */

    /* extract info values and keys
     *
     * These info variables are implementation dependend and can be used for
     * e.g. process placement. All unsupported values will be silently ignored.
     *
     * ParaStation will support:
     *
     *  - wdir:	The working directory of the new processes to spawn
     *  - nodetype: The type of node requested
     */
    if (!(getpmiv("info_num", spawnBuffer, numInfo, sizeof(numInfo)))) {
	elog("%s: invalid info count\n", __func__);
	return;
    }
    infos = atoi(numInfo);

    for (i=1; i<=infos; i++) {
	snprintf(buffer, sizeof(buffer), "info_key_%i", i);
	if ((getpmiv(buffer, spawnBuffer, nextkey, sizeof(nextkey)))) {
	    elog("%s: invalid info variable\n", __func__);
	    return;
	}

	if (!strcmp(nextkey, "wdir")) {
	    snprintf(buffer, sizeof(buffer), "info_val_%i", i);
	    pmiWdir = getpmivm(buffer, spawnBuffer);
	}
	if (!strcmp(nextkey, "nodeType")) {
	    snprintf(buffer, sizeof(buffer), "info_val_%i", i);
	    nodeType = getpmivm(buffer, spawnBuffer);
	}
    }

    /* TODO:
     * via spawn env:
     * - send preput values via spawn environment
     * - use minRank, maxRank to set PMI_RANK env correct
     * - set PMI_KVSNAME env var to kvsname
     *
     * - spawn the processes
     * - return error codes for spawned/non spawned processes: using
     *   errcodes=1,1,0,0
     * */

    /* do the actual spawn */
    elog("%s: would spawn processes now!\n", __func__);

    /*
    wdir = (pmiWdir) ? pmiWdir : task->workingdir;
    nodeType ...
    PSI_spawnStrict(np, wdir, argc, argv,
                        int strictArgv, int *errors, PStask_ID_t *tids);
    parse for spawn errors ...
    free/change pmi group if spawn failed
    */

    /* cleanup */
    if (argv) {
	for (i=0; i<argc; i++) free(argv[i]);
	free(argv);
    }
    free(pmiWdir);
    free(nodeType);

/*
SPAWN_ERROR:
    free(spawnBuffer);
*/

    /* send spawn result */
    bufPtr = buffer;
    bufLen = 0;
    setKVSCmd(&bufPtr, &bufLen, SPAWN_RESULT);
    addKVSInt32(&bufPtr, &bufLen, &np);
    addKVSString(&bufPtr, &bufLen, kvsname);
    addKVSInt32(&bufPtr, &bufLen, &gptr->minRank);
    addKVSInt32(&bufPtr, &bufLen, &gptr->maxRank);
    sendKvsMsg(msg->header.sender, buffer, bufLen);
}

void handleKvsMsg(PSLog_Msg_t *msg)
{
    uint8_t cmd;
    char *ptr;
    PMI_Group_t *gptr = NULL;

    if (msg->version < 3) {
	elog("%s: unsupported PSlog msg version '%i' from '%s'\n",
		    __func__, msg->version, PSC_printTID(msg->header.sender));
	terminateJob();
	return;
    }

    ptr = msg->buf;
    cmd = getKVSCmd(&ptr);
    if (cmd != SPAWN) gptr = getPMIgroup(msg->sender);

    PSIlog_log(PSILOG_LOG_KVS, "%s: cmd '%s' from %s\n",
	    __func__, PSKVScmdToString(cmd), PSC_printTID(msg->header.sender));

    switch (cmd) {
	case JOIN:
	    handleKVS_Join(msg, ptr, gptr);
	    break;
	case INIT:
	    handleKVS_Init(msg, gptr);
	    break;
	case PUT:
	    handleKVS_Put(msg, ptr, gptr);
	    break;
	case DAISY_BARRIER_IN:
	    handleKVS_Daisy_Barrier_In(msg, gptr);
	    break;
	case UPDATE_CACHE_FINISH:
	    handleKVS_Update_Cache_Finish(msg, ptr, gptr);
	    break;
	case LEAVE:
	    handleKVS_Leave(msg, gptr);
	    break;
	case SPAWN:
	    handleKVS_Spawn(msg, ptr);
	    break;
	default:
	    elog("%s: unsupported pmi kvs cmd '%i'\n", __func__, cmd);
	    terminateJob();
    }
}
