/*
 * ParaStation
 *
 * Copyright (C) 2023-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "psslurmprototypes.h"

#include <stddef.h>

#include "pluginmalloc.h"
#include "slurmmsg.h"

#include "psslurmgres.h"
#include "psslurmstep.h"
#include "psslurmjob.h"
#include "psslurmlog.h"
#include "psslurmbcast.h"

/**
 * @brief Free a job info response
 *
 * @param resp The job info response to free
 */
static inline void freeRespJobInfo(Slurm_Msg_t *sMsg)
{
    Resp_Job_Info_t *resp = sMsg->unpData;

    for (uint32_t i = 0; i < resp->numJobs; i++) {
	Slurm_Job_Rec_t *rec = &resp->jobs[i];

	ufree(rec->arrayTaskStr);
	ufree(rec->hetJobIDset);
	ufree(rec->container);
	ufree(rec->cluster);
	ufree(rec->nodes);
	ufree(rec->schedNodes);
	ufree(rec->partition);
	ufree(rec->account);
	ufree(rec->adminComment);
	ufree(rec->network);
	ufree(rec->comment);
	ufree(rec->batchFeat);
	ufree(rec->batchHost);
	ufree(rec->burstBuffer);
	ufree(rec->burstBufferState);
	ufree(rec->systemComment);
	ufree(rec->qos);
	ufree(rec->licenses);
	ufree(rec->stateDesc);
	ufree(rec->resvName);
	ufree(rec->mcsLabel);
	ufree(rec->containerID);
	ufree(rec->failedNode);
	ufree(rec->extra);
    }
    ufree(resp->jobs);
    ufree(resp);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_LAUNCH_PROLOG message
 *
 * @param data The request to free
 */
static inline void freeReqLaunchProlog(Slurm_Msg_t *sMsg)
{
    Req_Launch_Prolog_t *req = sMsg->unpData;

    ufree(req->aliasList);
    ufree(req->nodes);
    ufree(req->partition);
    ufree(req->stdErr);
    ufree(req->stdOut);
    ufree(req->workDir);
    ufree(req->x11AllocHost);
    ufree(req->x11MagicCookie);
    ufree(req->x11Target);
    envDestroy(req->spankEnv);
    ufree(req->userName);
    freeJobCred(req->cred);
    freeGresJobAlloc(req->gresList);
    ufree(req->gresList);
    ufree(req);

    sMsg->unpData = NULL;
}

/**
 * @brief Free REQUEST_JOB_STEP_STAT
 * and REQUEST_JOB_STEP_PIDS messages
 *
 * @param data The request to free
 */
static inline void freeStepHead(Slurm_Msg_t *sMsg)
{
    Slurm_Step_Head_t *head = sMsg->unpData;
    ufree(head);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_LAUNCH_TASKS message
 *
 * @param data The request to free
 */
static inline void freeReqLaunchTasks(Slurm_Msg_t *sMsg)
{
    Step_t *step = sMsg->unpData;
    Step_destroy(step);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_BATCH_JOB_LAUNCH message
 *
 * @param data The request to free
 */
static inline void freeReqBatchJobLaunch(Slurm_Msg_t *sMsg)
{
    Job_t *job = sMsg->unpData;
    Job_delete(job);

    sMsg->unpData = NULL;
}

/**
 * @brief Free REQUEST_SIGNAL_TASKS and
 * REQUEST_TERMINATE_TASKS messages
 *
 * @param data The request to free
 */
static inline void freeReqSignalTasks(Slurm_Msg_t *sMsg)
{
    Req_Signal_Tasks_t *req = sMsg->unpData;
    ufree(req);

    sMsg->unpData = NULL;
}

/**
 * @brief Free REQUEST_KILL_PREEMPTED,
 * REQUEST_KILL_TIMELIMIT, REQUEST_ABORT_JOB and
 * REQUEST_TERMINATE_JOB messages
 *
 * @param data The request to free
 */
static inline void freeReqTermJob(Slurm_Msg_t *sMsg)
{
    Req_Terminate_Job_t *req = sMsg->unpData;

    envDestroy(req->spankEnv);
    freeGresJobAlloc(&req->gresList);
    freeJobCred(req->cred);
    freeGresCred(&req->gresJobList);
    ufree(req->nodes);
    ufree(req->workDir);
    ufree(req->details);
    ufree(req);

    sMsg->unpData = NULL;
}

/**
 * @brief Free REQUEST_RECONFIGURE_WITH_CONFIG and
 * RESPONSE_CONFIG messages
 *
 * @param data The request to free
 */
static inline void freeSlurmConfigMsg(Slurm_Msg_t *sMsg)
{
    Config_Msg_t *config = sMsg->unpData;

    /* new configuration message since 21.08 */
    for (uint32_t i = 0; i < config->numFiles; i++) {
	ufree(config->files[i].name);
	ufree(config->files[i].data);
    }
    ufree(config->files);

    /* old configuration message (remove with support for 20.11) */
    ufree(config->slurm_conf);
    ufree(config->acct_gather_conf);
    ufree(config->cgroup_conf);
    ufree(config->cgroup_allowed_dev_conf);
    ufree(config->ext_sensor_conf);
    ufree(config->gres_conf);
    ufree(config->knl_cray_conf);
    ufree(config->knl_generic_conf);
    ufree(config->plugstack_conf);
    ufree(config->topology_conf);
    ufree(config->xtra_conf);
    ufree(config->slurmd_spooldir);
    ufree(config);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_KILL_PREEMPTED,
 *
 * @param data The request to free
 */
static inline void freeReqSuspendInt(Slurm_Msg_t *sMsg)
{
    Req_Suspend_Int_t *req = sMsg->unpData;
    ufree(req);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_FILE_BCAST message
 *
 * @param data The request to free
 */
static inline void freeReqFileBCast(Slurm_Msg_t *sMsg)
{
    BCast_t *bcast = sMsg->unpData;
    BCast_delete(bcast);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_REATTACH_TASKS message
 *
 * @param data The request to free
 */
static inline void freeReqReattachTasks(Slurm_Msg_t *sMsg)
{
    Req_Reattach_Tasks_t *req = sMsg->unpData;

    freeJobCred(req->cred);
    ufree(req->ioPorts);
    ufree(req->ctlPorts);
    ufree(req);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_JOB_NOTIFY message
 *
 * @param data The request to free
 */
static inline void freeReqJobNotify(Slurm_Msg_t *sMsg)
{
    Req_Job_Notify_t *req = sMsg->unpData;
    ufree(req->msg);
    ufree(req);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a RESPONSE_NODE_REGISTRATION message
 *
 * @param data The request to free
 */
static inline void freeRespNodeReg(Slurm_Msg_t *sMsg)
{
    Ext_Resp_Node_Reg_t *resp = sMsg->unpData;

    for (uint32_t i=0; i<resp->count; i++) {
	ufree(resp->entry[i].name);
	ufree(resp->entry[i].type);
    }

    ufree(resp->nodeName);
    ufree(resp->entry);
    ufree(resp);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_REBOOT_NODES message
 *
 * @param data The request to free
 */
static inline void freeReqRebootNodes(Slurm_Msg_t *sMsg)
{
    Req_Reboot_Nodes_t *req = sMsg->unpData;
    ufree(req->nodeList);
    ufree(req->reason);
    ufree(req->features);
    ufree(req);

    sMsg->unpData = NULL;
}

/**
 * @brief Free a REQUEST_JOB_ID message
 *
 * @param data The request to free
 */
static inline void freeReqJobID(Slurm_Msg_t *sMsg)
{
    Req_Job_ID_t *req = sMsg->unpData;
    ufree(req);

    sMsg->unpData = NULL;
}

bool __freeUnpackMsgData(Slurm_Msg_t *sMsg, const char *caller, const int line)
{
    if (!sMsg->unpData) return true;

    switch (sMsg->head.type) {
    case REQUEST_LAUNCH_PROLOG:
	freeReqLaunchProlog(sMsg);
	break;
    case REQUEST_JOB_STEP_STAT:
    case REQUEST_JOB_STEP_PIDS:
	freeStepHead(sMsg);
	break;
    case REQUEST_LAUNCH_TASKS:
	freeReqLaunchTasks(sMsg);
	break;
    case REQUEST_BATCH_JOB_LAUNCH:
	freeReqBatchJobLaunch(sMsg);
	break;
    case REQUEST_SIGNAL_TASKS:
    case REQUEST_TERMINATE_TASKS:
	freeReqSignalTasks(sMsg);
	break;
    case REQUEST_REATTACH_TASKS:
	freeReqReattachTasks(sMsg);
	break;
    case REQUEST_KILL_PREEMPTED:
    case REQUEST_KILL_TIMELIMIT:
    case REQUEST_ABORT_JOB:
    case REQUEST_TERMINATE_JOB:
	freeReqTermJob(sMsg);
	break;
    case REQUEST_SUSPEND_INT:
	freeReqSuspendInt(sMsg);
	break;
    case REQUEST_RECONFIGURE_WITH_CONFIG:
    case RESPONSE_CONFIG:
	freeSlurmConfigMsg(sMsg);
	break;
    case REQUEST_FILE_BCAST:
	freeReqFileBCast(sMsg);
	break;
    case REQUEST_JOB_NOTIFY:
	freeReqJobNotify(sMsg);
	break;
    case RESPONSE_NODE_REGISTRATION:
	freeRespNodeReg(sMsg);
	break;
    case RESPONSE_JOB_INFO:
	freeRespJobInfo(sMsg);
	break;
    case REQUEST_JOB_ID:
	freeReqJobID(sMsg);
	break;
    case REQUEST_REBOOT_NODES:
	freeReqRebootNodes(sMsg);
	break;
	/* no unpacked data to free */
    case REQUEST_COMPLETE_BATCH_SCRIPT:
    case REQUEST_UPDATE_JOB_TIME:
    case REQUEST_SHUTDOWN:
    case REQUEST_RECONFIGURE:
    case REQUEST_NODE_REGISTRATION_STATUS:
    case REQUEST_PING:
    case REQUEST_HEALTH_CHECK:
    case REQUEST_ACCT_GATHER_UPDATE:
    case REQUEST_ACCT_GATHER_ENERGY:
    case REQUEST_STEP_COMPLETE:
    case REQUEST_STEP_COMPLETE_AGGR:
    case REQUEST_DAEMON_STATUS:
    case REQUEST_FORWARD_DATA:
    case REQUEST_NETWORK_CALLERID:
    case MESSAGE_COMPOSITE:
    case RESPONSE_MESSAGE_COMPOSITE:
    case RESPONSE_SLURM_RC:
	flog("error: unexpected data in %s, caller %s:%i\n",
	     msgType2String(sMsg->head.type), caller, line);
	return false;
    default:
	flog("error: no cleanup function for %s, caller: %s:%i\n",
	     msgType2String(sMsg->head.type), caller, line);
	return false;
    }

    if (sMsg->unpData) {
	flog("error: pending data in %s, caller %s:%i\n",
	     msgType2String(sMsg->head.type), caller, line);
	return false;
    }

    return true;
}
