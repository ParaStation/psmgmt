/*
 * ParaStation
 *
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "list.h"
#include "pscommon.h"
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

/** Description of delayPSPMsg's configuration parameters */
static const pluginConfigDef_t confDef[] = {
    { "DebugMask", PLUGINCONFIG_VALUE_NUM,
      "Mask to steer debug output" },
};

typedef enum {
    DELAY_LOG_VERBOSE = 0x00001,   /**< Log every delayed message */
} NodeInfo_log_types_t;

/** Container for a single message to delay */
typedef struct {
    list_t next;         /**< used to put into DelayContainer_t.messages */
    DDBufferMsg_t *msg;  /**< delayed messages */
} msgContainer_t;

/**
 * @brief Create message container
 *
 * Create a new message container and store the message @a msg to it.
 *
 * @param msg Message to be put into the new message container
 *
 * @return On success a pointer to the new message conainer is
 * returned; or NULL in case of failure
 */
static msgContainer_t *newMsgContainer(DDBufferMsg_t *msg)
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

/**
 * @brief Delete message container
 *
 * Delete the message container @a msgContainer including the
 * contained message.
 *
 * @param msgContainer Message container to delete
 *
 * @return No return value
*/
static void delMsgContainer(msgContainer_t *msgContainer)
{
    if (msgContainer) free(msgContainer->msg);
    free(msgContainer);
}

typedef struct {
    list_t next;         /**< used to put into delayContainerList */
    uint16_t type;       /**< type of messages to delay */
    uint32_t subType;    /**< sub-type of messages to delay -- currently ignored */
    uint32_t delay;      /**< delay (in msec) the message shall suffer */
    int timerID;         /**< timer used to delay this type of message */
    list_t messages;     /**< queue of delayed messages */
} DelayContainer_t;

/**
 * @brief Create an empty container
 *
 * Create a new delay container.
 *
 * @return On success a pointer to the new delay conainer is returned;
 * or NULL in case of failure
 */
static DelayContainer_t *newDelayContainer(void)
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

/**
 * @brief Delete delay container
 *
 * Delete the delay container @a delayContainer and clean it up before
 * if necessary. This includes removing the corresponding timer and
 * dropping all messages still pending in the container.
 *
 * @param delayContainer Delay container to delete
 *
 * @return No return value
*/
static void delDelayContainer(DelayContainer_t *delayContainer)
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
	delMsgContainer(msg);
    }
    free(delayContainer);
}

/** List of all delay containers */
static LIST_HEAD(delayContainerList);

/**
 * @brief Find delay container
 *
 * Find the delay container identified by the message's type @a type
 * and its sub-type @a subType. If the latter is 0 the sub-type will
 * be ignored. If a corresponding delay container is found a pointer
 * to it will be reeturned.
 *
 * @param type Message type to be delayed according to the container
 *
 * @param subType Message sub-type; if 0, sub-types are ignored
 *
 * @return Pointer to the corresponding delay container if any or NULL
 * otherwise
 */
static DelayContainer_t *findDelayContainer(uint16_t type, uint32_t subType)
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

/**
 * @brief Message handler to delay a message
 *
 * Handle the message @a msg by delaying it according to a
 * corresponding delay container. The actual delay will be taken from
 * this container. If the message cannot be delayed since e.g. no
 * delay container is found or now new message container was
 * available, false is returned and the message is passed through to
 * its orginal handler if any.
 *
 * Keep in mind that this function will called, too, once the delayed
 * messages are actually delivered (via @ref deliverMsgs()). For this
 * @ref msgToDeliver will be set there to the message to deliver. This
 * function will not handle such messages @ref msgToDeliver is
 * pointing to and pass them through their original handler
 *
 * @param msg Message to handle (i.e. to delay)
 *
 * @return If the message can be delayed, true is returned marking the
 * message as fully handled; otherwise false is returned in order to
 * pass the message through to its original handler
 */
static bool delayHandler(DDBufferMsg_t *msg)
{
    if (msg == msgToDeliver) {
	/* fall back to original handlers */
	msgToDeliver = NULL;
	return false;
    }

    DelayContainer_t *delayC = findDelayContainer(msg->header.type, 0);

    if (!delayC) {
	pluginlog("%s: no delay for type %d\n", __func__, msg->header.type);
	return false;
    }

    msgContainer_t *msgContainer = newMsgContainer(msg);
    if (!msgContainer) {
	pluginlog("%s: unabled to cache message of type %d\n", __func__,
		  msg->header.type);
	return false;
    }

    if (getPluginLoggerMask() & DELAY_LOG_VERBOSE) {
	plugindbg(DELAY_LOG_VERBOSE, "delay %s msg %s ->",
		  PSDaemonP_printMsg(msg->header.type),
		  PSC_printTID(msg->header.sender));
	plugindbg(DELAY_LOG_VERBOSE, " %s\n",
		  PSC_printTID(msg->header.dest));
    }

    /* ensure we see the full delay for the first message */
    if (list_empty(&delayC->messages)) Timer_restart(delayC->timerID);
    list_add_tail(&msgContainer->next, &delayC->messages);

    return true;
}

/**
 * @brief Deliver delayed messages
 *
 * Deliver all delayed messages associated to the delay container @a
 * info is pointing to.
 *
 * @param timerID ID of the timer that expired and calls this function
 *
 * @param info Pointer to the delay container holding the messages to
 * deliver
 *
 * @return No return value
 */
static void deliverMsgs(int timerID, void *info)
{
    DelayContainer_t *delayContainer = info;

    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &delayContainer->messages) {
	msgContainer_t *msg = list_entry(m, msgContainer_t, next);

	if (getPluginLoggerMask() & DELAY_LOG_VERBOSE) {
	    plugindbg(DELAY_LOG_VERBOSE, "deliver %s msg %s ->",
		      PSDaemonP_printMsg(msg->msg->header.type),
		      PSC_printTID(msg->msg->header.sender));
	    plugindbg(DELAY_LOG_VERBOSE, " %s\n",
		      PSC_printTID(msg->msg->header.dest));
	}
	msgToDeliver = msg->msg;
	PSID_handleMsg(msg->msg);

	list_del(&msg->next);
	delMsgContainer(msg);
    }
}

/**
 * @brief Install handler to delay messages
 *
 * Install a handler that delays messages of type @a type and sub-type
 * @a subType by @a delay milliseconds.
 *
 * For the time being the sub-type will be ignored.
 *
 * @param type Message type to delay
 *
 * @param subType Message sub-type to delay -- currently ignored
 *
 * @param delay The delay to be applied to messages in milliseconds
 *
 * @return Return true if the handler was installed successfully or
 * false otherwise
 */
static bool installDelayHandler(uint16_t type, uint32_t subType, uint32_t delay)
{
    DelayContainer_t *delayContainer = findDelayContainer(type, subType);

    if (!delayContainer) {
	delayContainer = newDelayContainer();
	if (!delayContainer) {
	    PSID_log(-1, "%s: unabled to delay messages of type %d\n",
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

/**
 * @brief Remove delay handler
 *
 * Remove the delay handler described by the delay container @a
 * delayContainer. Removing the delay handler includes delivery of all
 * pending messages and cleaning up the corresponding timer.
 *
 * @param delayContainer Delay container describing the delay handler
 * to remove
 *
 * @return Return true if the handler was removed successfully or
 * false otherwise
 */
static bool doRemoveDelayHandler(DelayContainer_t *delayContainer)
{
    PSID_clearMsg(delayContainer->type, delayHandler);
    deliverMsgs(delayContainer->timerID, delayContainer);

    list_del(&delayContainer->next);
    delDelayContainer(delayContainer); // this also cleans up the timer

    return true;
}

/**
 * @brief Remove delay handler
 *
 * Remove the delay handler for messages of type @a type and sub-type
 * @a subType. This is basically a wrapper around @ref
 * doRemoveDelayHandler(). Thus, removing the handler will include
 * delivery of pending messages and removing the associated timer.
 *
 * For the time being the sub-type will be ignored.
 *
 * @param type Message type to delay
 *
 * @param subType Message sub-type to delay -- currently ignored
 *
 * @param delay The delay to be applied to messages in milliseconds
 *
 * @return Return true if the handler was removed successfully or
 * false otherwise
 */
static bool removeDelayHandler(uint16_t type, uint32_t subType)
{
    DelayContainer_t *delayContainer = findDelayContainer(type, subType);
    if (!delayContainer) return false;
    return doRemoveDelayHandler(delayContainer);
}

static pluginConfig_t config = NULL;

static char * doEval(const char *key, const pluginConfigVal_t *val,
		     const void *info)
{
    StrBuffer_t strBuf = { .buf = NULL };

    if (!strcmp(key, "DebugMask")) {
	uint32_t mask = val ? val->val.num : 0;
	maskPluginLogger(mask);
	if (mask) pluginlog("debugMask set to %#x\n", mask);
	addStrBuf("\tdebugMask set to ", &strBuf);
	char tmp[32];
	snprintf(tmp, sizeof(tmp), "%#x", mask);
	addStrBuf(tmp, &strBuf);
	addStrBuf("\n", &strBuf);
    } else {
	pluginlog("%s: unknown key '%s'\n", __func__, key);
    }

    return strBuf.buf;
}

static bool evalValue(const char *key, const pluginConfigVal_t *val,
		      const void *info)
{
    char *ret = doEval(key, val, info);
    free(ret);

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
	DelayContainer_t *delayC = list_entry(d, DelayContainer_t, next);
	doRemoveDelayHandler(delayC);
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
	delDelayContainer(delayContainer); // this also cleans timer and msgs
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
    addStrBuf(" key delays\n", &strBuf);
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

static void printDelays(StrBuffer_t *strBuf)
{
    if (list_empty(&delayContainerList)) {
	addStrBuf("\tno messages to be delayed\n\n", strBuf);
	return;
    }

    addStrBuf("\n", strBuf);
    list_t *d;
    list_for_each(d, &delayContainerList) {
	DelayContainer_t *delay = list_entry(d, DelayContainer_t, next);

	addStrBuf("\t", strBuf);
	addStrBuf(PSDaemonP_printMsg(delay->type), strBuf);
	char tmpStr[128];
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

static int16_t resolveMsgType(char *typeStr, StrBuffer_t *strBuf)
{
    int16_t msgType = PSDaemonP_resolveType(typeStr);
    if (msgType == -1) {
	/* maybe a number was given? */
	char *end;
	msgType = strtol(typeStr, &end, 0);
	if (*end || msgType < 1) {
	    addStrBuf("\tunknown message type '", strBuf);
	    addStrBuf(typeStr, strBuf);
	    addStrBuf("'\n", strBuf);
	    return -1;
	}
    }
    return msgType;
}

char * set(char *key, char *val)
{
    StrBuffer_t strBuf = { .buf = NULL };
    const pluginConfigDef_t *thisDef = pluginConfig_getDef(config, key);

    if (thisDef) {
	if (!pluginConfig_addStr(config, key, val)) {
	    addStrBuf("  Illegal value '", &strBuf);
	    addStrBuf(val, &strBuf);
	    addStrBuf("'\n", &strBuf);
	    return strBuf.buf;
	}
	return doEval(key, pluginConfig_get(config, key), NULL);
    }

    /* resolve message type */
    int16_t msgType = resolveMsgType(key, &strBuf);
    if (msgType == -1) return strBuf.buf;

    uint32_t delay = 0;
    if (val) {
	char *end;
	delay = strtol(val, &end, 0);
	if (*end) delay = 0;
    }

    if (delay < 100) {
	addStrBuf("\tillegal delay '", &strBuf);
	addStrBuf(val ? val : "<empty>", &strBuf);
	addStrBuf("' (must be >100)\n", &strBuf);
    } else if (!installDelayHandler(msgType, 0 , delay)) {
	addStrBuf("\tfailed to install delay handler for '", &strBuf);
	addStrBuf(key, &strBuf);
	addStrBuf("'\n", &strBuf);
    } else {
	addStrBuf("\tdelay '", &strBuf);
	addStrBuf(PSDaemonP_printMsg(msgType), &strBuf);
	addStrBuf("' by ", &strBuf);
	addStrBuf(val, &strBuf);
	addStrBuf(" msec\n", &strBuf);
    }

    return strBuf.buf;
}

char * unset(char *key)
{
    StrBuffer_t strBuf = { .buf = NULL };
    const pluginConfigDef_t *thisDef = pluginConfig_getDef(config, key);

    if (thisDef) {
	pluginConfig_remove(config, key);
	return doEval(key, NULL, NULL);
    }

    /* resolve message type */
    int16_t msgType = resolveMsgType(key, &strBuf);
    if (msgType == -1) return strBuf.buf;

    if (!removeDelayHandler(msgType, 0)) {
	addStrBuf("\tfailed to remove delay handler for '", &strBuf);
	addStrBuf(PSDaemonP_printMsg(msgType), &strBuf);
	addStrBuf("'\n", &strBuf);
    } else {
	addStrBuf("\tdelay handler for '", &strBuf);
	addStrBuf(PSDaemonP_printMsg(msgType), &strBuf);
	addStrBuf("' removed\n", &strBuf);
    }

    return strBuf.buf;
}
