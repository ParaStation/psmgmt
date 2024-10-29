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

    for (uint32_t i = 0; i < resp->numSlices; i++) {
	Job_Info_Slice_t *slice = &resp->slices[i];

	ufree(slice->arrayTaskStr);
	ufree(slice->hetJobIDset);
	ufree(slice->container);
	ufree(slice->cluster);
	ufree(slice->nodes);
	ufree(slice->schedNodes);
	ufree(slice->partition);
	ufree(slice->account);
	ufree(slice->adminComment);
	ufree(slice->network);
	ufree(slice->comment);
	ufree(slice->batchFeat);
	ufree(slice->batchHost);
	ufree(slice->burstBuffer);
	ufree(slice->burstBufferState);
	ufree(slice->systemComment);
	ufree(slice->qos);
	ufree(slice->licenses);
	ufree(slice->stateDesc);
	ufree(slice->resvName);
	ufree(slice->mcsLabel);
	ufree(slice->containerID);
	ufree(slice->failedNode);
	ufree(slice->extra);
	ufree(slice->prioArray);
	ufree(slice->prioArrayParts);
	ufree(slice->resvPorts);
    }
    ufree(resp->slices);
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
    ufree(req->stepManager);

    freeSlurmJobRecord(&req->jobRec);
    freeSlurmNodeRecords(&req->nodeRecords);
    freeSlurmPartRecord(&req->partRec);

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

    strShred(req->ioKey);
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

static void freeSlurmGresNodeStates(list_t *nsList)
{
    if (!nsList) return;

    list_t *g, *tmp;
    list_for_each_safe(g, tmp, nsList) {
	Slurm_Gres_Node_State_t *state =
	    list_entry(g, Slurm_Gres_Node_State_t, next);

	list_del(&state->next);

	for (uint16_t i=0; i<state->topoCnt; i++) {
	    ufree(state->topoCoreBitmap[i]);
	    ufree(state->topoGresBitmap[i]);
	    ufree(state->topoResCoreBitmap[i]);
	}

	ufree(state->topoCoreBitmap);
	ufree(state->topoGresBitmap);
	ufree(state->topoResCoreBitmap);
	ufree(state->topoGresCountAlloc);
	ufree(state->topoGresCountAvail);
	ufree(state->topoTypeID);
	ufree(state->topoTypeName);
	ufree(state);
    }
}

void freeSlurmNodeRecords(list_t *nrList)
{
    if (!nrList) return;

    list_t *g, *tmp;
    list_for_each_safe(g, tmp, nrList) {
	Slurm_Node_Record_t *nr = list_entry(g, Slurm_Node_Record_t, next);

	list_del(&nr->next);

	ufree(nr->commName);
	ufree(nr->name);
	ufree(nr->nodeHostname);
	ufree(nr->comment);
	ufree(nr->extra);
	ufree(nr->reason);
	ufree(nr->features);
	ufree(nr->featuresAct);
	ufree(nr->gres);
	ufree(nr->instanceID);
	ufree(nr->instanceType);
	ufree(nr->cpuSpecList);
	ufree(nr->gpuSpecBitmap);
	ufree(nr->mcsLabel);

	freeSlurmGresNodeStates(&nr->gresNodeStates);
	ufree(nr);
    }
}

void freeSlurmPartRecord(Slurm_Part_Record_t *pr)
{
    if (!pr) return;

    ufree(pr->name);
    ufree(pr->allowAccounts);
    ufree(pr->allowGroups);
    ufree(pr->allowQOS);
    ufree(pr->qosName);
    ufree(pr->allowAllocNodes);
    ufree(pr->alternate);
    ufree(pr->denyAccounts);
    ufree(pr->denyQOS);
    ufree(pr->origNodes);
}

static void freeSlurmJobArray(Slurm_Job_Array_t *ja)
{
    if (!ja) return;

    ufree(ja->taskIDBitmap);
}

static void freeSlurmJobResources(Slurm_Job_Resources_t *jr)
{
    if (!jr) return;

    ufree(jr->coreBitmap);
    ufree(jr->coreBitmapUsed);
    ufree(jr->nodeBitmap);
    ufree(jr->nodes);
    ufree(jr->cpuArrayValue);
    ufree(jr->cpuArrayReps);
    ufree(jr->cpus);
    ufree(jr->cpusUsed);
    ufree(jr->coresPerSocket);
    ufree(jr->memAllocated);
    ufree(jr->memUsed);
    ufree(jr->sockCoreRepCount);
    ufree(jr->socketsPerNode);
}

static void freeSlurmCronEntry(Slurm_Cron_Entry_t *ce)
{
    if (!ce) return;

    ufree(ce->minute);
    ufree(ce->hour);
    ufree(ce->dayOfMonth);
    ufree(ce->month);
    ufree(ce->dayOfWeek);
    ufree(ce->cronSpec);
}

static void freeSlurmJobDetails(Slurm_Job_Details_t *jd)
{
    if (!jd) return;

    ufree(jd->acctPollInt);
    ufree(jd->cpuBind);
    ufree(jd->memBind);
    ufree(jd->requiredNodes);
    ufree(jd->excludedNodes);
    ufree(jd->features);
    ufree(jd->clusterFeatures);
    ufree(jd->prefer);
    ufree(jd->jobSizeBitmap);
    ufree(jd->dependency);
    ufree(jd->origDependency);
    ufree(jd->err);
    ufree(jd->in);
    ufree(jd->out);
    ufree(jd->submitLine);
    ufree(jd->workDir);
    ufree(jd->envHash);
    ufree(jd->scriptHash);

    strvDestroy(jd->argV);
    envShred(jd->suppEnv);

    list_t *g, *tmp;
    list_for_each_safe(g, tmp, &jd->depList) {
	Slurm_Dep_List_t *dl = list_entry(g, Slurm_Dep_List_t, next);
	list_del(&dl->next);
	ufree(dl);
    }

    freeSlurmCronEntry(&jd->cronEntry);
}

static void freeSlurmFedDetails(Slurm_Job_Fed_Details_t *fd)
{
    if (!fd) return;

    ufree(fd->originStr);
    ufree(fd->siblingsActiveStr);
    ufree(fd->siblingsViableStr);
}

static void freeSlurmIdentity(Slurm_Identity_t *id)
{
    if (!id) return;

    ufree(id->pwName);
    ufree(id->pwGecos);
    ufree(id->pwDir);
    ufree(id->pwShell);
    ufree(id->gids);

    for (uint32_t i=0; i<id->gidsLen; i++) {
	ufree(id->grNames[i]);
    }
    ufree(id->grNames);
}

static void freeSlurmStepLayout(Slurm_Step_Layout_t *la)
{
    for (uint32_t i=0; i < la->nodeCount; i++) {
	ufree(la->taskIDs[i]);
    }
    ufree(la->taskIDs);

    ufree(la->numTaskIDs);
    ufree(la->compCPUsPerTask);
    ufree(la->compCPUsPerTaskReps);
    ufree(la->frontEnd);
    ufree(la->nodeList);
    ufree(la->netCred);
}

static void freeSlurmTResRecord(Slurm_TRes_Record_t *tr)
{
    ufree(tr->name);
    ufree(tr->type);
}

static void freeSlurmJobAcct(Slurm_Job_Acct_t *ja)
{
    for (uint32_t i=0; i < ja->numTResRecords; i++) {
	freeSlurmTResRecord(&ja->trr[i]);
    }
    ufree(ja->trr);

    TRes_destroy(ja->tres);
}

static void freeSlurmStepStates(list_t *stateList)
{
    list_t *g, *tmp;
    list_for_each_safe(g, tmp, stateList) {
	Slurm_Step_State_t *st = list_entry(g, Slurm_Step_State_t, next);
	list_del(&st->next);

	freeGresCred(&st->gresStepReq);
	freeGresCred(&st->gresStepAlloc);
	freeSlurmJobAcct(&st->acctData);
	freeSlurmStepLayout(&st->layout);

	ufree(st->container);
	ufree(st->containerID);
	ufree(st->cpuAllocReps);
	ufree(st->cpuAllocValues);
	ufree(st->exitNodeBitmap);
	ufree(st->jobCoreBitmap);
	ufree(st->host);
	ufree(st->resvPorts);
	ufree(st->name);
	ufree(st->network);
	ufree(st->tresAlloc);
	ufree(st->tresFormatAlloc);
	ufree(st->cpusPerTres);
	ufree(st->memPerTres);
	ufree(st->submitLine);
	ufree(st->tresBind);
	ufree(st->tresStep);
	ufree(st->tresFreq);
	ufree(st->tresNode);
	ufree(st->tresSocket);
	ufree(st->tresPerTask);
	ufree(st->memAlloc);
	ufree(st);
    }
}

void freeSlurmJobRecord(Slurm_Job_Record_t *jr)
{
    if (!jr) return;

    ufree(jr->limitSetTRes);
    ufree(jr->batchFeat);
    ufree(jr->container);
    ufree(jr->containerID);
    ufree(jr->failedNode);
    ufree(jr->hetJobIDSet);
    ufree(jr->stateDesc);
    ufree(jr->respHost);
    ufree(jr->resvPorts);
    ufree(jr->nodesCompleting);
    ufree(jr->nodesProlog);
    ufree(jr->nodes);
    ufree(jr->nodeBitmap);
    ufree(jr->partition);
    ufree(jr->name);
    ufree(jr->userName);
    ufree(jr->wckey);
    ufree(jr->allocNode);
    ufree(jr->account);
    ufree(jr->adminComment);
    ufree(jr->comment);
    ufree(jr->extra);
    ufree(jr->gresUsed);
    ufree(jr->network);
    ufree(jr->licenses);
    ufree(jr->licReq);
    ufree(jr->mailUser);
    ufree(jr->mcsLabel);
    ufree(jr->resvName);
    ufree(jr->batchHost);
    ufree(jr->burstBuffer);
    ufree(jr->burstBufferState);
    ufree(jr->systemComment);
    ufree(jr->tresAllocStr);
    ufree(jr->tresFormatAlloc);
    ufree(jr->tresReq);
    ufree(jr->tresFormatReq);
    ufree(jr->clusters);
    ufree(jr->originCluster);
    ufree(jr->cpusPerTres);
    ufree(jr->memPerTres);
    ufree(jr->tresBind);
    ufree(jr->tresFreq);
    ufree(jr->tresPerJob);
    ufree(jr->tresPerNode);
    ufree(jr->tresPerSocket);
    ufree(jr->tresPerTask);
    ufree(jr->selinuxContext);

    freeSlurmJobArray(&jr->jobArray);
    freeSlurmJobResources(&jr->jobRes);
    envShred(jr->spankJobEnv);
    freeSlurmJobDetails(&jr->details);
    freeSlurmFedDetails(&jr->fedDetails);
    freeSlurmIdentity(&jr->identity);
    freeSlurmStepStates(&jr->stateList);

    freeGresCred(&jr->gresJobReq);
    freeGresCred(&jr->gresJobAlloc);
}
