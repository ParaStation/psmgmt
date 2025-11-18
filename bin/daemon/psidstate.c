/*
 * ParaStation
 *
 * Copyright (C) 2008-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidstate.h"

#include <stdlib.h>
#include <netinet/in.h>
#include <signal.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "psserial.h"

#include "mcast.h"
#include "rdp.h"

#include "psidclient.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidhw.h"
#include "psidnodes.h"
#include "psidplugin.h"
#include "psidsession.h"
#include "psidstatus.h"
#include "psidutil.h"

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
    static int phase = -1;
    int numPlugins;

    phase++;
    PSID_flog("phase %d\n", phase);
    PSIDhook_call(PSIDHOOK_SHUTDOWN, &phase);

    switch (phase) {
    case 0:
	daemonState |= PSID_STATE_SHUTDOWN;
	PSID_registerLoopAct(PSID_shutdown);
	PSID_disableMasterSock();
	if (!PSIDclient_killAll(SIGTERM, false))
	    /* no clients => proceed immediately to next phase */
	    PSID_shutdown();
	break;
    case 1:
	if (!PSIDclient_killAll(SIGTERM, false)) PSID_shutdown();
	break;
    case 2:
	if (!PSIDclient_killAll(SIGKILL, false)) PSID_shutdown();
	break;
    case 3:
	if (!PSIDclient_killAll(SIGTERM, true)) PSID_shutdown();
	break;
    case 4:
	if (!PSIDclient_killAll(SIGKILL, true)) PSID_shutdown();
	break;
    case 5:
	PSIDplugin_setUnloadTmout(2);
	PSIDplugin_finalizeAll();
	if (!PSIDplugin_getNum()) PSID_shutdown();
	break;
    case 6:
	PSIDplugin_forceUnloadAll();
	if (!PSIDplugin_getNum()) PSID_shutdown();
	break;
    case 7:
	numPlugins = PSIDplugin_getNum();
	if (numPlugins) {
	    PSID_log("    Still %d plugins\n", numPlugins);
	    /* Stay in this phase */
	    phase--;
	    return;
	}
	if (!PSID_config->useMCast) {
	    releaseStatusTimer();
	} else {
	    exitMCast();
	}
	PSID_stopAllHW(); /* must be here due to RDP-broadcasting HW change */
	send_DAEMONSHUTDOWN(); /* shuts down the RDP connections */
	PSIDsession_finalize();
	finalizeSerial();
	RDP_finalize();
	PSID_unregisterLoopAct(PSID_shutdown);
	PSID_shutdownMasterSock(); /* used for locking => ALAP */
	PSID_flog("good bye\n");
	PSID_finalizeLogs();
	exit(0);
    default:
	PSID_flog("unknown phase %d\n", phase);
    }
}

void PSID_reset(void)
{
    static int phase = -1;
    int num;

    phase++;
    PSID_flog("phase %d\n", phase);

    if (!PSIDclient_getNum(false)) {
	/* reset the hardware if demanded */
	if (daemonState & PSID_STATE_RESET_HW) {
	    PSID_flog("resetting hardware: ");
	    PSID_stopAllHW();
	    PSID_startAllHW();
	}
	/* reset the state */
	daemonState &= ~(PSID_STATE_RESET | PSID_STATE_RESET_HW);
	if (phase) PSID_unregisterLoopAct(PSID_reset);
	phase = -1;
	PSID_flog(" done\n");
	return;
    }

    switch (phase) {
    case 0:
	daemonState |= PSID_STATE_RESET;
	PSID_registerLoopAct(PSID_reset);
	if (!PSIDclient_killAll(SIGTERM, false))
	    /* no clients => proceed immediately to next phase */
	    PSID_reset();
	break;
    case 1:
	if (!PSIDclient_killAll(SIGTERM, false)) PSID_reset();
	break;
    case 2:
	num = PSIDclient_killAll(SIGKILL, false);
	if (num) {
	    PSID_flog("still %d clients\n", num);
	    /* Stay in this phase */
	    phase--;
	    return;
	}
	PSID_reset();
	break;
    default:
	PSID_flog("unknown phase %d\n", phase);
    }
}

/**
 * @brief Handle a PSP_CD_DAEMONSTART message
 *
 * Handle the message @a msg of type PSP_CD_DAEMONSTART.
 *
 * This message type is used in order to trigger the startup of a
 * remote daemon via (x)inetd or systemd. For the actual mechanism to
 * start a daemon @ref PSC_startDaemon() is utilized
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONSTART(DDBufferMsg_t *msg)
{
    PSnodes_ID_t starter = PSC_getID(msg->header.dest);
    PSnodes_ID_t node = *(PSnodes_ID_t *) msg->buf;

    PSID_fdbg(PSID_LOG_STATUS, "starter=%d node=%d\n", starter, node);

    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_flog("task %s not allowed to start daemons\n",
		  PSC_printTID(msg->header.sender));
    } else if (starter==PSC_getMyID()) {
	if (PSC_validNode(node)) {
	    if (!PSIDnodes_isUp(node)) {
		in_addr_t addr = PSIDnodes_getAddr(node);
		if (addr != INADDR_ANY)	PSC_startDaemon(addr);
	    } else {
		PSID_flog("node %d already up\n", node);
	    }
	}
    } else {
	if (PSIDnodes_isUp(starter)) {
	    /* forward message */
	    sendMsg(&msg);
	} else {
	    PSID_flog("starter %d is down\n", starter);
	}
    }
    return true;
}

/**
 * @brief Handle a PSP_CD_DAEMONSTOP message
 *
 * Handle the message @a msg of type PSP_CD_DAEMONSTOP.
 *
 * If the local node is the final destination of the message, it will
 * be stopped using @ref shutdownNode(). Otherwise @a msg will be
 * forwarded to the correct destination.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONSTOP(DDMsg_t *msg)
{
    if (!PSID_checkPrivilege(msg->sender)) {
	PSID_flog("task %s not allowed to stop daemons\n",
		  PSC_printTID(msg->sender));
    } else if (PSC_getID(msg->dest) == PSC_getMyID()) {
	if (!(PSID_getDaemonState() & PSID_STATE_SHUTDOWN)) PSID_shutdown();
    } else {
	sendMsg(msg);
    }
    return true;
}

/**
 * @brief Handle a PSP_CD_DAEMONRESET message
 *
 * Handle the message @a msg of type PSP_CD_DAEMONRESET.
 *
 * If the local node is the final destination of the message, it will
 * be reseted using @ref doReset(). Otherwise @a msg will be forwarded
 * to the correct destination.
 *
 * @param msg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_DAEMONRESET(DDBufferMsg_t *msg)
{
    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_flog("task %s not allowed to reset daemons\n",
		  PSC_printTID(msg->header.sender));
    } else if (PSC_getID(msg->header.dest) == PSC_getMyID()) {
	if (*(int *)msg->buf & PSP_RESET_HW) daemonState |= PSID_STATE_RESET_HW;
	/* Resetting my node */
	if (!(PSID_getDaemonState() & PSID_STATE_RESET)) PSID_reset();
    } else {
	sendMsg(msg);
    }
    return true;
}

bool PSIDstate_init(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    return PSID_registerMsg(PSP_CD_DAEMONSTART, msg_DAEMONSTART)
	&& PSID_registerMsg(PSP_CD_DAEMONSTOP, (handlerFunc_t) msg_DAEMONSTOP)
	&& PSID_registerMsg(PSP_CD_DAEMONRESET, msg_DAEMONRESET);
}
