/*
 *               ParaStation
 * psidoption.c
 *
 * Handle option requests to the ParaStation daemon.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidoption.c,v 1.3 2003/10/29 17:21:53 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidoption.c,v 1.3 2003/10/29 17:21:53 eicker Exp $";
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

#include "psidoption.h"

static char errtxt[256]; /**< General string to create error messages */

extern struct timeval selecttimer;

void send_OPTIONS(int destnode)
{
    DDOptionMsg_t msg;

    msg.header.type = PSP_CD_SETOPTION;
    msg.header.sender = PSC_getMyTID();
    msg.header.dest = PSC_getTID(destnode, 0);
    msg.header.len = sizeof(msg);

    msg.count = 0;

    msg.opt[(int) msg.count].option = PSP_OP_HWSTATUS;
    msg.opt[(int) msg.count].value = PSnodes_getHWStatus(PSC_getMyID());
    msg.count++;

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	snprintf(errtxt, sizeof(errtxt),
		 "%s: sendMsg(): errno %d", __func__, errno);
	PSID_errlog(errtxt, 0);
    }
}

void msg_SETOPTION(DDOptionMsg_t *msg)
{
    int i;

    snprintf(errtxt, sizeof(errtxt), "%s from requester %s",
	     __func__, PSC_printTID(msg->header.sender));
    PSID_errlog(errtxt, 1);

    if (msg->header.dest == PSC_getMyTID()) {
	/* Message is for me */
	for (i=0; i<msg->count; i++) {
	    snprintf(errtxt, sizeof(errtxt), "%s: option %d value 0x%x",
		     __func__, msg->opt[i].option, msg->opt[i].value);
	    PSID_errlog(errtxt, 3);

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
		    selecttimer.tv_sec = msg->opt[i].value;
		}
	    break;
	    case PSP_OP_PROCLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info;
		    PSnodes_setProcs(PSC_getMyID(), msg->opt[i].value);

		    info.header.type = PSP_CD_SETOPTION;
		    info.header.sender = PSC_getMyTID();
		    info.header.len = sizeof(info);

		    info.count = 1;
		    info.opt[0].option = msg->opt[i].option;
		    info.opt[0].value = msg->opt[i].value;

		    /* Info all nodes about my PROCLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setProcs(PSC_getID(msg->header.sender),
				     msg->opt[i].value);
		}
		break;
	    case PSP_OP_UIDLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info;
		    PSnodes_setUser(PSC_getMyID(), msg->opt[i].value);

		    info.header.type = PSP_CD_SETOPTION;
		    info.header.sender = PSC_getMyTID();
		    info.header.len = sizeof(info);

		    info.count = 1;
		    info.opt[0].option = msg->opt[i].option;
		    info.opt[0].value = msg->opt[i].value;

		    /* Info all nodes about my UIDLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setUser(PSC_getID(msg->header.sender),
				    msg->opt[i].value);
		}
		break;
	    case PSP_OP_GIDLIMIT:
		if (PSC_getPID(msg->header.sender)) {
		    DDOptionMsg_t info;
		    PSnodes_setGroup(PSC_getMyID(), msg->opt[i].value);

		    info.header.type = PSP_CD_SETOPTION;
		    info.header.sender = PSC_getMyTID();
		    info.header.len = sizeof(info);

		    info.count = 1;
		    info.opt[0].option = msg->opt[i].option;
		    info.opt[0].value = msg->opt[i].value;

		    /* Info all nodes about my GIDLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSnodes_setGroup(PSC_getID(msg->header.sender),
				     msg->opt[i].value);
		}
		break;
	    case PSP_OP_HWSTATUS:
		PSnodes_setHWStatus(PSC_getID(msg->header.sender),
				    msg->opt[i].value);
		break;
	    case PSP_OP_CPUS:
		PSnodes_setCPUs(PSC_getID(msg->header.sender),
				msg->opt[i].value);
		break;
	    case PSP_OP_PSIDDEBUG:
		PSID_setDebugLevel(msg->opt[i].value);
		PSC_setDebugLevel(msg->opt[i].value);

		if (msg->opt[i].value) {
		    snprintf(errtxt, sizeof(errtxt),
			     "Debugging mode with debuglevel %d enabled",
			     msg->opt[i].value);
		} else {
		    snprintf(errtxt, sizeof(errtxt),
			     "Debugging mode disabled");
		}
		PSID_errlog(errtxt, 0);
		break;
	    case PSP_OP_RDPDEBUG:
		setDebugLevelRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPPKTLOSS:
		setPktLossRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPMAXRETRANS:
		setMaxRetransRDP(msg->opt[i].value);
		break;
	    case PSP_OP_MCASTDEBUG:
		setDebugLevelMCast(msg->opt[i].value);
		break;
	    default:
		snprintf(errtxt, sizeof(errtxt), "%s: unknown option %d",
			 __func__, msg->opt[i].option);
		PSID_errlog(errtxt, 0);
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

    snprintf(errtxt, sizeof(errtxt), "%s from node %d for requester %s",
	     __func__, id, PSC_printTID(msg->header.sender));
    PSID_errlog(errtxt, 1);

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
	int i;
	for (i=0; i<msg->count; i++) {
	    snprintf(errtxt, sizeof(errtxt), "%s option: %d",
		     __func__, msg->opt[i].option);
	    PSID_errlog(errtxt, 3);

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
		    msg->opt[i].value = PSID_getParam(MYRINETindex,
						      msg->opt[i].option);
		} else {
		    msg->opt[i].value = -1;
		}
		break;
	    }
	    case PSP_OP_PSIDDEBUG:
		msg->opt[i].value = PSID_getDebugLevel();
		break;
	    case PSP_OP_PSIDSELECTTIME:
		msg->opt[i].value = selecttimer.tv_sec;
		break;
	    case PSP_OP_PROCLIMIT:
		msg->opt[i].value = PSnodes_getProcs(PSC_getMyID());
		break;
	    case PSP_OP_UIDLIMIT:
		msg->opt[i].value = PSnodes_getUser(PSC_getMyID());
		break;
	    case PSP_OP_GIDLIMIT:
		msg->opt[i].value = PSnodes_getGroup(PSC_getMyID());
		break;
	    case PSP_OP_RDPDEBUG:
		msg->opt[i].value = getDebugLevelRDP();
		break;
	    case PSP_OP_RDPPKTLOSS:
		msg->opt[i].value = getPktLossRDP();
		break;
	    case PSP_OP_RDPMAXRETRANS:
		msg->opt[i].value = getMaxRetransRDP();
		break;
	    case PSP_OP_MCASTDEBUG:
		msg->opt[i].value = getDebugLevelMCast();
		break;
	    default:
		snprintf(errtxt, sizeof(errtxt), "%s: unknown option %d",
			 __func__, msg->opt[i].option);
		PSID_errlog(errtxt, 0);
		return;
	    }
	}

	/*
	 * prepare the message to route it to the receiver
	 */
	msg->header.len = sizeof(*msg);
	msg->header.type = PSP_CD_SETOPTION;
	msg->header.dest = msg->header.sender;
	msg->header.sender = PSC_getMyTID();

	sendMsg(msg);
    }
}
