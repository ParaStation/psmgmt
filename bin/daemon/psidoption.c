/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psidoption.h"

#include <stdbool.h>
#include <errno.h>
#include <sys/resource.h>

#include "rdp.h"
#include "mcast.h"
#include "selector.h"

#include "pscommon.h"
#include "psdaemonprotocol.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidnodes.h"
#include "psidtask.h"
#include "psidstatus.h"
#include "psidhook.h"
#include "psidaccount.h"
#include "psidplugin.h"
#include "psidclient.h"

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
    if (PSIDnodes_getProtoV(destnode) < 344) {
	msg.opt[(int) msg.count].option = PSP_OP_PROTOCOLVERSION;
	msg.opt[(int) msg.count].value = PSProtocolVersion;
	msg.count++;
	msg.opt[(int) msg.count].option = PSP_OP_DAEMONPROTOVERSION;
	msg.opt[(int) msg.count].value = PSDaemonProtocolVersion;
	msg.count++;
    }
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
    msg.opt[(int) msg.count].option = PSP_OP_BINDGPUS;
    msg.opt[(int) msg.count].value = PSIDnodes_bindGPUs(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_BINDNICS;
    msg.opt[(int) msg.count].value = PSIDnodes_bindNICs(PSC_getMyID());
    msg.count++;
    msg.opt[(int) msg.count].option = PSP_OP_SUPPL_GRPS;
    msg.opt[(int) msg.count].value = PSIDnodes_supplGrps(PSC_getMyID());
    msg.count++;
    /* max 3 left (if ProtoV < 344) */

    if (sendMsg(&msg) == -1 && errno != EWOULDBLOCK) {
	PSID_warn(-1, errno, "%s: sendMsg()", __func__);
    }

    send_acct_OPTIONS(PSC_getTID(destnode, 0), 0);
    send_GUID_OPTIONS(PSC_getTID(destnode, 0), PSIDNODES_USER);
    send_GUID_OPTIONS(PSC_getTID(destnode, 0), PSIDNODES_GROUP);
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
    default:
	PSID_flog("invalid type %d\n", type);
	guid.u = 0;
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

static void set_rlimit(PSP_Option_t option, PSP_Optval_t value)
{
    int resource;
    struct rlimit limit;

    switch (option) {
#ifdef RLIMIT_AS
    case PSP_OP_RL_AS:
	resource = RLIMIT_AS;
	break;
#endif
    case PSP_OP_RL_CORE:
	resource = RLIMIT_CORE;
	break;
    case PSP_OP_RL_CPU:
	resource = RLIMIT_CPU;
	break;
    case PSP_OP_RL_DATA:
	resource = RLIMIT_DATA;
	break;
    case PSP_OP_RL_FSIZE:
	resource = RLIMIT_FSIZE;
	break;
    case PSP_OP_RL_LOCKS:
	resource = RLIMIT_LOCKS;
	break;
    case PSP_OP_RL_MEMLOCK:
	resource = RLIMIT_MEMLOCK;
	break;
#ifdef RLIMIT_MSGQUEUE
    case PSP_OP_RL_MSGQUEUE:
	resource = RLIMIT_MSGQUEUE;
	break;
#endif
    case PSP_OP_RL_NOFILE:
	resource = RLIMIT_NOFILE;
	break;
    case PSP_OP_RL_NPROC:
	resource = RLIMIT_NPROC;
	break;
    case PSP_OP_RL_RSS:
	resource = RLIMIT_RSS;
	break;
#ifdef RLIMIT_SIGPENDING
    case PSP_OP_RL_SIGPENDING:
	resource = RLIMIT_SIGPENDING;
	break;
#endif
    case PSP_OP_RL_STACK:
	resource = RLIMIT_STACK;
	break;
    default:
	PSID_flog("unknown option %d\n", option);
	return;
    }

    limit.rlim_cur = value;
    limit.rlim_max = value;

    if (setrlimit(resource, &limit)) {
	PSID_warn(-1, errno, "%s: setrlimit(%d, %d)", __func__,
		  resource, value);
    } else {
	/* We might have to inform other facilities, too */
	switch (resource) {
	case RLIMIT_NOFILE:
	    if (limit.rlim_cur == RLIM_INFINITY) {
		PSID_flog("cannot handle unlimited files\n");
		break;
	    }
	    if (PSIDclient_setMax(value) < 0) {
		PSID_exit(errno, "%s: Failed to adapt PSIDclient", __func__);
	    }
	    if (Selector_setMax(value) < 0) {
		PSID_exit(errno, "%s: Failed to adapt Selector", __func__);
	    }
	    break;
	default:
	    /* nothing to do */
	    break;
	}
    }
}


/**
 * @brief Handle a PSP_CD_SETOPTION message
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
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_SETOPTION(DDOptionMsg_t *msg)
{
    PSID_fdbg(PSID_LOG_OPTION, "from requester %s\n",
	      PSC_printTID(msg->header.sender));

    if (!PSID_checkPrivilege(msg->header.sender)) {
	PSID_flog("task %s not allowed to modify options\n",
		  PSC_printTID(msg->header.sender));
	return true;
    }

    if (msg->header.dest == PSC_getMyTID()) {
	/* Message is for me */
	for (int i = 0; i < msg->count; i++) {
	    PSIDnodes_gu_t guType;
	    PSIDnodes_guid_t guid;

	    PSID_fdbg(PSID_LOG_OPTION, "option %d value 0x%x\n",
		      msg->opt[i].option, msg->opt[i].value);

	    switch (msg->opt[i].option) {
	    case PSP_OP_PSIDSELECTTIME:
		if (msg->opt[i].value > 0) {
		    PSID_config->selectTime = msg->opt[i].value;
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
		if (PSC_validNode(id) && !PSIDnodes_isUp(id)) {
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
	    case PSP_OP_BINDGPUS:
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

		    PSIDnodes_setBindGPUs(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my BINDGPUS */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setBindGPUs(PSC_getID(msg->header.sender),
					  msg->opt[i].value);
		}
		break;
	    case PSP_OP_BINDNICS:
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

		    PSIDnodes_setBindNICs(PSC_getMyID(), msg->opt[i].value);

		    /* Info all nodes about my BINDNICS */
		    broadcastMsg(&info);
		} else {
		    PSIDnodes_setBindNICs(PSC_getID(msg->header.sender),
					  msg->opt[i].value);
		}
		break;
	    case PSP_OP_CLR_CPUMAP:
		PSIDnodes_clearCPUMap(PSC_getMyID());
		break;
	    case PSP_OP_APP_CPUMAP:
		PSIDnodes_appendCPUMap(PSC_getMyID(), msg->opt[i].value);
		break;
	    case PSP_OP_TRIGGER_DIST:
		PSIDhook_call(PSIDHOOK_DIST_INFO, &msg->opt[i].value);
		break;
	    case PSP_OP_ALLOWUSERMAP:
		PSIDnodes_setAllowUserMap(PSC_getMyID(), msg->opt[i].value);
		break;
	    case PSP_OP_HWSTATUS:
		PSIDnodes_setHWStatus(PSC_getID(msg->header.sender),
				      msg->opt[i].value);
		break;
	    case PSP_OP_PROTOCOLVERSION:
		PSIDnodes_setProtoV(PSC_getID(msg->header.sender),
				    msg->opt[i].value);
		break;
	    case PSP_OP_DAEMONPROTOVERSION:
		PSIDnodes_setDmnProtoV(PSC_getID(msg->header.sender),
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
	    case PSP_OP_RDPRETRANS:
		setRetransRDP(msg->opt[i].value);
		break;
	    case PSP_OP_RDPSTATISTICS:
		RDP_setStatistics(msg->opt[i].value);
		break;
	    case PSP_OP_MCASTDEBUG:
		setDebugMaskMCast(msg->opt[i].value);
		break;
	    case PSP_OP_FREEONSUSP:
		PSID_config->freeOnSuspend = msg->opt[i].value;
		break;
	    case PSP_OP_NODESSORT:
		PSID_config->nodesSort = msg->opt[i].value;
		break;
	    case PSP_OP_ADD_ACCT:
		PSID_addAcct(msg->opt[i].value);
		break;
	    case PSP_OP_REM_ACCT:
		PSID_remAcct(msg->opt[i].value);
		break;
	    case PSP_OP_KILLDELAY:
		if (PSC_getPID(msg->header.sender)) {
		    /* Ignore info sent by other daemons for compatibility */
		    PSIDnodes_setKillDelay(PSC_getMyID(), msg->opt[i].value);
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
	    case PSP_OP_MAXSTATTRY:
		PSIDnodes_setMaxStatTry(PSC_getMyID(), msg->opt[i].value);
		break;
	    case PSP_OP_PLUGINUNLOADTMOUT:
		PSIDplugin_setUnloadTmout(msg->opt[i].value);
		break;
	    case PSP_OP_OBSOLETE:
		PStasklist_cleanupObsolete();
		break;
	    case PSP_OP_LISTEND:
		/* Ignore */
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
		set_rlimit(msg->opt[i].option, msg->opt[i].value);
		break;
	    default:
		PSID_flog("unknown option %d\n", msg->opt[i].option);
	    }
	}
    } else {
	/* Message is for a remote node */
	sendMsg(msg);
    }
    return true;
}

/**
 * @brief Handle a PSP_CD_GETOPTION message
 *
 * Handle the message @a inmsg of type PSP_CD_GETOPTION.
 *
 * If the final destination of @a msg is the local daemon, the
 * requested options within this message are determined and send
 * within a PSP_CD_SETOPTION to the requester. Otherwise this message
 * es forwarded to the corresponding remote daemon.
 *
 * @param inmsg Pointer to message to handle
 *
 * @return Always return true
 */
static bool msg_GETOPTION(DDOptionMsg_t *msg)
{
    int id = PSC_getID(msg->header.dest);

    PSID_fdbg(PSID_LOG_OPTION, "from node %d for requester %s\n",
	      id, PSC_printTID(msg->header.sender));

    if (id!=PSC_getMyID()) {
	DDErrorMsg_t errmsg = {
	    .header = {
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

	    PSID_fdbg(PSID_LOG_OPTION, "option %d\n", msg->opt[in].option);

	    msg->opt[out].option = msg->opt[in].option;

	    switch (msg->opt[in].option) {
	    case PSP_OP_PROTOCOLVERSION:
		msg->opt[out].value = PSIDnodes_getProtoV(PSC_getMyID());
		break;
	    case PSP_OP_DAEMONPROTOVERSION:
		msg->opt[out].value = PSIDnodes_getDmnProtoV(PSC_getMyID());
		break;
	    case PSP_OP_PSIDDEBUG:
		msg->opt[out].value = PSID_getDebugMask();
		break;
	    case PSP_OP_PSIDSELECTTIME:
		msg->opt[out].value = PSID_config->selectTime;
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
	    case PSP_OP_UID:
	    case PSP_OP_GID:
	    case PSP_OP_ADMUID:
	    case PSP_OP_ADMGID:
		guType = getGU(msg->opt[in].option);
		send_GUID_OPTIONS(msg->header.sender, guType);
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
	    case PSP_OP_BINDGPUS:
		msg->opt[out].value = PSIDnodes_bindGPUs(PSC_getMyID());
		break;
	    case PSP_OP_BINDNICS:
		msg->opt[out].value = PSIDnodes_bindNICs(PSC_getMyID());
		break;
	    case PSP_OP_CPUMAP:
		send_CPUMap_OPTIONS(msg->header.sender);
		/* Do not send option again */
		out--;
		break;
	    case PSP_OP_ALLOWUSERMAP:
		msg->opt[out].value = PSIDnodes_allowUserMap(PSC_getMyID());
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
	    case PSP_OP_RDPRETRANS:
		msg->opt[out].value = getRetransRDP();
		break;
	    case PSP_OP_RDPSTATISTICS:
		msg->opt[out].value = RDP_getStatistics();
		break;
	    case PSP_OP_MCASTDEBUG:
		msg->opt[out].value = getDebugMaskMCast();
		break;
	    case PSP_OP_MASTER:
		msg->opt[out].value = getMasterID();
		break;
	    case PSP_OP_FREEONSUSP:
		msg->opt[out].value = PSID_config->freeOnSuspend;
		break;
	    case PSP_OP_NODESSORT:
		msg->opt[out].value = PSID_config->nodesSort;
		break;
	    case PSP_OP_ACCT:
		send_acct_OPTIONS(msg->header.sender, 1);
		/* Do not send option again */
		out--;
		break;
	    case PSP_OP_KILLDELAY:
		msg->opt[out].value = PSIDnodes_killDelay(PSC_getMyID());
		break;
	    case PSP_OP_SUPPL_GRPS:
		msg->opt[out].value = PSIDnodes_supplGrps(PSC_getMyID());
		break;
	    case PSP_OP_MAXSTATTRY:
		msg->opt[out].value = PSIDnodes_maxStatTry(PSC_getMyID());
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
	    case PSP_OP_PLUGINAPIVERSION:
		msg->opt[out].value = PSIDplugin_getAPIversion();
		break;
	    case PSP_OP_PLUGINUNLOADTMOUT:
		msg->opt[out].value = PSIDplugin_getUnloadTmout();
		break;
	    case PSP_OP_OBSOLETE:
		msg->opt[out].value = PStasklist_count(&obsoleteTasks);
		break;
	    case PSP_OP_NODELISTHASH:
		msg->opt[out].value = PSID_config->nodeListHash;
		break;
	    default:
		PSID_flog("unknown option %d\n", msg->opt[in].option);
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
    return true;
}

/**
 * @brief Drop a PSP_CD_GETOPTION message
 *
 * Drop the message @a msg of type PSP_CD_GETOPTION.
 *
 * Since the requesting process waits for a reaction to its request a
 * corresponding answer is created.
 *
 * @param msg Pointer to message to drop
 *
 * @return Always return true
 */
static bool drop_GETOPTION(DDBufferMsg_t *msg)
{
    DDErrorMsg_t errmsg = {
	.header = {
	    .type = PSP_CD_ERROR,
	    .dest = msg->header.sender,
	    .sender = PSC_getMyTID(),
	    .len = sizeof(errmsg) },
	.error = EHOSTUNREACH,
	.request = msg->header.type };
    sendMsg(&errmsg);
    return true;
}

void initOptions(void)
{
    PSID_fdbg(PSID_LOG_VERB, "\n");

    PSID_registerMsg(PSP_CD_SETOPTION, (handlerFunc_t) msg_SETOPTION);
    PSID_registerMsg(PSP_CD_GETOPTION, (handlerFunc_t) msg_GETOPTION);

    PSID_registerDropper(PSP_CD_GETOPTION, drop_GETOPTION);
}
