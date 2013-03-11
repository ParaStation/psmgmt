/*
 * ParaStation
 *
 * Copyright (C) 2006-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <errno.h>

#include "list.h"

#include "pscommon.h"

#include "psidcomm.h"
#include "psidutil.h"
#include "psidoption.h"
#include "psidtask.h"
#include "psidnodes.h"

#include "psidaccount.h"

/** List to store all known accounter tasks */
typedef struct {
    list_t next;
    PStask_ID_t acct;
} PSID_acct_t;

/** List of all known accounter tasks */
static LIST_HEAD(PSID_accounters);

void PSID_addAcct(PStask_ID_t acctr)
{
    PSID_acct_t *acct;
    list_t *pos;

    PSID_log(PSID_LOG_VERB, "%s: %s\n", __func__, PSC_printTID(acctr));

    list_for_each(pos, &PSID_accounters) {
	PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
	if (acct->acct == acctr) {
	    PSID_log(PSID_LOG_VERB, "%s: %s found\n", __func__,
		     PSC_printTID(acctr));
	    return;
	}
    }

    acct = malloc(sizeof(*acct));
    acct->acct = acctr;
    list_add_tail(&acct->next, &PSID_accounters);
}

void PSID_remAcct(PStask_ID_t acctr)
{
    list_t *pos, *tmp;

    PSID_log(PSID_LOG_VERB, "%s: %s\n", __func__, PSC_printTID(acctr));

    list_for_each_safe(pos, tmp, &PSID_accounters) {
	PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
	if (acct->acct == acctr) {
	    list_del(pos);
	    free(acct);
	    return;
	}
    }
    PSID_log(-1, "%s: %s not found\n", __func__, PSC_printTID(acctr));
}

void PSID_cleanAcctFromNode(PSnodes_ID_t node)
{
    list_t *pos, *tmp;

    PSID_log(PSID_LOG_VERB, "%s: %d\n", __func__, node);

    list_for_each_safe(pos, tmp, &PSID_accounters) {
	PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
	if (PSC_getID(acct->acct) == node) {
	    PStask_ID_t tid = acct->acct;
	    list_del(pos);
	    free(acct);
	    PSID_log(-1, "%s: %s removed\n", __func__, PSC_printTID(tid));
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

    PSID_log(PSID_LOG_VERB, "%s: %s %d\n", __func__, PSC_printTID(dest), all);

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

void PSID_msgACCOUNT(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_VERB, "%s: from %s\n", __func__,
	     PSC_printTID(msg->header.sender));

    if (msg->header.dest == PSC_getMyTID()) {
	DDTypedBufferMsg_t *tmsg = (DDTypedBufferMsg_t *) msg;
	list_t *pos, *tmp;
	PStask_t *task;
	char *ptr = tmsg->buf;
	uid_t uid;
	static int lastStartUID = -1;

	/* skip logger TID and rank to get to UID */
	ptr += sizeof(PStask_ID_t);
	ptr += sizeof(int32_t);
	uid = *(uid_t *)ptr;

	/* save uid for next incoming slot msg */
	if (tmsg->type == PSP_ACCOUNT_START) {
	    lastStartUID = uid;
	}

	/* no uid in msg, used saved one */
	if (tmsg->type == PSP_ACCOUNT_SLOTS) {
	    uid = lastStartUID;
	}

	/* message for me, let's forward to all authorized accounters */

	list_for_each_safe(pos, tmp, &PSID_accounters) {
	    PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);

	    if (!(task = PStasklist_find(&managedTasks, acct->acct))) {
		PSID_log(PSID_LOG_VERB, "%s: task for accounter '%s' not"
			    " found\n", __func__, PSC_printTID(acct->acct));
		continue;
	    }

	    /* Let accounter running as adminuser receive all accounting
	     * messages. Unprivileged users may only see their own
	     * accounting data.
	     */
	    if (task->uid != 0 && task->uid != uid &&
		!PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
			       (PSIDnodes_guid_t){.u=task->uid}) &&
		!PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
			       (PSIDnodes_guid_t){.g=task->gid})) {
		continue;
	    }

	    msg->header.dest = acct->acct;
	    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
		PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    }
	}
	/* Restore original dest to allow re-use of msg */
	msg->header.dest = PSC_getMyTID();
    } else if (PSC_getPID(msg->header.dest)) {
	/* forward to accounter */
	if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	}
    }
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
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_ACCOUNT, PSID_msgACCOUNT);
}
