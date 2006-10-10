/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

#include "rdp.h"
#include "mcast.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "psnodes.h"
#include "hardware.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidtask.h"
#include "psidtimer.h"
#include "psidstatus.h"
#include "psidhw.h"
#include "psidaccount.h"

#include "psidoption.h"

void send_OPTIONS(PSnodes_ID_t destnode)
{
    DDOptionMsg_t msg = {
	.header = {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getTID(destnode, 0),
	    .len = sizeof(msg) },
	.count = 0,
	.opt = {{ .option = 0, .value = 0 }} };

    msg.opt[(int) msg.count].option = PSP_OP_HWSTATUS;
    msg.opt[(int) msg.count].value = PSnodes_getHWStatus(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_PROCLIMIT;
    msg.opt[(int) msg.count].value = PSnodes_getProcs(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_UIDLIMIT;
    msg.opt[(int) msg.count].value = PSnodes_getUser(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_GIDLIMIT;
    msg.opt[(int) msg.count].value = PSnodes_getGroup(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_ADMINUID;
    msg.opt[(int) msg.count].value = PSnodes_getAdminUser(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_ADMINGID;
    msg.opt[(int) msg.count].value = PSnodes_getAdminGroup(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_OVERBOOK;
    msg.opt[(int) msg.count].value = PSnodes_overbook(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_EXCLUSIVE;
    msg.opt[(int) msg.count].value = PSnodes_exclusive(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_STARTER;
    msg.opt[(int) msg.count].value = PSnodes_isStarter(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_RUNJOBS;
    msg.opt[(int) msg.count].value = PSnodes_runJobs(PSC_getMyID());
    msg.count++;

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }

    send_acct_OPTIONS(PSC_getTID(destnode, 0), 0);
    /* @todo send_user_OPTIONS(PSC_getTID(destnode, 0)); */
    /* @todo send_admin_OPTIONS(PSC_getTID(destnode, 0)); */
}

void msg_SETOPTION(DDOptionMsg_t *msg)
{
    int i;

    PSID_log(PSID_LOG_OPTION, "%s: from requester %s\n",
	     __func__, PSC_printTID(msg->header.sender));

    if (msg->header.dest == PSC_getMyTID()) {
	/* Message is for me */
	for (i=0; i<msg->count; i++) {
	    PSID_log(PSID_LOG_OPTION, "%s: option %d value 0x%x\n",
		     __func__, msg->opt[i].option, msg->opt[i].value);

	    switch (msg->opt[i].option) {
	    case PSP_OP_PSM_SPS:
	    case PSP_OP_PSM_RTO:
	    case PSP_OP_PSM_HNPEND:
	    case PSP_OP_PSM_ACKPEND:
	    {
		static int MYRINETindex = -1;

		if (MYRINETindex == -1) {
		    MYRINETindex = HW_index("myrinet");
		}

		if (MYRINETindex != -1) {
		    PSID_setParam(MYRINETindex,
				  msg->opt[i].option, msg->opt[i].value);
		}
		break;
	    }
	    case PSP_OP_PSIDSELECTTIME:
		if (msg->opt[i].value > 0) {
		    selectTime.tv_sec = msg->opt[i].value;
		}
	    break;
	    case PSP_OP_PROCLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };
			    
		    PSnodes_setProcs(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my PROCLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setProcs(PSC_getID(msg->header.sender),
				     msg->opt[i].value);
		}
		break;
	    case PSP_OP_UIDLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };
			    
		    PSnodes_setUser(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my UIDLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setUser(PSC_getID(msg->header.sender),
				    msg->opt[i].value);
		}
		break;
	    case PSP_OP_ADMINUID:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };
			    
		    PSnodes_setAdminUser(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my ADMINUID */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setAdminUser(PSC_getID(msg->header.sender),
					 msg->opt[i].value);
		}
		break;
	    case PSP_OP_GIDLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };

		    PSnodes_setGroup(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my GIDLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setGroup(PSC_getID(msg->header.sender),
				     msg->opt[i].value);
		}
		break;
	    case PSP_OP_ADMINGID:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };

		    PSnodes_setAdminGroup(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my ADMINGID */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setAdminGroup(PSC_getID(msg->header.sender),
					  msg->opt[i].value);
		}
		break;
	    case PSP_OP_OVERBOOK:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };
			    
		    PSnodes_setOverbook(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my OVERBOOK */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setOverbook(PSC_getID(msg->header.sender),
					   msg->opt[i].value);
		}
		break;
	    case PSP_OP_EXCLUSIVE:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };
			    
		    PSnodes_setExclusive(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my EXCLUSIVE */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setExclusive(PSC_getID(msg->header.sender),
					   msg->opt[i].value);
		}
		break;
	    case PSP_OP_STARTER:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };
			    
		    PSnodes_setIsStarter(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my STARTER */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setIsStarter(PSC_getID(msg->header.sender),
					   msg->opt[i].value);
		}
		break;
	    case PSP_OP_RUNJOBS:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info = {
			.header = {
			    .type = PSP_CD_SETOPTION,
			    .sender = PSC_getMyTID(),
			    .dest = 0,
			    .len = sizeof(info) },
			.count = 1,
			.opt = {{ .option = msg->opt[i].option,
				  .value = msg->opt[i].value }} };
			    
		    PSnodes_setRunJobs(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my RUNJOBS */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setRunJobs(PSC_getID(msg->header.sender),
				       msg->opt[i].value);
		}
		break;
	    case PSP_OP_HWSTATUS:
		PSnodes_setHWStatus(PSC_getID(msg->header.sender),
				    msg->opt[i].value);
		break;
	    case PSP_OP_PSIDDEBUG:
		PSID_setDebugMask(msg->opt[i].value);
		PSC_setDebugMask(msg->opt[i].value);

		if (msg->opt[i].value) {
		    PSID_log(-1, "Debugging mode with mask 0x%x enabled\n",
			     msg->opt[i].value);
		} else {
		    PSID_log(-1, "Debugging mode disabled\n");
		}
		break;
	    case PSP_OP_RDPDEBUG:
		setDebugMaskRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPPKTLOSS:
		setPktLossRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPMAXRETRANS:
		setMaxRetransRDP(msg->opt[i].value);
		break;
	    case PSP_OP_MCASTDEBUG:
		setDebugMaskMCast(msg->opt[i].value);
		break;
	    case PSP_OP_FREEONSUSP:
		config->freeOnSuspend = msg->opt[i].value;
		break;
	    case PSP_OP_HANDLEOLD:
		config->handleOldBins = msg->opt[i].value;
		break;
	    case PSP_OP_NODESSORT:
		config->nodesSort = msg->opt[i].value;
		break;
	    case PSP_OP_ADD_ACCT:
		PSID_addAcct(msg->opt[i].value);
		break;
	    case PSP_OP_REM_ACCT:
		PSID_remAcct(msg->opt[i].value);
		break;
	    case PSP_OP_LISTEND:
		/* Ignore */
		break;
	    default:
		PSID_log(-1, "%s: unknown option %d\n", __func__,
			msg->opt[i].option);
	    }
	}
    } else {
	/* Message is for a remote node */
	sendMsg(msg);
    }

}

void msg_GETOPTION(DDOptionMsg_t *msg)
{
    int id = PSC_getID(msg->header.dest);

    PSID_log(PSID_LOG_OPTION, "%s: from node %d for requester %s\n",
	     __func__, id, PSC_printTID(msg->header.sender));

    if (id!=PSC_getMyID()) {
	DDErrorMsg_t errmsg = (DDErrorMsg_t) {
	    .header = (DDMsg_t) {
		.type = PSP_CD_ERROR,
		.dest = msg->header.sender,
		.sender = PSC_getMyTID(),
		.len = sizeof(errmsg) },
	    .request = msg->header.type,
	    .error = 0};

	/* a request for a remote daemon */
	if (PSnodes_isUp(id)) {
	    /*
	     * transfer the request to the remote daemon
	     */
	    if (sendMsg(msg) == -1 && errno != EWOULDBLOCK) {
		/* system error */
		errmsg.error = EHOSTUNREACH;
		sendMsg(&errmsg);
	    }
	} else {
	    /* node is down */
	    errmsg.error = EHOSTDOWN;
	    sendMsg(&errmsg);
	}
    } else {
	int in, out;
	for (in=0, out=0; in<msg->count; in++, out++) {
	    PSID_log(PSID_LOG_OPTION, "%s: option %d\n",
		     __func__, msg->opt[in].option);

	    msg->opt[out].option = msg->opt[in].option;
	    
	    switch (msg->opt[in].option) {
	    case PSP_OP_PSM_SPS:
	    case PSP_OP_PSM_RTO:
	    case PSP_OP_PSM_HNPEND:
	    case PSP_OP_PSM_ACKPEND:
	    {
		static int MYRINETindex = -1;

		if (MYRINETindex == -1) {
		    MYRINETindex = HW_index("myrinet");
		}

		if (MYRINETindex != -1) {
		    msg->opt[out].value = PSID_getParam(MYRINETindex,
							msg->opt[in].option);
		} else {
		    msg->opt[out].value = -1;
		}
		break;
	    }
	    case PSP_OP_PSIDDEBUG:
		msg->opt[out].value = PSID_getDebugMask();
		break;
	    case PSP_OP_PSIDSELECTTIME:
		msg->opt[out].value = selectTime.tv_sec;
		break;
	    case PSP_OP_PROCLIMIT:
		msg->opt[out].value = PSnodes_getProcs(PSC_getMyID());
		break;
	    case PSP_OP_UIDLIMIT:
		msg->opt[out].value = PSnodes_getUser(PSC_getMyID());
		break;
	    case PSP_OP_GIDLIMIT:
		msg->opt[out].value = PSnodes_getGroup(PSC_getMyID());
		break;
	    case PSP_OP_ADMINUID:
		msg->opt[out].value = PSnodes_getAdminUser(PSC_getMyID());
		break;
	    case PSP_OP_ADMINGID:
		msg->opt[out].value = PSnodes_getAdminGroup(PSC_getMyID());
		break;
	    case PSP_OP_OVERBOOK:
		msg->opt[out].value = PSnodes_overbook(PSC_getMyID());
		break;
	    case PSP_OP_EXCLUSIVE:
		msg->opt[out].value = PSnodes_exclusive(PSC_getMyID());
		break;
	    case PSP_OP_STARTER:
		msg->opt[out].value = PSnodes_isStarter(PSC_getMyID());
		break;
	    case PSP_OP_RUNJOBS:
		msg->opt[out].value = PSnodes_runJobs(PSC_getMyID());
		break;
	    case PSP_OP_RDPDEBUG:
		msg->opt[out].value = getDebugMaskRDP();
		break;
	    case PSP_OP_RDPPKTLOSS:
		msg->opt[out].value = getPktLossRDP();
		break;
	    case PSP_OP_RDPMAXRETRANS:
		msg->opt[out].value = getMaxRetransRDP();
		break;
	    case PSP_OP_MCASTDEBUG:
		msg->opt[out].value = getDebugMaskMCast();
		break;
	    case PSP_OP_MASTER:
		msg->opt[out].value = getMasterID();
		break;
	    case PSP_OP_FREEONSUSP:
		msg->opt[out].value = config->freeOnSuspend;
		break;
	    case PSP_OP_HANDLEOLD:
		msg->opt[out].value = config->handleOldBins;
		break;
	    case PSP_OP_NODESSORT:
		msg->opt[out].value = config->nodesSort;
		break;
	    case PSP_OP_ACCT:
		send_acct_OPTIONS(msg->header.sender, 1);
		/* Do not send option again */
		out--;
		break;
	    default:
		PSID_log(-1, "%s: unknown option %d\n", __func__,
			 msg->opt[in].option);
		msg->opt[out].option = PSP_OP_UNKNOWN;
	    }
	}

	/*
	 * prepare the message to route it to the receiver
	 */
	msg->header.len = sizeof(*msg);
	msg->header.type = PSP_CD_SETOPTION;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();
	msg->count = out;

	if (msg->count) sendMsg(msg);
    }
}
