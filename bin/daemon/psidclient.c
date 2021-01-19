/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#define __USE_GNU
#include <sys/socket.h>
#undef __USE_GNU

#include "selector.h"
#include "rdp.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "pstask.h"

#include "psidutil.h"
#include "psidtask.h"
#include "psidtimer.h"
#include "psidmsgbuf.h"
#include "psidcomm.h"
#include "psidaccount.h"
#include "psidnodes.h"
#include "psidstatus.h"
#include "psidsignal.h"
#include "psidstate.h"
#include "psidflowcontrol.h"

#include "psidclient.h"

/* possible values of clients.flags */
#define INITIALCONTACT  0x00000001   /* No message yet (only accept()ed) */
#define FLUSH           0x00000002   /* Flush is under way */
#define CLOSE           0x00000004   /* About to close the connection */

/** Information we have on current clients */
typedef struct {
    PStask_ID_t tid;     /**< Clients task ID */
    PStask_t *task;      /**< Clients task structure */
    unsigned int flags;  /**< Special flags (INITIALCONTACT, FLUSH, CLOSE) */
    int pendingACKs;     /**< SENDSTOPACK messages to wait for */
    list_t msgs;         /**< Chain of undelivered messages */
    PSIDFlwCntrl_hash_t stops;  /**< Hash-table to track SENDSTOPs */
} client_t;

/** Array (indexed by file-descriptor number) to store info on clients */
static client_t *clients = NULL;

/** Maximum number of clients the module currently can take care of */
static int maxClientFD = 0;

static void msg_CLIENTCONNECT(int fd, DDBufferMsg_t *bufmsg);

static int handleClientConnectMsg(int fd, void *info)
{
    DDBufferMsg_t msg;

    int msglen;

    PSID_log(PSID_LOG_COMM, "%s(%d)\n", __func__, fd);

    /* read the whole msg */
    msglen = recvMsg(fd, (DDMsg_t*)&msg, sizeof(msg));

    if (msglen==0) {
	/* closing connection */
	PSID_log(PSID_LOG_CLIENT, "%s(%d): close connection\n", __func__, fd);
	PSIDclient_delete(fd);
    } else if (msglen==-1) {
	if (errno != EAGAIN) {
	    int eno = errno;
	    PSID_warn(-1, eno, "%s(%d): recvMsg()", __func__, fd);
	    if (eno == EBADF) PSIDclient_delete(fd);
	}
    } else {
	if (msg.header.type != PSP_CD_CLIENTCONNECT) {
	    PSID_log(-1, "%s: Unexpected message of type %s\n", __func__,
		     PSP_printMsg(msg.header.type));
	    return 0;
	}
	msg_CLIENTCONNECT(fd, &msg);
    }

    return 0;
}

void PSIDclient_register(int fd, PStask_ID_t tid, PStask_t *task)
{
    clients[fd].tid = tid;
    clients[fd].task = task;
    clients[fd].flags |= INITIALCONTACT;
    clients[fd].pendingACKs = 0;
    INIT_LIST_HEAD(&clients[fd].msgs);
    PSIDFlwCntrl_emptyHash(clients[fd].stops);

    Selector_register(fd, handleClientConnectMsg, NULL);
}

PStask_ID_t PSIDclient_getTID(int fd)
{
    if (fd < 0 || fd >= maxClientFD) {
	PSID_log(-1, "%s(%d): file descriptor out of range\n", __func__, fd);
	return PSC_getMyTID();
    }

    return clients[fd].tid;
}

PStask_t *PSIDclient_getTask(int fd)
{
    if (fd < 0 || fd >= maxClientFD) {
	PSID_log(-1, "%s(%d): file descriptor out of range\n", __func__, fd);
	return NULL;
    }

    return clients[fd].task;
}

static int handleClientMsg(int fd, void *info)
{
    DDBufferMsg_t msg;

    int msglen;

    PSID_log(PSID_LOG_COMM, "%s(%d)\n", __func__, fd);

    /* read the whole msg */
    msglen = recvMsg(fd, (DDMsg_t*)&msg, sizeof(msg));

    if (msglen==0) {
	/* closing connection */
	PSID_log(PSID_LOG_CLIENT, "%s(%d): close connection\n", __func__, fd);
	PSIDclient_delete(fd);
    } else if (msglen==-1) {
	if (errno != EAGAIN) {
	    int eno = errno;
	    PSID_warn(-1, eno, "%s(%d): recvMsg()", __func__, fd);
	    if (eno == EBADF) PSIDclient_delete(fd);
	}
    } else {
	PStask_ID_t tid = PSIDclient_getTID(fd);
	if (msg.header.sender != tid) {
	    PSID_log(-1, "%s: Got msg from %s on socket %d", __func__,
		     PSC_printTID(msg.header.sender), fd);
	    PSID_log(-1, " assigned to %s\n", PSC_printTID(tid));
	    /* drop message silently */
	    return 0;
	}
	PStask_t *task = PSIDclient_getTask(fd);
	if (task->obsolete) {
	    /* Drop all messages besides PSP_DD_CHILDDEAD */
	    if (msg.header.type != PSP_DD_CHILDDEAD) return 0;

	    /* Tell message handler that forwarder is obsolete */
	    msg.header.sender = PSC_getTID(-2, PSC_getPID(msg.header.sender));
	}
	if (!PSID_handleMsg(&msg)) {
	    PSID_log(-1, "%s: Problem on socket %d\n", __func__, fd);
	}
    }

    return 0;
}

void PSIDclient_setEstablished(int fd, Selector_CB_t handler, void *info)
{
    Selector_remove(fd);
    Selector_register(fd, handler ? handler : handleClientMsg, info);

    clients[fd].flags &= ~INITIALCONTACT;
}

int PSIDclient_isEstablished(int fd)
{
    return !(clients[fd].flags & INITIALCONTACT);
}

/**
 * @brief Actually send a message
 *
 * Send message @a msg to file-descriptor @a fd in a non-blocking
 * fashion. In fact not the whole message is sent but @a offset bytes
 * at the beginning of @a msg are left out.
 *
 * @param fd File-descriptor to send the message to
 *
 * @param msg Message to send
 *
 * @param offset Number of bytes skipped at the beginning of @a msg
 *
 * @return Upon success the offset of the next byte to send is
 * returned. This might be used as an offset for subsequent calls. Or
 * -1 if an error occurred.
 */
static int do_send(int fd, DDMsg_t *msg, int offset)
{
    PStask_t *task;
    int n, i, eno;

    for (n=offset, i=1; (n<msg->len) && (i>0);) {
	errno = 0;
	i = send(fd, &(((char*)msg)[n]), msg->len-n, MSG_DONTWAIT);
	if (i<=0) {
	    switch (errno) {
	    case EINTR:
		break;
	    case EAGAIN:
		return n;
	    default:
		eno = errno;
		PSID_warn((eno==EPIPE) ? PSID_LOG_CLIENT : -1, eno,
			  "%s: error on socket %d", __func__, fd);
		task = PSIDclient_getTask(fd);
		if (task) {
		    if (!task->killat) {
			task->killat = time(NULL) + 10;
		    }
		    /* Make sure we get all pending messages */
		    Selector_enable(fd);
		} else {
		    PSID_log(-1, "%s: No task\n", __func__);
		    PSIDclient_delete(fd);
		}
		errno = eno;

		return i;
	    }
	} else
	    n+=i;
    }
    return n;
}

static int storeMsgClient(int fd, DDMsg_t *msg, int offset)
{
    int blockedRDP;
    PSIDmsgbuf_t *msgbuf = PSIDMsgbuf_get(msg->len);

    if (!msgbuf) {
	errno = ENOMEM;
	return -1;
    }

    memcpy(msgbuf->msg, msg, msg->len);
    msgbuf->offset = offset;

    blockedRDP = RDP_blockTimer(1);
    list_add_tail(&msgbuf->next, &clients[fd].msgs);
    RDP_blockTimer(blockedRDP);

    return 0;
}

/**
 * @brief Flush messages to client.
 *
 * Try to send all messages to the client connected via file
 * descriptor @a fd that could not be delivered in prior calls to
 * PSIDclient_send() or flushClientMsgs().
 *
 * @param fd The file descriptor the messages to send are associated with
 *
 * @param info Dummy argument to match Selector_CB_t's signature
 *
 * @return If all pending messages were delivered, 0 is returned. If
 * not all messages were delivered, 1 is returned. Or -1 if a problem
 * occurred.
 *
 * @see PSIDclient_send()
 */
static int flushClientMsgs(int fd, void *info)
{
    list_t *m, *tmp;
    int blockedRDP;

    if (fd < 0 || fd >= maxClientFD) {
	errno = EINVAL;
	return -1;
    }

    if (clients[fd].flags & (FLUSH | CLOSE)) return 1;

    blockedRDP = RDP_blockTimer(1);

    clients[fd].flags |= FLUSH;

    list_for_each_safe(m, tmp, &clients[fd].msgs) {
	PSIDmsgbuf_t *msgbuf = list_entry(m, PSIDmsgbuf_t, next);
	DDMsg_t *msg = (DDMsg_t *)msgbuf->msg;
	int sent = do_send(fd, msg, msgbuf->offset);

	if (sent < 0) {
	    RDP_blockTimer(blockedRDP);

	    return sent;
	}

	if (sent != msg->len) {
	    msgbuf->offset = sent;
	    break;
	}

	list_del(&msgbuf->next);
	PSIDMsgbuf_put(msgbuf);
    }

    if (list_empty(&clients[fd].msgs) && !clients[fd].pendingACKs) {
	/* Use the stop-hash to actually send SENDCONT msgs */
	int num = PSIDFlwCntrl_sendContMsgs(clients[fd].stops, clients[fd].tid);
	PSID_log(PSID_LOG_FLWCNTRL, "%s: sent %d msgs\n", __func__, num);
    }

    clients[fd].flags &= ~FLUSH;

    RDP_blockTimer(blockedRDP);

    if (!list_empty(&clients[fd].msgs)) return 1;

    Selector_vacateWrite(fd);

    return 0;
}

int PSIDclient_send(DDMsg_t *msg)
{
    PStask_t *task = PStasklist_find(&managedTasks, msg->dest);
    int fd, sent = 0;

    if (PSID_getDebugMask() & PSID_LOG_CLIENT) {
	PSID_log(PSID_LOG_CLIENT, "%s(type %s (len=%d) to %s\n",
		 __func__, PSDaemonP_printMsg(msg->type), msg->len,
		 PSC_printTID(msg->dest));
    }

    if (PSC_getID(msg->dest)!=PSC_getMyID()) {
	errno = EHOSTUNREACH;
	PSID_log(-1, "%s: dest not found\n", __func__);
	return -1;
    }

    if (!task || task->fd==-1) {
	PSID_log(PSID_LOG_CLIENT, "%s: no fd for task %s to send%s\n",
		 __func__, PSC_printTID(msg->dest), task ? "" : " (no task)");
	if (PSID_getDebugMask() & PSID_LOG_MSGDUMP) PSID_dumpMsg(msg);
	errno = EHOSTUNREACH;
	return -1;
    }
    fd = task->fd;

    if (!list_empty(&clients[fd].msgs)) flushClientMsgs(fd, NULL);

    if (list_empty(&clients[fd].msgs)) {
	PSID_log(PSID_LOG_CLIENT, "%s: use fd %d\n", __func__, fd);
	sent = do_send(fd, msg, 0);
    }

    if (sent<0) return sent;
    if (sent != msg->len) {
	if (storeMsgClient(fd, msg, sent)) {
	    PSID_warn(-1, errno, "%s: Failed to store message", __func__);
	    errno = ENOBUFS;
	} else {
	    Selector_awaitWrite(fd, flushClientMsgs, NULL);

	    if (PSIDFlwCntrl_applicable(msg)) {
		int ret = PSIDFlwCntrl_addStop(clients[fd].stops, msg->sender);

		if (ret < 0) {
		    PSID_warn(-1, errno, "%s: Failed to store stopTID",
			      __func__);
		    errno = ENOBUFS;
		    return -1;
		}

		if (!ret) {
		    errno = 0;
		    return msg->len; /* suppress sending of SENDSTOP */
		} else {
		    /* yet another SENDSTOPACK is pending */
		    clients[fd].pendingACKs++;
		}

	    }
	    errno = EWOULDBLOCK;
	}
	return -1;
    }

    return sent;
}

/* @todo we need to timeout if message is too small */
int PSIDclient_recv(int fd, DDMsg_t *msg, size_t size)
{
    int n;
    int count = 0;

    if (!msg || size < sizeof(*msg)) {
	PSID_log(-1, "%s: invalid msg\n", __func__);
	errno = EINVAL;
	return -1;
    }

    msg->len = sizeof(*msg);

    if (!PSIDclient_isEstablished(fd)) {
	/*
	 * if this is the first contact of the client, the client may
	 * use an incompatible msg format
	 */
	if (size < sizeof(DDInitMsg_t)) {
	    errno = ENOMEM;
	    return -1;
	}

	n = count = read(fd, msg, sizeof(DDInitMsg_t));
	if (!count) {
	    /* Socket close before initial message was sent */
	    PSID_log(PSID_LOG_CLIENT,
		     "%s(%d) socket already closed\n", __func__, fd);
	} else if (count!=msg->len) {
	    /* if wrong msg format initiate a disconnect */
	    PSID_log(-1, "%d=%s(%d): initial message with incompatible type\n",
		     n, __func__, fd);
	    count=n=0;
	}
    } else do {
	if (!count) {
	    /* First chunk of data */
	    n = read(fd, msg, sizeof(*msg));
	} else {
	    /* Later on we have msg->len */
	    n = read(fd, &((char*) msg)[count], msg->len-count);
	}
	if (n>0) {
	    if (!count) {
		/* Just received first chunk of data */
		if (msg->len > size) {
		    /* message will not fit into msg */
		    errno = EMSGSIZE;
		    n = -1;
		    /* @todo we should remove message from fd (with timeout!) */
		    break;
		}
	    }
	    count+=n;
	} else if (n<0 && (errno==EINTR)) {
	    continue;
	} else if (n<0 && (errno==ECONNRESET)) {
	    /* socket is closed unexpectedly */
	    n = 0;
	    break;
	} else break;
    } while (msg->len > count);

    if (count && count==msg->len) {
	return msg->len;
    } else {
	return n;
    }
}


/**
 * @brief Close connection to client.
 *
 * Close the connection to the client connected via the file
 * descriptor @a fd. Afterwards the relevant part of the client table
 * is reset.
 *
 * @param fd The file descriptor the client is connected through.
 *
 * @return No return value.
 */
static void closeConnection(int fd)
{
    list_t *m, *tmp;
    PStask_ID_t tid = PSIDclient_getTID(fd);
    int blockedRDP;

    if (fd<0) {
	PSID_log(-1, "%s(%d): fd < 0\n", __func__, fd);
	return;
    }

    clients[fd].tid = -1;
    if (clients[fd].task) clients[fd].task->fd = -1;
    clients[fd].task = NULL;

    if (clients[fd].flags & CLOSE) return;

    blockedRDP = RDP_blockTimer(1);

    clients[fd].flags |= CLOSE;

    list_for_each_safe(m, tmp, &clients[fd].msgs) {
	PSIDmsgbuf_t *mp = list_entry(m, PSIDmsgbuf_t, next);
	DDBufferMsg_t *msg = (DDBufferMsg_t *)mp->msg;

	list_del(&mp->next);
	PSID_dropMsg(msg);
	PSIDMsgbuf_put(mp);
    }
    Selector_vacateWrite(fd);

    /* Now use the stop-hash to actually send SENDCONT msgs */
    PSIDFlwCntrl_sendContMsgs(clients[fd].stops, tid);

    clients[fd].flags &= ~CLOSE;

    RDP_blockTimer(blockedRDP);

    Selector_remove(fd);
    shutdown(fd, SHUT_RDWR);
    close(fd);
}

void PSIDclient_delete(int fd)
{
    PStask_t *task;

    PSID_log(fd<0 ? -1 : PSID_LOG_CLIENT, "%s(%d)\n", __func__, fd);
    if (fd<0) return;

    task = PSIDclient_getTask(fd);

    PSID_log(PSID_LOG_CLIENT, "%s: closing connection to %s\n",
	     __func__, task ? PSC_printTID(task->tid) : "<unknown>");
    closeConnection(fd);

    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__,
		 PSC_printTID(PSIDclient_getTID(fd)));
	return;
    }

    if (task->group == TG_FORWARDER && !task->released) {
	DDErrorMsg_t msg;
	PStask_ID_t child;
	int sig = -1;

	PSID_log(-1, "%s: Unreleased forwarder %s\n",
		 __func__, PSC_printTID(task->tid));

	/* Tell logger about unreleased forwarders */
	msg.header.type = PSP_CC_ERROR;
	msg.header.dest = task->loggertid;
	msg.header.sender = task->tid;
	msg.header.len = sizeof(msg.header);
	sendMsg(&msg);

	while ((child = PSID_getSignal(&task->childList, &sig))) {
	    PStask_t *childTask = PStasklist_find(&managedTasks, child);
	    PSID_log(-1, "%s: kill child %s\n", __func__, PSC_printTID(child));

	    if (!childTask || childTask->forwarder != task) {
		/* Maybe the child's task was obsoleted */
		childTask = PStasklist_find(&obsoleteTasks, child);
		/* Still not the right childTask? */
		if (childTask && childTask->forwarder != task) childTask = NULL;
	    }

	    /* Since forwarder is gone eliminate all references */
	    if (childTask) childTask->forwarder = NULL;

	    /* Try to kill the child, again (obsolete childs are yet gone) */
	    if (childTask && !childTask->obsolete) {
		PSID_kill(-child, SIGKILL, 0);
	    }

	    /* Assume child is dead */
	    msg.header.type = PSP_DD_CHILDDEAD;
	    msg.header.dest = task->ptid;
	    msg.header.sender = task->tid;
	    msg.error = 0;
	    msg.request = child;
	    msg.header.len = sizeof(msg);
	    sendMsg(&msg);

	    if (childTask && childTask->fd == -1) PSIDtask_cleanup(childTask);

	    sig = -1;
	};

	task->released = true;
    }

    /* Unregister TG_(PSC)SPAWNER from parent process */
    if (task->group == TG_SPAWNER || task->group == TG_PSCSPAWNER) {

	/* Find correct parent and remove dead spawner from list of children */
	PStask_t *parent = PStasklist_find(&managedTasks, task->ptid);
	if (!parent || !PSID_removeSignal(&parent->childList, task->tid, -1)) {
	    /* Maybe the parent's task was obsoleted */
	    parent = PStasklist_find(&obsoleteTasks, task->ptid);
	    /* Still not the right parent? */
	    if (parent
		&&  !PSID_removeSignal(&parent->childList, task->tid, -1)) {
		parent = NULL;
	    }
	}

	if (parent && parent->removeIt
	    && PSID_emptySigList(&parent->childList)) {
	    PSID_log(PSID_LOG_TASK, "%s: PSIDtask_cleanup(parent)\n", __func__);
	    PSIDtask_cleanup(parent);
	}
    }

    /* Unregister TG_ACCOUNT */
    if (task->group == TG_ACCOUNT) {
	DDOptionMsg_t acctmsg = {
	    .header = {
		.type = PSP_CD_SETOPTION,
		.sender = PSC_getMyTID(),
		.dest = 0,
		.len = sizeof(acctmsg) },
	    .count = 0,
	    .opt = {{ .option = 0, .value = 0 }} };
	acctmsg.opt[(int) acctmsg.count].option = PSP_OP_REM_ACCT;
	acctmsg.opt[(int) acctmsg.count].value = task->tid;
	acctmsg.count++;

	broadcastMsg(&acctmsg);

	PSID_remAcct(task->tid);
    }

    /* Send accounting info for logger */
    if (task->group == TG_LOGGER
	&& (task->request || task->partitionSize > 0)) {
	DDTypedBufferMsg_t msg;
	char *ptr = msg.buf;

	msg.header.type = PSP_CD_ACCOUNT;
	msg.header.dest = PSC_getMyTID();
	msg.header.sender = task->tid;
	msg.header.len = sizeof(msg.header);

	msg.type = (task->numChild > 0) ? PSP_ACCOUNT_END : PSP_ACCOUNT_DELETE;
	msg.header.len += sizeof(msg.type);

	/* logger's TID, this identifies a task uniquely */
	*(PStask_ID_t *)ptr = task->tid;
	ptr += sizeof(PStask_ID_t);
	msg.header.len += sizeof(PStask_ID_t);

	/* current rank */
	*(int32_t *)ptr = task->rank;
	ptr += sizeof(int32_t);
	msg.header.len += sizeof(int32_t);

	/* child's uid */
	*(uid_t *)ptr = task->uid;
	ptr += sizeof(uid_t);
	msg.header.len += sizeof(uid_t);

	/* child's gid */
	*(gid_t *)ptr = task->gid;
	ptr += sizeof(gid_t);
	msg.header.len += sizeof(gid_t);

	if (task->numChild > 0) {
	    struct timeval now, walltime;

	    /* total number of children */
	    *(int32_t *)ptr = task->numChild;
	    ptr += sizeof(int32_t);
	    msg.header.len += sizeof(int32_t);

	    /* walltime used by logger */
	    gettimeofday(&now, NULL);
	    timersub(&now, &task->started, &walltime);
	    memcpy(ptr, &walltime, sizeof(walltime));
	    //ptr += sizeof(walltime);
	    msg.header.len += sizeof(walltime);
	}

	sendMsg((DDMsg_t *)&msg);
    }

    /* Cleanup, if no forwarder available; otherwise wait for CHILDDEAD */
    if (!task->forwarder) {
	PSID_log(PSID_LOG_TASK, "%s: PSIDtask_cleanup()\n", __func__);
	PSIDtask_cleanup(task);
    }

    return;
}

int PSIDclient_killAll(int sig, int killAdminTasks)
{
    list_t *t;
    int ret = 0;

    PSID_log(PSID_LOG_CLIENT, "%s(%d, %d)\n", __func__, sig, killAdminTasks);

    /* loop over all tasks */
    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	pid_t pid = PSC_getPID(task->tid);

	if (task->deleted) continue;
	if (task->group==TG_MONITOR) continue;
	if ((task->group==TG_ADMIN || task->group==TG_FORWARDER)
	    && !killAdminTasks) continue;

	PSID_log(PSID_LOG_CLIENT, "%s: send %s to %s pid %d fd %d\n", __func__,
		 strsignal(sig), PSC_printTID(task->tid), pid, task->fd);

	if (pid > 0) {
	    if (sig == SIGKILL) kill(pid, SIGCONT);
	    kill(pid, sig);
	    ret++;
	}

	if (sig==SIGKILL && killAdminTasks && task->fd != -1) {
	    PSID_log(-1, "%s: PSIDclient_delete()\n", __func__);
	    PSIDclient_delete(task->fd);
	}
    }

    return ret;
}

void PSIDclient_releaseACK(int fd)
{
    if (fd < 0 || fd >= maxClientFD) {
	PSID_log(-1, "%s(%d): file descriptor out of range\n", __func__, fd);
	return;
    }

    clients[fd].pendingACKs--;

    if (!clients[fd].pendingACKs && list_empty(&clients[fd].msgs)) {
	/* Use the stop-hash to actually send SENDCONT msgs */
	int ret = PSIDFlwCntrl_sendContMsgs(clients[fd].stops, clients[fd].tid);
	PSID_log(PSID_LOG_FLWCNTRL, "%s: sent %d msgs\n", __func__, ret);
    }
}

pid_t getpgid(pid_t); /* @todo HACK HACK HACK */

/**
 * @brief Handle a PSP_CD_CLIENTCONNECT message.
 *
 * Handle the message @a bufmsg of type PSP_CD_CLIENTCONNECT. For
 * compatibility the file-descriptor @a fd the message was received
 * from is not passed explicitly. Instead this file-descriptor has to
 * be appended to the actual message by the calling function.
 *
 * This kind of message is send by a client message in order to
 * connect to the local daemon. It's should be the first message
 * received from the client and will be used in order to identify and
 * authenticate the client process.
 *
 * As a result either a PSP_CD_CLIENTESTABLISHED message will be send
 * back to the client in order to accept the connection. Otherwise a
 * PSP_CD_CLIENTREFUSED is delivered and the connection to the client
 * is closed.
 *
 * @param bufmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_CLIENTCONNECT(int fd, DDBufferMsg_t *bufmsg)
{
    DDInitMsg_t *msg = (DDInitMsg_t *)bufmsg;

    pid_t pid;
    uid_t uid;
    gid_t gid;

#ifdef SO_PEERCRED
    socklen_t size;
    struct ucred cred;

    size = sizeof(cred);
    getsockopt(fd, SOL_SOCKET, SO_PEERCRED, (void*) &cred, &size);
    pid = cred.pid;
    uid = cred.uid;
    gid = cred.gid;
#else
    pid = PSC_getPID(msg->header.sender);
    uid = msg->uid;
    gid = msg->gid;
#endif
    PStask_ID_t tid = PSC_getTID(-1, pid);

    PSID_log(PSID_LOG_CLIENT,
	     "%s: from %s at fd %d, group=%s, version=%d, uid=%d\n",
	     __func__, PSC_printTID(tid), fd, PStask_printGrp(msg->group),
	     msg->version, uid);
    /*
     * first check if it is a reconnection
     * this might happen due to an exec() call.
     */
    PStask_t *task = PStasklist_find(&managedTasks, tid);
    if (!task && msg->group != TG_SPAWNER && msg->group != TG_PSCSPAWNER) {
	PStask_ID_t pgtid = PSC_getTID(-1, getpgid(pid));

	task = PStasklist_find(&managedTasks, pgtid);

	if (msg->group == TG_ADMIN) {
	    /*
	     * psiadmin never forks. This is another psiadmin started
	     * from within a shell script. Forget about this task.
	     */
	    PSID_log(PSID_LOG_CLIENT, "%s: no reconnection since task is %s\n",
		     __func__, PStask_printGrp(msg->group));
	    task = NULL;
	}

	if (task && (task->group == TG_LOGGER || task->group == TG_ADMIN
		     || task->group == TG_ADMINTASK) ) {
	    /*
	     * Logger, psiadmin and admin-tasks never fork. This is
	     * another executable started from within a shell
	     * script. Forget about this task.
	     */
	    PSID_log(PSID_LOG_CLIENT, "%s: no reconnection since parent-task"
		     " is %s\n", __func__, PStask_printGrp(task->group));
	    task = NULL;
	}

	if (task) {
	    /* Spawned process has changed pid */
	    /* This might happen due to stuff in PSI_RARG_PRE_0 */
	    PStask_t *child = PStask_clone(task);

	    PSID_log(PSID_LOG_CLIENT, "%s: reconnection with changed PID"
		     "%d -> %d\n", __func__, PSC_getPID(task->tid), pid);

	    if (child) {
		child->tid = tid;
		child->duplicate = true;
		PStasklist_enqueue(&managedTasks, child);

		if (task->forwarder) {
		    /* Register new child to its forwarder */
		    PSID_setSignal(&task->forwarder->childList, child->tid, -1);
		} else {
		    PSID_log(-1, "%s: task %s has no forwarder\n",
			     __func__, PSC_printTID(task->tid));
		}

		/* We want to handle the reconnected child now */
		task = child;
	    }
	}
    }
    if (task) {
	/* re-connection */
	/* use the old task struct but close the old fd */
	PSID_log(PSID_LOG_CLIENT, "%s: reconnecting task %s, old/new fd ="
		 " %d/%d\n", __func__, PSC_printTID(task->tid), task->fd, fd);

	/* close the previous socket */
	if (task->fd > 0) {
	    closeConnection(task->fd);
	} else {
	    /* Remove the old selector created while accept()ing connection */
	    Selector_remove(fd);
	}
	task->fd = fd;

	/* This is needed for gmspawner */
	if (msg->group == TG_GMSPAWNER) {
	    task->group = msg->group;

	    /* Fix the info about the spawner task */
	    decJobs(1, 1);
	    incJobs(1, 0);
	}
    } else {
	char tasktxt[128];
	task = PStask_new();
	task->tid = tid;
	task->fd = fd;
	task->uid = uid;
	task->gid = gid;
	/* New connection, this task will become logger */
	if (msg->group == TG_ANY) {
	    task->group = TG_LOGGER;
	    task->loggertid = tid;
	} else {
	    task->group = msg->group;
	}

	/* TG_(PSC)SPAWNER have to get a special handling */
	if (task->group == TG_SPAWNER || task->group == TG_PSCSPAWNER) {
	    PStask_t *parent;
	    PStask_ID_t ptid;

	    ptid = PSC_getTID(-1, msg->ppid);

	    parent = PStasklist_find(&managedTasks, ptid);

	    if (parent) {
		/* register the child */
		PSID_setSignal(&parent->childList, task->tid, -1);

		task->ptid = ptid;

		switch (task->group) {
		case TG_SPAWNER:
		    task->loggertid = parent->loggertid;
		    break;
		case TG_PSCSPAWNER:
		    task->loggertid = tid;
		    break;
		default:
		    PSID_log(-1, "%s: group %s not handled\n",
			     __func__, PStask_printGrp(msg->group));
		}
	    } else {
		/* no parent !? kill the task */
		PSID_sendSignal(task->tid, task->uid, PSC_getMyTID(), -1, 0,0);
	    }
	}

	PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	PSID_log(PSID_LOG_CLIENT, "%s: request from: %s\n", __func__, tasktxt);

	PStasklist_enqueue(&managedTasks, task);

	/* Tell everybody about the new task */
	incJobs(1, (task->group==TG_ANY));

	/* Remove the old selector created while accept()ing connection */
	Selector_remove(fd);
    }

    /* Seed the sequence of reservation IDs */
    task->nextResID = task->tid + 0x042;

    PSIDclient_register(fd, tid, task);

    /* Get the number of processes */
    PSID_NodeStatus_t status = getStatusInfo(PSC_getMyID());

    /* Reject or accept connection */
    DDTypedBufferMsg_t outmsg = {
	.header = { .type = PSP_CD_CLIENTESTABLISHED,
		    .sender = PSC_getMyTID(),
		    .dest = tid,
		    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_CONN_ERR_NONE,
	.buf = { 0 } };

    /* Connection refused answer message */
    if (msg->version < 324 || msg->version > PSProtocolVersion) {
	outmsg.type = PSP_CONN_ERR_VERSION;
	uint32_t protoV = PSProtocolVersion;
	PSP_putTypedMsgBuf(&outmsg, __func__, "protoV", &protoV,
			   sizeof(protoV));
    } else if (!task) {
	outmsg.type = PSP_CONN_ERR_NOSPACE;
    } else if (uid && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_USER,
					  (PSIDnodes_guid_t){.u=uid})) {
	outmsg.type = PSP_CONN_ERR_UIDLIMIT;
    } else if (gid && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_GROUP,
					  (PSIDnodes_guid_t) {.g=gid})) {
	outmsg.type = PSP_CONN_ERR_GIDLIMIT;
    } else if (PSIDnodes_getProcs(PSC_getMyID()) != PSNODES_ANYPROC
	       && status.jobs.normal > PSIDnodes_getProcs(PSC_getMyID())) {
	outmsg.type = PSP_CONN_ERR_PROCLIMIT;
	int32_t maxProcs = PSIDnodes_getProcs(PSC_getMyID());
	PSP_putTypedMsgBuf(&outmsg, __func__, "maxProcs", &maxProcs,
			   sizeof(maxProcs));
    } else if (PSID_getDaemonState() & PSID_STATE_NOCONNECT) {
	outmsg.type = PSP_CONN_ERR_STATENOCONNECT;
	PSID_log(-1, "%s: daemon state problems: state is %x\n",
		 __func__, PSID_getDaemonState());
    }

    if (outmsg.type != PSP_CONN_ERR_NONE || msg->group == TG_RESET) {
	outmsg.header.type = PSP_CD_CLIENTREFUSED;

	PSID_log(PSID_LOG_CLIENT, "%s: connection refused: group %s task %s"
		 " version %d vs. %d uid %d gid %d jobs %d %d\n", __func__,
		 PStask_printGrp(msg->group), PSC_printTID(task->tid),
		 msg->version, PSProtocolVersion, uid, gid, status.jobs.normal,
		 PSIDnodes_getProcs(PSC_getMyID()));
	sendMsg(&outmsg);

	/* clean up */
	PSID_log(-1, "%s: PSIDclient_delete()\n", __func__);
	PSIDclient_delete(fd);

	if (msg->group==TG_RESET && !uid) PSID_reset();
    } else {
	PSIDclient_setEstablished(fd, handleClientMsg, NULL);
	task->protocolVersion = msg->version;

	if (task->protocolVersion < 344) {
	    outmsg.type = PSC_getMyID();
	} else {
	    bool mixedProto = PSID_mixedProto();
	    PSP_putTypedMsgBuf(&outmsg, __func__, "mixedProto", &mixedProto,
			       sizeof(mixedProto));
	    PSnodes_ID_t myID = PSC_getMyID();
	    PSP_putTypedMsgBuf(&outmsg, __func__, "myID", &myID, sizeof(myID));
	}

	sendMsg(&outmsg);

	if (task->group == TG_ACCOUNT) {
	    /* Register accounter */
	    DDOptionMsg_t acctmsg = {
		.header = {
		    .type = PSP_CD_SETOPTION,
		    .sender = PSC_getMyTID(),
		    .dest = 0,
		    .len = sizeof(acctmsg) },
		.count = 0,
		.opt = {{ .option = 0, .value = 0 }} };
	    acctmsg.opt[(int) acctmsg.count].option = PSP_OP_ADD_ACCT;
	    acctmsg.opt[(int) acctmsg.count].value = task->tid;
	    acctmsg.count++;

	    broadcastMsg(&acctmsg);

	    PSID_addAcct(task->tid);
	}

    }
}

/**
 * @brief Handle a PSP_CC_MSG message.
 *
 * Handle the message @a msg of type PSP_CC_MSG.
 *
 * This kind of message is used for communication between clients and
 * might have some internal types. In order to not break this type of
 * communication, a PSP_CC_ERROR message has to be passed to the
 * original sender, if something went wrong during passing the
 * original message towards its final destination.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_CC_MSG(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_CLIENT, "%s: from %s", __func__,
	     PSC_printTID(msg->header.sender));
    PSID_log(PSID_LOG_CLIENT, " to %s\n", PSC_printTID(msg->header.dest));

    if (msg->header.dest == PSC_getMyTID()) {
	PSID_log(-1, "%s: from %s to me?! Dropping...\n", __func__,
		 PSC_printTID(msg->header.sender));
	PSID_dropMsg(msg);
	return;
    }

    /* Forward this message. If this fails, send an error message. */
    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	PSID_log(PSID_LOG_CLIENT, "%s: sending failed\n", __func__);
	PSID_dropMsg(msg);
    }
}

/**
 * @brief Drop a PSP_CC_MSG message.
 *
 * Drop the message @a msg of type PSP_CC_MSG.
 *
 * Since the sender might wait for an answer within a higher-level
 * protocol a corresponding answer is created on this lower level to
 * send a hint that the original messages is dropped.
 *
 * @param msg Pointer to the message to drop.
 *
 * @return No return value.
 */
static void drop_CC_MSG(DDBufferMsg_t *msg)
{
    DDMsg_t errmsg;

    errmsg.type = PSP_CC_ERROR;
    errmsg.dest = msg->header.sender;
    errmsg.sender = msg->header.dest;
    errmsg.len = sizeof(errmsg);

    sendMsg(&errmsg);
}

/**
 * @brief Init client structure
 *
 * Initialize the client structure @a client.
 *
 * @param client The client to initialize.
 *
 * @return No return value.
 */
static void clientInit(client_t *client)
{
    client->tid = -1;
    client->task = NULL;
    client->flags = 0;
    client->pendingACKs = 0;
    INIT_LIST_HEAD(&client->msgs);
    PSIDFlwCntrl_initHash(client->stops);
}

void PSIDclient_init(void)
{
    int numFiles;

    PSIDFlwCntrl_init();

    if (clients) {
	PSID_log(-1, "%s: already initialized\n", __func__);
	return;
    }

    numFiles = sysconf(_SC_OPEN_MAX);
    if (numFiles <= 0) {
	PSID_exit(errno, "%s: sysconf(_SC_OPEN_MAX) returns %d", __func__,
		  numFiles);
	return;
    }

    if (PSIDclient_setMax(numFiles) < 0) {
	PSID_exit(errno, "%s: PSIDclient_setMax()", __func__);
	return;
    }

    PSID_registerMsg(PSP_CC_MSG, msg_CC_MSG);

    PSID_registerDropper(PSP_CC_MSG, drop_CC_MSG);
}

static inline void fixList(list_t *list, list_t *oldHead)
{
    if (list->next == oldHead) {
	/* list was empty */
	INIT_LIST_HEAD(list);
    } else {
	/* fix reverse pointers */
	list->next->prev = list;
	list->prev->next = list;
    }
}

int PSIDclient_setMax(int max)
{
    int oldMax = maxClientFD;
    client_t *oldClients = clients;
    int fd;

    if (maxClientFD >= max) return 0; /* don't shrink */

    maxClientFD = max;

    clients = realloc(clients, sizeof(*clients) * maxClientFD);
    if (!clients) {
	PSID_warn(-1, ENOMEM, "%s", __func__);
	errno = ENOMEM;
	return -1;
    }

    /* Restore old lists if necessary */
    if (clients != oldClients) {
	for (fd=0; fd < oldMax; fd++) {
	    int h;
	    fixList(&clients[fd].msgs, &oldClients[fd].msgs);
	    for (h=0; h<FLWCNTRL_HASH_SIZE; h++) {
		fixList(&clients[fd].stops[h], &oldClients[fd].stops[h]);
	    }
	}
    }

    /* Initialize new clients */
    for (fd = oldMax; fd < maxClientFD; fd++) {
	clientInit(&clients[fd]);
    }

    return 0;
}

void PSIDclient_clearMem(void)
{
    int fd;

    /* Need to put MsgBufs to free() non-small messages */
    for (fd = 0; fd < maxClientFD; fd++) {
	list_t *m, *tmp;

	list_for_each_safe(m, tmp, &clients[fd].msgs) {
	    PSIDmsgbuf_t *mp = list_entry(m, PSIDmsgbuf_t, next);

	    list_del(&mp->next);
	    PSIDMsgbuf_put(mp);
	}
    }

    if (clients) {
	free(clients);
	clients = NULL;
    }
    maxClientFD = 0;
}
