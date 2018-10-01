/*
 * ParaStation
 *
 * Copyright (C) 2017-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>

#include "pluginmalloc.h"
#include "slurmcommon.h"
#include "timer.h"
#include "selector.h"
#include "errno.h"

#include "psslurmcomm.h"
#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmconfig.h"

#include "psslurmmsg.h"

/* list which holds all buffered messages */
static LIST_HEAD(msgBufList);

const char *msgType2String(int type)
{
    switch(type) {
	case REQUEST_BATCH_JOB_LAUNCH:
	    return "REQUEST_BATCH_JOB_LAUNCH";
	case REQUEST_LAUNCH_TASKS:
	    return "REQUEST_LAUNCH_TASKS";
	case REQUEST_SIGNAL_TASKS:
	    return "REQUEST_SIGNAL_TASKS";
	case REQUEST_CHECKPOINT_TASKS:
	    return "REQUEST_CHECKPOINT_TASKS";
	case REQUEST_TERMINATE_TASKS:
	    return "REQUEST_TERMINATE_TASKS";
	case REQUEST_KILL_PREEMPTED:
	    return "REQUEST_KILL_PREEMPTED";
	case REQUEST_KILL_TIMELIMIT:
	    return "REQUEST_KILL_TIMELIMIT";
	case REQUEST_REATTACH_TASKS:
	    return "REQUEST_REATTACH_TASKS";
	case REQUEST_SIGNAL_JOB:
	    return "REQUEST_SIGNAL_JOB";
	case REQUEST_SUSPEND_INT:
	    return "REQUEST_SUSPEND_INT";
	case REQUEST_ABORT_JOB:
	    return "REQUEST_ABORT_JOB";
	case REQUEST_TERMINATE_JOB:
	    return "REQUEST_TERMINATE_JOB";
	case REQUEST_COMPLETE_BATCH_SCRIPT:
	    return "REQUEST_COMPLETE_BATCH_SCRIPT";
	case REQUEST_UPDATE_JOB_TIME:
	    return "REQUEST_UPDATE_JOB_TIME";
	case REQUEST_SHUTDOWN:
	    return "REQUEST_SHUTDOWN";
	case REQUEST_RECONFIGURE:
	    return "REQUEST_RECONFIGURE";
	case REQUEST_REBOOT_NODES:
	    return "REQUEST_REBOOT_NODES";
	case REQUEST_NODE_REGISTRATION_STATUS:
	    return "REQUEST_NODE_REGISTRATION_STATUS";
	case REQUEST_PING:
	    return "REQUEST_PING";
	case REQUEST_HEALTH_CHECK:
	    return "REQUEST_HEALTH_CHECK";
	case REQUEST_ACCT_GATHER_UPDATE:
	    return "REQUEST_ACCT_GATHER_UPDATE";
	case REQUEST_ACCT_GATHER_ENERGY:
	    return "REQUEST_ACCT_GATHER_ENERGY";
	case REQUEST_JOB_ID:
	    return "REQUEST_JOB_ID";
	case REQUEST_FILE_BCAST:
	    return "REQUEST_FILE_BCAST";
	case REQUEST_STEP_COMPLETE:
	    return "REQUEST_STEP_COMPLETE";
	case REQUEST_JOB_STEP_STAT:
	    return "REQUEST_JOB_STEP_STAT";
	case REQUEST_JOB_STEP_PIDS:
	    return "REQUEST_JOB_STEP_PIDS";
	case REQUEST_DAEMON_STATUS:
	    return "REQUEST_DAEMON_STATUS";
	case REQUEST_JOB_NOTIFY:
	    return "REQUEST_JOB_NOTIFY";
	case REQUEST_FORWARD_DATA:
	    return "REQUEST_FORWARD_DATA";
	case REQUEST_LAUNCH_PROLOG:
	    return "REQUEST_LAUNCH_PROLOG";
	case REQUEST_COMPLETE_PROLOG:
	    return "REQUEST_COMPLETE_PROLOG";
	case RESPONSE_PING_SLURMD:
	    return "RESPONSE_PING_SLURMD";
	case RESPONSE_SLURM_RC:
	    return "RESPONSE_SLURM_RC";
	case RESPONSE_ACCT_GATHER_UPDATE:
	    return "RESPONSE_ACCT_GATHER_UPDATE";
	case RESPONSE_ACCT_GATHER_ENERGY:
	    return "RESPONSE_ACCT_GATHER_ENERGY";
	case RESPONSE_JOB_ID:
	    return "RESPONSE_JOB_ID";
	case RESPONSE_JOB_STEP_STAT:
	    return "RESPONSE_JOB_STEP_STAT";
	case RESPONSE_JOB_STEP_PIDS:
	    return "RESPONSE_JOB_STEP_PIDS";
	case RESPONSE_LAUNCH_TASKS:
	    return "RESPONSE_LAUNCH_TASKS";
	case RESPONSE_FORWARD_FAILED:
	    return "RESPONSE_FORWARD_FAILED";
	case MESSAGE_TASK_EXIT:
	    return "MESSAGE_TASK_EXIT";
	case MESSAGE_NODE_REGISTRATION_STATUS:
	    return "MESSAGE_NODE_REGISTRATION_STATUS";
	case MESSAGE_EPILOG_COMPLETE:
	    return "MESSAGE_EPILOG_COMPLETE";
	case RESPONSE_REATTACH_TASKS:
	    return "RESPONSE_REATTACH_TASKS";
    }
    return "unknown";
}

void initSlurmMsg(Slurm_Msg_t *sMsg)
{
    memset(sMsg, 0, sizeof(Slurm_Msg_t));
    sMsg->sock = -1;
    sMsg->source = -1;
    initSlurmMsgHead(&sMsg->head);
}

void freeSlurmMsg(Slurm_Msg_t *sMsg)
{
    if (sMsg->source == -1) {
	/* local connection */
	if (sMsg->sock != -1) {
	    closeSlurmCon(sMsg->sock);
	    sMsg->sock = -1;
	}
    }

    initSlurmMsg(sMsg);
}

void dupSlurmMsgHead(Slurm_Msg_Header_t *dupHead, Slurm_Msg_Header_t *head)
{
    uint32_t i;

    if (!dupHead || !head) {
	mlog("%s: invalid dupHead or head\n", __func__);
	return;
    }

    memcpy(dupHead, head, sizeof(*dupHead));

    if (head->nodeList) {
	dupHead->nodeList = strdup(head->nodeList);
    }

    if (head->fwSize) {
	dupHead->fwdata =
	    umalloc(head->fwSize * sizeof(Slurm_Forward_Data_t));

	memcpy(dupHead->fwdata, head->fwdata,
	       head->fwSize * sizeof(Slurm_Forward_Data_t));

	for (i=0; i<head->fwSize; i++) {
	    if (head->fwdata[i].body.bufSize) {
		dupHead->fwdata[i].body.buf =
		    umalloc(head->fwdata[i].body.bufSize);
		memcpy(dupHead->fwdata[i].body.buf, head->fwdata[i].body.buf,
		       head->fwdata[i].body.bufUsed);
	    } else {
		memset(&dupHead->fwdata[i].body, 0,
		       sizeof(dupHead->fwdata[i].body));
	    }
	}
    }
}

Slurm_Msg_t * dupSlurmMsg(Slurm_Msg_t *sMsg)
{
    Slurm_Msg_t *dupMsg = umalloc(sizeof(*dupMsg));

    initSlurmMsg(dupMsg);
    dupMsg->sock = sMsg->sock;
    dupMsg->data = dupDataBuffer(sMsg->data);
    dupMsg->ptr = dupMsg->data->buf + (sMsg->ptr - sMsg->data->buf);
    dupMsg->recvTime = sMsg->recvTime;

    dupSlurmMsgHead(&dupMsg->head, &sMsg->head);

    return dupMsg;
}

void releaseSlurmMsg(Slurm_Msg_t *sMsg)
{
    if (!sMsg) return;

    if (sMsg->data) {
	if (sMsg->data->buf) ufree(sMsg->data->buf);
	ufree(sMsg->data);
    }
    if (sMsg->head.nodeList) ufree(sMsg->head.nodeList);

    ufree(sMsg);
}

void initSlurmMsgHead(Slurm_Msg_Header_t *head)
{
    memset(head, 0, sizeof(Slurm_Msg_Header_t));
    head->version = slurmProto;
    head->flags |= SLURM_GLOBAL_AUTH_KEY;
    head->treeWidth = 1;
}

void freeSlurmMsgHead(Slurm_Msg_Header_t *head)
{
    uint32_t i;

    if (head->fwdata) {
	for (i=0; i<head->fwSize; i++) {
	    ufree(head->fwdata[i].body.buf);
	}
    }

    ufree(head->nodeList);
    ufree(head->fwdata);
}

void clearMsgBuf(void)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &msgBufList) {
	Slurm_Msg_Buf_t *msgBuf = list_entry(pos, Slurm_Msg_Buf_t, list);
	deleteMsgBuf(msgBuf);
    }
}

void deleteMsgBuf(Slurm_Msg_Buf_t *msgBuf)
{
    if (!msgBuf) return;

    /* remove a registered socket */
    if (msgBuf->sock != -1 && Selector_isRegistered(msgBuf->sock)) {
	Selector_vacateWrite(msgBuf->sock);
    }

    /* remove a registered timer */
    if (msgBuf->timerID != -1) {
	Timer_remove(msgBuf->timerID);
    }

    freeSlurmMsgHead(&msgBuf->head);
    freeSlurmAuth(msgBuf->auth);
    ufree(msgBuf->body.buf);
    list_del(&msgBuf->list);
    ufree(msgBuf);
}

Slurm_Msg_Buf_t *saveSlurmMsg(Slurm_Msg_Header_t *head, PS_SendDB_t *body,
			      Slurm_Auth_t *auth, int sock, size_t written)
{
    Slurm_Msg_Buf_t *msgBuf;

    mdbg(PSSLURM_LOG_COMM, "%s: save msg type %s written %zu\n",
	 __func__, msgType2String(head->type), written);

    msgBuf = umalloc(sizeof(*msgBuf));
    msgBuf->sock = sock;
    msgBuf->offset = written;
    msgBuf->timerID = -1;
    msgBuf->sendRetry = 0;
    msgBuf->conRetry = 0;
    msgBuf->maxConRetry = getConfValueI(&Config, "RECONNECT_MAX_RETRIES");
    msgBuf->authTime = (auth) ? time(NULL) : 0;

    /* dup msg head */
    dupSlurmMsgHead(&msgBuf->head, head);

    /* save data buffer */
    msgBuf->body.buf = NULL;
    memToDataBuffer(body->buf, body->bufUsed, &msgBuf->body);

    /* dup auth */
    msgBuf->auth = (auth) ? dupSlurmAuth(auth) : NULL;

    /* save to list */
    list_add_tail(&msgBuf->list, &msgBufList);

    return msgBuf;
}

bool needMsgResend(uint16_t type)
{
    switch(type) {
	case REQUEST_STEP_COMPLETE:
	case REQUEST_COMPLETE_BATCH_SCRIPT:
	case MESSAGE_TASK_EXIT:
	case RESPONSE_LAUNCH_TASKS:
	case RESPONSE_REATTACH_TASKS:
	    return true;
	case RESPONSE_PING_SLURMD:
	case MESSAGE_NODE_REGISTRATION_STATUS:
	case MESSAGE_EPILOG_COMPLETE: /* slurmctld will resend terminate req */
	case RESPONSE_ACCT_GATHER_UPDATE:
	case RESPONSE_ACCT_GATHER_ENERGY:
	case RESPONSE_JOB_ID:
	case RESPONSE_JOB_STEP_STAT:
	case RESPONSE_JOB_STEP_PIDS:
	case RESPONSE_SLURM_RC:
	    return false;
    }

    mlog("%s: warning: undefined msg type %s (%u)\n",
	 __func__, msgType2String(type), type);

    return false;
}

int resendSlurmMsg(int sock, void *msg)
{
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    Slurm_Msg_Buf_t *savedMsg = msg;
    int ret;
    size_t written;

    if (!savedMsg->auth) {
	savedMsg->auth = getSlurmAuth();
	if (!savedMsg->auth) {
	    flog("getting a slurm authentication token failed\n");
	    goto CLEANUP;
	}
	savedMsg->authTime = time(NULL);
    }

    packSlurmMsg(&data, &savedMsg->head, &savedMsg->body, savedMsg->auth);

    ret = sendDataBuffer(sock, &data, savedMsg->offset, &written);
    int eno = errno;

    savedMsg->sendRetry++;
    savedMsg->offset += written;

    mdbg(PSSLURM_LOG_COMM, "%s: type %s ret %i retry %u written %zu\n",
	 __func__, msgType2String(savedMsg->head.type), ret,
	 savedMsg->sendRetry, written);

    if (ret == -1) {
	/* default authTime is 300 (= default TTL of munge cred) */
	if (time(NULL) - savedMsg->authTime >
	    getConfValueI(&Config, "RESEND_TIMEOUT")) {
	    mlog("%s: resend timeout reached, dropping message %s\n", __func__,
		 msgType2String(savedMsg->head.type));
	    goto CLEANUP;
	}

	if (!written) {
	    if (eno == EAGAIN || eno == EINTR) return 0;
	    mwarn(eno, "%s: error writing message %s: ",
		 __func__, msgType2String(savedMsg->head.type));
	    goto CLEANUP;
	}
	return 0;
    } else {
	/* all data has been written */
	mdbg(PSSLURM_LOG_COMM, "%s: success sending %s\n", __func__,
	     msgType2String(savedMsg->head.type));
    }

CLEANUP:
    deleteMsgBuf(savedMsg);

    return 0;
}

static void handleReconTimeout(int timerId, void *data)
{
    Slurm_Msg_Buf_t *savedMsg = data;

    if (savedMsg->sock == -1) {
	/* try to connect to slurmctld */
	savedMsg->sock = openSlurmctldCon();

	if (savedMsg->sock < 0) {
	    mlog("%s: connection attempt %u of %u to slurmctld failed\n",
		 __func__, savedMsg->conRetry, savedMsg->maxConRetry);

	    if (savedMsg->maxConRetry == -1) {
		savedMsg->conRetry++;
		return;
	    }

	    if (++savedMsg->conRetry >= savedMsg->maxConRetry) {
		mlog("%s: maximal %i reconnect attempts reached, dropping msg "
		     "'%s'\n", __func__, savedMsg->conRetry,
		     msgType2String(savedMsg->head.type));

		/* drop the saved msg */
		deleteMsgBuf(savedMsg);
	    }
	    return;
	}
    }

    /* non blocking write */
    setFDblock(savedMsg->sock, false);

    /* remove the timer */
    Timer_remove(timerId);
    savedMsg->timerID = -1;

    /* let the resend function handle it */
    Selector_awaitWrite(savedMsg->sock, resendSlurmMsg, savedMsg);
}

int setReconTimer(Slurm_Msg_Buf_t *savedMsg)
{
    struct timeval timeout = {0, 0};

    timeout.tv_sec = getConfValueI(&Config, "RECONNECT_TIME");

    savedMsg->timerID = Timer_registerEnhanced(&timeout,
					       handleReconTimeout, savedMsg);

    if (savedMsg->timerID == -1) {
	mlog("%s: setting resend timer failed\n", __func__);
    }

    return savedMsg->timerID;
}
