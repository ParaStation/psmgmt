/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "list.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "timer.h"

#include "plugin.h"
#include "pluginlog.h"
#include "pluginmalloc.h"
#include "pluginpsconfig.h"

#include "psidcomm.h"
#include "psidplugin.h"
#include "psidutil.h"

int requiredAPI = 136;

char name[] = "delayPSPMsg";

int version = 100;

plugin_dep_t dependencies[] = {
    { NULL, 0 } };

/** Description of fakenode's configuration parameters */
static const pluginConfigDef_t confDef[] = {
    { "DebugMask", PLUGINCONFIG_VALUE_NUM,
      "Mask to steer debug output" },
};

pluginConfig_t config = NULL;

static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    if (!strcmp(key, "DebugMask")) {
	uint32_t mask = val ? val->val.num : 0;
	maskPluginLogger(mask);
	if (mask) pluginlog("debugMask set to %#x\n", mask);
    } else {
	pluginlog("%s: unknown key '%s'\n", __func__, key);
    }

    return true;
}

typedef struct {
    list_t next;         /**< used to put into DelayContainer_t.messages */
    DDBufferMsg_t *msg;  /**< delayed messages */
} msgContainer_t;

typedef struct {
    list_t next;         /**< */
    uint16_t type;       /**< type of messages to delay */
    uint32_t subType;    /**< sub-type of messages to delay -- currently ignored */
    uint32_t delay;
    int timerID;         /**< timer used to delay this type of message */
    list_t messages;     /**< queue of delayed messages */
} DelayContainer_t;

/** @doctodo */
static LIST_HEAD(delayContainerList);

msgContainer_t *newMsgContainer(DDBufferMsg_t *msg)
{
    if (!msg) return NULL;

    msgContainer_t *msgContainer = malloc(sizeof(*msgContainer));
    if (msgContainer) {
	msgContainer->msg = malloc(msg->header.len);
	if (!msgContainer->msg) {
	    free(msgContainer);
	    return NULL;
	}
	memcpy(msgContainer->msg, msg, msg->header.len);
    }
    return msgContainer;
}

void clearMsgContainer(msgContainer_t * msgContainer)
{
    if (msgContainer) free(msgContainer->msg);
    free(msgContainer);
}

DelayContainer_t *newDelayContainer(void)
{
    DelayContainer_t *delayContainer = malloc(sizeof(*delayContainer));

    if (delayContainer) {
	delayContainer->type = 0;
	delayContainer->subType = 0;
	delayContainer->timerID = -1;
	INIT_LIST_HEAD(&delayContainer->messages);
    }
    return delayContainer;
}

void clearDelayContainer(DelayContainer_t *delayContainer)
{
    if (!delayContainer) return;

    if (delayContainer->timerID > -1) {
	Timer_remove(delayContainer->timerID);
	delayContainer->timerID = -1;
    }

    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &delayContainer->messages) {
	msgContainer_t *msg = list_entry(m, msgContainer_t, next);
	list_del(&msg->next);
	clearMsgContainer(msg);
    }
    free(delayContainer);
}

DelayContainer_t *findDelayContainer(uint16_t type, uint32_t subType)
{
    list_t *d;
    list_for_each(d, &delayContainerList) {
	DelayContainer_t *delay = list_entry(d, DelayContainer_t, next);
	if (delay->type != type) continue;
	if (delay->subType && subType && delay->subType != subType) continue;
	return delay;
    }
    return NULL;
}

/** pointer to the message currently delivered by deliverMsgs() */
static DDBufferMsg_t *msgToDeliver = NULL;

bool delayHandler(DDBufferMsg_t *msg)
{
    if (msg == msgToDeliver) {
	// fall back to original handlers
	msgToDeliver = NULL;
	return false;
    }

    DelayContainer_t *delayContainer = findDelayContainer(msg->header.type, 0);

    if (!delayContainer) {
	PSID_log(-1, "%s: no delay for type %d\n", __func__, msg->header.type);
	return false;
    }

    msgContainer_t *msgContainer = newMsgContainer(msg);
    if (!msgContainer) {
	PSID_log(-1, "%s: unabled to cache messsage of type %d\n", __func__,
		 msg->header.type);
	return false;
    }

    list_add_tail(&msgContainer->next, &delayContainer->messages);

    return true;
}

void deliverMsgs(int timerID, void *info)
{
    DelayContainer_t *delayContainer = info;

    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &delayContainer->messages) {
	msgContainer_t *msg = list_entry(m, msgContainer_t, next);

	msgToDeliver = msg->msg;
	PSID_handleMsg(msg->msg);

	list_del(&msg->next);
	clearMsgContainer(msg);
    }
}

bool installDelayHandler(uint16_t type, uint32_t subType, uint32_t delay)
{
    DelayContainer_t *delayContainer = findDelayContainer(type, subType);

    if (!delayContainer) {
	delayContainer = newDelayContainer();
	if (!delayContainer) {
	    PSID_log(-1, "%s: unabled to delay messsages of type %d\n",
		     __func__, type /*, subType*/);
	    return false;
	}
	delayContainer->type = type;
	delayContainer->subType = subType;
	PSID_registerMsg(type, delayHandler);
	list_add_tail(&delayContainer->next, &delayContainerList);
    }
    /* old delay handler; we just have to re-time */
    if (delayContainer->timerID > -1) Timer_remove(delayContainer->timerID);

    struct timeval timeout = {
	.tv_sec = delay / 1000,
	.tv_usec = (delay * 1000) % (1000*1000) };

    delayContainer->delay = delay;
    delayContainer->timerID =
	Timer_registerEnhanced(&timeout, deliverMsgs, delayContainer);

    return true;
}

int initialize(FILE *logfile)
{
    initPluginLogger(name, logfile);

    /* init configuration (depends on psconfig) */
    pluginConfig_new(&config);
    pluginConfig_setDef(config, confDef);

    pluginConfig_load(config, "DelayPSPMsg");
    pluginConfig_verify(config);

    /* Activate configuration values */
    pluginConfig_traverse(config, evalValue, NULL);

    pluginlog("(%i) successfully started\n", version);

    return 0;
}

void finalize(void)
{
    /* deliver all messages and cleanup handlers and timers */
    list_t *d, *tmp;
    list_for_each_safe(d, tmp, &delayContainerList) {
	DelayContainer_t *delayContainer = list_entry(d, DelayContainer_t, next);

	PSID_clearMsg(delayContainer->type, delayHandler);
	deliverMsgs(delayContainer->timerID, delayContainer);

	list_del(&delayContainer->next);
	clearDelayContainer(delayContainer); // this also cleans up the timer
    }

    PSIDplugin_unload(name);
}

void cleanup(void)
{
    pluginlog("%s\n", __func__);

    /* deliver all messages and cleanup handlers and timers */
    list_t *d, *tmp;
    list_for_each_safe(d, tmp, &delayContainerList) {
	DelayContainer_t *delayContainer = list_entry(d, DelayContainer_t, next);

	list_del(&delayContainer->next);

	PSID_clearMsg(delayContainer->type, delayHandler);
	clearDelayContainer(delayContainer); // this also cleans timer and msgs
    }

    pluginConfig_destroy(config);
    config = NULL;

    pluginlog("%s: Done\n", __func__);
}


char * help(char *key)
{
    StrBuffer_t strBuf = { .buf = NULL };

    addStrBuf(
	"\tDelay specific types of messages for debugging purposes.\n\n"
	"\tUse the show directive to list all message types delayed:\n",
	&strBuf);
    addStrBuf("\t\tplugin show ", &strBuf);
    addStrBuf(name, &strBuf);
    addStrBuf("\n", &strBuf);
    addStrBuf(
	"\tUse the set directive add a new message type to delay\n",
	&strBuf);
    addStrBuf("\t\tplugin set ", &strBuf);
    addStrBuf(name, &strBuf);
    addStrBuf(" <message type> <delay in msec>\n", &strBuf);
    addStrBuf(
	"\tUse the unset directive clear a messages type from delay\n",
	&strBuf);
    addStrBuf("\t\tplugin unset ", &strBuf);
    addStrBuf(name, &strBuf);
    addStrBuf(" <message type>\n", &strBuf);
    addStrBuf("\n# configuration options #\n\n", &strBuf);

    pluginConfig_helpDesc(config, &strBuf);
    return strBuf.buf;
}

void printDelays(StrBuffer_t *strBuf)
{
    if (list_empty(&delayContainerList)) {
	addStrBuf("\tno messages to be delayed\n\n", strBuf);
	return;
    }

    addStrBuf("\n", strBuf);
    list_t *d;
    list_for_each(d, &delayContainerList) {
	DelayContainer_t *delay = list_entry(d, DelayContainer_t, next);
	char tmpStr[128];

	addStrBuf("\t", strBuf);
	addStrBuf(PSDaemonP_printMsg(delay->type), strBuf);
	snprintf(tmpStr, sizeof(tmpStr), "\tdelayed by %d msec\n", delay->delay);
	addStrBuf(tmpStr, strBuf);
    }
    addStrBuf("\n", strBuf);
}

char * show(char *key)
{
    StrBuffer_t strBuf = { .buf = NULL };

    if (!key) {
	/* Show the whole configuration */
	addStrBuf("\n", &strBuf);
	pluginConfig_traverse(config, pluginConfig_showVisitor,&strBuf);
    } else if (!strncmp(key, "delays", strlen("delays"))) {
	printDelays(&strBuf);
    } else if (!pluginConfig_showKeyVal(config, key, &strBuf)) {
	addStrBuf(" '", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf("' is unknown\n", &strBuf);
    }

    return strBuf.buf;
}

char * set(char *key, char *val)
{
    char l[128];
    if (!strcmp(key, "delay")) {
	int delay;

	if (sscanf(val, "%i", &delay) != 1) {
	    snprintf(l, sizeof(l), "\ndelay '%s' not a number\n", val);
	} else {
	    snprintf(l, sizeof(l), "\tdelay now %d msec\n", delay);
	}
    } else {
	snprintf(l, sizeof(l), "\nInvalid key '%s' for cmd set:"
		 " use 'plugin help delaySlurmMsg' for help.\n", key);
    }

    return strdup(l);
}

char * unset(char *key)
{
    char l[128];
    if (!strcmp(key, "delay")) {
	int delay;
	char val[] = "100";

	if (sscanf(val, "%i", &delay) != 1) {
	    snprintf(l, sizeof(l), "\ndelay '%s' not a number\n", val);
	} else {
	    snprintf(l, sizeof(l), "\tdelay now %d msec\n", delay);
	}
    } else {
	snprintf(l, sizeof(l), "\nInvalid key '%s' for cmd set:"
		 " use 'plugin help delaySlurmMsg' for help.\n", key);
    }

    return strdup(l);
}
