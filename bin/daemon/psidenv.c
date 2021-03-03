/*
 * ParaStation
 *
 * Copyright (C) 2011-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "psprotocol.h"
#include "pscommon.h"
#include "psidutil.h"
#include "psidcomm.h"
#include "psidtask.h"
#include "psidnodes.h"

#include "psidenv.h"

/**
 * @brief Send info on environment
 *
 * Send information on the environment variable @a key to task @a
 * dest. The information (i.e. name and value) is sent as a character
 * string.
 *
 * @param dest Task ID of the destination to send info to
 *
 * @param key Name of the environment variable to send
 *
 * @return No return value
 */
static void sendSingleEnv(PStask_ID_t dest, char *key)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = PSP_INFO_QUEUE_ENVS,
	.buf = {0}};
    size_t strLen, bufLen = sizeof(msg.buf);
    char *envStr = getenv(key);

    if (envStr) {
	strLen = snprintf(msg.buf, bufLen, "%s=%s", key, envStr);
    } else {
	strLen = snprintf(msg.buf, bufLen, "%s=<NULL>", key);
    }
    if (strLen > bufLen) {
	msg.buf[bufLen-4] = '.';
	msg.buf[bufLen-3] = '.';
	msg.buf[bufLen-2] = '.';
    }
    msg.buf[bufLen-1] = '\0';

    strLen = strlen(msg.buf)+1;
    msg.header.len += strLen;
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	return;
    }
    msg.header.len -= strLen;

    msg.type = PSP_INFO_QUEUE_SEP;
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	return;
    }
}

extern char **environ;

void PSID_sendEnvList(PStask_ID_t dest, char *key)
{
    char **myEnv = environ;

    if (strcmp(key, "*")) {
	sendSingleEnv(dest, key);
    } else {
	char myKey[BufTypedMsgSize];

	while (*myEnv) {
	    char *end = strchr(*myEnv, '=');

	    if (end) {
		memcpy(myKey, *myEnv, end - *myEnv);
		*(myKey + (end - *myEnv)) = '\0';

		sendSingleEnv(dest, myKey);
	    }

	    myEnv++;
	}
    }
}

/**
 * @brief Handle a PSP_CD_ENV message.
 *
 * Handle the message @a inmsg of type PSP_CD_ENV.
 *
 * With this kind of message a administrator will request to
 * modify or unset an environment variable. The action is encrypted in
 * the type-part of @a inmsg. The buf-part will hold the name of the
 * variable (PSP_ENV_UNSET) or a string of the form name=value
 * (PSP_ENV_SET).
 *
 * An answer will be sent as an PSP_CD_ENVRES message.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_ENV(DDTypedBufferMsg_t *inmsg)
{
    int destID = PSC_getID(inmsg->header.dest), ret = 0;

    PSID_log(PSID_LOG_ENV, "%s(%s, %s)\n", __func__,
	     PSC_printTID(inmsg->header.sender), inmsg->buf);

    if (!PSID_checkPrivilege(inmsg->header.sender)) {
	PSID_log(-1, "%s: task %s not allowed to modify environments\n",
		 __func__, PSC_printTID(inmsg->header.sender));
	ret = EACCES;
	goto end;
    }

    if (destID != PSC_getMyID()) {
	if (!PSIDnodes_isUp(destID)) {
	    ret = EHOSTDOWN;
	    goto end;
	}
	if (sendMsg(inmsg) == -1 && errno != EWOULDBLOCK) {
	    ret = errno;
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	    goto end;
	}
	return; /* destination node will send ENVRES message */
    } else {
	switch (inmsg->type) {
	case PSP_ENV_SET:
	{
	    char *key = inmsg->buf, *val = strchr(key, '=');

	    *val = '\0';
	    val++;

	    if (!*key) {
		PSID_warn(-1, errno, "%s: No key given to set", __func__);
		ret = EINVAL;
		goto end;
	    }

	    ret = setenv(key, val, 1);
	    if (ret) {
		ret = errno;
		PSID_warn(-1, errno, "%s: setenv(%s)", __func__, key);
		goto end;
	    }
	    break;
	}
	case PSP_ENV_UNSET:

	    if (!*inmsg->buf) {
		PSID_warn(-1, errno, "%s: No key given to unset", __func__);
		ret = EINVAL;
		goto end;
	    }

	    ret = unsetenv(inmsg->buf);
	    if (ret) {
		ret = errno;
		PSID_warn(-1, errno, "%s: unsetenv(%s)", __func__, inmsg->buf);
		goto end;
	    }
	    break;
	default:
	    PSID_log(-1, "%s: Unknown message type %d\n", __func__,
		     inmsg->type);
	    ret = -1;
	    goto end;
	}
    }

end:
    {
	DDTypedMsg_t msg = {
	    .header = {
		.type = PSP_CD_ENVRES,
		.dest = inmsg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(msg) },
	    .type = ret };
	if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	    PSID_warn(-1, errno, "%s: sendMsg()", __func__);
	}
    }
}

void initEnvironment(void)
{
    /* Register msg-handlers for environment modifications */
    PSID_registerMsg(PSP_CD_ENV, (handlerFunc_t)msg_ENV);
    PSID_registerMsg(PSP_CD_ENVRES, frwdMsg);
}
