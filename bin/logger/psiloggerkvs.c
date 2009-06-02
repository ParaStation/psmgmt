/*
 *               ParaStation
 *
 * Copyright (C) 2007-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 * @author
 * Michael Rauh <rauh@par-tec.com>
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

/** Array to store the forwarder TIDs joined to kvs. */
static PStask_ID_t *clientKvsTID;

/** Array to store the received TIDs for tracking the barrier/cache updates. */
static PStask_ID_t *clientKvsTrackTID;

/** The actual size of clientKvsTID */
static int maxKvsClients = 64;

/** Number of clients joined to kvs */
static int noKvsClients = 0;

/** The number of received kvs barrier_in */
static int kvsBarrierInCount = 0;

/** The number of received kvs cache updates */
static int kvsCacheUpdateCount = 0;

/** The number of kvs update msgs sends */
static int kvsUpdateMsgCount = 0;

/** Outputs kvs debug msg if set */
static int debug_kvs = 0;

/** Set the timeout of the barrier */
static int barrierTimeout = 0;

/** Id of the barrier timer */
static int timerid = -1;

/** track if we need to distribute an update */
static int kvsChanged = 0;

/** flag enabling the daisy-chain broadcast */
static int useDaisyChain =0;

/**
 * @brief Wrapper for send kvs messages.
 *
 * Wrapper of SendMsg functions with error handling for key value space messages.
 *
 * @return No return value.
 */
static void sendKvsMsg(PStask_ID_t tid, char *msg)
{
    ssize_t len;

    if (!msg) {
	PSIlog_log(-1, "%s: invalid kvs message\n", __func__);
	terminateJob();
    }

    len = strlen(msg);
    if (!(len && msg[len - 1] == '\n')) {
	/* assert */
	PSIlog_log(-1, "%s: invalid kvs message\n", __func__);
	terminateJob();
    }

    /* send the msg */
    sendMsg(tid, KVS, msg, len + 1);
}

void initLoggerKvs(void)
{
    char kvs_name[KVSNAME_MAX], *envstr;
    int i;

    clientKvsTID = malloc (sizeof(*clientKvsTID) * maxKvsClients);
    clientKvsTrackTID = malloc (sizeof(*clientKvsTrackTID) * maxKvsClients);

    if (!clientKvsTID || !clientKvsTrackTID) {
	PSIlog_log(-1, "%s: out of memory\n", __func__);
	terminateJob();
	exit(EXIT_FAILURE);
    }

    for (i=0; i<maxKvsClients; i++) {
	clientKvsTID[i] = -1;
	clientKvsTrackTID[i] = -1;
    }

    /* init the kvs */
    kvs_init();

    /* set the name of the kvs */
    if (!(envstr = getenv("PMI_KVS_TMP"))) {
	strncpy(kvs_name,"kvs_localhost_0",sizeof(kvs_name) -1);
    } else {
	snprintf(kvs_name,sizeof(kvs_name),"kvs_%s_0",
		    envstr);
    }

    /* create global kvs */
    if((kvs_create(kvs_name))) {
	PSIlog_log(-1, "%s: Failed to create default kvs\n", __func__);
	terminateJob();
    }

    /* set the init size of the job */
    if ((envstr = getenv("PMI_SIZE"))) {
	noKvsClients = atoi(envstr);
    } else {
	PSIlog_log(-1, "%s: PMI_SIZE is not correct set.\n", __func__);
	terminateJob();
    }

    /* init the timer structure */
    if (!Timer_isInitialized()) {
	Timer_init(stderr);
    }

    /* set the timeout for barrier */
    if ((envstr = getenv("PMI_BARRIER_TMOUT"))) {
	barrierTimeout = atoi(envstr);
	if (barrierTimeout == -1) {
	    PSIlog_log(PSILOG_LOG_VERB,	"pmi barrier timeout disabled\n");
	} else {
	    PSIlog_log(PSILOG_LOG_VERB,	"pmi barrier timeout: %i\n",
		       barrierTimeout);
	}
    }

    /* set kvs debug mode */
    if ((envstr = getenv("PMI_DEBUG"))) {
	debug_kvs = atoi(envstr);
    } else if ((envstr = getenv("PMI_DEBUG_KVS"))) {
	debug_kvs = atoi(envstr);
    }
}

void switchDaisyChain(int val)
{
    useDaisyChain = val;
}

/**
 * @brief Send kvs message to all clients.
 *
 * Send a key value space message to all pmi clients which joined the kvs.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @param len The size of the msg to send.
 *
 * @return No return value.
 */
static void sendMsgToKvsClients(char *msg)
{
    int i;

    if (!msg) {
	PSIlog_log(-1, "%s: invalid msg\n", __func__);
	return;
    }

    if (useDaisyChain) {
	if (clientKvsTID[0] != -1) {
	    sendKvsMsg(clientKvsTID[0], msg);
	} else {
	    PSIlog_log(-1, "%s: daisy-chain enabled but unusable\n", __func__);
	    return;
	}
    } else {
	for (i=0; i< maxKvsClients; i++) {
	    if (clientKvsTID[i] != -1) sendKvsMsg(clientKvsTID[i], msg);
	}
    }
}

/**
 * @brief Handle a kvs put request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsPut(PSLog_Msg_t *msg)
{
    char kvsname[KVSNAME_MAX], name[KEYLEN_MAX];
    char value[VALLEN_MAX], retbuf[PMIU_MAXLINE];
    char *ptr = msg->buf;

    /* parse arguments */
    getpmiv("kvsname",ptr,kvsname,sizeof(kvsname));
    getpmiv("key",ptr,name,sizeof(name));
    getpmiv("value",ptr,value,sizeof(value));

    if (kvsname[0] == 0 || kvsname[0] == 0 || value[0] == 0) {
	PSIlog_log(-1, "%s: invalid kvs put cmd\n", __func__);
	snprintf(retbuf, sizeof(retbuf),
		 "cmd=put_result rc=-1 msg=error_invalid_put_msg\n");
    } else {
	/* put the value in kvs */
	if (!(kvs_put(kvsname, name, value))) {
	    snprintf(retbuf, sizeof(retbuf), "cmd=put_result rc=0\n");
	    kvsChanged = 1;
	} else {
	    snprintf(retbuf, sizeof(retbuf),
		     "cmd=put_result rc=-1 msg=error_kvs_error\n");
	    PSIlog_log(-1, "%s: error saving value to kvs\n", __func__);
	}
    }

    /* return result to forwarder */
    sendKvsMsg(msg->header.sender, retbuf);
}

/**
 * @brief Handle a kvs get request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsGet(PSLog_Msg_t *msg)
{
    char kvsname[KVSNAME_MAX], name[KEYLEN_MAX], *value, retbuf[PMIU_MAXLINE];
    char *ptr = msg->buf;

    /* parse arguments */
    getpmiv("kvsname",ptr,kvsname,sizeof(kvsname));
    getpmiv("key",ptr,name,sizeof(name));

    if (kvsname[0] == 0 || name[0] == 0 ) {
	PSIlog_log(-1, "%s: invalid kvs get cmd\n", __func__);
	snprintf(retbuf, sizeof(retbuf),
		 "cmd=get_result rc=-1 msg=error_invalid_get_msg\n");
    } else {
	/* get the value from kvs */
	if ((value = kvs_get(kvsname, name))) {
	    snprintf(retbuf, sizeof(retbuf),
		     "cmd=get_result rc=0 value=%s\n", value);
	} else {
	    snprintf(retbuf, sizeof(retbuf),
		     "cmd=get_result rc=-1 msg=error_kvs_error\n");
	    PSIlog_log(-1, "%s: error getting value from kvs\n", __func__);
	}
    }
    /* return result to forwarder */
    sendKvsMsg(msg->header.sender, retbuf);
}

/**
 * @brief Handle a kvs create request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsCreate(PSLog_Msg_t *msg)
{
    char kvsname[KVSNAME_MAX], retbuf[PMIU_MAXLINE];
    char *ptr = msg->buf;

    /* parse arguments */
    if (getpmiv("kvsname", ptr,kvsname, sizeof(kvsname))) {
	PSIlog_log(-1, "%s: bad kvs create msg\n", __func__);
	snprintf(retbuf, sizeof(retbuf), "cmd=newkvs rc=-1\n");
    } else {
	/* create kvs */
	if (!(kvs_create(kvsname))) {
	    snprintf(retbuf, sizeof(retbuf), "cmd=newkvs kvsname=%s\n",
		     kvsname);
	} else {
	    snprintf(retbuf, sizeof(retbuf), "cmd=newkvs rc=-1\n");
	}
    }
    /* return result to forwarder */
    sendKvsMsg(msg->header.sender, retbuf);
}

/**
 * @brief Handle a kvs destroy request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsDestroy(PSLog_Msg_t *msg)
{
    char kvsname[KVSNAME_MAX], retbuf[PMIU_MAXLINE];
    char *ptr = msg->buf;

    /* parse arguments */
    if (getpmiv("kvsname",ptr,kvsname,sizeof(kvsname))) {
	PSIlog_log(-1, "%s: bad kvs destroy msg\n", __func__);
	snprintf(retbuf, sizeof(retbuf), "cmd=kvs_destroyed rc=-1\n");
    } else {
	/* destroy kvs */
	if (!(kvs_destroy(kvsname))) {
	    snprintf(retbuf, sizeof(retbuf), "cmd=kvs_destroyed rc=0\n");
	} else {
	    snprintf(retbuf, sizeof(retbuf), "cmd=kvs_destroyed rc=-1\n");
	}
    }
    /* return result to forwarder */
    sendKvsMsg(msg->header.sender, retbuf);
}

/**
 * @brief Handle a kvs getbyidx request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsGetByIdx(PSLog_Msg_t *msg)
{
    char idx[VALLEN_MAX], kvsname[KVSNAME_MAX], retbuf[PMIU_MAXLINE];
    char name[KVSNAME_MAX];
    char *ptr = msg->buf, *ret, *value;
    int index, len;

    /* parse arguments */
    getpmiv("idx", ptr,idx,sizeof(idx));
    getpmiv("kvsname", ptr,kvsname,sizeof(kvsname));

    if (idx[0] == 0 || kvsname[0] == 0) {
	PSIlog_log(-1, "%s: bad kvs getbyidx msg\n", __func__);
	snprintf(retbuf, sizeof(retbuf),
		 "getbyidx_results rc=-1 reason=wrong_getbyidx_cmd");
    } else {
	index = atoi(idx);
	/* find and return the value */
	if ((ret = kvs_getbyidx(kvsname,index))) {
	    value = strchr(ret,'=') + 1;
	    if (value) {
		len = strlen(ret) - strlen(value) - 1;
	    } else {
		len = strlen(ret) -1;
	    }
	    strncpy(name, ret, len);
	    name[len] = '\0';
	    snprintf(retbuf, sizeof(retbuf),
		     "getbyidx_results rc=0 nextidx=%d key=%s val=%s\n",
		    ++index, name, value);
	} else {
	    snprintf(retbuf, sizeof(retbuf),
		     "getbyidx_results rc=-2 reason=no_more_keyvals\n");
	}
    }

    /* return result to forwarder */
    sendKvsMsg(msg->header.sender, retbuf);
}

/**
 * @brief Distribute kvs update.
 *
 * Send the updated key value space after barrier_in to all clients.
 *
 * @return No return value.
 */
static void sendKvsUpdateToClients(void)
{
    int i, kvsvalcount, valup;
    char kvsmsg[PMIU_MAXLINE], nextval[KEYLEN_MAX + VALLEN_MAX];
    char *kvsname;

    kvsUpdateMsgCount = 0;
    for(i=0; i< kvs_count(); i++) {
	if (!(kvsname = kvs_getKvsNameByIndex(i))) {
	    PSIlog_log(-1, "%s: error finding kvs while update\n", __func__);
	    terminateJob();
	}
	kvsvalcount = kvs_count_values(kvsname);
	valup = 0;
	while (kvsvalcount > valup) {
	    snprintf(kvsmsg, sizeof(kvsmsg),
		     "cmd=kvs_update_cache kvsname=%s", kvsname);
	    /* add the values to the msg */
	    while (kvsvalcount > valup) {
		snprintf(nextval, sizeof(nextval), " %s",
			 kvs_getbyidx(kvsname,valup));
		if ((strlen(nextval) + strlen(kvsmsg) + 2 ) > PMIU_MAXLINE) {
		    break;
		}
		strcat(kvsmsg,nextval);
		valup++;
	    }

	    /* send the msg to all kvs clients */
	    strcat(kvsmsg,"\n");
	    sendMsgToKvsClients(kvsmsg);
	    kvsUpdateMsgCount++;
	}
    }

    /* we are up to date now */
    kvsChanged = 0;

    /* send update finished msg */
    snprintf(kvsmsg, sizeof(kvsmsg), "cmd=kvs_update_cache_finish\n");
    sendMsgToKvsClients(kvsmsg);
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
static void handleBarrierTimeout(void)
{
    PSIlog_log(-1, "Timeout: Not all clients joined the first pmi barrier: "
		   "joined=%i left=%i\n", kvsBarrierInCount,
		    noKvsClients - kvsBarrierInCount);

    /* kill all childs */
    terminateJob();
}

#define USEC_PER_CLIENT 500
/**
 * @brief Set the timeout for the barrier/update msgs.
 *
 * @return No return value.
 */
static void setBarrierTimeout(void)
{
    struct timeval timer;

    if (barrierTimeout == -1) {
	return;
    }

    if (barrierTimeout) {
	/* timeout from user */
	timer.tv_sec = barrierTimeout;
	timer.tv_usec = 0;
    } else {
	/* timeout after 1 min + n * USEC_PER_CLIENT ms */
	timer.tv_sec = 60 + noKvsClients/(1000000/USEC_PER_CLIENT);
	timer.tv_usec = noKvsClients%(1000000/USEC_PER_CLIENT)*USEC_PER_CLIENT;
    }

    timerid = Timer_register(&timer, handleBarrierTimeout);

    if (timerid == -1) {
	PSIlog_log(-1, "%s: failed to set timer\n", __func__);
    }
}

/**
 * @brief Handle a kvs barrier_in request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsBarrierIn(PSLog_Msg_t *msg)
{
    char reply[PMIU_MAXLINE];
    int i;

    /* check if last barrier ended successfully */
    if (kvsCacheUpdateCount > 0) {
	PSIlog_log(-1, "%s: received barrier_in from %s while"
		   " waiting for cache update results\n",
		   __func__, PSC_printTID(msg->header.sender));
	terminateJob();
    }

    /* check for double barrier in msg */
    for (i=0; i< maxKvsClients; i++) {
	if (clientKvsTrackTID[i] == msg->sender) {
	    PSIlog_log(-1, "%s: received barrier_in twice from %s\n",
		       __func__, PSC_printTID(msg->header.sender));
	    terminateJob();
	}
    }

    /* Set timeout waiting for all clients to join barrier */
    if (kvsBarrierInCount == 0) {
	setBarrierTimeout();
    }

    /* save sender to array */
    clientKvsTrackTID[kvsBarrierInCount] = msg->header.sender;
    kvsBarrierInCount++;

    /* debugging output */
    if (debug_kvs) {
	PSIlog_log(-1, "%s: ->barrier_in, barrierCount:%i,"
		   " noKvsClients:%i\n",
		   __func__, kvsBarrierInCount, noKvsClients);
    }

    /* all clients joined barrier */
    if (kvsBarrierInCount == noKvsClients) {
	if (timerid != -1) Timer_remove(timerid);
	timerid = -1;
	/* reset tracking array */
	for (i=0; i< maxKvsClients; i++) {
	    clientKvsTrackTID[i] = -1;
	}

	kvsBarrierInCount = 0;

	if (kvsChanged) {
	    /* distribute kvs update */
	    sendKvsUpdateToClients();
	} else {
	    /* send all Clients barrier_out */
	    snprintf(reply, sizeof(reply), "cmd=barrier_out\n");
	    sendMsgToKvsClients(reply);
	}

    }
}

/**
 * @brief Handle a kvs count request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsCount(PSLog_Msg_t *msg)
{
    char reply[PMIU_MAXLINE];

    /* return result */
    snprintf(reply, sizeof(reply),
	     "cmd=kvs_count count=%i rc=0\n", kvs_count());
    sendKvsMsg(msg->header.sender, reply);
}

/**
 * @brief Handle a kvs count values request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsValueCount(PSLog_Msg_t *msg)
{
    char reply[PMIU_MAXLINE];
    char kvsname[KVSNAME_MAX];
    char *ptr = msg->buf;

    /* parse arguments */
    if (getpmiv("kvsname", ptr,kvsname, sizeof(kvsname))) {
	PSIlog_log(-1, "%s: bad kvs value count msg\n", __func__);
	snprintf(reply, sizeof(reply), "cmd=kvs_value_count rc=-1\n");
    } else {
	/* return result */
	snprintf(reply, sizeof(reply),
		 "cmd=kvs_value_count count=%i kvsname=%s rc=0\n",
		 kvs_count_values(kvsname), kvsname);
    }
    sendKvsMsg(msg->header.sender, reply);
}

/**
 * @brief Handle a kvs update cache result msg.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsUpdateCacheResult(PSLog_Msg_t *msg)
{
    char *ptr = msg->buf;
    char mc[VALLEN_MAX], reply[PMIU_MAXLINE];
    int i;

    /* check if barrier_in is finished */
    if (kvsBarrierInCount >0 ) {
	PSIlog_log(-1, "%s: received new barrier_in from %s while"
		   " waiting for update cache results\n",
		   __func__, PSC_printTID(msg->header.sender));
	terminateJob();
    }

    /* check for double update cache msg */
    for (i=0; i< maxKvsClients; i++) {
	if (clientKvsTrackTID[i] == msg->sender) {
	    PSIlog_log(-1, "%s: received update cache result twice from %s\n",
		       __func__, PSC_printTID(msg->header.sender));
	    terminateJob();
	}
    }

    /* parse arguments */
    getpmiv("mc",ptr,mc,sizeof(mc));

    if (mc[0] == 0) {
	PSIlog_log(-1, "%s: invalid kvs update cache reply\n", __func__);
	return;
    }

    /* check if client got all the updates */
    if (atoi(mc) != kvsUpdateMsgCount) {
	PSIlog_log(-1, "%s: kvs client did not get all kvs update msgs\n",
		   __func__);
	terminateJob();
    }

    /* Set timeout waiting for all clients to join barrier */
    if (kvsCacheUpdateCount == 0) {
	setBarrierTimeout();
    }

    /* save sender to array */
    kvsCacheUpdateCount++;
    clientKvsTrackTID[kvsBarrierInCount] = msg->header.sender;

    /* received all update requests */
    if (kvsCacheUpdateCount == noKvsClients) {
	if (timerid != -1) Timer_remove(timerid);
	timerid=-1;
	barrierTimeout = -1;
	kvsCacheUpdateCount = 0;
	kvsUpdateMsgCount = 0;
	/* reset tracking array */
	for (i=0; i< maxKvsClients; i++) {
	    clientKvsTrackTID[i] = -1;
	}
	/* send all Clients barrier_out */
	snprintf(reply,sizeof(reply),"cmd=barrier_out\n");
	sendMsgToKvsClients(reply);
    }
}

/**
 * @brief Handle a kvs join request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsJoin(PSLog_Msg_t *msg)
{
    static int count = 0;

    if (msg->sender >= maxKvsClients) {
	int i;

	clientKvsTID = realloc(clientKvsTID,
			       sizeof(*clientKvsTID) * 2 * msg->sender);
	clientKvsTrackTID = realloc(clientKvsTrackTID,
				    sizeof(*clientKvsTrackTID)*2*msg->sender);

	if (!clientKvsTID || !clientKvsTrackTID) {
	    PSIlog_log(-1, "%s: realloc() failed.\n", __func__);
	    terminateJob();
	    exit(EXIT_FAILURE);
	}

	for (i=maxKvsClients; i<2*msg->sender; i++) {
	    clientKvsTID[i] = -1;
	    clientKvsTrackTID[i] = -1;
	}

	maxKvsClients = 2*msg->sender;
    }
    if (clientKvsTID[msg->sender] != -1) {
	PSIlog_log(-1, "%s: %s (rank %d) already in kvs.\n", __func__,
		   PSC_printTID(msg->header.sender), msg->sender);
    } else {
	clientKvsTID[msg->sender] = msg->header.sender;
	count++;
	if (count > noKvsClients) {
	    noKvsClients++;
	}
    }
}

/**
 * @brief Handle a kvs leave request.
 *
 * @param msg The kvs message received from the forwarder.
 *
 * @return No return value.
 */
static void handleKvsLeave(PSLog_Msg_t *msg)
{
    /* Try to find the corresponding client */
    int i = 0;
    char reply[PMIU_MAXLINE];

    while ((i < maxKvsClients) && (clientKvsTID[i] != msg->header.sender)) i++;

    if (i == maxKvsClients) {
	PSIlog_log(-1, "%s: error invalid leave kvs msg from: %s.\n",
		   __func__, PSC_printTID(msg->header.sender));
	errno = EBADMSG;
	terminateJob();
    }
    clientKvsTID[i] = -1;

    /* Remove client from barrier_in waiting */
    if (kvsBarrierInCount > 0) {
	for (i=0; i< maxKvsClients; i++) {
	    if (clientKvsTrackTID[i] == msg->header.sender) {
		clientKvsTrackTID[i] = -1;
		kvsBarrierInCount--;
		noKvsClients--;
	    }
	}

	/* all clients joined barrier */
	if (kvsBarrierInCount == noKvsClients) {
	    /* reset tracking array */
	    for (i=0; i< maxKvsClients; i++) {
		clientKvsTrackTID[i] = -1;
	    }

	    kvsBarrierInCount = 0;
	    /* send kvs update to all clients */
	    sendKvsUpdateToClients();
	}

    } else if (kvsCacheUpdateCount > 0) {
	for (i=0; i< maxKvsClients; i++) {
	    if (clientKvsTrackTID[i] == msg->header.sender) {
		clientKvsTrackTID[i] = -1;
		kvsCacheUpdateCount--;
		noKvsClients--;
	    }
	}

	/* received all update requests */
	if (kvsCacheUpdateCount == noKvsClients) {
	    kvsCacheUpdateCount = 0;
	    kvsUpdateMsgCount = 0;
	    /* reset tracking array */
	    for (i=0; i< maxKvsClients; i++) {
		clientKvsTrackTID[i] = -1;
	    }
	    /* send all Clients barrier_out */
	    snprintf(reply, sizeof(reply), "cmd=barrier_out\n");
	    sendMsgToKvsClients(reply);
	}

    } else {
	noKvsClients--;
    }
}

int getNumKvsClients(void)
{
    return noKvsClients;
}

/**
 * @brief Extract the cmd from a kvs message.
 *
 * @param msg The kvs msg to extract the cmd from.
 *
 * @return Returns a pointer to the extracted cmd or
 * NULL on error.
 */
static char *getKvsCmd(char *msg)
{

    const char delimiters[] =" \n";
    char *cmd, *saveptr;

    if (!msg || strlen(msg) < 5 ) {
	return NULL;
    }

    cmd = strtok_r(msg,delimiters,&saveptr);

    while ( cmd != NULL ) {
	if( !strncmp(cmd,"cmd=",4) ) {
	    cmd = cmd +4;
	    return cmd;
	}
	cmd = strtok_r(NULL,delimiters,&saveptr);
    }
    return NULL;
}

/**
 * @brief Parse and handle a pmi kvs message.
 *
 * @param msg The received kvs msg to handle.
 *
 * @return No return value.
 */
void handleKvsMsg(PSLog_Msg_t *msg)
{
    char cmd[VALLEN_MAX], *cmdtmp, *msgCopy;

    if (debug_kvs) {
	PSIlog_log(-1, "%s: new pmi kvs msg: '%s'\n", __func__, msg->buf);
    }

    /* extract the kvs command */
    if(!(msgCopy = strdup(msg->buf))) {
	PSIlog_log(-1, "%s: out of memory, exiting\n", __func__);
	terminateJob();
	exit(EXIT_FAILURE);
    }

    if (!(cmdtmp = getKvsCmd(msgCopy))) {
	PSIlog_log(-1, "%s: invalid kvs cmd\n", __func__);
	terminateJob();
    }
    strncpy(cmd, cmdtmp, sizeof(cmd));
    free(msgCopy);

    /* handle the kvs command */
    if (!strcmp(cmd,"put")) {
	handleKvsPut(msg);
    } else if (!strcmp(cmd,"get")) {
	handleKvsGet(msg);
    } else if (!strcmp(cmd,"getbyidx")) {
	handleKvsGetByIdx(msg);
    } else if (!strcmp(cmd,"create_kvs")) {
	handleKvsCreate(msg);
    } else if (!strcmp(cmd,"destroy_kvs")) {
	handleKvsDestroy(msg);
    } else if (!strcmp(cmd,"barrier_in")) {
	handleKvsBarrierIn(msg);
    } else if (!strcmp(cmd,"get_kvs_count")) {
	handleKvsCount(msg);
    } else if (!strcmp(cmd,"get_kvs_value_count")) {
	handleKvsValueCount(msg);
    } else if (!strcmp(cmd,"kvs_update_cache_result")) {
	handleKvsUpdateCacheResult(msg);
    } else if (!strcmp(cmd,"join_kvs")) {
	handleKvsJoin(msg);
    } else if (!strcmp(cmd,"leave_kvs")) {
	handleKvsLeave(msg);
    } else {
	PSIlog_log(-1, "%s: unsupported pmi kvs cmd '%s'\n", __func__, cmd);
	terminateJob();
    }
}
