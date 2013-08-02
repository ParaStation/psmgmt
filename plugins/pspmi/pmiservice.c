/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "pscommon.h"
#include "pluginmalloc.h"
#include "psprotocol.h"
#include "psidtask.h"
#include "psidforwarder.h"
#include "psipartition.h"

#include "pmiforwarder.h"
#include "pmilog.h"

#include "pmiservice.h"

#define MPIEXEC_BINARY BINDIR "/mpiexec"

/**
 * @brief Send environment
 *
 * Send the environment @a env using the message template @a msg. @a
 * msg has to have the destination address already filled in.
 *
 * This function might send several messages of both types,
 * PSP_SPAWN_ENV and PSP_SPAWN_ENVCNTD, depending on the size of @a
 * env as a whole and the single key-value pairs of the environment.
 *
 * @param msg Message template prepared to send the environment
 *
 * @param env The environment to send
 *
 * @param len Pointer to current length of @a msg's buffer.
 *
 * @return If an error occurred, -1 is return. On success this
 * function returns 0. In the latter case, @a msg will point to the
 * last message still to be sent to the destination. The current
 * length of the message's buffer is given back in @a len.
 */
static int sendEnv(DDTypedBufferMsg_t *msg, char **env, size_t *len)
{
    char *off = NULL;
    int num = 0;

    *len = 0;

    if (!env) return 0;

    msg->header.len = sizeof(msg->header) + sizeof(msg->type);

    while (1) {
	if (off) msg->type = PSP_SPAWN_ENVCNTD;

	*len = PStask_encodeEnv(msg->buf, sizeof(msg->buf), env, &num, &off);
	msg->header.len += *len;

	if (num == -1) {
	    /* last entry done */
	    if (msg->type == PSP_SPAWN_ENVCNTD) {
		if (sendDaemonMsg((DDMsg_t *) (msg))<0) {
		    return -1;
		}
		msg->header.len -= *len;
		*len = 0;
	    }
	    return 0;
	}

	if (sendDaemonMsg((DDMsg_t *) (msg))<0) {
	    return -1;
	}
	msg->header.len -= *len;
	msg->type = PSP_SPAWN_ENV;
    }

    elog("%s: Never be here\n", __func__);
    return -1;
}

/**
 * @brief Send argument-vector
 *
 * Send the argument-vector @a argv using the message template @a
 * msg. @a msg has to have the destination address already filled in.
 *
 * This function might send several messages of both types,
 * PSP_SPAWN_ARG and PSP_SPAWN_ARGCNTD, depending on the size of @a
 * argv as a whole and the single arguments.
 *
 * @param msg Message template prepared to send the argument-vector.
 *
 * @param argv The argument-vector to send
 *
 * @return If an error occurred, -1 is return. On success this
 * function returns 0.
 */
static int sendArgv(DDTypedBufferMsg_t *msg, char **argv)
{
    char *off = NULL;
    int num = 0, len;

    if (!argv) return 0;

    msg->header.len = sizeof(msg->header) + sizeof(msg->type);

    while (num != -1) {
	msg->type = (off) ? PSP_SPAWN_ARGCNTD : PSP_SPAWN_ARG;

	len = PStask_encodeArgv(msg->buf, sizeof(msg->buf), argv, &num, &off);

	msg->header.len += len;
	if (sendDaemonMsg((DDMsg_t *)(msg))<0) {
	    return -1;
	}
	msg->header.len -= len;
    }

    return 0;
}

/**
 * @brief Send task-structure
 *
 * Send the actual task structure stored in @a task using the message
 * template @a msg. @a msg has to have the destination address already
 * filled in.
 *
 * This function might send -- besides the initial PSP_SPAWN_TASK
 * message -- more messages containing trailing part of the task's
 * working-directory. The latter will use messages of type
 * PSP_SPAWN_WDIRCNTD.
 *
 * @param msg Message template prepared to send the task-structure.
 *
 * @param task The task to send.
 *
 * @return If an error occurs, -1 is returned. On success this
 * function returns 0.
 */
static int sendTask(DDTypedBufferMsg_t *msg, PStask_t *task)
{
    /* pack the task information in the msg */
    char *offset = NULL;
    size_t len = PStask_encodeTask(msg->buf, sizeof(msg->buf), task, &offset);

    msg->header.len += len;
    if (sendDaemonMsg((DDMsg_t *) msg)<0) {
	return -1;
    }
    msg->header.len -= len;

    while (offset) {
	msg->type = PSP_SPAWN_WDIRCNTD;
	if (strlen(offset) < sizeof(msg->buf)) {
	    /* tail fits into buffer */
	    strcpy(msg->buf, offset);
	    len = strlen(offset) + 1;
	    offset = NULL;
	} else {
	    /* buffer to small */
	    strncpy(msg->buf, offset, sizeof(msg->buf) - 1);
	    msg->buf[sizeof(msg->buf) - 1] = '\0';
	    len =  sizeof(msg->buf);
	    offset += sizeof(msg->buf) - 1;
	}

	msg->header.len += len;
	if (sendDaemonMsg((DDMsg_t *) msg)<0) {
	    return -1;
	}
	msg->header.len -= len;
    }

    return 0;
}

static int sendSpawnMessage(PStask_t *task)
{
    DDTypedBufferMsg_t msg;
    size_t len;

    /* send the spawn request */
    msg.header.type = PSP_CD_SPAWNREQ;
    msg.header.dest = PSC_getTID(PSC_getMyID(), 0);
    msg.header.sender = PSC_getMyTID();
    msg.header.len = sizeof(msg.header) + sizeof(msg.type);

    /* send actual task */
    msg.type = PSP_SPAWN_TASK;
    if (sendTask(&msg, task) < 0) {
	elog("%s: sending task failed\n", __func__);
	goto error;
    }

    /* send argv stuff */
    msg.type = PSP_SPAWN_ARG;
    if (sendArgv(&msg, task->argv) < 0) {
	elog("%s: sending argv failed\n", __func__);
	goto error;
    }

    /* send the static part of the environment */
    msg.type = PSP_SPAWN_ENV;
    if (sendEnv(&msg, task->environ, &len) < 0) {
	elog("%s: sending env failed\n", __func__);
	goto error;
    }

    /* send spawn end */
    msg.type = PSP_SPAWN_END;
    if (sendDaemonMsg((DDMsg_t *) (&msg))<0) {
	elog("%s: sending spawn end failed\n", __func__);
	goto error;
    }

    PStask_delete(task);

    return 1;

 error:
    PStask_delete(task);
    return 0;

}

int spawnService(char *np, char **c_argv, int c_argc, char **c_env, int c_envc,
		    char *wdir, char *nType, int usize, char *hostfile,
		    char *hosts, int serviceRank, char *kvsTmp)
{
    PStask_t *myTask, *task;
    int envc = 0, argc = 0, i;
    char *next, buffer[1024];

    /* TODO: how to specify the nodeType in spawn message? */

    if (!(myTask = getChildTask())) {
	elog("%s: cannot find my child's task structure\n", __func__);
	return 0;
    }

    if (!(task = PStask_new())) {
	elog("%s: cannot create a new task\n", __func__);
	return 0;
    }

    /* copy data from my task */
    task->uid = myTask->uid;
    task->gid = myTask->gid;
    task->aretty = myTask->aretty;
    task->loggertid = myTask->loggertid;
    task->ptid = myTask->tid;
    task->group = TG_KVS;
    task->rank = serviceRank -1;
    task->winsize = myTask->winsize;
    task->termios = myTask->termios;

    /* set work dir */
    if (wdir) {
	task->workingdir = wdir;
    } else if (myTask->workingdir) {
	task->workingdir = ustrdup(myTask->workingdir);
    } else {
	task->workingdir = NULL;
    }

    /* build arguments */
    snprintf(buffer, sizeof(buffer), "%d", usize);

    task->argc = c_argc + 5;
    task->argv = umalloc((task->argc + 1) * sizeof(char *));

    task->argv[argc++] = ustrdup(MPIEXEC_BINARY);
    task->argv[argc++] = ustrdup("-np");
    task->argv[argc++] = ustrdup(np);
    task->argv[argc++] = ustrdup("-u");
    task->argv[argc++] = ustrdup(buffer);

    for (i=0; i<c_argc; i++) {
	task->argv[argc++] = c_argv[i];
    }
    task->argv[argc] = NULL;

    /* build environment */
    for (envc=0; myTask->environ[envc]; envc++);

    task->environ = umalloc((envc + c_envc + 8  + 1) * sizeof(char *));
    i=0;
    for (envc=0; myTask->environ[envc]; envc++) {
	next = myTask->environ[envc];

	/* skip troublesome old env vars */
	if (!(strncmp(next, "__KVS_PROVIDER_TID=", 17))) continue;
	if (!(strncmp(next, "PMI_ENABLE_SOCKP=", 17))) continue;
	if (!(strncmp(next, "PMI_RANK=", 9))) continue;
	if (!(strncmp(next, "PMI_PORT=", 9))) continue;
	if (!(strncmp(next, "PMI_FD=", 7))) continue;
	if (!(strncmp(next, "PMI_KVS_TMP=", 12))) continue;

	task->environ[i] = ustrdup(myTask->environ[envc]);
	if (!myTask->environ[envc +1]) break;
	i++;
    }
    envc = i;

    for (i=0; i<c_envc; i++) {
	task->environ[envc++] = ustrdup(c_env[i]);
    }

    /* add additional env vars */
    task->environ[envc++] = ustrdup(kvsTmp);
    snprintf(buffer, sizeof(buffer), "__PMI_SPAWN_SERVICE_RANK=%i",
		serviceRank -2);
    task->environ[envc++] = ustrdup(buffer);
    snprintf(buffer, sizeof(buffer), "__PMI_SPAWN_PARENT=%i", PSC_getMyTID());
    task->environ[envc++] = ustrdup(buffer);
    task->environ[envc++] = ustrdup("SERVICE_KVS_PROVIDER=1");
    task->environ[envc++] = ustrdup("PMI_SPAWNED=1");
    snprintf(buffer, sizeof(buffer), "PMI_SIZE=%s", np);
    task->environ[envc++] = ustrdup(buffer);
    if (hosts) {
	snprintf(buffer, sizeof(buffer), "%s=%s", ENV_NODE_HOSTS, hosts);
	task->environ[envc++] = ustrdup(buffer);
	ufree(hosts);
    }
    if (hostfile) {
	snprintf(buffer, sizeof(buffer), "%s=%s", ENV_NODE_HOSTFILE, hostfile);
	task->environ[envc++] = ustrdup(buffer);
	ufree(hostfile);
    }

    task->environ[envc] = NULL;
    task->envSize = envc;

    return sendSpawnMessage(task);
}
