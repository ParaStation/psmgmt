/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/time.h>

#include "list.h"
#include "logging.h"
#include "timer.h"

#include "plugin.h"
#include "psidutil.h"
#include "psidplugin.h"

#include "slurmmsg.h"
#include "slurmerrno.h"
#include "psslurmhandles.h"
#include "psslurmmsg.h"
#include "psslurmtypes.h"

int requiredAPI = 107;

char name[] = "delaySlurmMsg";

int version = 100;

plugin_dep_t dependencies[] = {
    { "psslurm", 115 },
    { NULL, 0 } };

#define nlog(...) if (PSID_logger) logger_funcprint(PSID_logger, name,	\
						    -1, __VA_ARGS__)


static struct timeval timeout = {0, 100*1000}; // 100 msec

#define MSG_TYPE REQUEST_LAUNCH_TASKS

static slurmdHandlerFunc_t origHandler = NULL;

typedef struct {
    list_t next;
    Slurm_Msg_t *msg;
} msgContainer_t;

static LIST_HEAD(msgList);

static int delayTimer = -1;

void releaseMsgs(void)
{
    list_t *m, *tmp;

    if (delayTimer > -1) {
	Timer_remove(delayTimer);
	delayTimer = -1;
    }
    nlog("%s now\n", __func__);

    /* pass steps to handler */
    list_for_each_safe(m, tmp, &msgList) {
	msgContainer_t *mCnt = list_entry(m, msgContainer_t, next);
	if (mCnt->msg && origHandler) origHandler(mCnt->msg);
	list_del(&mCnt->next);
	if (mCnt->msg) psSlurmReleaseMsg(mCnt->msg);
	free(mCnt);
    }
}

static int delaySlurmMsg(Slurm_Msg_t *sMsg)
{
    if (delayTimer == -1) delayTimer = Timer_register(&timeout, releaseMsgs);

    if (delayTimer == -1) {
	nlog("cannot delay message, deliver immediately\n");
	if (origHandler) origHandler(sMsg);
    } else {
	msgContainer_t *mCnt = malloc(sizeof(*mCnt));
	mCnt->msg = psSlurmDupMsg(sMsg);
	list_add_tail(&mCnt->next, &msgList);
	nlog("delay message of type %d by %ld msec\n", MSG_TYPE,
	     1000 * timeout.tv_sec +  timeout.tv_usec / 1000);
    }
    return SLURM_NO_RC;
}

#define getHandle(pHandle, symbol)		\
    symbol = dlsym(pHandle, #symbol);					\
    if (!symbol) {							\
	nlog(#symbol "() not found\n");					\
	return 1;							\
    }

int initialize(FILE *logfile)
{
    void *handle = PSIDplugin_getHandle("psslurm");

    /* get psslurm function handles */
    if (!handle) {
	nlog("getting psslurm handle failed\n");
	return 1;
    }
    getHandle(handle, psSlurmRegMsgHandler);
    getHandle(handle, psSlurmClrMsgHandler);
    getHandle(handle, psSlurmDupMsg);
    getHandle(handle, psSlurmReleaseMsg);

    origHandler = psSlurmRegMsgHandler(MSG_TYPE, delaySlurmMsg);

    return 0;
}


void finalize(void)
{
    if (origHandler) {
	psSlurmRegMsgHandler(MSG_TYPE, origHandler);
	origHandler = NULL;
    } else {
	psSlurmClrMsgHandler(MSG_TYPE);
    }

    PSIDplugin_unload(name);
}

char * help(char *key)
{
    char *helpText = "\tDelay certain messages within psslurm.\n"
	"\tAfter removing this plugin shall be handled immediately again\n"
	"\tas expected.\n";

    return strdup(helpText);
}

char * show(char *key)
{
    char showTxt[128];

    snprintf(showTxt, sizeof(showTxt), "\tdelay is %ld msec\n",
	     1000 * timeout.tv_sec +  timeout.tv_usec / 1000);

    return strdup(showTxt);
}

char * set(char *key, char *val)
{
    char l[128];
    if (!strcmp(key, "delay")) {
	int delay;

	if (sscanf(val, "%i", &delay) != 1) {
	    snprintf(l, sizeof(l), "\ndelay '%s' not a number\n", val);
	} else {
	    timeout.tv_sec = delay/1000;
	    timeout.tv_usec = (delay % 1000) * 1000;
	    snprintf(l, sizeof(l), "\tdelay now %ld msec\n",
		     1000 * timeout.tv_sec +  timeout.tv_usec / 1000);
	}
    } else {
	snprintf(l, sizeof(l), "\nInvalid key '%s' for cmd set:"
		 " use 'plugin help delaySlurmMsg' for help.\n", key);
    }

    return strdup(l);
}
