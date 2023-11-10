/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidflowcontrol.h"

#include <stdlib.h>

#include "selector.h"

#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "psitems.h"

#include "psidnodes.h"
#include "psidcomm.h"
#include "psidutil.h"
#include "psidtask.h"
#include "psidclient.h"

/** StopTID structure */
typedef struct {
    list_t next;              /**< used to put into hashtable-lists */
    PStask_ID_t tid;          /**< unique task identifier */
} stopTID_t;

/** data structure to handle a pool of stopTIDs */
static PSitems_t stopTIDs = NULL;

/* ==================== Pool management for stopTIDs ==================== */

static bool relocStopTID(void *item)
{
    stopTID_t *orig = item, *repl = PSitems_getItem(stopTIDs);

    if (!repl) return false;

    repl->tid = orig->tid;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

/**
 * @brief Garbage collection
 *
 * Do garbage collection on unused stopTID structures. Since this
 * module will keep pre-allocated buffers for stopTID structures its
 * memory-footprint might have grown after phases of heavy
 * usage. Thus, this function shall be called regularly in order to
 * free() stopTID structures no longer required.
 *
 * @return No return value.
 */
static void PSIDFlwCntrl_gc(void)
{
    PSitems_gc(stopTIDs, relocStopTID);
}

/* ==================== Hash management for stopTIDs ==================== */

void PSIDFlwCntrl_initHash(PSIDFlwCntrl_hash_t hash)
{
    int i;
    for (i = 0; i < FLWCNTRL_HASH_SIZE; i++) INIT_LIST_HEAD(&hash[i]);
}

void PSIDFlwCntrl_emptyHash(PSIDFlwCntrl_hash_t hash)
{
    int i;
    for (i = 0; i < FLWCNTRL_HASH_SIZE; i++) {
	if (list_empty(&hash[i])) continue;

	list_t *s, *tmp;
	list_for_each_safe(s, tmp, &hash[i]) {
	    stopTID_t *stop = list_entry(s, stopTID_t, next);

	    list_del(&stop->next);
	    PSitems_putItem(stopTIDs, stop);
	}
    }
}

int PSIDFlwCntrl_addStop(PSIDFlwCntrl_hash_t table, PStask_ID_t key)
{
    list_t *t;
    list_for_each(t, &table[key%FLWCNTRL_HASH_SIZE]) {
	stopTID_t *stid = list_entry(t, stopTID_t, next);

	if (stid->tid == key) return 0;
    }

    stopTID_t *stop = PSitems_getItem(stopTIDs);
    if (!stop) return -1;

    stop->tid = key;
    list_add_tail(&stop->next, &table[key%FLWCNTRL_HASH_SIZE]);

    return 1;
}

int PSIDFlwCntrl_sendContMsgs(PSIDFlwCntrl_hash_t stops, PStask_ID_t sender)
{
    int idx, num = 0;
    list_t *s, *tmp;

    for (idx = 0; idx < FLWCNTRL_HASH_SIZE; idx++) {
	list_for_each_safe(s, tmp, &stops[idx]) {
	    stopTID_t *stop = list_entry(s, stopTID_t, next);
	    if (PSC_getPID(stop->tid) && PSIDnodes_isUp(PSC_getID(stop->tid))) {
		DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				    .sender = sender,
				    .dest = stop->tid,
				    .len = sizeof(contmsg) };
		sendMsg(&contmsg);
		num++;
	    }
	    list_del(&stop->next);
	    PSitems_putItem(stopTIDs, stop);
	}
    }

    return num;
}

bool PSIDFlwCntrl_applicable(DDMsg_t *msg)
{
    if (PSC_getPID(msg->sender)) {
	switch (msg->type) {
	case PSP_CD_ACCOUNT:
	case PSP_CD_SENDCONT:
	case PSP_CD_SENDSTOP:
	case PSP_DD_SENDCONT:
	case PSP_DD_SENDSTOP:
	case PSP_DD_SENDSTOPACK:
	case PSP_DD_REGISTERPART:
	case PSP_DD_REGISTERPARTSL:
	    return false;
	}
    }

    return true;
}

/**
 * @brief Handle PSP_DD_SENDSTOP message
 *
 * Handle the PSP_DD_SENDSTOP message @a msg.
 *
 * By sending such message the sending process requests the receiving
 * process to stop sending further messages to this
 * destination. Depending on a additional flag within @a msg the
 * sending process might expect an acknowledgment of the SENDSTOP to
 * take effect.
 *
 * This mechanism is used in order to implement a flow control on the
 * communication path between client processes or a client process and
 * a remote daemon process.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SENDSTOP(DDTypedMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    DDMsg_t ackmsg = { .type = PSP_DD_SENDSTOPACK,
		       .sender = msg->header.dest,
		       .dest = msg->header.sender,
		       .len = sizeof(ackmsg) };
    if (!task) return true;

    if (task->group == TG_LOGGER) {
	msg->header.type = PSP_CD_SENDSTOP;
	sendMsg(msg);
    } else if (task->fd != -1) {
	PSID_fdbg(PSID_LOG_FLWCNTRL, "client %s at %d temporarily disabled",
		  PSC_printTID(msg->header.dest), task->fd);
	PSID_dbg(PSID_LOG_FLWCNTRL, " by %s\n", PSC_printTID(msg->header.sender));

	if (!task->activeStops) Selector_disable(task->fd);
	task->activeStops++;
    }
    if (msg->header.len > sizeof(DDMsg_t) && msg->type) sendMsg(&ackmsg);
    return true;
}

/**
 * @brief Handle PSP_DD_SENDSTOPACK message
 *
 * Handle the PSP_DD_SENDSTOPACK message @a msg.
 *
 * This type of message is sent on request of the originator of the
 * corresponding SENDSTOP message. It acknowledges that the request to
 * stop sending further messages has taken effect.
 *
 * This mechanism is used in order to implement a flow control on the
 * communication path between client processes or a client process and
 * a remote daemon process.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SENDSTOPACK(DDMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->dest);
    if (!task) return true;

    PSID_fdbg(PSID_LOG_FLWCNTRL, "from %s", PSC_printTID(msg->sender));
    PSID_dbg(PSID_LOG_FLWCNTRL, " to %s\n", PSC_printTID(msg->dest));

    if (task->fd != -1) PSIDclient_releaseACK(task->fd);
    return true;
}

/**
 * @brief Handle PSP_DD_SENDCONT message
 *
 * Handle the PSP_DD_SENDCONT message @a msg.
 *
 * By sending such message the sending process requests the receiving
 * process to continue sending messages to this destination.
 *
 * This mechanism is used in order to implement a flow control on the
 * communication path between client processes or a client process and
 * a remote daemon process.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SENDCONT(DDMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->dest);
    if (!task) return true;

    if (task->group == TG_LOGGER) {
	msg->type = PSP_CD_SENDCONT;
	sendMsg(msg);
    } else if (task->fd != -1) {
	PSID_fdbg(PSID_LOG_FLWCNTRL, "client %s at %d re-enabled",
		  PSC_printTID(msg->dest), task->fd);
	PSID_dbg(PSID_LOG_FLWCNTRL, " by %s\n", PSC_printTID(msg->sender));

	task->activeStops--;
	if (!task->activeStops) Selector_enable(task->fd);
    }
    return true;
}

void PSIDFlwCntrl_clearMem(void)
{
    PSitems_clearMem(stopTIDs);
    stopTIDs = NULL;
}

void PSIDFlwCntrl_printStat(void)
{
    PSID_flog("Stops %d/%d (used/avail)\t%d/%d (gets/grows)\n",
	      PSitems_getUsed(stopTIDs), PSitems_getAvail(stopTIDs),
	      PSitems_getUtilization(stopTIDs), PSitems_getDynamics(stopTIDs));
}


void PSIDFlwCntrl_init(void)
{
    if (PSitems_isInitialized(stopTIDs)) return;
    stopTIDs = PSitems_new(sizeof(stopTID_t), "stopdTIDs");
    if (!stopTIDs) {
	PSID_flog("cannot get stopTIDs items\n");
	return;
    }

    PSID_registerMsg(PSP_DD_SENDSTOP, (handlerFunc_t) msg_SENDSTOP);
    PSID_registerMsg(PSP_DD_SENDCONT, (handlerFunc_t) msg_SENDCONT);
    PSID_registerMsg(PSP_DD_SENDSTOPACK, (handlerFunc_t) msg_SENDSTOPACK);

    PSID_registerLoopAct(PSIDFlwCntrl_gc);

    return;
}
