/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

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

#include "psidclient.h"

/* possible values of clients.flags */
#define INITIALCONTACT  0x00000001   /* No message yet (only accept()ed) */

static struct {
    PStask_ID_t tid;     /**< Clients task ID */
    PStask_t *task;      /**< Clients task structure */
    unsigned int flags;  /**< Special flags. Up to now only INITIALCONTACT */
    msgbuf_t *msgs;      /**< Chain of undelivered messages */
} clients[FD_SETSIZE];

void registerClient(int fd, PStask_ID_t tid, PStask_t *task)
{
    clients[fd].tid = tid;
    clients[fd].task = task;
    clients[fd].flags |= INITIALCONTACT;
    clients[fd].msgs = NULL;
}

PStask_ID_t getClientTID(int fd)
{
    return clients[fd].tid;
}

PStask_t *getClientTask(int fd)
{
    return clients[fd].task;
}

void setEstablishedClient(int fd)
{
    clients[fd].flags &= ~INITIALCONTACT;
}

int isEstablishedClient(int fd)
{
    return !(clients[fd].flags & INITIALCONTACT);
}

static int do_send(int fd, DDMsg_t *msg, int offset)
{
    int n, i;

    for (n=offset, i=1; (n<msg->len) && (i>0);) {
	i = send(fd, &(((char*)msg)[n]), msg->len-n, MSG_DONTWAIT);
	if (i<=0) {
	    switch (errno) {
	    case EINTR:
		break;
	    case EAGAIN:
		return n;
		break;
	    default:
		PSID_warn((errno==EPIPE) ? PSID_LOG_CLIENT : -1, errno,
			  "%s: error on socket %d", __func__, fd);
		deleteClient(fd);
		return i;
	    }
	} else
	    n+=i;
    }
    return n;
}

static int storeMsgClient(int fd, DDMsg_t *msg, int offset)
{
    msgbuf_t *msgbuf = clients[fd].msgs;

    if (msgbuf) {
	/* Search for end of list */
	while (msgbuf->next) msgbuf = msgbuf->next;
	msgbuf->next = getMsg();
	msgbuf = msgbuf->next;
    } else {
	msgbuf = clients[fd].msgs = getMsg();
    }

    if (!msgbuf) {
	errno = ENOMEM;
	return -1;
    }

    msgbuf->msg = malloc(msg->len);
    if (!msgbuf->msg) {
	errno = ENOMEM;
	return -1;
    }
    memcpy(msgbuf->msg, msg, msg->len);

    msgbuf->offset = offset;

    return 0;
}

int flushClientMsgs(int fd)
{
    if (fd<0 || fd >= FD_SETSIZE) {
	errno = EINVAL;
	return -1;
    }

    while (clients[fd].msgs) {
	msgbuf_t *msgbuf = clients[fd].msgs;
	DDMsg_t *msg = msgbuf->msg;
	PStask_ID_t sender = msg->sender, dest = msg->dest;
	int sent = do_send(fd, msg, msgbuf->offset);

	if (sent<0 || !clients[fd].msgs) return sent;
	if (sent != msg->len) {
	    msgbuf->offset = sent;
	    break;
	}

	if (PSC_getPID(sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = dest,
				.dest = sender,
				.len = sizeof(DDMsg_t) };
	    sendMsg(&contmsg);
	}

	clients[fd].msgs = msgbuf->next;
	freeMsg(msgbuf);
    }

    if (clients[fd].msgs) {
	errno = EWOULDBLOCK;
	return -1;
    }

    return 0;
}

int sendClient(DDMsg_t *msg)
{
    PStask_t *task = PStasklist_find(managedTasks, msg->dest);
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
	PSID_log(task ? -1 : PSID_LOG_CLIENT,
		 "%s: no fd for task %s to send\n", __func__,
		 PSC_printTID(msg->dest));
	errno = EHOSTUNREACH;
	return -1;
    }
    fd = task->fd;

    if (clients[fd].msgs) flushClientMsgs(fd);

    if (!clients[fd].msgs) {
	PSID_log(PSID_LOG_CLIENT, "%s: use fd %d\n", __func__, fd);
	sent = do_send(fd, msg, 0);
    }

    if (sent<0) return sent;
    if (sent != msg->len) {
	if (!storeMsgClient(fd, msg, sent)) {
	    FD_SET(fd, &PSID_writefds);
	    errno = EWOULDBLOCK;
	}
	return -1;
    }

    return sent;
}

/* @todo This will handle different client versions */
int recvInitialMsg(int fd, DDInitMsg_t *msg, size_t size)
{
    return 0;
}

/* @todo we need to timeout if message to small */
int recvClient(int fd, DDMsg_t *msg, size_t size)
{
    int n;
    int count = 0;

    if (!isEstablishedClient(fd)) {
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


void closeConnection(int fd)
{
    if (fd<0) {
	PSID_log(-1, "%s(%d): fd < 0\n", __func__, fd);
	return;
    }

    clients[fd].tid = -1;
    if (clients[fd].task) clients[fd].task->fd = -1;
    clients[fd].task = NULL;
    while (clients[fd].msgs) {
	msgbuf_t *mp = clients[fd].msgs;

	clients[fd].msgs = clients[fd].msgs->next;
	if (PSC_getPID(mp->msg->sender)) {
	    DDMsg_t contmsg = { .type = PSP_DD_SENDCONT,
				.sender = mp->msg->dest,
				.dest = mp->msg->sender,
				.len = sizeof(DDMsg_t) };
	    sendMsg(&contmsg);
	}
	handleDroppedMsg(mp->msg);
	freeMsg(mp);
    }

    shutdown(fd, SHUT_RDWR);
    close(fd);

    FD_CLR(fd, &PSID_readfds);
    FD_CLR(fd, &PSID_writefds);
}

void deleteClient(int fd)
{
    PStask_t *task;
    PStask_ID_t tid;

    if (fd<0) {
	PSID_log(-1, "%s(%d): fd < 0\n", __func__, fd);
	return;
    }

    PSID_log(PSID_LOG_CLIENT, "%s(%d)\n", __func__, fd);

    tid = clients[fd].tid;
    closeConnection(fd);

    if (tid==-1) return;

    task = PStasklist_find(managedTasks, tid);
    if (!task) {
	PSID_log(-1, "%s: Task %s not found\n", __func__, PSC_printTID(tid));
	return;
    }

    /* Tell logger about unreleased forwarders */
    if (task->group == TG_FORWARDER && !task->released) {
	DDMsg_t msg;

	PSID_log(-1, "%s: Unreleased forwarder %s\n",
		 __func__, PSC_printTID(tid));

	msg.type = PSP_CC_ERROR;
	msg.dest = task->loggertid;
	msg.sender = task->tid;
	msg.len = sizeof(msg);
	sendMsg(&msg);
    }

    /* Deregister TG_(PSC)SPAWNER from parent process */
    if (task->group == TG_SPAWNER || task->group == TG_PSCSPAWNER) {
	PStask_t *parent = PStasklist_find(managedTasks, task->ptid);

	if (parent) {
	    /* Remove dead spawner from list of childs */
	    PSID_removeSignal(&parent->childs, tid, -1);

	    if (parent->removeIt && !parent->childs) {
		PSID_log(PSID_LOG_TASK,
			 "%s: PStask_cleanup(parent)\n", __func__);
		PStask_cleanup(parent->tid);
	    }
	}
    }

    /* Deregister TG_ACCOUNT */
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

	msg.type = (task->nextRank < 1) ? PSP_ACCOUNT_DELETE : PSP_ACCOUNT_END;
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

	if (task->nextRank > 0) {
	    struct timeval now, walltime;

	    /* total number of childs */
	    *(int32_t *)ptr = task->nextRank;
	    ptr += sizeof(int32_t);
	    msg.header.len += sizeof(int32_t);

	    /* walltime used by logger */
	    gettimeofday(&now, NULL);
	    timersub(&now, &task->started, &walltime);
	    memcpy(ptr, &walltime, sizeof(walltime));
	    ptr += sizeof(walltime);
	    msg.header.len += sizeof(walltime);
	}

	sendMsg((DDMsg_t *)&msg);
    }

    PSID_log(PSID_LOG_CLIENT, "%s: closing connection to %s\n",
	     __func__, PSC_printTID(tid));

    PSID_log(PSID_LOG_TASK, "%s: PStask_cleanup()\n", __func__);
    PStask_cleanup(tid);

    return;
}

int killAllClients(int sig, int killAdminTasks)
{
    PStask_t *task;
    int ret = 0;

    PSID_log(PSID_LOG_CLIENT, "%s(%d, %d)\n", __func__, sig, killAdminTasks);

    /* loop over all tasks */
    for (task=managedTasks; task; task=task->next) {
	pid_t pid = PSC_getPID(task->tid);

	if (task->deleted) continue;
	if (task->group==TG_MONITOR) continue;
	if ((task->group==TG_ADMIN || task->group==TG_FORWARDER)
	    && !killAdminTasks) continue;

	PSID_log(PSID_LOG_CLIENT, "%s: sending %s to %s pid %d fd %d\n",
		 __func__, sys_siglist[sig],
		 PSC_printTID(task->tid), pid, task->fd);

	if (pid > 0) {
	    kill(pid, sig);
	    ret++;
	}

	if (sig==SIGKILL && killAdminTasks && task->fd>=0) {
	    deleteClient(task->fd);
	}
    }

    return ret;
}

pid_t getpgid(pid_t); /* @todo HACK HACK HACK */

/**
 * @brief Handle a PSP_CD_CLIENTCONNECT message.
 *
 * Handle the message @a bufmsg of type PSP_CD_CLIENTCONNECT. For
 * compatibility the file-descriptor @a fd the message was received
 * from is not passed explicitely. Instead this file-descriptor has to
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
static void msg_CLIENTCONNECT(DDBufferMsg_t *bufmsg)
{
    size_t off = bufmsg->header.len - sizeof(bufmsg->header);
    int fd = *(int *) (bufmsg->buf + off);
    DDInitMsg_t *msg = (DDInitMsg_t *)bufmsg;

    PStask_t *task;
    DDTypedBufferMsg_t outmsg;
    PSID_NodeStatus_t status;
    pid_t pid;
    uid_t uid;
    gid_t gid;
    PStask_ID_t tid;

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
    tid = PSC_getTID(-1, pid);

    PSID_log(PSID_LOG_CLIENT,
	     "%s: from %s at fd %d, group=%s, version=%d, uid=%d\n",
	     __func__, PSC_printTID(tid), fd, PStask_printGrp(msg->group),
	     msg->version, uid);
    /*
     * first check if it is a reconnection
     * this can happen due to a exec call.
     */
    task = PStasklist_find(managedTasks, tid);
    if (!task && msg->group != TG_SPAWNER && msg->group != TG_PSCSPAWNER) {
	PStask_ID_t pgtid = PSC_getTID(-1, getpgid(pid));

	task = PStasklist_find(managedTasks, pgtid);

	if (task && (task->group == TG_LOGGER || task->group == TG_ADMIN)) {
	    /*
	     * Logger never fork. This is another executable started from
	     * within a shell script. Forget about this task.
	     */
	    task = NULL;
	}

	if (task) {
	    /* Spawned process has changed pid */
	    /* This might happen due to stuff in PSI_RARG_PRE_0 */
	    PStask_t *child = PStask_clone(task);

	    if (child) {
		child->tid = tid;
		child->duplicate = 1;
		PStasklist_enqueue(&managedTasks, child);

		if (task->forwardertid) {
		    PStask_t *forwarder = PStasklist_find(managedTasks,
							  task->forwardertid);
		    if (forwarder) {
			/* Register new child to its forwarder */
			PSID_setSignal(&forwarder->childs, child->tid, -1);
		    } else {
			PSID_log(-1, "%s: forwarder %s not found\n",
				 __func__, PSC_printTID(task->forwardertid));
		    }
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
	/* reconnection */
	/* use the old task struct but close the old fd */
	PSID_log(PSID_LOG_CLIENT, "%s: reconnection, old/new fd = %d/%d\n",
		 __func__, task->fd, fd);

	/* close the previous socket */
	if (task->fd > 0) {
	    closeConnection(task->fd);
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

	    parent = PStasklist_find(managedTasks, ptid);

	    if (parent) {
		/* register the child */
		PSID_setSignal(&parent->childs, task->tid, -1);

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
    }

    registerClient(fd, tid, task);

    /*
     * Get the number of processes
     */
    status = getStatusInfo(PSC_getMyID());

    /*
     * Reject or accept connection
     */
    outmsg.header.type = PSP_CD_CLIENTESTABLISHED;
    outmsg.header.dest = tid;
    outmsg.header.sender = PSC_getMyTID();
    outmsg.header.len = sizeof(outmsg.header) + sizeof(outmsg.type);

    outmsg.type = PSP_CONN_ERR_NONE;

    /* Connection refused answer message */
    if (msg->version < 324 || msg->version > PSProtocolVersion) {
	outmsg.type = PSP_CONN_ERR_VERSION;
	*(uint32_t *)outmsg.buf = PSProtocolVersion;
	outmsg.header.len += sizeof(uint32_t);
    } else if (!task) {
	outmsg.type = PSP_CONN_ERR_NOSPACE;
    } else if (uid && !PSIDnodes_testGUID(PSC_getMyID(),PSIDNODES_USER,
					  (PSIDnodes_guid_t){.u=uid})) {
	outmsg.type = PSP_CONN_ERR_UIDLIMIT;
    } else if (gid && !PSIDnodes_testGUID(PSC_getMyID(), PSIDNODES_GROUP,
					  (PSIDnodes_guid_t) {.g=gid})) {
	outmsg.type = PSP_CONN_ERR_GIDLIMIT;
    } else if (PSIDnodes_getProcs(PSC_getMyID()) !=  PSNODES_ANYPROC
	       && status.jobs.normal > PSIDnodes_getProcs(PSC_getMyID())) {
	outmsg.type = PSP_CONN_ERR_PROCLIMIT;
	*(int *)outmsg.buf = PSIDnodes_getProcs(PSC_getMyID());
	outmsg.header.len += sizeof(int);
    } else if (PSID_getDaemonState() & PSID_STATE_NOCONNECT) {
	outmsg.type = PSP_CONN_ERR_STATENOCONNECT;
	PSID_log(-1, "%s: daemon state problems: state is %x\n",
		 __func__, PSID_getDaemonState());
    }

    if ((outmsg.type != PSP_CONN_ERR_NONE) || (msg->group == TG_RESET)) {
	outmsg.header.type = PSP_CD_CLIENTREFUSED;

	PSID_log(PSID_LOG_CLIENT, "%s: connection refused:"
		 "group %s task %s version %d vs. %d uid %d gid %d"
		 " jobs %d %d\n",
		 __func__, PStask_printGrp(msg->group),
		 PSC_printTID(task->tid), msg->version, PSProtocolVersion,
		 uid, gid,
		 status.jobs.normal, PSIDnodes_getProcs(PSC_getMyID()));

	sendMsg(&outmsg);

	/* clean up */
	deleteClient(fd);

	if (msg->group==TG_RESET && !uid) PSID_reset();
    } else {
	setEstablishedClient(fd);
	task->protocolVersion = msg->version;

	outmsg.type = PSC_getMyID();

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
    PSID_log(PSID_LOG_CLIENT, "to %s", PSC_printTID(msg->header.dest));

    /* Forward this message. If this fails, send an error message. */
    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
	PStask_ID_t temp = msg->header.dest;
	PSID_log(PSID_LOG_CLIENT, "failed");

	msg->header.type = PSP_CC_ERROR;
	msg->header.dest = msg->header.sender;
	msg->header.sender = temp;
	msg->header.len = sizeof(msg->header);

	sendMsg(msg);
    }
    PSID_log(PSID_LOG_CLIENT, "\n");
}

void initClients(void)
{
    int fd;

    for (fd=0; fd<FD_SETSIZE; fd++) {
	clients[fd].tid = -1;
	clients[fd].task = NULL;
	clients[fd].flags = 0;
	clients[fd].msgs = NULL;
    }

    PSID_registerMsg(PSP_CD_CLIENTCONNECT, msg_CLIENTCONNECT);
    PSID_registerMsg(PSP_CC_MSG, msg_CC_MSG);
}
