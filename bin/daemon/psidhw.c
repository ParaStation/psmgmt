/*
 * ParaStation
 *
 * Copyright (C) 2006-2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
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
#include <sys/types.h>
#include <sys/wait.h>

#include "hardware.h"
#include "selector.h"

#include "pscommon.h"
#include "psprotocol.h"

#include "psidutil.h"
#include "psidnodes.h"
#include "psidcomm.h"
#include "psidscripts.h"
#include "psidamd.h"
#include "psidintel.h"
#include "psidppc.h"

#include "psidhw.h"

#define RETRY_SLEEP 5
#define MAX_RETRY 12

long PSID_getVirtCPUs(void)
{
    long virtCPUs = 0, retry = 0;

    while (!virtCPUs) {
	virtCPUs = sysconf(_SC_NPROCESSORS_CONF);
	if (virtCPUs) break;

	retry++;
	if (retry > MAX_RETRY) {
	    PSID_log(-1 ,"%s: Found no CPU for %d sec. This most probably is"
		     " not true. Exiting\n", __func__, RETRY_SLEEP * MAX_RETRY);
	    PSID_finalizeLogs();
	    exit(1);
	}
	PSID_log(-1, "%s: found no CPU. sleep(%d)...\n", __func__, RETRY_SLEEP);
	sleep(RETRY_SLEEP);
    }

    PSID_log(PSID_LOG_VERB, "%s: got %ld virtual CPUs\n", __func__, virtCPUs);

    return virtCPUs;
}

long PSID_getPhysCPUs(void)
{
    long physCPUs = 0, retry = 0;

    while (!physCPUs) {
	if (PSID_GenuineIntel()) {
	    physCPUs = PSID_getPhysCPUs_IA32();
	} else if (PSID_AuthenticAMD()) {
	    physCPUs = PSID_getPhysCPUs_AMD();
	} else if (PSID_PPC()) {
	    physCPUs = PSID_getPhysCPUs_PPC();
	} else {
	    /* generic case (assume no SMT) */
	    PSID_log(-1, "%s: Generic case.\n", __func__);
	    physCPUs = PSID_getVirtCPUs();
	}
	if (physCPUs) break;

	retry++;
	if (retry > MAX_RETRY) {
	    PSID_log(-1 ,"%s: Found no CPU for %d sec. This most probably is"
		     " not true. Exiting\n", __func__, RETRY_SLEEP * MAX_RETRY);
	    PSID_finalizeLogs();
	    exit(1);
	}
	PSID_log(-1, "%s: found no CPU. sleep(%d)...\n", __func__, RETRY_SLEEP);
	sleep(RETRY_SLEEP);
    }

    PSID_log(PSID_LOG_VERB, "%s: got %ld physical CPUs\n", __func__, physCPUs);

    return physCPUs;
}

/** Info to be passed to @ref prepSwitchEnv() and @ref switchHWCB(). */
typedef struct {
    int hw;    /**< Hardware-type to prepare for. */
    int on;    /**< Switch-mode, i.e. on (1) or off (0). */
} switchInfo_t;

/**
 * @brief Prepare for switch-scripts
 *
 * Prepare the environment for executing switchHW scripts. @a info
 * contains extra-information packed into a @ref switchInfo_t
 * structure.
 *
 * @param info Extra information within a @ref switchInfo_t structure.
 *
 * @return Always return 0.
 */
static int prepSwitchEnv(void *info)
{
    int hw = -1;
    char buf[20];

    if (info) {
	switchInfo_t *i = (switchInfo_t *)info;
	hw = i->hw;
    }

    if (hw > -1) {
	int i;
	for (i=0; i<HW_getEnvSize(hw); i++) putenv(HW_dumpEnv(hw, i));
    }

    snprintf(buf, sizeof(buf), "%d", PSC_getMyID());
    setenv("PS_ID", buf, 1);

    setenv("PS_INSTALLDIR", PSC_lookupInstalldir(NULL), 1);

    return 0;
}

/**
 * @brief Inform nodes
 *
 * Inform all other nodes on the local status of the communication
 * hardware. Therefore a messages describing the hardware available on
 * the local node is broadcasted to all other nodes that are currently
 * up.
 *
 * @return No return value.
 */
static void informOtherNodes(void)
{
    DDOptionMsg_t msg = (DDOptionMsg_t) {
	.header = (DDMsg_t) {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = 0,
	    .len = sizeof(msg) },
	.count = 1,
	.opt = {(DDOption_t) {
	    .option = PSP_OP_HWSTATUS,
	    .value = PSIDnodes_getHWStatus(PSC_getMyID()) }
	}};

    if (broadcastMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: broadcastMsg()", __func__);
    }
}

/**
 * @brief Callback for switch-scripts
 *
 * Callback used by switchHW scripts. @a fd is the file-descriptor
 * containing the exit-status of the script. @a info contains
 * extra-information packed into a @ref switchInfo_t structure.
 *
 * @param fd File-descriptor containing script's exit-status.
 *
 * @param info Extra information within a @ref switchInfo_t structure.
 *
 * @return Always return 0.
 */
static int switchHWCB(int fd, PSID_scriptCBInfo_t *info)
{
    int result, hw = -1, iofd = -1, on = 0;
    char *hwName, *hwScript;

    if (!info) {
	PSID_log(-1, "%s: No extra info\n", __func__);
    } else {
	if (info->info) {
	    switchInfo_t *i = (switchInfo_t *)info->info;
	    hw = i->hw;
	    on = i->on;
	    free(info->info);
	}
	iofd = info->iofd;
	free(info);
    }
    if (hw > -1) {
	hwName = HW_name(hw);
	hwScript = HW_getScript(hw, on ? HW_STARTER : HW_STOPPER);
    } else {
	hwName = hwScript = "unknown";
    }

    PSID_readall(fd, &result, sizeof(result));
    close(fd);
    if (result) {
	char line[128] = "<not connected>";
	if (iofd > -1) {
	    int num = PSID_readall(iofd, line, sizeof(line));
	    int eno = errno;
	    if (num < 0) {
		PSID_warn(-1, eno, "%s: read(iofd)", __func__);
		line[0] = '\0';
	    } else if (num == sizeof(line)) {
		strcpy(&line[sizeof(line)-4], "...");
	    } else {
		line[num]='\0';
	    }
	}
	PSID_log(-1, "%s: script(%s, %s) returned %d: '%s'\n", __func__,
		 hwName, hwScript, result, line);
    } else if (hw > -1) {
	int oldState = PSIDnodes_getHWStatus(PSC_getMyID());

	PSID_log(PSID_LOG_HW, "%s: script(%s, %s): success\n", __func__,
		 hwName, hwScript);
	if (on) {
	    PSIDnodes_setHWStatus(PSC_getMyID(), oldState | (1<<hw));
	} else {
	    PSIDnodes_setHWStatus(PSC_getMyID(), oldState & ~(1<<hw));
	}
	if (oldState != PSIDnodes_getHWStatus(PSC_getMyID())) {
	    informOtherNodes();
	}
    }
    if (iofd > -1) close(iofd); /* Discard further output */

    Selector_remove(fd);

    return 0;
}

/**
 * @brief Switch distinct communciation hardware.
 *
 * Switch the distinct communication hardware @a hw on or off
 * depending on the value of @a on. @a hw is a unique number
 * describing the hardware and is defined from the configuration
 * file. If the flag @a on is different from 0, the hardware is
 * switched on. Otherwise it's switched off.
 *
 * If switching succeeded and the corresponding hardware changed
 * state, all other nodes are informed on the changed hardware
 * situation on the local node.
 *
 * @param hw A unique number of the communication hardware to start.
 *
 * @param on Flag marking the hardware to be brought up or down.
 *
 * @return No return value.
 */
static void switchHW(int hw, int on)
{
    char *script = HW_getScript(hw, on ? HW_STARTER : HW_STOPPER);

    if (hw<0 || hw>HW_num()) {
	PSID_log(-1, "%s: hw = %d out of range\n", __func__, hw);
	return;
    }

    if (script) {
	switchInfo_t *info = malloc(sizeof(*info));
	if (!info) {
	    PSID_warn(-1, errno, "%s: malloc()", __func__);
	    return;
	}
	info->hw = hw;
	info->on = on;

	if (PSID_execScript(script, prepSwitchEnv, switchHWCB, info) < 0) {
	    PSID_log(-1, "%s: Failed to execute '%s' for hw '%s'\n",
		     __func__, script, HW_name(hw));
	}
    } else {
	/* No script, assume HW is switched anyhow */
	int oldState = PSIDnodes_getHWStatus(PSC_getMyID());

	PSID_log(PSID_LOG_HW, "%s: assume %s already %s\n",
		 __func__, HW_name(hw), on ? "up" : "down");
	if (on) {
	    PSIDnodes_setHWStatus(PSC_getMyID(), oldState | (1<<hw));
	} else {
	    PSIDnodes_setHWStatus(PSC_getMyID(), oldState & ~(1<<hw));
	}
	if (oldState != PSIDnodes_getHWStatus(PSC_getMyID())) {
	    informOtherNodes();
	}
    }
}

void PSID_startAllHW(void)
{
    int hw;
    for (hw=0; hw<HW_num(); hw++) {
	if (PSIDnodes_getHWType(PSC_getMyID()) & (1<<hw)) switchHW(hw, 1);
    }
}

void PSID_stopAllHW(void)
{
    int hw;
    for (hw=HW_num()-1; hw>=0; hw--) {
	if (PSIDnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) switchHW(hw, 0);
    }
}

/**
 * @brief Prepare for counter-scripts
 *
 * Prepare the environment for executing getCounter scripts. @a info
 * contains extra-information in a @ref DDTypedBufferMsg_t
 * structure, actually the original message requesting counter
 * information.
 *
 * @param info Extra information within a @ref DDTypedBufferMsg_t
 * structure.
 *
 * @return Always return 0.
 */
static int prepCounterEnv(void *info)
{
    int hw = -1;

    if (info) {
	DDTypedBufferMsg_t *inmsg = info;
	hw = *(int *) inmsg->buf;
    }

    if (hw > -1) {
	/* Put the hardware's environment into the real one */
	int i;
	char buf[20];

	for (i=0; i<HW_getEnvSize(hw); i++) putenv(HW_dumpEnv(hw, i));

	snprintf(buf, sizeof(buf), "%d", PSC_getMyID());
	setenv("PS_ID", buf, 1);

	setenv("PS_INSTALLDIR", PSC_lookupInstalldir(NULL), 1);
    }

    return 0;
}

/**
 * @brief Callback for counter-scripts
 *
 * Callback used by getCounter scripts. @a fd is the file-descriptor
 * containing the exit-status of the script. @a info contains
 * extra-information in a @ref DDTypedBufferMsg_t structure, actually
 * the original message requesting counter information.
 *
 * @param fd File-descriptor containing script's exit-status.
 *
 * @param info Extra information within a @ref DDTypedBufferMsg_t
 * structure.
 *
 * @return Always return 0.
 */
static int getCounterCB(int fd, PSID_scriptCBInfo_t *info)
{
    PStask_ID_t dest = 0;
    PSP_Info_t type = 0;
    int result, hw = -1, iofd = -1, num, eno = 0;
    char *hwName, *hwScript;
    DDTypedBufferMsg_t msg;

    if (!info) {
	PSID_log(-1, "%s: No extra info\n", __func__);
    } else {
	if (info->info) {
	    DDTypedBufferMsg_t *inmsg = info->info;
	    hw = *(int *) inmsg->buf;
	    dest = inmsg->header.sender;
	    type = inmsg->type;
	    free(info->info);
	}
	iofd = info->iofd;
	free(info);
    }
    if (hw > -1) {
	int header = type == PSP_INFO_COUNTHEADER;
	hwName = HW_name(hw);
	hwScript = HW_getScript(hw, header ? HW_HEADERLINE : HW_COUNTER);
	if (!hwScript) hwScript = "unknown";
    } else {
	hwName = hwScript = "unknown";
    }

    msg = (DDTypedBufferMsg_t) {
	.header = { .type = PSP_CD_INFORESPONSE,
		    .sender = PSC_getMyTID(),
		    .dest = dest,
		    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = type,
	.buf = { 0 } };

    PSID_readall(fd, &result, sizeof(result));
    close(fd);
    if (iofd == -1) {
	PSID_log(-1, "%s: %s\n", __func__, msg.buf);
	num = snprintf(msg.buf, sizeof(msg.buf), "<not connected>");
    } else {
	num = PSID_readall(iofd, msg.buf, sizeof(msg.buf));
	eno = errno;
	close(iofd); /* Discard further output */
    }
    if (num < 0) {
	PSID_warn(-1, eno, "%s: read(iofd)", __func__);
	num = snprintf(msg.buf, sizeof(msg.buf),
		       "%s: read(iofd) failed\n", __func__) + 1;
    } else if (num == sizeof(msg.buf)) {
	strcpy(&msg.buf[sizeof(msg.buf)-4], "...");
    } else {
	msg.buf[num]='\0';
	num++;
    }
    msg.header.len += num;

    if (result) {
	PSID_log(-1, "%s: script(%s, %s) returned %d: %s\n",
		 __func__, hwName, hwScript, result, msg.buf);
    } else {
	PSID_log(PSID_LOG_HW, "%s: callScript(%s, %s): success\n",
		 __func__, hwName, hwScript);
    }

    if (dest) sendMsg(&msg);

    Selector_remove(fd);

    return 0;
}

void PSID_getCounter(DDTypedBufferMsg_t *inmsg)
{
    int hw = *(int *) inmsg->buf;
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_CD_INFORESPONSE,
	    .sender = PSC_getMyTID(),
	    .dest = inmsg->header.sender,
	    .len = sizeof(msg.header) + sizeof(msg.type) },
	.type = inmsg->type,
	.buf = { 0 } };

    if (PSIDnodes_getHWStatus(PSC_getMyID()) & (1<<hw)) {
	int header = (PSP_Info_t) inmsg->type == PSP_INFO_COUNTHEADER;
	char *script = HW_getScript(hw, header ? HW_HEADERLINE : HW_COUNTER);

	if (script) {
	    DDTypedBufferMsg_t *info = malloc(inmsg->header.len);

	    if (!info) {
		PSID_warn(-1, errno, "%s: malloc()", __func__);
		return;
	    }
	    memcpy(info, inmsg, inmsg->header.len);

	    if (PSID_execScript(script, prepCounterEnv, getCounterCB, info)<0) {
		PSID_log(PSID_LOG_HW,
			 "%s: Failed to execute '%s' for hw '%s'\n",
			 __func__, script, HW_name(hw));
		snprintf(msg.buf, sizeof(msg.buf),
			 "%s: Failed to execute '%s' for hw '%s'\n",
			 __func__, script, HW_name(hw));
	    } else {
		/* answer created within callback */
		return;
	    }
	} else {
	    /* No script, cannot get counter */
	    PSID_log(PSID_LOG_HW, "%s: no %s-script for %s available\n",
		     __func__, header ? "header" : "counter", HW_name(hw));
	    snprintf(msg.buf, sizeof(msg.buf),
		     "%s: no %s-script for %s available", __func__,
		     header ? "header" : "counter", HW_name(hw));
	}
    } else {
	/* No HW, cannot get counter */
	PSID_log(-1, "%s: no %s hardware available\n", __func__, HW_name(hw));
	snprintf(msg.buf, sizeof(msg.buf), "%s: no %s hardware available",
		 __func__, HW_name(hw));
    }

    sendMsg(&msg);
}

void PSID_setParam(int hw, PSP_Option_t type, PSP_Optval_t value)
{
    return;
}

PSP_Optval_t PSID_getParam(int hw, PSP_Option_t type)
{
    return -1;
}

/**
 * @brief Handle PSP_CD_HWSTART message
 *
 * Handle the message @a msg of type PSP_CD_HWSTART.
 *
 * Start the communication hardware as described within @a msg.
 *
 * @param msg Pointer to message to handle.
 *
 * @return No return value.
 */
static void msg_HWSTART(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_HW, "%s: requester %s\n",
	     __func__, PSC_printTID(msg->header.sender));

    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_log(-1, "%s: task %s not allowed to start HW\n",
		 __func__, PSC_printTID(msg->header.sender));

	return;
    }

    if (msg->header.dest == PSC_getMyTID()) {
	int hw = *(int *)msg->buf;

	if (hw == -1) {
	    PSID_startAllHW();
	} else {
	    switchHW(hw, 1);
	}
    } else {
	sendMsg(msg);
    }
}

/**
 * @brief Handle PSP_CD_HWSTOP message
 *
 * Handle the message @a msg of type PSP_CD_HWSTOP.
 *
 * Stop the communication hardware as described within @a msg. If
 * stopping succeeded and the corresponding hardware was up before,
 * all other nodes are informed on the change hardware situation on
 * the local node.
 *
 * @param msg Pointer to message to handle.
 *
 * @return No return value.
 */
static void msg_HWSTOP(DDBufferMsg_t *msg)
{
    PSID_log(PSID_LOG_HW, "%s: requester %s\n",
	     __func__, PSC_printTID(msg->header.sender));

    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_log(-1, "%s: task %s not allowed to stop HW\n",
		 __func__, PSC_printTID(msg->header.sender));

	return;
    }

    if (msg->header.dest == PSC_getMyTID()) {
	int hw = *(int *)msg->buf;

	if (hw == -1) {
	    PSID_stopAllHW();
	} else {
	    switchHW(hw, 0);
	}
    } else {
	sendMsg(msg);
    }
}

void initHW(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_HWSTART, msg_HWSTART);
    PSID_registerMsg(PSP_CD_HWSTOP, msg_HWSTOP);
}
