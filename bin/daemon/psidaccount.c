/*
 *               ParaStation
 *
 * Copyright (C) 2006-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
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

#include "psidaccount.h"

/** List to store all known accounter tasks */
typedef struct {
    struct list_head next;
    PStask_ID_t acct;
} PSID_acct_t;

/** List of all known accounter tasks */
static LIST_HEAD(PSID_accounters);

void PSID_addAcct(PStask_ID_t acctr)
{
    PSID_acct_t *acct;
    struct list_head *pos;

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
};

void PSID_remAcct(PStask_ID_t acctr)
{
    struct list_head *pos, *tmp;

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
    struct list_head *pos, *tmp;

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
    struct list_head *pos, *tmp;

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

/**
 * @brief Handle PSP_CD_ACCOUNT message.
 *
 * Handle the message @a msg of type PSP_CD_ACCOUNT. If the message is
 * destined to the local daemon it will be forwarded to all registered
 * accounter tasks. I.e. the message will be multiplexed if more than
 * one accounter is registered through the corresponding @ref
 * PSID_addAcct() calls.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_ACCOUNT(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_VERB, "%s: from %s\n", __func__,
	     PSC_printTID(msg->header.sender));

    if (msg->header.dest == PSC_getMyTID()) {
	/* message for me, let's forward to all known accounters */
	struct list_head *pos, *tmp;
	list_for_each_safe(pos, tmp, &PSID_accounters) {
	    PSID_acct_t *acct = list_entry(pos, PSID_acct_t, next);
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
    struct list_head *pos;
    int num = 0;

    list_for_each(pos, &PSID_accounters) num++;

    return num;
}

void initAccount(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_ACCOUNT, msg_ACCOUNT);
}
