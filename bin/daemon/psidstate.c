/*
 * ParaStation
 *
 * Copyright (C) 2008-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"

#include "mcast.h"
#include "rdp.h"

#include "psidutil.h"
#include "psidtimer.h"
#include "psidcomm.h"
#include "psidrdp.h"
#include "psidclient.h"
#include "psidstatus.h"
#include "psidhw.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidplugin.h"
#include "psidhook.h"

#include "psidstate.h"

/**
 * The internal status of the ParaStation daemon. This is used for the
 * functionality provided by PSID_shutdown() and PSID_reset(). This
 * status might be requested from the outside via
 * PSID_getDaemonState().
 *
 * @see PSID_shutdown(), PSID_reset(), PSID_getDaemonState()
 */
static PSID_DaemonState_t daemonState = PSID_STATE_NONE;

PSID_DaemonState_t PSID_getDaemonState(void)
{
    return daemonState;
}

void PSID_shutdown(void)
{
    static int phase = 0, numPlugins;
    static struct timeval shutdownTimer, now;

    if (!phase) timerclear(&shutdownTimer);

    gettimeofday(&now, NULL);
    if (timercmp(&now, &shutdownTimer, <)) {
	PSID_log(PSID_LOG_TIMER, "%s: not ready: [%ld:%ld]<[%ld:%ld]\n",
		 __func__, now.tv_sec, now.tv_usec,
		 shutdownTimer.tv_sec, shutdownTimer.tv_usec);
	return;
    }

    PSID_log(-1, "%s(%d)\n", __func__, phase);

    PSID_log(PSID_LOG_TIMER, "%s: now[%ld:%ld], shutdown[%ld:%ld]\n",
	     __func__, now.tv_sec, now.tv_usec,
	     shutdownTimer.tv_sec, shutdownTimer.tv_usec);

    gettimeofday(&shutdownTimer, NULL);
    mytimeradd(&shutdownTimer, 1, 0);


    switch (phase) {
    case 0:
	daemonState |= PSID_STATE_SHUTDOWN;
	PSID_registerLoopAct(PSID_shutdown);
	PSIDhook_call(PSIDHOOK_SHUTDOWN, NULL);
	PSID_disableMasterSock();
    case 1:
	PSIDclient_killAll(SIGTERM, 0);
	break;
    case 2:
	PSIDclient_killAll(SIGKILL, 0);
	break;
    case 3:
	PSIDclient_killAll(SIGTERM, 1);
	break;
    case 4:
	PSIDclient_killAll(SIGKILL, 1);
	break;
    case 5:
	PSIDplugin_setUnloadTmout(2);
	PSIDplugin_forceUnloadAll();
	break;
    case 6:
	numPlugins = PSIDplugin_getNum();
	if (numPlugins) {
	    PSID_log(-1, "    Still %d plugins\n", numPlugins);
	    /* Stay in this phase */
	    phase--;
	    break;
	}
	if (!PSID_config->useMCast) {
	    releaseStatusTimer();
	} else {
	    exitMCast();
	}
	PSID_stopAllHW(); /* must be here due to RDP-broadcasting HW change */
	send_DAEMONSHUTDOWN(); /* shuts down the RDP connections */
	exitRDP();
	PSID_unregisterLoopAct(PSID_shutdown);
	PSID_shutdownMasterSock(); /* used for locking => ALAP */
	PSID_log(-1, "%s: good bye\n", __func__);
	PSID_finalizeLogs();
	exit(0);
    default:
	PSID_log(-1, "%s: unknown phase %d\n", __func__, phase);
    }

    phase++;
}

void PSID_reset(void)
{
    static int phase = 0;
    static struct timeval resetTimer, now;
    int num = 1;

    if (!phase) timerclear(&resetTimer);

    gettimeofday(&now, NULL);
    if (timercmp(&now, &resetTimer, <)) {
	PSID_log(PSID_LOG_TIMER, "%s: not ready: [%ld:%ld]<[%ld:%ld]\n",
		 __func__, now.tv_sec, now.tv_usec,
		 resetTimer.tv_sec, resetTimer.tv_usec);
	return;
    }

    PSID_log(-1, "%s(%d)\n", __func__, phase);

    PSID_log(PSID_LOG_TIMER, "%s: now[%ld:%ld], reset[%ld:%ld]\n",
	     __func__, now.tv_sec, now.tv_usec,
	     resetTimer.tv_sec, resetTimer.tv_usec);

    gettimeofday(&resetTimer, NULL);
    mytimeradd(&resetTimer, 1, 0);

    switch (phase) {
    case 0:
	daemonState |= PSID_STATE_RESET;
	PSID_registerLoopAct(PSID_reset);
    case 1:
	num = PSIDclient_killAll(SIGTERM, 0);
	break;
    case 2:
	num = PSIDclient_killAll(SIGKILL, 0);
	break;
    case 3:
	num = PSIDclient_killAll(SIGTERM, 0);
	if (num) {
	    PSID_log(-1, "%s: still %d clients in phase %d. Continue\n",
		     __func__, num, phase);
	}
	num = 0;
	break;
    default:
	PSID_log(-1, "%s: unknown phase %d\n", __func__, phase);
    }

    phase++;

    if (!num) {
	/* reset the hardware if demanded */
	if (daemonState & PSID_STATE_RESET_HW) {
	    PSID_log(-1, "%s: resetting hardware", __func__);
	    PSID_stopAllHW();
	    PSID_startAllHW();
	}
	/* reset the state */
	daemonState &= ~(PSID_STATE_RESET | PSID_STATE_RESET_HW);
	phase = 0;
	PSID_unregisterLoopAct(PSID_reset);
	PSID_log(-1, "%s: done\n", __func__);
    }
}

/**
 * @brief Handle a PSP_CD_DAEMONSTART message.
 *
 * Handle the message @a msg of type PSP_CD_DAEMONSTART.
 *
 * This message type is used in order to trigger the startup of a
 * remote daemon via (x)inetd.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONSTART(DDBufferMsg_t *msg)
{
    PSnodes_ID_t starter = PSC_getID(msg->header.dest);
    PSnodes_ID_t node = *(PSnodes_ID_t *) msg->buf;

    PSID_log(PSID_LOG_STATUS, "%s: received (starter=%d node=%d)\n",
	     __func__, starter, node);

    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_log(-1, "%s: task %s not allowed to start daemons\n", __func__,
		 PSC_printTID(msg->header.sender));

	return;
    }

    if (starter==PSC_getMyID()) {
	if (node<PSC_getNrOfNodes()) {
	    if (!PSIDnodes_isUp(node)) {
		in_addr_t addr = PSIDnodes_getAddr(node);
		if (addr != INADDR_ANY)	PSC_startDaemon(addr);
	    } else {
		PSID_log(-1, "%s: node %d already up\n", __func__, node);
	    }
	}
    } else {
	if (PSIDnodes_isUp(starter)) {
	    /* forward message */
	    sendMsg(&msg);
	} else {
	    PSID_log(-1, "%s: starter %d is down\n", __func__, starter);
	}
    }
}

/**
 * @brief Handle a PSP_CD_DAEMONSTOP message.
 *
 * Handle the message @a msg of type PSP_CD_DAEMONSTOP.
 *
 * If the local node is the final destination of the message, it will
 * be stopped using @ref shutdownNode(). Otherwise @a msg will be
 * forwarded to the correct destination.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONSTOP(DDMsg_t *msg)
{
    if (!PSID_checkPrivilege(msg->sender)) {
	PSID_log(-1, "%s: task %s not allowed to stop daemons\n", __func__,
		 PSC_printTID(msg->sender));

	return;
    }

    if (PSC_getID(msg->dest) == PSC_getMyID()) {
	PSID_shutdown();
    } else {
	sendMsg(msg);
    }
}

/**
 * @brief Handle a PSP_CD_DAEMONRESET message.
 *
 * Handle the message @a msg of type PSP_CD_DAEMONRESET.
 *
 * If the local node is the final destination of the message, it will
 * be reseted using @ref doReset(). Otherwise @a msg will be forwarded
 * to the correct destination.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_DAEMONRESET(DDBufferMsg_t *msg)
{
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_log(-1, "%s: task %s not allowed to reset daemons\n", __func__,
		 PSC_printTID(msg->header.sender));

	return;
    }

    if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	if (*(int *)msg->buf & PSP_RESET_HW) daemonState |= PSID_STATE_RESET_HW;
	/* Resetting my node */
	PSID_reset();
    } else {
	sendMsg(msg);
    }
}

void initState(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_DAEMONSTART, msg_DAEMONSTART);
    PSID_registerMsg(PSP_CD_DAEMONSTOP, (handlerFunc_t) msg_DAEMONSTOP);
    PSID_registerMsg(PSP_CD_DAEMONRESET, msg_DAEMONRESET);
}
