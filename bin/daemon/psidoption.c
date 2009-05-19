/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "rdp.h"
#include "mcast.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "hardware.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidnodes.h"
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
    msg.opt[(int) msg.count].value = PSIDnodes_getHWStatus(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_PROTOCOLVERSION;
    msg.opt[(int) msg.count].value = PSProtocolVersion;
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_DAEMONPROTOVERSION;
    msg.opt[(int) msg.count].value = PSDaemonProtocolVersion;
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_PROCLIMIT;
    msg.opt[(int) msg.count].value = PSIDnodes_getProcs(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_OVERBOOK;
    msg.opt[(int) msg.count].value = PSIDnodes_overbook(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_EXCLUSIVE;
    msg.opt[(int) msg.count].value = PSIDnodes_exclusive(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_STARTER;
    msg.opt[(int) msg.count].value = PSIDnodes_isStarter(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_RUNJOBS;
    msg.opt[(int) msg.count].value = PSIDnodes_runJobs(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_PINPROCS;
    msg.opt[(int) msg.count].value = PSIDnodes_pinProcs(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_BINDMEM;
    msg.opt[(int) msg.count].value = PSIDnodes_bindMem(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_ACCTPOLL;
    msg.opt[(int) msg.count].value = PSIDnodes_acctPollI(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_SUPPL_GRPS;
    msg.opt[(int) msg.count].value = PSIDnodes_supplGrps(PSC_getMyID());
    msg.count++;

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }

    send_acct_OPTIONS(PSC_getTID(destnode, 0), 0);
    send_GUID_OPTIONS(PSC_getTID(destnode, 0), PSIDNODES_USER, 0);
    send_GUID_OPTIONS(PSC_getTID(destnode, 0), PSIDNODES_GROUP, 0);
}

static PSIDnodes_gu_t getGU(PSP_Option_t opt)
{
    switch (opt) {
    case PSP_OP_UID:
    case PSP_OP_SET_UID:
    case PSP_OP_ADD_UID:
    case PSP_OP_REM_UID:
	return PSIDNODES_USER;
	break;
    case PSP_OP_GID:
    case PSP_OP_SET_GID:
    case PSP_OP_ADD_GID:
    case PSP_OP_REM_GID:
	return PSIDNODES_GROUP;
	break;
    case PSP_OP_ADMUID:
    case PSP_OP_SET_ADMUID:
    case PSP_OP_ADD_ADMUID:
    case PSP_OP_REM_ADMUID:
	return PSIDNODES_ADMUSER;
	break;
    case PSP_OP_ADMGID:
    case PSP_OP_SET_ADMGID:
    case PSP_OP_ADD_ADMGID:
    case PSP_OP_REM_ADMGID:
	return PSIDNODES_ADMGROUP;
	break;
    default:
	return -1;
    }
}

static PSIDnodes_guid_t getGUID(PSIDnodes_gu_t type, PSP_Optval_t val)
{
    PSIDnodes_guid_t guid;

    switch (type) {
    case PSIDNODES_USER:
    case PSIDNODES_ADMUSER:
	guid.u=val;
	break;
    case PSIDNODES_GROUP:
    case PSIDNODES_ADMGROUP:
	guid.g=val;
	break;
    }

    return guid;
}

static void send_rlimit_OPTIONS(PStask_ID_t dest, PSP_Option_t option)
{
    DDOptionMsg_t msg = {
	.header = {
	    .type = PSP_CD_SETOPTION,
	    .sender = PSC_getMyTID(),
	    .dest = dest,
	    .len = sizeof(msg) },
	.count = 0,
	.opt = {{ .option = 0, .value = 0 }} };
    struct rlimit limit;
    int unknown=0;

    switch (option) {
#ifdef RLIMIT_AS
    case PSP_OP_RL_AS:
	getrlimit(RLIMIT_AS, &limit);
	break;
#endif
    case PSP_OP_RL_CORE:
	getrlimit(RLIMIT_CORE, &limit);
	break;
    case PSP_OP_RL_CPU:
	getrlimit(RLIMIT_CPU, &limit);
	break;
    case PSP_OP_RL_DATA:
	getrlimit(RLIMIT_DATA, &limit);
	break;
    case PSP_OP_RL_FSIZE:
	getrlimit(RLIMIT_FSIZE, &limit);
	break;
    case PSP_OP_RL_LOCKS:
	getrlimit(RLIMIT_LOCKS, &limit);
	break;
    case PSP_OP_RL_MEMLOCK:
	getrlimit(RLIMIT_MEMLOCK, &limit);
	break;
#ifdef RLIMIT_MSGQUEUE
    case PSP_OP_RL_MSGQUEUE:
	getrlimit(RLIMIT_MSGQUEUE, &limit);
	break;
#endif
    case PSP_OP_RL_NOFILE:
	getrlimit(RLIMIT_NOFILE, &limit);
	break;
    case PSP_OP_RL_NPROC:
	getrlimit(RLIMIT_NPROC, &limit);
	break;
    case PSP_OP_RL_RSS:
	getrlimit(RLIMIT_RSS, &limit);
	break;
#ifdef RLIMIT_SIGPENDING
    case PSP_OP_RL_SIGPENDING:
	getrlimit(RLIMIT_SIGPENDING, &limit);
	break;
#endif
    case PSP_OP_RL_STACK:
	getrlimit(RLIMIT_STACK, &limit);
	break;
    default:
	unknown=1;
    }

    if (unknown) {
	msg.opt[(int) msg.count].option = PSP_OP_UNKNOWN;
	msg.opt[(int) msg.count].value = -1;
	msg.count++;
    } else {
	msg.opt[(int) msg.count].option = option;
	msg.opt[(int) msg.count].value = limit.rlim_cur;
	msg.count++;

	msg.opt[(int) msg.count].option = option;
	msg.opt[(int) msg.count].value = limit.rlim_max;
	msg.count++;
    }

    msg.opt[(int) msg.count].option = PSP_OP_LISTEND;
    msg.opt[(int) msg.count].value = 0;
    msg.count++;
    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }
}


/**
 * @brief Handle a PSP_CD_SETOPTION message.
 *
 * Handle the message @a inmsg of type PSP_CD_SETOPTION.
 *
 * If the final destination of @a msg is the local daemon, the options
 * provided within this message are set to the corresponding
 * values. Otherwise this message es forwarded, either to the
 * corresponding local or remote client process or a remote daemon.
 *
 * PSP_CD_SETOPTION messages to client processes are usually responses
 * to PSP_CD_GETOPTION request of this processes.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_SETOPTION(DDOptionMsg_t *msg)
{
    int i;

    PSID_log(PSID_LOG_OPTION, "%s: from requester %s\n",
	     __func__, PSC_printTID(msg->header.sender));

    if (msg->header.dest == PSC_getMyTID()) {
	/* Message is for me */
	for (i=0; i<msg->count; i++) {
	    PSIDnodes_gu_t guType;
	    PSIDnodes_guid_t guid;

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
	    case PSP_OP_STATUS_TMOUT:
		if (msg->opt[i].value > 0) {
		    setStatusTimeout(msg->opt[i].value);
		}
		break;
	    case PSP_OP_STATUS_DEADLMT:
		if (msg->opt[i].value > 0) {
		    setDeadLimit(msg->opt[i].value);
		}
		break;
	    case PSP_OP_STATUS_BCASTS:
		if (msg->opt[i].value >= 0) {
		    setMaxStatBCast(msg->opt[i].value);
		}
		break;
	    case PSP_OP_MASTER:
	    {
		PSnodes_ID_t id = msg->opt[i].value;
		if (PSIDnodes_validID(id) && !PSIDnodes_isUp(id)) {
		    send_DAEMONCONNECT(id);
		}
		break;
	    }
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

		    PSIDnodes_setProcs(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my PROCLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setProcs(PSC_getID(msg->header.sender),
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

		    PSIDnodes_setGUID(PSC_getMyID(), PSIDNODES_USER,
				     (PSIDnodes_guid_t){.u=msg->opt[i].value});

		    /* Info all nodes about my UIDLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setGUID(PSC_getID(msg->header.sender),
				      PSIDNODES_USER,
				     (PSIDnodes_guid_t){.u=msg->opt[i].value});
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

		    PSIDnodes_setGUID(PSC_getMyID(), PSIDNODES_ADMUSER,
				     (PSIDnodes_guid_t){.u=msg->opt[i].value});

		    /* Info all nodes about my ADMINUID */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setGUID(PSC_getID(msg->header.sender),
				      PSIDNODES_ADMUSER,
				     (PSIDnodes_guid_t){.u=msg->opt[i].value});
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

		    PSIDnodes_setGUID(PSC_getMyID(), PSIDNODES_GROUP,
				     (PSIDnodes_guid_t){.g=msg->opt[i].value});

		    /* Info all nodes about my GIDLIMIT */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setGUID(PSC_getID(msg->header.sender),
				      PSIDNODES_GROUP,
				     (PSIDnodes_guid_t){.g=msg->opt[i].value});
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

		    PSIDnodes_setGUID(PSC_getMyID(), PSIDNODES_ADMGROUP,
				     (PSIDnodes_guid_t){.g=msg->opt[i].value});

		    /* Info all nodes about my ADMINGID */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setGUID(PSC_getID(msg->header.sender),
				      PSIDNODES_ADMGROUP,
				     (PSIDnodes_guid_t){.g=msg->opt[i].value});
		}
		break;
	    case PSP_OP_SET_UID:
	    case PSP_OP_SET_GID:
	    case PSP_OP_SET_ADMUID:
	    case PSP_OP_SET_ADMGID:
		guType = getGU(msg->opt[i].option);
		guid = getGUID(guType, msg->opt[i].value);

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

		    PSIDnodes_setGUID(PSC_getMyID(), guType, guid);
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setGUID(PSC_getID(msg->header.sender),
				      guType, guid);
		}
		break;
	    case PSP_OP_ADD_UID:
	    case PSP_OP_ADD_GID:
	    case PSP_OP_ADD_ADMUID:
	    case PSP_OP_ADD_ADMGID:
		guType = getGU(msg->opt[i].option);
		guid = getGUID(guType, msg->opt[i].value);

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

		    PSIDnodes_addGUID(PSC_getMyID(), guType, guid);
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_addGUID(PSC_getID(msg->header.sender),
				      guType, guid);
		}
		break;
	    case PSP_OP_REM_UID:
	    case PSP_OP_REM_GID:
	    case PSP_OP_REM_ADMUID:
	    case PSP_OP_REM_ADMGID:
		guType = getGU(msg->opt[i].option);
		guid = getGUID(guType, msg->opt[i].value);

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

		    PSIDnodes_remGUID(PSC_getMyID(), guType, guid);
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_remGUID(PSC_getID(msg->header.sender),
				      guType, guid);
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

		    PSIDnodes_setOverbook(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my OVERBOOK */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setOverbook(PSC_getID(msg->header.sender),
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

		    PSIDnodes_setExclusive(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my EXCLUSIVE */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setExclusive(PSC_getID(msg->header.sender),
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

		    PSIDnodes_setIsStarter(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my STARTER */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setIsStarter(PSC_getID(msg->header.sender),
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

		    PSIDnodes_setRunJobs(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my RUNJOBS */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setRunJobs(PSC_getID(msg->header.sender),
					 msg->opt[i].value);
		}
		break;
	    case PSP_OP_PINPROCS:
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

		    PSIDnodes_setPinProcs(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my PINPROCS */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setPinProcs(PSC_getID(msg->header.sender),
					  msg->opt[i].value);
		}
		break;
	    case PSP_OP_BINDMEM:
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

		    PSIDnodes_setBindMem(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my BINDMEM */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setBindMem(PSC_getID(msg->header.sender),
					 msg->opt[i].value);
		}
		break;
	    case PSP_OP_CLR_CPUMAP:
		PSIDnodes_clearCPUMap(PSC_getMyID());
		break;
	    case PSP_OP_APP_CPUMAP:
		PSIDnodes_appendCPUMap(PSC_getMyID(), msg->opt[i].value);
		break;
	    case PSP_OP_HWSTATUS:
		PSIDnodes_setHWStatus(PSC_getID(msg->header.sender),
				      msg->opt[i].value);
		break;
	    case PSP_OP_PROTOCOLVERSION:
		PSIDnodes_setProtoVersion(PSC_getID(msg->header.sender),
					     msg->opt[i].value);
		break;
	    case PSP_OP_DAEMONPROTOVERSION:
		PSIDnodes_setDaemonProtoVersion(PSC_getID(msg->header.sender),
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
	    case PSP_OP_RDPTMOUT:
		setTmOutRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPMAXRETRANS:
		setMaxRetransRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPMAXACKPEND:
		setMaxAckPendRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPRSNDTMOUT:
		setRsndTmOutRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPCLSDTMOUT:
		setClsdTmOutRDP(msg->opt[i].value);
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
	    case PSP_OP_ACCTPOLL:
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

		    PSIDnodes_setAcctPollI(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my ACCTPOLL */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setAcctPollI(PSC_getID(msg->header.sender),
					   msg->opt[i].value);
		}
		break;
	    case PSP_OP_SUPPL_GRPS:
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

		    PSIDnodes_setSupplGrps(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my SUPPL_GRPS */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setSupplGrps(PSC_getID(msg->header.sender),
					  msg->opt[i].value);
		}
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

/**
 * @brief Handle a PSP_CD_GETOPTION message.
 *
 * Handle the message @a inmsg of type PSP_CD_GETOPTION.
 *
 * If the final destination of @a msg is the local daemon, the
 * requested options within this message are determined and send
 * within a PSP_CD_SETOPTION to the requestor. Otherwise this message
 * es forwarded to the corresponding remote daemon.
 *
 * @param inmsg Pointer to the message to handle.
 *
 * @return No return value.
 */
static void msg_GETOPTION(DDOptionMsg_t *msg)
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
	if (PSIDnodes_isUp(id)) {
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
	    PSIDnodes_gu_t guType;

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
	    case PSP_OP_PROTOCOLVERSION:
		msg->opt[out].value =
		    PSIDnodes_getProtoVersion(PSC_getMyID());
		break;
	    case PSP_OP_DAEMONPROTOVERSION:
		msg->opt[out].value =
		    PSIDnodes_getDaemonProtoVersion(PSC_getMyID());
		break;
	    case PSP_OP_PSIDDEBUG:
		msg->opt[out].value = PSID_getDebugMask();
		break;
	    case PSP_OP_PSIDSELECTTIME:
		msg->opt[out].value = selectTime.tv_sec;
		break;
	    case PSP_OP_STATUS_TMOUT:
		msg->opt[out].value = getStatusTimeout();
		break;
	    case PSP_OP_STATUS_DEADLMT:
		msg->opt[out].value = getDeadLimit();
		break;
	    case PSP_OP_STATUS_BCASTS:
		msg->opt[out].value = getMaxStatBCast();
		break;
	    case PSP_OP_PROCLIMIT:
		msg->opt[out].value = PSIDnodes_getProcs(PSC_getMyID());
		break;
	    case PSP_OP_UIDLIMIT:
		send_GUID_OPTIONS(msg->header.sender, PSIDNODES_USER, 1);
		/* Do not send option again */
		out--;
		break;
	    case PSP_OP_GIDLIMIT:
		send_GUID_OPTIONS(msg->header.sender, PSIDNODES_GROUP, 1);
		/* Do not send option again */
		out--;
		break;
	    case PSP_OP_ADMINUID:
		send_GUID_OPTIONS(msg->header.sender, PSIDNODES_ADMUSER, 1);
		/* Do not send option again */
		out--;
		break;
	    case PSP_OP_ADMINGID:
		send_GUID_OPTIONS(msg->header.sender, PSIDNODES_ADMGROUP, 1);
		/* Do not send option again */
		out--;
		break;
	    case PSP_OP_UID:
	    case PSP_OP_GID:
	    case PSP_OP_ADMUID:
	    case PSP_OP_ADMGID:
		guType = getGU(msg->opt[in].option);
		send_GUID_OPTIONS(msg->header.sender, guType, 0);
		/* Do not send option again */
		out--;
		break;
	    case PSP_OP_OVERBOOK:
		msg->opt[out].value = PSIDnodes_overbook(PSC_getMyID());
		break;
	    case PSP_OP_EXCLUSIVE:
		msg->opt[out].value = PSIDnodes_exclusive(PSC_getMyID());
		break;
	    case PSP_OP_STARTER:
		msg->opt[out].value = PSIDnodes_isStarter(PSC_getMyID());
		break;
	    case PSP_OP_RUNJOBS:
		msg->opt[out].value = PSIDnodes_runJobs(PSC_getMyID());
		break;
	    case PSP_OP_PINPROCS:
		msg->opt[out].value = PSIDnodes_pinProcs(PSC_getMyID());
		break;
	    case PSP_OP_BINDMEM:
		msg->opt[out].value = PSIDnodes_bindMem(PSC_getMyID());
		break;
	    case PSP_OP_CPUMAP:
		send_CPUMap_OPTIONS(msg->header.sender);
		/* Do not send option again */
		out--;
		break;
	    case PSP_OP_RDPDEBUG:
		msg->opt[out].value = getDebugMaskRDP();
		break;
	    case PSP_OP_RDPPKTLOSS:
		msg->opt[out].value = getPktLossRDP();
		break;
	    case PSP_OP_RDPTMOUT:
		msg->opt[out].value = getTmOutRDP();
		break;
	    case PSP_OP_RDPMAXRETRANS:
		msg->opt[out].value = getMaxRetransRDP();
		break;
	    case PSP_OP_RDPMAXACKPEND:
		msg->opt[out].value = getMaxAckPendRDP();
		break;
	    case PSP_OP_RDPRSNDTMOUT:
		msg->opt[out].value = getRsndTmOutRDP();
		break;
	    case PSP_OP_RDPCLSDTMOUT:
		msg->opt[out].value = getClsdTmOutRDP();
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
	    case PSP_OP_ACCTPOLL:
		msg->opt[out].value = PSIDnodes_acctPollI(PSC_getMyID());
		break;
	    case PSP_OP_SUPPL_GRPS:
		msg->opt[out].value = PSIDnodes_supplGrps(PSC_getMyID());
		break;
	    case PSP_OP_RL_AS:
	    case PSP_OP_RL_CORE:
	    case PSP_OP_RL_CPU:
	    case PSP_OP_RL_DATA:
	    case PSP_OP_RL_FSIZE:
	    case PSP_OP_RL_LOCKS:
	    case PSP_OP_RL_MEMLOCK:
	    case PSP_OP_RL_MSGQUEUE:
	    case PSP_OP_RL_NOFILE:
	    case PSP_OP_RL_NPROC:
	    case PSP_OP_RL_RSS:
	    case PSP_OP_RL_SIGPENDING:
	    case PSP_OP_RL_STACK:
		send_rlimit_OPTIONS(msg->header.sender, msg->opt[in].option);
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

void initOptions(void)
{
    PSID_log(PSID_LOG_VERB, "%s()\n", __func__);

    PSID_registerMsg(PSP_CD_SETOPTION, (handlerFunc_t) msg_SETOPTION);
    PSID_registerMsg(PSP_CD_GETOPTION, (handlerFunc_t) msg_GETOPTION);
}
