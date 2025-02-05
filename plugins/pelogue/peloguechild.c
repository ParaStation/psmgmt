/*
 * ParaStation
 *
 * Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "peloguechild.h"

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "list.h"
#include "pscommon.h"
#include "psenv.h"
#include "psprotocol.h"
#include "psserial.h"

#include "psidhook.h"
#include "psidsignal.h"

#include "pluginforwarder.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "psaccounthandles.h"

#include "peloguecomm.h"
#include "pelogueconfig.h"
#include "pelogueforwarder.h"
#include "peloguelog.h"

/** List of all children */
static LIST_HEAD(childList);

char *childType2String(PElogueType_t type)
{
    switch (type) {
    case PELOGUE_PROLOGUE:
	return "PROLOGUE";
    case PELOGUE_EPILOGUE:
	return "EPILOGUE";
    default:
	return NULL;
    }
}

static char *rootHome = NULL;

static bool setRootHome(void)
{
    char *pwBuf = NULL;
    struct passwd *spasswd = PSC_getpwnamBuf("root", &pwBuf);
    if (!spasswd) {
	mwarn(errno, "%s: getpwnam(root)", __func__);
	return false;
    }

    rootHome = ustrdup(spasswd->pw_dir);
    free(pwBuf);
    return true;
}

static char *hostName = NULL;

static bool setHostName(void)
{
    struct utsname utsBuf;

    if (uname(&utsBuf) < 0) {
	mwarn(errno, "%s: uname()", __func__);
	return false;
    }
    hostName = ustrdup(utsBuf.nodename);
    return true;
}


PElogueChild_t *addChild(char *plugin, char *jobid, PElogueType_t type)
{
    PElogueChild_t *child = malloc(sizeof(*child));

    if (child) {
	if ((!rootHome && !setRootHome()) || (!hostName && !setHostName())) {
	    free(child);
	    return NULL;
	}

	child->plugin = plugin;
	child->jobid = jobid;
	child->type = type;
	child->mainPElogue = -1;
	child->tmpDir = NULL;
	child->rootHome = rootHome;
	child->hostName = hostName;
	child->timeout = 0;
	child->env = envNew(NULL);
	child->uid = 0;
	child->gid = 0;
	child->startTime = 0;
	child->fwData = NULL;
	child->argv = NULL;
	child->signalFlag = 0;
	child->exit = 0;

	list_add_tail(&child->next, &childList);
    }
    return child;
}

PElogueChild_t *findChild(const char *plugin, const char *jobid)
{
    list_t *c;
    if (!plugin || !jobid) return NULL;
    list_for_each(c, &childList) {
	PElogueChild_t *child = list_entry(c, PElogueChild_t, next);

	if (!strcmp(child->plugin, plugin) && !strcmp(child->jobid, jobid)) {
	    return child;
	}
    }
    return NULL;
}

/** Some counters for basic pelogue statistics */
static struct {
    int locProSucc;
    int locProFail;
    int remProSucc;
    int remProFail;
    int locEpiSucc;
    int locEpiFail;
    int remEpiSucc;
    int remEpiFail;
} PEstat = { 0, 0, 0, 0, 0, 0, 0, 0 };

static void updateStatistics(PElogueChild_t *child)
{
    if (child->type == PELOGUE_PROLOGUE) {
	if (child->mainPElogue == PSC_getMyID()) {
	    if (child->exit) PEstat.locProFail++; else PEstat.locProSucc++;
	} else {
	    if (child->exit) PEstat.remProFail++; else PEstat.remProSucc++;
	}
    } else {
	if (child->mainPElogue == PSC_getMyID()) {
	    if (child->exit) PEstat.locEpiFail++; else PEstat.locEpiSucc++;
	} else {
	    if (child->exit) PEstat.remEpiFail++; else PEstat.remEpiSucc++;
	}
    }
}

static void manageTempDir(PElogueChild_t *child, bool create)
{
    char *confTmpDir = getPluginConfValueC(child->plugin, "DIR_TEMP");
    char tmpDir[PATH_MAX];
    struct stat statbuf;

    if (!confTmpDir || !strlen(confTmpDir)) return;

    snprintf(tmpDir, sizeof(tmpDir), "%s/%s", confTmpDir, child->jobid);

    if (create) {
	if (stat(tmpDir, &statbuf) == -1) {
	    if (mkdir(tmpDir, S_IRWXU) == -1) {
		mdbg(PELOGUE_LOG_WARN, "%s: mkdir (%s): %s\n", __func__,
		     tmpDir, strerror(errno));
	    } else {
		if (chown(tmpDir, child->uid, child->gid) == -1) {
		    mwarn(errno, "%s: chown(%s)", __func__, tmpDir);
		}
		child->tmpDir = ustrdup(tmpDir);
	    }
	}
    } else if (child->tmpDir) {
	/* delete temp directory in epilogue */
	removeDir(child->tmpDir, true);
	free(child->tmpDir);
	child->tmpDir = NULL;
    }
}

static void fwCallback(int32_t forwStatus, Forwarder_Data_t *fwData)
{
    PElogueChild_t *child = fwData->userData;
    int exitStatus = 1;

    if (!child) return;

    if (fwData->exitRcvd) {
	int pelogueStatus = fwData->chldExitStatus;
	if (WIFEXITED(pelogueStatus)) {
	    exitStatus = WEXITSTATUS(pelogueStatus);
	} else if (WIFSIGNALED(pelogueStatus)) {
	    exitStatus = WTERMSIG(pelogueStatus) + 0x100;
	}
    } else if (WIFEXITED(forwStatus)) {
	exitStatus = WEXITSTATUS(forwStatus);
    } else if (WIFSIGNALED(forwStatus)) {
	exitStatus = WTERMSIG(forwStatus) + 0x100;
    }

    /* let other plugins get information about completed pelogue */
    child->exit = exitStatus;
    PSIDhook_call(PSIDHOOK_PELOGUE_FINISH, child);

    updateStatistics(child);

    if (child->type == PELOGUE_PROLOGUE && child->exit) {
	/* delete temp directory in epilogue or if prologue failed */
	manageTempDir(child, false);
    }

    flog("local %s exit %i job %s to node %d\n",
	 child->type == PELOGUE_PROLOGUE ? "prologue" : "epilogue", child->exit,
	 child->jobid, child->mainPElogue);

    /* send result to mother superior */
    sendPElogueFinish(child);

    /* cleanup */
    if (!deleteChild(child)) {
	flog("deleting child '%s' failed\n", fwData->jobID);
    }
    /* fwData will be cleaned up within pluginforwarder */
}

/**
 * @brief Provides hook PSIDHOOK_PELOGUE_OE
 *
 * Provides hook PSIDHOOK_PELOGUE_OE for psslurm
 * to handle stdout and stderr streams of the prologue/epilogue.
 */
static void handlePeIO(Forwarder_Data_t *fwdata, PElogue_OEtype_t type,
		       DDTypedBufferMsg_t *msg)
{
    PElogue_OEdata_t oeData = {
	.type = type,
	.child = fwdata->userData,
    };

    /* read message */
    PS_DataBuffer_t data;
    initPSDataBuffer(&data, msg->buf, msg->header.len - DDTypedBufMsgOffset);
    oeData.msg = getStringM(&data);

    /* hook to forward STDOUT/STDERR to psslurm */
    PSIDhook_call(PSIDHOOK_PELOGUE_OE, &oeData);

    ufree(oeData.msg);
}

/**
 * @brief Handle messages form pelogue forwarder
 */
static bool handlePeFwMsg(DDTypedBufferMsg_t *msg, Forwarder_Data_t *fwdata)
{
    if (msg->header.type != PSP_PF_MSG) return false;

    switch (msg->type) {
    case PLGN_STDOUT:
	handlePeIO(fwdata, PELOGUE_OE_STDOUT, msg);
	break;
    case PLGN_STDERR:
	handlePeIO(fwdata, PELOGUE_OE_STDERR, msg);
	break;
    default:
	flog("unexpected msg, type %d from TID %s (%s) jobid %s\n", msg->type,
	     PSC_printTID(msg->header.sender), fwdata->pTitle, fwdata->jobID);
	return false;
    }

    return true;
}

void startChild(PElogueChild_t *child)
{
    char ctype[20], fname[100];
    bool prlg = child->type == PELOGUE_PROLOGUE;
    bool frntnd = child->mainPElogue == PSC_getMyID();

    /* create/destroy temp dir */
    manageTempDir(child, prlg);

    child->fwData = ForwarderData_new();
    snprintf(fname, sizeof(fname), "pelogue-%s", child->plugin);
    child->fwData->pTitle = ustrdup(fname);
    child->fwData->jobID = ustrdup(child->jobid);
    child->fwData->userData = child;
    child->fwData->graceTime = 3;
    child->fwData->killSession = psAccountSignalSession;
    child->fwData->callback = fwCallback;
    child->fwData->childFunc = execPElogueScript;
    child->fwData->timeoutChild = child->timeout;
    child->fwData->handleFwMsg = handlePeFwMsg;
    child->fwData->fwChildOE = child->fwStdOE;

    snprintf(ctype, sizeof(ctype), "%s %s", frntnd ? "local" : "remote",
	     prlg ? "prologue" : "epilogue");

    if (!startForwarder(child->fwData)) {
	flog("exec %s-script failed\n", ctype);

	child->exit = -2;

	/* let other plugins get information about completed pelogue */
	PSIDhook_call(PSIDHOOK_PELOGUE_FINISH, child);

	sendPElogueFinish(child);

	ForwarderData_delete(child->fwData);
	deleteChild(child);

	return;
    }

    mdbg(PELOGUE_LOG_PROCESS, "%s: %s for job %s:%s started\n", __func__,
	 ctype, child->plugin, child->jobid);
}

void signalChild(PElogueChild_t *child, int signal, char *reason)
{
    /* forwarder did not start yet */
    if (!child || !child->fwData) {
	flog("no child or forwarder to signal\n");
	return;
    }

    /* save the signal we are about to send */
    if (signal == SIGTERM || signal == SIGKILL) {
	child->signalFlag = signal;
    }

    /* send the signal */
    Forwarder_Data_t *fwData = child->fwData;
    if (fwData->cSid > 0) {
	flog("signal %i to pelogue '%s' - reason '%s' - sid %i\n", signal,
	     child->jobid, reason, fwData->cSid);
	fwData->killSession(fwData->cSid, signal);
    } else if (fwData->cPid > 0) {
	flog("signal %i to pelogue '%s' - reason '%s' - pid %i\n", signal,
	     child->jobid, reason, fwData->cPid);
	pskill(fwData->cPid, signal, child->uid);
    } else if ((signal == SIGTERM || signal == SIGKILL) && fwData->tid != -1) {
	pskill(PSC_getPID(fwData->tid), SIGTERM, child->uid);
    } else {
	flog("invalid forwarder data for signal %i to job '%s'\n", signal,
	     child->jobid);
    }
}

bool deleteChild(PElogueChild_t *child)
{
    if (!child) return false;

    free(child->plugin);
    free(child->jobid);
    free(child->tmpDir);
    envDestroy(child->env);
    if (child->fwData) {
	/* detach from forwarder */
	child->fwData->callback = NULL;
	child->fwData->userData = NULL;
    }
    if (child->argv) {
	for (int i = 0; child->argv[i]; i++) free(child->argv[i]);
	free(child->argv);
    }

    list_del(&child->next);
    free(child);

    return true;
}

void clearChildList(void)
{
    list_t *c, *tmp;
    list_for_each_safe(c, tmp, &childList) {
	PElogueChild_t *child = list_entry(c, PElogueChild_t, next);
	if (child->fwData && child->fwData->killSession) {
	    child->fwData->killSession(child->fwData->cSid, SIGKILL);
	}
	deleteChild(child);
    }
    free(rootHome);
    rootHome = NULL;
    free(hostName);
    hostName = NULL;
}

char *printChildStatistics(strbuf_t buf)
{
    strbufAdd(buf, "\nprologue statistics (success/failed):\n");

    char line[160];
    snprintf(line, sizeof(line), "\tlocal: (%d/%d)\n",
	     PEstat.locProSucc, PEstat.locProFail);
    strbufAdd(buf, line);
    snprintf(line, sizeof(line), "\tremote: (%d/%d)\n\n",
	     PEstat.remProSucc, PEstat.remProFail);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "epilogue statistics (success/failed):\n");
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "\tlocal: (%d/%d)\n",
	     PEstat.locEpiSucc, PEstat.locEpiFail);
    strbufAdd(buf, line);
    snprintf(line, sizeof(line), "\tremote: (%d/%d)\n\n",
	     PEstat.remEpiSucc, PEstat.remEpiFail);
    strbufAdd(buf, line);

    return strbufSteal(buf);
}
