/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "psaccountproc.h"
#include "psaccountlog.h"

#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "pslog.h"
#include "psserial.h"
#include "pscommon.h"
#include "psidcomm.h"

#include "psaccountscript.h"

typedef enum {
    CMD_SET_POLL_TIME = 100,
} FW_Cmds_t;

/**
 * @brief Parse stdout/stderr from collect script
 *
 * @param msg The message to parse
 *
 * @param fwdata Structure holding all forwarder information
 *
 * @return Returns 1 on success otherwise 0 is returned
 */
static int handleFwMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata)
{
    Collect_Script_t *script = fwdata->userData;
    char *ptr = msg->buf, *data;

    switch (msg->type) {
	case STDOUT:
	    data = getStringM(&ptr);
	    script->func(data);
	    ufree(data);
	    break;
	    data = getStringM(&ptr);
	    mlog("%s: error from %s script: %s\n", __func__, fwdata->pTitle,
		 data);
	    ufree(data);
	    break;
	default:
	    mlog("%s: unhandled msg type %d\n", __func__, msg->type);
	    return 0;
    }

    return 1;
}

/**
 * @brief Execute the collect script periodically
 *
 * Run the collect script periodically and forward the stdout and stderr
 * to the mother psid where it will be handled by a callback function.
 *
 * @param fwdata Structure holding all forwarder information
 *
 * @param rerun The number of this function is called
 */
static void execCollectScript(Forwarder_Data_t *fwdata, int rerun)
{
    Collect_Script_t *script = fwdata->userData;

    while(true) {
	errno = 0;
	pid_t child = fork();
	if (child < 0) {
	    mlog("%s: fork() %s failed: %s\n", __func__, fwdata->pTitle,
		 strerror(errno));
	    exit(1);
	}

	if (!child) {
	    /* This is the child */
	    char *argv[2] = { script->path, NULL };
	    execvp(argv[0], argv);
	    /* never be here */
	    exit(1);
	}

	/* parent */
	while (true) {
	    int status;
	    if (waitpid(child, &status, 0) < 0) {
		if (errno == EINTR) continue;
		mlog("%s: parent kill() errno: %i\n", __func__, errno);
		killpg(child, SIGKILL);
		exit(1);
	    }
	    break;
	}

	sleep(script->poll);
    }
}

bool Script_test(char *spath, char *title)
{
    if (!spath) {
	return false;
    }

    struct stat sbuf;
    if (stat(spath, &sbuf) == -1) {
	mwarn(errno, "%s: %s script %s not found:", __func__, title, spath);
	return false;
    }
    if (!(sbuf.st_mode & S_IFREG) || !(sbuf.st_mode & S_IXUSR)) {
	mlog("%s: %s script %s is not a valid executable script\n",
		__func__, title, spath);
	return false;
    }
    return true;
}

/**
 * @brief Update the poll time of the script
 *
 * @param fwdata The forwarder management structure
 *
 * @param ptr Holding the new poll time
 */
static void handleSetPollTime(Forwarder_Data_t *fwdata, char *ptr)
{
    Collect_Script_t *script = fwdata->userData;
    getUint32(&ptr, &script->poll);
}

/**
 * @brief Handle a message from mother send to script forwarder
 *
 * @param msg The message to handle
 *
 * @param fwdata The forwarder management structure
 *
 * @return Returns 1 on success and 0 otherwise
 */
static int fwCMD_handleMthrMsg(PSLog_Msg_t *msg, Forwarder_Data_t *fwdata)
{
    FW_Cmds_t type = (FW_Cmds_t)msg->type;

    switch (type) {
	case CMD_SET_POLL_TIME:
	    handleSetPollTime(fwdata, msg->buf);
	    break;
	default:
	    mlog("%s: unexpected msg, type %d (PSlog type %s) from TID %s (%s) "
		 "jobid %s\n", __func__, type, PSLog_printMsgType(msg->type),
		 PSC_printTID(msg->sender), fwdata->pTitle, fwdata->jobID);
	    return 0;
    }

    return 1;
}

Collect_Script_t *Script_start(char *title, char *path,
			       scriptDataHandler_t *func, uint32_t poll)
{
    if (!title) {
	mlog("%s: invalid title given\n", __func__);
	return false;
    }
    if (!path) {
	mlog("%s: invalid path given\n", __func__);
	return false;
    }
    if (!func) {
	mlog("%s: invalid func given\n", __func__);
	return false;
    }

    if (!Script_test(path, title)) {
	mlog("%s: invalid %s script given\n", __func__, title);
	return false;
    }

    Collect_Script_t *script = umalloc(sizeof(*script));
    script->path = ustrdup(path);
    script->func = func;
    script->poll = poll;

    Forwarder_Data_t *fwdata = ForwarderData_new();
    fwdata->pTitle = ustrdup(title);
    fwdata->jobID = ustrdup("collect");
    fwdata->graceTime = 1;
    fwdata->killSession = signalSession;
    fwdata->handleFwMsg = handleFwMsg;
    fwdata->childFunc = execCollectScript;
    fwdata->fwChildOE = true;
    fwdata->userData = script;
    fwdata->handleMthrMsg = fwCMD_handleMthrMsg;

    if (!startForwarder(fwdata)) {
	mlog("%s: starting %s script forwarder failed\n", __func__, title);
	ForwarderData_delete(fwdata);
	ufree(script->path);
	ufree(script);
	return NULL;
    }

    script->fwdata = fwdata;
    return script;
}

void Script_finalize(Collect_Script_t *script)
{
    if (!script) {
	mlog("%s: invalid script given\n", __func__);
	return;
    }

    shutdownForwarder(script->fwdata);
    ufree(script->path);
    ufree(script);
}

bool Script_setPollTime(Collect_Script_t *script, uint32_t poll)
{
    if (!script || !script->fwdata) {
	mlog("%s: invalid script or forwarder data\n", __func__);
	return false;
    }

    PSLog_Msg_t msg = {
	.header = {
	    .type = PSP_CC_MSG,
	    .dest = script->fwdata->tid,
	    .sender = PSC_getMyTID(),
	    .len = offsetof(PSLog_Msg_t, buf) },
	.version = PLUGINFW_PROTO_VERSION,
	.type = (PSLog_msg_t)CMD_SET_POLL_TIME,
	.sender = -1};
    DDBufferMsg_t *bMsg = (DDBufferMsg_t *)&msg;
    uint32_t len = htonl(sizeof(poll));

    /* Add data including its length mimicking addData */
    PSP_putMsgBuf(bMsg, "len", &len, sizeof(len));
    PSP_putMsgBuf(bMsg, "time", &poll, sizeof(poll));

    if (sendMsg(&msg) == -1) return false;
    script->poll = poll;
    return true;
}