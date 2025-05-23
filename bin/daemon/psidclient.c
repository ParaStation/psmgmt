/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#define _GNU_SOURCE
#include "psidclient.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include "list.h"
#include "pscio.h"
#include "pscommon.h"
#include "psdaemonprotocol.h"
#include "pslog.h"

#include "rdp.h"

#include "psidutil.h"
#include "psidtask.h"
#include "psidmsgbuf.h"
#include "psidcomm.h"
#include "psidaccount.h"
#include "psidnodes.h"
#include "psidstatus.h"
#include "psidsignal.h"
#include "psidstate.h"
#include "psidflowcontrol.h"

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
    PSID_fdbg(PSID_LOG_COMM, "fd %d\n", fd);

    /* read the whole msg */
    DDBufferMsg_t msg;
    ssize_t msglen = PSIDclient_recv(fd, &msg);

    if (!msglen) {
	/* closing connection */
	PSID_fdbg(PSID_LOG_CLIENT, "close connection to %d\n", fd);
	PSIDclient_delete(fd);
    } else if (msglen == -1) {
	if (errno != EAGAIN) {
	    int eno = errno;
	    PSID_fwarn(eno, "(%d): PSIDclient_recv()", fd);
	    if (eno == EBADF) PSIDclient_delete(fd);
	}
    } else {
	if (msg.header.type != PSP_CD_CLIENTCONNECT) {
	    PSID_flog("Unexpected message of type %s\n",
		      PSP_printMsg(msg.header.type));
	    return 0;
	}
	msg_CLIENTCONNECT(fd, &msg);
    }

    return 0;
}

static bool doCheckClient(int fd, const char *caller)
{
    if (!clients) {
	PSID_log("%s(%d): not initialized\n", caller, fd);
	return false;
    }  else if (fd < 0 || fd >= maxClientFD) {
	PSID_log("%s(%d): file descriptor out of range\n", caller, fd);
	return false;
    }
    return true;
}

#define checkClient(fd) doCheckClient(fd, __func__)

void PSIDclient_register(int fd, PStask_ID_t tid, PStask_t *task)
{
    if (!checkClient(fd)) return;

    clients[fd].tid = tid;
    clients[fd].task = task;
    clients[fd].flags |= INITIALCONTACT;
    clients[fd].pendingACKs = 0;
    INIT_LIST_HEAD(&clients[fd].msgs);
    PSIDFlwCntrl_emptyHash(clients[fd].stops);

    PSCio_setFDblock(fd, false);

    Selector_register(fd, handleClientConnectMsg, NULL);
}

PStask_ID_t PSIDclient_getTID(int fd)
{
    if (!checkClient(fd)) return PSC_getMyTID();

    return clients[fd].tid;
}

PStask_t *PSIDclient_getTask(int fd)
{
    if (!checkClient(fd)) return NULL;

    return clients[fd].task;
}

static int handleClientMsg(int fd, void *info)
{
    PSID_fdbg(PSID_LOG_COMM, "fd %d\n", fd);

    /* read the whole msg */
    DDBufferMsg_t msg;
    ssize_t msglen = PSIDclient_recv(fd, &msg);

    if (!msglen) {
	/* closing connection */
	PSID_fdbg(PSID_LOG_CLIENT, "close connection to %d\n", fd);
	PSIDclient_delete(fd);
    } else if (msglen == -1) {
	if (errno != EAGAIN) {
	    int eno = errno;
	    PSID_fwarn(eno, "(%d): PSIDclient_recv()", fd);
	    if (eno == EBADF) PSIDclient_delete(fd);
	}
    } else {
	PStask_ID_t tid = PSIDclient_getTID(fd);
	if (msg.header.sender != tid) {
	    PSID_flog("got msg from %s on socket %d",
		      PSC_printTID(msg.header.sender), fd);
	    PSID_log(" assigned to %s\n", PSC_printTID(tid));
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
	    PSID_flog("unable to handle message on socket %d\n", fd);
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

bool PSIDclient_isEstablished(int fd)
{
    if (!checkClient(fd)) return false;
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
static ssize_t doSend(int fd, DDMsg_t *msg, size_t offset)
{
    char *buf = (char *)msg;
    size_t sent;
    ssize_t ret = PSCio_sendSProg(fd, buf + offset, msg->len - offset, &sent);
    if (ret < 0) {
	int eno = errno;
	if (eno == EAGAIN || eno == EINTR) {
	    return offset + sent;
	}
	PSID_fdwarn((eno == EPIPE) ? PSID_LOG_CLIENT : -1, eno,
		    "error on socket %d", fd);
	PStask_t *task = PSIDclient_getTask(fd);
	if (task) {
	    if (!task->killat) task->killat = time(NULL) + 10;
	    /* Make sure we get all pending messages */
	    Selector_enable(fd);
	} else {
	    PSID_flog("no task\n");
	    PSIDclient_delete(fd);
	}
	errno = eno;

	return ret;
    }

    return offset + ret;
}

/**
 * brief Store message
 *
 * Put the message @a msg into a msgbuf and append it to the list of
 * undeliverd messages of file descriptor @a fd. The msgbuf's offset
 * will be set to @a offset and will be used to notice the already
 * sent bytes of @a msg.
 *
 * @param fd File descriptor to store the message to
 *
 * @param msg Message to store
 *
 * @param offset Amount of bytes of @a msg already sent
 *
 * @return If the message was stored, true is returned; or false if no
 * msgbuf was available
 */
static bool storeMsg(int fd, DDMsg_t *msg, size_t offset)
{
    if (!checkClient(fd)) return false;

    PSIDmsgbuf_t *msgbuf = PSIDMsgbuf_get(msg->len);
    if (!msgbuf) {
	errno = ENOMEM;
	return false;
    }

    memcpy(msgbuf->msg, msg, msg->len);
    msgbuf->offset = offset;

    int blockedRDP = RDP_blockTimer(true);
    list_add_tail(&msgbuf->next, &clients[fd].msgs);
    RDP_blockTimer(blockedRDP);

    return true;
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
    if (!checkClient(fd)) {
	errno = EINVAL;
	return -1;
    }

    if (clients[fd].flags & (FLUSH | CLOSE)) return 1;

    int blockedRDP = RDP_blockTimer(true);

    clients[fd].flags |= FLUSH;

    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &clients[fd].msgs) {
	PSIDmsgbuf_t *msgbuf = list_entry(m, PSIDmsgbuf_t, next);
	DDMsg_t *msg = (DDMsg_t *)msgbuf->msg;

	ssize_t sent = doSend(fd, msg, msgbuf->offset);
	if (sent < 0) {
	    RDP_blockTimer(blockedRDP);
	    return sent;
	} else if (sent != msg->len) {
	    msgbuf->offset = sent;
	    break;
	}

	list_del(&msgbuf->next);
	PSIDMsgbuf_put(msgbuf);
    }

    if (list_empty(&clients[fd].msgs) && !clients[fd].pendingACKs) {
	/* Use the stop-hash to actually send SENDCONT msgs */
	int num = PSIDFlwCntrl_sendContMsgs(clients[fd].stops, clients[fd].tid);
	PSID_fdbg(PSID_LOG_FLWCNTRL, "sent %d msgs\n", num);
    }

    clients[fd].flags &= ~FLUSH;

    RDP_blockTimer(blockedRDP);

    if (!list_empty(&clients[fd].msgs)) return 1;

    Selector_vacateWrite(fd);

    return 0;
}

int PSIDclient_send(DDMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_CLIENT, "(type %s (len=%d) to %s\n",
	      PSDaemonP_printMsg(msg->type), msg->len, PSC_printTID(msg->dest));

    if (PSC_getID(msg->dest) != PSC_getMyID()) {
	errno = EHOSTUNREACH;
	PSID_flog("dest %s not found\n", PSC_printTID(msg->dest));
	return -1;
    }

    PStask_t *task = PStasklist_find(&managedTasks, msg->dest);
    if (!task || task->fd == -1) {
	PSID_fdbg(PSID_LOG_CLIENT, "no fd for task %s to send%s\n",
		  PSC_printTID(msg->dest), task ? "" : " (no task)");
	if (PSID_getDebugMask() & PSID_LOG_MSGDUMP) PSID_dumpMsg(msg);
	errno = EHOSTUNREACH;
	return -1;
    }
    int fd = task->fd;
    if (!checkClient(fd)) {
	errno = EHOSTUNREACH;
	return -1;
    }

    if (!list_empty(&clients[fd].msgs)) flushClientMsgs(fd, NULL);

    ssize_t sent = 0;
    if (list_empty(&clients[fd].msgs)) {
	PSID_fdbg(PSID_LOG_CLIENT, "use fd %d\n", fd);
	sent = doSend(fd, msg, 0);
    }

    if (sent == msg->len || sent < 0) return sent; // send done or error

    /* msg at most partly sent */
    if (!storeMsg(fd, msg, sent)) {
	PSID_fwarn(errno, "Failed to store message");
	errno = ENOBUFS;
	return -1;
    }
    Selector_awaitWrite(fd, flushClientMsgs, NULL);

    if (PSIDFlwCntrl_applicable(msg)) {
	int ret = PSIDFlwCntrl_addStop(clients[fd].stops, msg->sender);
	if (ret < 0) {
	    PSID_fwarn(errno, "Failed to store stopTID");
	    errno = ENOBUFS;
	    return -1;
	} else if (!ret) {
	    errno = 0;
	    return msg->len; /* suppress sending of SENDSTOP */
	} else {
	    /* yet another SENDSTOPACK is pending */
	    clients[fd].pendingACKs++;
	}
    }
    errno = EWOULDBLOCK;
    return -1;
}

static ssize_t doClientRecv(int fd, DDBufferMsg_t *msg)
{
    if (!msg) {
	PSID_flog("invalid msg\n");
	errno = EINVAL;
	return -1;
    }
    msg->header.len = sizeof(msg->header);

    ssize_t ret;
    if (!PSIDclient_isEstablished(fd)) {
	/* client's first contact => might use an incompatible msg format */
	ret = PSCio_recvBufP(fd, msg, sizeof(DDInitMsg_t));
	if (!ret) {
	    /* Socket close before initial message was sent */
	    PSID_fdbg(PSID_LOG_CLIENT, "socket %d already closed\n", fd);
	} else if (ret != msg->header.len) {
	    /* if wrong msg format initiate a disconnect */
	    PSID_flog("bad initial message on fd %d: got %zd\n", fd, ret);
	    ret = 0;
	}
    } else {
	// @todo think about timeout on messed up protocol
	ret = PSCio_recvMsg(fd, msg);
	/* socket is closed unexpectedly */
	if (ret < 0 && errno == ECONNRESET) ret = 0;
    }
    return ret;
}

ssize_t PSIDclient_recv(int fd, DDBufferMsg_t *msg)
{
    ssize_t ret = doClientRecv(fd, msg);

    if (ret < 0) {
	PSID_fwarn(errno, "(%d/%s)", fd, PSC_printTID(PSIDclient_getTID(fd)));
    } else if (ret && ret != msg->header.len) {
	PSID_flog("(%d/%s) type %s (len=%d) from %s", fd,
		  PSC_printTID(PSIDclient_getTID(fd)),
		  PSDaemonP_printMsg(msg->header.type), msg->header.len,
		  PSC_printTID(msg->header.sender));
	PSID_log(" dest %s only %zd bytes\n",
		 PSC_printTID(msg->header.dest), ret);
    } else if (PSID_getDebugMask() & PSID_LOG_COMM) {
	PSID_fdbg(PSID_LOG_COMM, "(%d/%s) type %s (len=%d) from %s", fd,
		  PSC_printTID(PSIDclient_getTID(fd)),
		  PSDaemonP_printMsg(msg->header.type), msg->header.len,
		  PSC_printTID(msg->header.sender));
	PSID_dbg(PSID_LOG_COMM, " dest %s\n", PSC_printTID(msg->header.dest));
    }

    return ret;
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
    if (!checkClient(fd)) return;

    clients[fd].tid = -1;
    if (clients[fd].task) clients[fd].task->fd = -1;
    clients[fd].task = NULL;

    if (clients[fd].flags & CLOSE) return;

    int blockedRDP = RDP_blockTimer(true);

    clients[fd].flags |= CLOSE;

    list_t *m, *tmp;
    list_for_each_safe(m, tmp, &clients[fd].msgs) {
	PSIDmsgbuf_t *mp = list_entry(m, PSIDmsgbuf_t, next);
	DDBufferMsg_t *msg = (DDBufferMsg_t *)mp->msg;

	list_del(&mp->next);
	PSID_dropMsg(msg);
	PSIDMsgbuf_put(mp);
    }
    Selector_vacateWrite(fd);

    /* Now use the stop-hash to actually send SENDCONT msgs */
    PStask_ID_t tid = PSIDclient_getTID(fd);
    PSIDFlwCntrl_sendContMsgs(clients[fd].stops, tid);

    clients[fd].flags &= ~CLOSE;

    RDP_blockTimer(blockedRDP);

    Selector_remove(fd);
    shutdown(fd, SHUT_RDWR);
    close(fd);
}

static void sendAcctLost(PStask_ID_t forwarder, PStask_t *client)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_ACCOUNT,
	    .sender = forwarder,
	    .dest = PSC_getMyTID(),
	    .len = 0 },
	.type = PSP_ACCOUNT_LOST };

    /* partition holder identifies job uniquely (logger's TID as fallback) */
    PStask_ID_t acctRoot = client->loggertid;
    if (client->partHolder != -1) acctRoot = client->partHolder;
    PSP_putTypedMsgBuf(&msg, "acctRoot", &acctRoot, sizeof(acctRoot));

    /* child's pid */
    pid_t pid = PSC_getPID(client->tid);
    PSP_putTypedMsgBuf(&msg, "pid", &pid, sizeof(pid));

    /* pagesize */
    int64_t pagesize = sysconf(_SC_PAGESIZE);
    if (pagesize < 1) pagesize = 0;
    PSP_putTypedMsgBuf(&msg, "pagesize", &pagesize, sizeof(pagesize));

    sendMsg(&msg);
}

void PSIDclient_delete(int fd)
{
    PSID_fdbg(fd < 0 ? -1 : PSID_LOG_CLIENT, "fd %d\n", fd);
    if (fd < 0) return;

    PStask_t *task = PSIDclient_getTask(fd);

    PSID_fdbg(PSID_LOG_CLIENT, "closing connection to %s\n",
	      task ? PSC_printTID(task->tid) : "<unknown>");
    closeConnection(fd);

    if (!task) {
	PSID_flog("task %s not found\n", PSC_printTID(PSIDclient_getTID(fd)));
	return;
    }

    if (task->group == TG_FORWARDER && !task->released) {

	PSID_flog("unreleased forwarder %s\n", PSC_printTID(task->tid));

	/* Tell logger about unreleased forwarders */
	DDErrorMsg_t msg = {
	    .header = {
		.type = PSP_CC_ERROR,
		.sender = task->tid,
		.dest = task->loggertid,
		.len = sizeof(msg.header) } };
	sendMsg(&msg);

	PStask_ID_t child;
	int sig = -1;
	while ((child = PSID_getSignal(&task->childList, &sig))) {
	    PStask_t *childTask = PStasklist_find(&managedTasks, child);
	    PSID_flog("kill child %s\n", PSC_printTID(child));

	    if (!childTask || childTask->forwarder != task) {
		/* Maybe the child's task was obsoleted */
		childTask = PStasklist_find(&obsoleteTasks, child);
		/* Still not the right childTask? */
		if (childTask && childTask->forwarder != task) childTask = NULL;
	    }

	    if (childTask) {
		/* Tell accounters about unreleased forwarder's client */
		sendAcctLost(task->tid, childTask);

		/* Since forwarder is gone eliminate all references */
		childTask->forwarder = NULL;

		/* Try to kill the child, again (obsolete childs are yet gone) */
		if (!childTask->obsolete) PSID_kill(-child, SIGKILL, 0);
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
	DDTypedBufferMsg_t msg = {
	    .header = {
		.type = PSP_CD_ACCOUNT,
		.sender = task->tid,
		.dest = PSC_getMyTID(),
		.len = 0 },
	    .type = (task->numChild > 0) ? PSP_ACCOUNT_END:PSP_ACCOUNT_DELETE};

	/* logger's TID identifies a task uniquely */
	PSP_putTypedMsgBuf(&msg, "TID", &task->tid, sizeof(task->tid));
	PSP_putTypedMsgBuf(&msg, "rank", &task->rank, sizeof(task->rank));
	PSP_putTypedMsgBuf(&msg, "UID", &task->uid, sizeof(task->uid));
	PSP_putTypedMsgBuf(&msg, "GID", &task->gid, sizeof(task->gid));

	if (task->numChild > 0) {
	    struct timeval now, walltime;

	    /* total number of children */
	    PSP_putTypedMsgBuf(&msg, "numChild", &task->numChild,
			       sizeof(task->numChild));

	    /* walltime used by logger */
	    gettimeofday(&now, NULL);
	    timersub(&now, &task->started, &walltime);
	    PSP_putTypedMsgBuf(&msg, "walltime", &walltime, sizeof(walltime));
	}

	sendMsg((DDMsg_t *)&msg);
    }

    /* Cleanup, if no forwarder available; otherwise wait for CHILDDEAD */
    if (!task->forwarder) {
	PSID_fdbg(PSID_LOG_TASK, "PSIDtask_cleanup()\n");
	PSIDtask_cleanup(task);
    }

    return;
}

int PSIDclient_getNum(bool admTasks)
{
    int cnt = 0;
    list_t *t;
    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);

	if (task->deleted) continue;
	if (task->group==TG_ADMIN || task->group==TG_FORWARDER) {
	    if (admTasks) cnt++;
	} else {
	    if (!admTasks) cnt++;
	}
    }
    return cnt;
}

int PSIDclient_killAll(int sig, bool killAdmTasks)
{
    int ret = 0;

    PSID_fdbg(PSID_LOG_CLIENT, "(%d, %d)\n", sig, killAdmTasks);

    /* loop over all tasks */
    list_t *t;
    list_for_each(t, &managedTasks) {
	PStask_t *task = list_entry(t, PStask_t, next);
	pid_t pid = PSC_getPID(task->tid);

	if (task->deleted) continue;
	if ((task->group==TG_ADMIN || task->group==TG_FORWARDER
	     || task->group==TG_PLUGINFW) && !killAdmTasks) continue;

	PSID_fdbg(PSID_LOG_CLIENT, "send %s to %s pid %d fd %d\n",
		  strsignal(sig), PSC_printTID(task->tid), pid, task->fd);

	if (pid > 0) {
	    if (sig == SIGKILL) kill(pid, SIGCONT);
	    kill(pid, sig);
	    ret++;
	}

	if (sig==SIGKILL && killAdmTasks && task->fd != -1) {
	    PSID_flog("PSIDclient_delete()\n");
	    PSIDclient_delete(task->fd);
	}
    }

    return ret;
}

void PSIDclient_releaseACK(int fd)
{
    if (!checkClient(fd)) return;

    clients[fd].pendingACKs--;

    if (!clients[fd].pendingACKs && list_empty(&clients[fd].msgs)) {
	/* Use the stop-hash to actually send SENDCONT msgs */
	int ret = PSIDFlwCntrl_sendContMsgs(clients[fd].stops, clients[fd].tid);
	PSID_fdbg(PSID_LOG_FLWCNTRL, "sent %d msgs\n", ret);
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

    PSID_fdbg(PSID_LOG_CLIENT, "from %s at fd %d: group=%s version=%d uid=%d\n",
	      PSC_printTID(tid), fd, PStask_printGrp(msg->group),
	      msg->version, uid);
    /*
     * first check if it is a reconnection
     * this might happen due to an exec() call.
     */
    PStask_t *task = PStasklist_find(&managedTasks, tid);
    if (!task) {
	PStask_ID_t pgtid = PSC_getTID(-1, getpgid(pid));

	task = PStasklist_find(&managedTasks, pgtid);

	if (msg->group == TG_ADMIN) {
	    /*
	     * psiadmin never forks. This is another psiadmin started
	     * from within a shell script. Forget about this task.
	     */
	    PSID_fdbg(PSID_LOG_CLIENT, "no reconnection since task is %s\n",
		      PStask_printGrp(msg->group));
	    task = NULL;
	}

	if (task && (task->group == TG_LOGGER || task->group == TG_ADMIN
		     || task->group == TG_ADMINTASK) ) {
	    /*
	     * Logger, psiadmin and admin-tasks never fork. This is
	     * another executable started from within a shell
	     * script. Forget about this task.
	     */
	    PSID_fdbg(PSID_LOG_CLIENT, "no reconnection since parent-task"
		      " is %s\n", PStask_printGrp(task->group));
	    task = NULL;
	}

	if (task) {
	    /* Spawned process has changed pid */
	    /* This might happen due to stuff in PSI_RARG_PRE_0 */
	    PStask_t *child = PStask_clone(task);

	    PSID_fdbg(PSID_LOG_CLIENT, "reconnection with changed PID"
		      "%d -> %d\n", PSC_getPID(task->tid), pid);

	    if (child) {
		child->tid = tid;
		child->duplicate = true;
		PStasklist_enqueue(&managedTasks, child);

		if (task->forwarder) {
		    /* Register new child to its forwarder */
		    PSID_setSignal(&task->forwarder->childList, child->tid, -1);
		} else {
		    PSID_flog("task %s has no forwarder\n",
			      PSC_printTID(task->tid));
		}

		/* We want to handle the reconnected child now */
		task = child;
	    }
	}
    }
    if (task) {
	/* re-connection */
	/* use the old task struct but close the old fd */
	PSID_fdbg(PSID_LOG_CLIENT, "reconnecting task %s, old/new fd = %d/%d\n",
		  PSC_printTID(task->tid), task->fd, fd);

	/* close the previous socket */
	if (task->fd > 0) {
	    closeConnection(task->fd);
	} else {
	    /* Remove the old selector created while accept()ing connection */
	    Selector_remove(fd);
	}
	task->fd = fd;
    } else {
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

	if (PSID_getDebugMask() & PSID_LOG_CLIENT) {
	    char tasktxt[128];
	    PStask_snprintf(tasktxt, sizeof(tasktxt), task);
	    PSID_flog("request from %s\n", tasktxt);
	}

	PStasklist_enqueue(&managedTasks, task);

	/* Tell everybody about the new task */
	incTaskCount(task->group==TG_ANY);

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
		    .len = DDTypedBufMsgOffset },
	.type = PSP_CONN_ERR_NONE,
	.buf = { 0 } };

    /* Connection refused answer message */
    if (msg->version < 346 || msg->version > PSProtocolVersion) {
	outmsg.type = PSP_CONN_ERR_VERSION;
	uint32_t protoV = PSProtocolVersion;
	PSP_putTypedMsgBuf(&outmsg, "protoV", &protoV, sizeof(protoV));
    } else if (!task) {
	outmsg.type = PSP_CONN_ERR_NOSPACE;
    } else if (uid && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_USER,
					  (PSIDnodes_guid_t){.u=uid})) {
	outmsg.type = PSP_CONN_ERR_UIDLIMIT;
    } else if (gid && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_GROUP,
					  (PSIDnodes_guid_t) {.g=gid})) {
	outmsg.type = PSP_CONN_ERR_GIDLIMIT;
    } else if (PSIDnodes_getProcs(PSC_getMyID()) != PSNODES_ANYPROC
	       && status.tasks.normal > PSIDnodes_getProcs(PSC_getMyID())) {
	outmsg.type = PSP_CONN_ERR_PROCLIMIT;
	int32_t maxProcs = PSIDnodes_getProcs(PSC_getMyID());
	PSP_putTypedMsgBuf(&outmsg, "maxProcs", &maxProcs, sizeof(maxProcs));
    } else if (PSID_getDaemonState() & PSID_STATE_NOCONNECT) {
	outmsg.type = PSP_CONN_ERR_STATENOCONNECT;
	PSID_flog("daemon state problems: state is %x\n", PSID_getDaemonState());
    }

    if (outmsg.type != PSP_CONN_ERR_NONE || msg->group == TG_RESET) {
	outmsg.header.type = PSP_CD_CLIENTREFUSED;

	PSID_fdbg(PSID_LOG_CLIENT, "connection refused: group %s task %s"
		  " version %d vs. %d uid %d gid %d jobs %d %d\n",
		  PStask_printGrp(msg->group), PSC_printTID(task->tid),
		  msg->version, PSProtocolVersion, uid, gid, status.tasks.normal,
		  PSIDnodes_getProcs(PSC_getMyID()));
	sendMsg(&outmsg);

	/* clean up */
	PSID_flog("PSIDclient_delete()\n");
	PSIDclient_delete(fd);

	if (msg->group == TG_RESET && !uid
	    && !(PSID_getDaemonState() & PSID_STATE_RESET)) PSID_reset();
    } else {
	PSIDclient_setEstablished(fd, handleClientMsg, NULL);
	task->protocolVersion = msg->version;

	bool mixed = PSID_mixedProto();
	PSP_putTypedMsgBuf(&outmsg, "mixedProto", &mixed, sizeof(mixed));
	PSnodes_ID_t myID = PSC_getMyID();
	PSP_putTypedMsgBuf(&outmsg, "myID", &myID, sizeof(myID));
	PSnodes_ID_t nrOfNodes = PSC_getNrOfNodes();
	PSP_putTypedMsgBuf(&outmsg, "nrOfNodes", &nrOfNodes, sizeof(nrOfNodes));
	char *instDir = PSC_lookupInstalldir(NULL);
	uint16_t dirLen = strlen(instDir);
	PSP_putTypedMsgBuf(&outmsg, "dirLen", &dirLen, sizeof(dirLen));
	PSP_putTypedMsgBuf(&outmsg, "instDir", instDir, dirLen);

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
 * @brief Handle a PSP_CC_MSG message
 *
 * Handle the message @a msg of type PSP_CC_MSG.
 *
 * This kind of message is used for communication between clients and
 * might have some internal types. In order to not break this type of
 * communication, a PSP_CC_ERROR message has to be passed to the
 * original sender, if something went wrong during passing the
 * original message towards its final destination.
 *
 * @param msg Pointer to the message to handle
 *
 * @return Always return true
 */
static bool msg_CC_MSG(DDBufferMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_CLIENT, "from %s", PSC_printTID(msg->header.sender));
    PSID_dbg(PSID_LOG_CLIENT, " to %s\n", PSC_printTID(msg->header.dest));

    if (msg->header.dest == PSC_getMyTID()) {
	PSID_flog("from %s to me?! Dropping...\n",
		  PSC_printTID(msg->header.sender));
	PSID_dropMsg(msg);
	return true;
    }

    /* Try to forward this message; might be dropped silently in sendMsg() */
    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	PSID_fdbg(PSID_LOG_CLIENT, "send failed\n");
    }
    return true;
}

/**
 * @brief Drop a PSP_CC_MSG message
 *
 * Drop the message @a msg of type PSP_CC_MSG.
 *
 * Since the sender might wait for an answer within a higher-level
 * protocol, a corresponding answer is created on this lower level to
 * send a hint that the original messages is dropped.
 *
 * @param msg Pointer to the message to drop
 *
 * @return Always return true
 */
static bool drop_CC_MSG(DDBufferMsg_t *msg)
{
    DDBufferMsg_t errmsg = {
	.header = {
	    .type = PSP_CC_ERROR,
	    .dest = msg->header.sender,
	    .sender = msg->header.dest,
	    .len = 0 } };
    /* include the PSLog header (plus some bytes) */
    PSP_putMsgBuf(&errmsg, "pslogHeader", msg, PSLog_headerSize);

    sendMsg(&errmsg);
    return true;
}

/**
 * @brief Init client structure
 *
 * Initialize the client structure @a client.
 *
 * @param client The client to initialize
 *
 * @return No return value
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
    PSIDFlwCntrl_init();
    PSIDMsgbuf_init();

    if (clients) {
	PSID_flog("already initialized\n");
	return;
    }

    long numFiles = sysconf(_SC_OPEN_MAX);
    if (numFiles <= 0) {
	PSID_exit(errno, "%s: sysconf(_SC_OPEN_MAX) returns %ld", __func__,
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

int PSIDclient_setMax(int max)
{
    int oldMax = maxClientFD;
    if (oldMax >= max) return 0; /* don't shrink */

    maxClientFD = max;
    size_t oldClients = (size_t)clients;
    client_t *newClients = realloc(clients, sizeof(*clients) * maxClientFD);
    if (!newClients) {
	PSID_fwarn(ENOMEM, "realloc()");
	maxClientFD = oldMax;
	errno = ENOMEM;
	return -1;
    }

    /* Restore old lists if necessary */
    if (newClients != clients) {
	for (int fd = 0; fd < oldMax; fd++) {
	    size_t oldClnt = oldClients + fd * sizeof(client_t);
	    void *oldHead = (void *)(oldClnt + offsetof(client_t, msgs));
	    list_fix(&newClients[fd].msgs, oldHead);
	    for (int h = 0; h < FLWCNTRL_HASH_SIZE; h++) {
		void *oldStop = (void *)(oldClnt + offsetof(client_t, stops)
					 + h * sizeof(PSIDFlwCntrl_hash_t));
		list_fix(&newClients[fd].stops[h], oldStop);
	    }
	}
    }
    /* Initialize new clients */
    for (int fd = oldMax; fd < maxClientFD; fd++) clientInit(&newClients[fd]);

    clients = newClients;

    return 0;
}

void PSIDclient_clearMem(void)
{
    /* Need to put MsgBufs to free() non-small messages */
    for (int fd = 0; fd < maxClientFD; fd++) {
	list_t *m, *tmp;
	list_for_each_safe(m, tmp, &clients[fd].msgs) {
	    PSIDmsgbuf_t *mp = list_entry(m, PSIDmsgbuf_t, next);

	    list_del(&mp->next);
	    PSIDMsgbuf_put(mp);
	}
    }

    free(clients);
    clients = NULL;
    maxClientFD = 0;
}
