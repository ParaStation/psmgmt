/*
 * ParaStation
 *
 * Copyright (C) 2006-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidaccount.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

#include "list.h"
#include "pscommon.h"
#include "psprotocol.h"

#include "psidcomm.h"
#include "psidutil.h"
#include "psidtask.h"
#include "psidnodes.h"

/** List to store all known accounter tasks */
typedef struct {
    list_t next;
    PStask_ID_t acct;
} PSID_acct_t;

/** List of all known accounter tasks */
static LIST_HEAD(PSID_accounters);

void PSID_addAcct(PStask_ID_t acctr)
{
    list_t *pos;

    PSID_fdbg(PSID_LOG_VERB, "%s\n", PSC_printTID(acctr));

    list_for_each(pos, &PSID_accounters) {
	PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
	if (acct->acct == acctr) {
	    PSID_fdbg(PSID_LOG_VERB, "%s found\n", PSC_printTID(acctr));
	    return;
	}
    }

    PSID_acct_t *acct = malloc(sizeof(*acct));
    acct->acct = acctr;
    list_add_tail(&acct->next, &PSID_accounters);
}

void PSID_remAcct(PStask_ID_t acctr)
{
    list_t *pos, *tmp;

    PSID_fdbg(PSID_LOG_VERB, "%s\n", PSC_printTID(acctr));

    list_for_each_safe(pos, tmp, &PSID_accounters) {
	PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
	if (acct->acct == acctr) {
	    list_del(pos);
	    free(acct);
	    return;
	}
    }
    PSID_flog("%s not found\n", PSC_printTID(acctr));
}

void PSID_cleanAcctFromNode(PSnodes_ID_t node)
{
    list_t *pos, *tmp;

    PSID_fdbg(PSID_LOG_VERB, "%d\n", node);

    list_for_each_safe(pos, tmp, &PSID_accounters) {
	PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
	if (PSC_getID(acct->acct) == node) {
	    PStask_ID_t tid = acct->acct;
	    list_del(pos);
	    free(acct);
	    PSID_flog("%s removed\n", PSC_printTID(tid));
	}
    }
}

void send_acct_OPTIONS(PStask_ID_t dest, int all)
{
    DDOptionMsg_t msg = {
	.header = {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg) },
	.count = 0,
	.opt = {{ .option = 0, .value = 0 }} };
    PSP_Option_t option = all ? PSP_OP_ACCT : PSP_OP_ADD_ACCT;
    list_t *pos, *tmp;

    PSID_fdbg(PSID_LOG_VERB, "%s %d\n", PSC_printTID(dest), all);

    list_for_each_safe(pos, tmp, &PSID_accounters) {
	PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
	if (all || PSC_getID(acct->acct) == PSC_getMyID()) {
	    msg.opt[(int) msg.count].option = option;
	    msg.opt[(int) msg.count].value = acct->acct;
	    msg.count++;
	}
	if (msg.count == DDOptionMsgMax) {
	    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
		PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    }
	    msg.count = 0;
	}
    }

    msg.opt[(int) msg.count].option = PSP_OP_LISTEND;
    msg.opt[(int) msg.count].value = 0;
    msg.count++;
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }
}

/**
 * @brief Forward PSP_CD_ACCOUNT message locally.
 *
 * Forward the message @a msg of type PSP_CD_ACCOUNT to a local
 * accounter. Beforehand permissions of the accounter to receive this
 * specific accounting message will be double-checked. I.e. only
 * accounters owned by root, an admin user or a member of the admin
 * group are allowed receive all accounting message. All other users
 * will only receive messages concerning their own jobs.
 *
 * @param msg Pointer to the message to forward.
 *
 * @return No return value.
 */
static void localForward_ACCOUNT(DDTypedBufferMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->header.dest);
    char *ptr = msg->buf;
    uid_t uid;

    /* skip logger TID */
    ptr += sizeof(PStask_ID_t);

    if (msg->type != PSP_ACCOUNT_SLOTS) {
	/* skip rank to get to UID */
	ptr += sizeof(int32_t);
    }
    uid = *(uid_t *)ptr;

    if (!task) {
	PSID_flog("%s not found\n", PSC_printTID(msg->header.dest));
	return;
    }

    if (task->group != TG_ACCOUNT) return; /* Original task might be obsolete */

    /* Unprivileged users may only see their own accounting data. */
    if (task->uid != 0 && task->uid != uid &&
	!PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
			    (PSIDnodes_guid_t){.u=task->uid}) &&
	!PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
			    (PSIDnodes_guid_t){.g=task->gid})) {
	return;
    }

    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }
}

/**
 * @brief Handle PSP_CD_ACCOUNT message
 *
 * Handle the message @a msg of type PSP_CD_ACCOUNT. If the message is
 * destined to the local daemon, it will be forwarded to all registered
 * accounter tasks. I.e. the message will be multiplexed if more than
 * one accounter is registered through the corresponding @ref
 * PSID_addAcct() calls.
 *
 * Delivery of messages to local accounters will be filtered by @ref
 * localForward_ACCOUNT().
 *
 * @param msg Pointer to the message to handle
 *
 * @return Always return true
 */
static bool msg_ACCOUNT(DDTypedBufferMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_VERB, "from %s\n", PSC_printTID(msg->header.sender));

    if (msg->header.dest == PSC_getMyTID()) {
	/* message for me, let's forward to all authorized accounters */
	list_t *pos, *tmp;

	list_for_each_safe(pos, tmp, &PSID_accounters) {
	    PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
	    msg->header.dest = acct->acct;

	    if (PSC_getID(acct->acct) == PSC_getMyID()) {
		/* local accounter */
		localForward_ACCOUNT(msg);
	    } else {
		if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
		    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
		}
	    }
	}
	/* Restore original dest to allow re-use of msg */
	msg->header.dest = PSC_getMyTID();
    } else if (PSC_getPID(msg->header.dest)) {
	/* forward to accounter */
	localForward_ACCOUNT(msg);
    }
    return true;
}

int PSID_getNumAcct(void)
{
    list_t *pos;
    int num = 0;

    list_for_each(pos, &PSID_accounters) num++;

    return num;
}

void initAccount(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    PSID_registerMsg(PSP_CD_ACCOUNT, (handlerFunc_t) msg_ACCOUNT);
}
