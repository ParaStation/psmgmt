/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmmsg.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include "pscio.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"
#include "selector.h"
#include "timer.h"

#include "slurmcommon.h"
#include "slurmmsg.h"
#include "psslurmcomm.h"
#include "psslurmconfig.h"
#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmproto.h"
#include "psslurmprototypes.h"

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
	case REQUEST_TERMINATE_TASKS:
	    return "REQUEST_TERMINATE_TASKS";
	case REQUEST_KILL_PREEMPTED:
	    return "REQUEST_KILL_PREEMPTED";
	case REQUEST_KILL_TIMELIMIT:
	    return "REQUEST_KILL_TIMELIMIT";
	case REQUEST_REATTACH_TASKS:
	    return "REQUEST_REATTACH_TASKS";
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
	case REQUEST_STEP_COMPLETE_AGGR:
	    return "REQUEST_STEP_COMPLETE_AGGR";
	case REQUEST_NETWORK_CALLERID:
	    return "REQUEST_NETWORK_CALLERID";
	case MESSAGE_COMPOSITE:
	    return "MESSAGE_COMPOSITE";
	case RESPONSE_MESSAGE_COMPOSITE:
	    return "RESPONSE_MESSAGE_COMPOSITE";
	case RESPONSE_NODE_REGISTRATION:
	    return "RESPONSE_NODE_REGISTRATION";
	case REQUEST_CONFIG:
	    return "REQUEST_CONFIG";
	case RESPONSE_CONFIG:
	    return "RESPONSE_CONFIG";
	case REQUEST_JOB_STEP_CREATE:
	    return "REQUEST_JOB_STEP_CREATE";
	case RESPONSE_JOB_STEP_CREATE:
	    return "RESPONSE_JOB_STEP_CREATE";
	case SRUN_JOB_COMPLETE:
	    return "SRUN_JOB_COMPLETE";
	case REQUEST_CANCEL_JOB_STEP:
	    return "REQUEST_CANCEL_JOB_STEP";
	case SRUN_NODE_FAIL:
	    return "SRUN_NODE_FAIL";
	case SRUN_TIMEOUT:
	    return "SRUN_TIMEOUT";
	case REQUEST_UPDATE_JOB_STEP:
	    return "REQUEST_UPDATE_JOB_STEP";
	case REQUEST_STEP_LAYOUT:
	    return "REQUEST_STEP_LAYOUT";
	case REQUEST_JOB_SBCAST_CRED:
	    return "REQUEST_JOB_SBCAST_CRED";
	case REQUEST_HET_JOB_ALLOC_INFO:
	    return "REQUEST_HET_JOB_ALLOC_INFO";
    }

    static char buf[64];
    snprintf(buf, sizeof(buf), "<unkown %d>", type);

    return buf;
}

const char *strRemoteAddr(Slurm_Msg_t *sMsg)
{
    static char addr[64];
    snprintf(addr, sizeof(addr), "%u.%u.%u.%u:%u",
	     (sMsg->head.addr.ip & 0x000000ff),
	     (sMsg->head.addr.ip & 0x0000ff00) >> 8,
	     (sMsg->head.addr.ip & 0x00ff0000) >> 16,
	     (sMsg->head.addr.ip & 0xff000000) >> 24,
	     sMsg->head.addr.port);
    return addr;
}

void initSlurmMsg(Slurm_Msg_t *sMsg)
{
    memset(sMsg, 0, sizeof(*sMsg));
    sMsg->sock = -1;
    sMsg->source = -1;
    initSlurmMsgHead(&sMsg->head);
}

void clearSlurmMsg(Slurm_Msg_t *sMsg)
{
    /* close connection if local */
    if (sMsg->source == -1 && sMsg->sock != -1) closeSlurmCon(sMsg->sock);

    if (sMsg->unpData) freeUnpackMsgData(sMsg);
    freeSlurmMsgHead(&sMsg->head);

    initSlurmMsg(sMsg);
}

void dupSlurmMsgHead(Slurm_Msg_Header_t *dupHead, Slurm_Msg_Header_t *head)
{
    if (!dupHead || !head) {
	flog("invalid dupHead or head\n");
	return;
    }

    memcpy(dupHead, head, sizeof(*dupHead));

    if (head->fwNodeList) dupHead->fwNodeList = ustrdup(head->fwNodeList);

    if (head->fwResSize) {
	dupHead->fwRes = umalloc(head->fwResSize * sizeof(Slurm_Forward_Res_t));

	memcpy(dupHead->fwRes, head->fwRes,
	       head->fwResSize * sizeof(Slurm_Forward_Res_t));

	for (uint16_t i = 0; i < head->fwResSize; i++) {
	    dupHead->fwRes[i].body = PSdbDup(head->fwRes[i].body);
	}
    }
}

Slurm_Msg_t * dupSlurmMsg(Slurm_Msg_t *sMsg)
{
    Slurm_Msg_t *dupMsg = umalloc(sizeof(*dupMsg));

    memcpy(dupMsg, sMsg, sizeof(*dupMsg));

    dupMsg->data = PSdbDup(sMsg->data);
    dupSlurmMsgHead(&dupMsg->head, &sMsg->head);

    return dupMsg;
}

void releaseSlurmMsg(Slurm_Msg_t *sMsg)
{
    if (!sMsg) return;

    PSdbDestroy(sMsg->data);
    freeSlurmMsgHead(&sMsg->head);

    ufree(sMsg);
}

void initSlurmMsgHead(Slurm_Msg_Header_t *head)
{
    memset(head, 0, sizeof(*head));
    head->version = slurmProto;
    head->flags |= SLURM_GLOBAL_AUTH_KEY;
    head->fwTreeWidth = 1;
    head->addr.family = AF_INET;
}

void freeSlurmMsgHead(Slurm_Msg_Header_t *head)
{
    if (head->fwRes) {
	for (uint16_t i = 0; i < head->fwResSize; i++) {
	    PSdbDestroy(head->fwRes[i].body);
	}
    }

    ufree(head->fwNodeList);
    ufree(head->fwAliasNetCred);
    ufree(head->fwRes);
}

void clearMsgBuf(void)
{
    list_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &msgBufList) {
	Slurm_Msg_Buf_t *msgBuf = list_entry(pos, Slurm_Msg_Buf_t, next);
	deleteMsgBuf(msgBuf, false);
    }
}

void deleteMsgBuf(Slurm_Msg_Buf_t *msgBuf, bool success)
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

    /* cleanup connection if no answer is expected or message got lost */
    if (msgBuf->sock != -1) closeSlurmConEx(msgBuf->sock, success);

    freeSlurmMsgHead(&msgBuf->head);
    freeSlurmAuth(msgBuf->auth);
    PSdbDestroy(msgBuf->body);
    list_del(&msgBuf->next);
    ufree(msgBuf);
}

Slurm_Msg_Buf_t *saveSlurmMsg(Slurm_Msg_Header_t *head, PS_SendDB_t *body,
			      Req_Info_t *req, Slurm_Auth_t *auth,
			      int sock, size_t written)
{
    mdbg(PSSLURM_LOG_COMM, "%s: save msg type %s written %zu\n",
	 __func__, msgType2String(head->type), written);

    Slurm_Msg_Buf_t *msgBuf = umalloc(sizeof(*msgBuf));
    msgBuf->sock = sock;
    msgBuf->offset = written;
    msgBuf->timerID = -1;
    msgBuf->sendRetry = 0;
    msgBuf->conRetry = 0;
    msgBuf->maxConRetry = getConfValueI(Config, "RECONNECT_MAX_RETRIES");
    msgBuf->authTime = (auth) ? time(NULL) : 0;
    msgBuf->auth = (auth) ? dupSlurmAuth(auth) : NULL;
    msgBuf->req = req;

    /* dup msg head */
    dupSlurmMsgHead(&msgBuf->head, head);

    if (msgBuf->sock < 0) {
	/* resending opens a new connection and needs authentication */
	msgBuf->head.flags &= ~SLURM_NO_AUTH_CRED;
    }

    /* save data buffer */
    msgBuf->body = PSdbNew(NULL, 0);
    memToDataBuffer(body->buf, body->bufUsed, msgBuf->body);

    list_add_tail(&msgBuf->next, &msgBufList);

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

    flog("warning: undefined msg type %s (%u)\n", msgType2String(type), type);

    return false;
}

int resendSlurmMsg(int sock, void *msg)
{
    PS_SendDB_t data = { .bufUsed = 0, .useFrag = false };
    Slurm_Msg_Buf_t *savMsg = msg;
    bool success = false;

    if (!savMsg->auth) {
	savMsg->auth = getSlurmAuth(&savMsg->head, PSdbGetBuf(savMsg->body),
				    PSdbGetUsed(savMsg->body));
	if (!savMsg->auth) {
	    flog("getting a slurm authentication token failed\n");
	    goto CLEANUP;
	}
	savMsg->authTime = time(NULL);
    }

    packSlurmMsg(&data, &savMsg->head, savMsg->body, savMsg->auth);

    size_t written;
    int ret = sendDataBuffer(sock, &data, savMsg->offset, &written);
    int eno = errno;

    savMsg->sendRetry++;
    savMsg->offset += written;

    fdbg(PSSLURM_LOG_COMM | PSSLURM_LOG_PROTO,
	 "type %s ret %i retry %u written %zu\n",
	 msgType2String(savMsg->head.type), ret, savMsg->sendRetry, written);

    if (ret == -1) {
	/* default authTime is 300 (= default TTL of munge cred) */
	if (time(NULL) - savMsg->authTime
	    > getConfValueI(Config, "RESEND_TIMEOUT")) {
	    flog("timeout, drop %s\n", msgType2String(savMsg->head.type));
	    goto CLEANUP;
	}

	if (!written) {
	    if (eno == EAGAIN || eno == EINTR) return 0;
	    fwarn(eno, "error on %s", msgType2String(savMsg->head.type));
	    goto CLEANUP;
	}
	return 0;
    } else {
	/* all data has been written */
	success = true;
	flog("success on %s\n", msgType2String(savMsg->head.type));
    }

CLEANUP:
    deleteMsgBuf(savMsg, success);

    return 0;
}

static void handleReconTimeout(int timerId, void *data)
{
    Slurm_Msg_Buf_t *savedMsg = data;

    if (savedMsg->sock == -1) {
	/* try to connect to slurmctld */
	savedMsg->sock = openSlurmctldCon(savedMsg->req);

	if (savedMsg->sock < 0) {
	    flog("connection attempt %u of %u to slurmctld to re-send msg %s"
		 " failed\n", savedMsg->conRetry +1, savedMsg->maxConRetry,
		 msgType2String(savedMsg->head.type));

	    if (savedMsg->maxConRetry == -1) {
		savedMsg->conRetry++;
		return;
	    }

	    if (++savedMsg->conRetry >= savedMsg->maxConRetry) {
		flog("max reconnect attempts (%i) reached, dropping msg %s\n",
		     savedMsg->conRetry, msgType2String(savedMsg->head.type));

		/* drop the saved msg and free request only here if
		 * the connection could not be opened; otherwise req
		 * will become part of the connection */
		ufree(savedMsg->req);
		deleteMsgBuf(savedMsg, false);
	    }
	    return;
	}
    }

    /* non blocking write */
    PSCio_setFDblock(savedMsg->sock, false);

    /* remove the timer */
    Timer_remove(timerId);
    savedMsg->timerID = -1;

    /* let the resend function handle it */
    Selector_awaitWrite(savedMsg->sock, resendSlurmMsg, savedMsg);
}

int setReconTimer(Slurm_Msg_Buf_t *savedMsg)
{
    struct timeval timeout = {0, 0};

    timeout.tv_sec = getConfValueI(Config, "RECONNECT_TIME");

    savedMsg->timerID = Timer_registerEnhanced(&timeout,
					       handleReconTimeout, savedMsg);

    if (savedMsg->timerID == -1) flog("setting resend timer failed\n");

    return savedMsg->timerID;
}
