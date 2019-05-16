/*
 * ParaStation
 *
 * Copyright (C) 2016-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psserial.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "slurmcommon.h"

#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmpscomm.h"
#include "psslurmconfig.h"

bool __packSlurmAuth(PS_SendDB_t *data, Slurm_Auth_t *auth,
		     const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!auth) {
	mlog("%s: invalid auth pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    addStringToMsg(auth->method, data);
    addUint32ToMsg(auth->version, data);
    addStringToMsg(auth->cred, data);

    return true;
}

bool __unpackSlurmAuth(Slurm_Msg_t *sMsg, Slurm_Auth_t **authPtr,
		       const char *caller, const int line)
{
    Slurm_Auth_t *auth;
    char **ptr = &sMsg->ptr;

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!authPtr) {
	mlog("%s: invalid auth pointer from '%s' at %i\n",
		__func__, caller, line);
	return false;
    }

    auth = umalloc(sizeof(Slurm_Auth_t));
    auth->method = getStringM(ptr);
    getUint32(ptr, &auth->version);
    auth->cred = getStringM(ptr);

    *authPtr = auth;

    return true;
}

static Gres_Cred_t *unpackGresStep(char **ptr, uint16_t index)
{
    Gres_Cred_t *gres;
    uint32_t magic;
    uint8_t more;
    unsigned int i;

    gres = getGresCred();
    gres->job = 0;

    getUint32(ptr, &magic);
    getUint32(ptr, &gres->id);
    getUint64(ptr, &gres->countAlloc);
    getUint32(ptr, &gres->nodeCount);
    gres->nodeInUse = getBitString(ptr);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error: '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	releaseGresCred(gres);
	return NULL;
    }

    mdbg(PSSLURM_LOG_GRES, "%s: index '%i' pluginID '%u' gresCountAlloc '%lu'"
	    " nodeCount '%u' nodeInUse '%s'\n", __func__, index, gres->id,
	    gres->countAlloc, gres->nodeCount, gres->nodeInUse);

    /* bit allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    gres->bitAlloc[i] = getBitString(ptr);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_alloc '%s'\n", __func__,
		    i, gres->bitAlloc[i]);
	}
    }

    return gres;
}

static Gres_Cred_t *unpackGresJob(char **ptr, uint16_t index)
{
    uint32_t magic;
    uint8_t more;
    unsigned int i;
    Gres_Cred_t *gres;

    gres = getGresCred();
    gres->job = 1;

    getUint32(ptr, &magic);
    getUint32(ptr, &gres->id);
    getUint64(ptr, &gres->countAlloc);
    gres->typeModel = getStringM(ptr);
    getUint32(ptr, &gres->nodeCount);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	releaseGresCred(gres);
	return NULL;
    }

    mdbg(PSSLURM_LOG_GRES, "%s: index '%i' pluginID '%u' "
	    "gresCountAlloc '%lu' nodeCount '%u'\n", __func__, index,
	    gres->id, gres->countAlloc, gres->nodeCount);

    /* bit allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    gres->bitAlloc[i] = getBitString(ptr);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_alloc "
		    "'%s'\n", __func__, i,
		    gres->bitAlloc[i]);
	}
    }

    /* bit step allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->bitStepAlloc = umalloc(sizeof(char *) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    gres->bitStepAlloc[i] = getBitString(ptr);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' bit_step_alloc '%s'\n",
		    __func__, i, gres->bitStepAlloc[i]);
	}
    }

    /* count step allocation */
    getUint8(ptr, &more);
    if (more) {
	gres->countStepAlloc = umalloc(sizeof(uint64_t) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    getUint64(ptr, &(gres->countStepAlloc)[i]);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' gres_cnt_step_alloc "
		    "'%lu'\n", __func__, i, gres->countStepAlloc[i]);
	}
    }

    return gres;
}

static void unpackGres(char **ptr, list_t *gresList, uint32_t jobid,
		uint32_t stepid, uid_t uid)
{
    uint16_t count, i;

    /* extract gres job data */
    getUint16(ptr, &count);
    mdbg(PSSLURM_LOG_GRES, "%s: job data: id '%u:%u' uid '%u' gres job "
	    "count '%u'\n", __func__, jobid, stepid,  uid, count);

    for (i=0; i<count; i++) {
	Gres_Cred_t *gres = unpackGresJob(ptr, i);
	if (!gres) continue;
	list_add_tail(&gres->next, gresList);
    }

    /* extract gres step data */
    getUint16(ptr, &count);
    mdbg(PSSLURM_LOG_GRES, "%s: step data: id '%u:%u' uid '%u' gres step "
	    "count '%u'\n", __func__, jobid, stepid,  uid, count);

    for (i=0; i<count; i++) {
	Gres_Cred_t *gres = unpackGresStep(ptr, i);
	if (!gres) continue;
	list_add_tail(&gres->next, gresList);
    }
}

bool __unpackJobCred(Slurm_Msg_t *sMsg, JobCred_t **credPtr,
		     list_t *gresList, char **credEnd, const char *caller,
		     const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!credPtr) {
	mlog("%s: invalid credPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!gresList) {
	mlog("%s: invalid gresList from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!credEnd) {
	mlog("%s: invalid credEnd from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    JobCred_t *cred = ucalloc(sizeof(JobCred_t));
    char **ptr = &sMsg->ptr;

    /* jobid / stepid */
    getUint32(ptr, &cred->jobid);
    getUint32(ptr, &cred->stepid);
    /* uid */
    getUint32(ptr, &cred->uid);

    uint16_t msgVer = sMsg->head.version;
    if (msgVer >= SLURM_17_11_PROTO_VERSION) {
	/* gid */
	getUint32(ptr, &cred->gid);
	/* username */
	cred->username = getStringM(ptr);
	/* gids */
	getUint32Array(ptr, &cred->gids, &cred->gidsLen);
    }

    /* gres job/step allocations */
    unpackGres(ptr, gresList, cred->jobid, cred->stepid, cred->uid);

    /* count of specialized cores */
    getUint16(ptr, &cred->jobCoreSpec);
    /* job/step memory limit */
    getUint64(ptr, &cred->jobMemLimit);
    getUint64(ptr, &cred->stepMemLimit);
    /* job constraints */
    cred->jobConstraints = getStringM(ptr);
    /* step hostlist */
    cred->stepHL = getStringM(ptr);
    if (!cred->stepHL) {
	mlog("%s: empty step hostlist in credential\n", __func__);
	goto ERROR;
    }
    if (msgVer >= SLURM_17_11_PROTO_VERSION) {
	/* x11 */
	getUint16(ptr, &cred->x11);
    }
    /* time */
    getTime(ptr, &cred->ctime);
    /* total core count */
    getUint32(ptr, &cred->totalCoreCount);
    /* job core bitmap */
    cred->jobCoreBitmap = getBitString(ptr);
    /* step core bitmap */
    cred->stepCoreBitmap = getBitString(ptr);
    /* core array size */
    getUint16(ptr, &cred->coreArraySize);

    mdbg(PSSLURM_LOG_PART, "%s: totalCoreCount '%u' coreArraySize '%u' "
	 "stepCoreBitmap '%s'\n", __func__, cred->totalCoreCount,
	 cred->coreArraySize, cred->stepCoreBitmap);

    if (cred->coreArraySize) {
	uint32_t len;

	getUint16Array(ptr, &cred->coresPerSocket, &len);
	if (len != cred->coreArraySize) {
	    mlog("%s: invalid corePerSocket size %u should be %u\n", __func__,
		 len, cred->coreArraySize);
	    goto ERROR;
	}
	getUint16Array(ptr, &cred->socketsPerNode, &len);
	if (len != cred->coreArraySize) {
	    mlog("%s: invalid socketsPerNode size %u should be %u\n", __func__,
		 len, cred->coreArraySize);
	    goto ERROR;
	}
	getUint32Array(ptr, &cred->sockCoreRepCount, &len);
	if (len != cred->coreArraySize) {
	    mlog("%s: invalid sockCoreRepCount size %u should be %u\n", __func__,
		 len, cred->coreArraySize);
	    goto ERROR;
	}
    }
    /* job number of hosts */
    getUint32(ptr, &cred->jobNumHosts);
    /* job hostlist */
    cred->jobHostlist = getStringM(ptr);
    /* munge signature */
    *credEnd = *ptr;
    cred->sig = getStringM(ptr);

    *credPtr = cred;

    return true;

ERROR:
    freeJobCred(cred);
    return false;
}

bool __unpackBCastCred(Slurm_Msg_t *sMsg, BCast_Cred_t *cred,
		       const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!cred) {
	mlog("%s: invalid cred from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;
    /* init cred */
    memset(cred, 0, sizeof(*cred));
    /* creation time */
    getTime(ptr, &cred->ctime);
    /* expiration time */
    getTime(ptr, &cred->etime);
    /* jobid */
    getUint32(ptr, &cred->jobid);

    uint16_t msgVer = sMsg->head.version;
    if (msgVer >= SLURM_17_11_PROTO_VERSION) {
	/* uid */
	getUint32(ptr, &cred->uid);
	/* gid */
	getUint32(ptr, &cred->gid);
	/* username */
	cred->username = getStringM(ptr);
	/* gids */
	getUint32Array(ptr, &cred->gids, &cred->gidsLen);
    }

    /* hostlist */
    cred->hostlist = getStringM(ptr);
    /* credential end */
    cred->end = *ptr;
    /* signature */
    cred->sig = getStringML(ptr, &cred->sigLen);

    return true;
}

bool __packSlurmHeader(PS_SendDB_t *data, Slurm_Msg_Header_t *head,
		       const char *caller, const int line)
{
    uint32_t i;
    const char *hn;

    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!head) {
	mlog("%s: invalid head pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* protocol version */
    addUint16ToMsg(head->version, data);
    /* flags */
    addUint16ToMsg(head->flags, data);
    /* index */
    addUint16ToMsg(head->index, data);
    /* type */
    addUint16ToMsg(head->type, data);
    /* body len */
    addUint32ToMsg(head->bodyLen, data);

    /* forward */
    addUint16ToMsg(head->forward, data);
    if (head->forward > 0) {
	/* nodelist */
	addStringToMsg(head->nodeList, data);
	/* timeout */
	addUint32ToMsg(head->timeout, data);
	/* tree width */
	addUint16ToMsg(head->treeWidth, data);
    }

    /* return list */
    addUint16ToMsg(head->returnList, data);
    for (i=0; i<head->returnList; i++) {
	/* error */
	addUint32ToMsg(head->fwdata[i].error, data);

	/* msg type */
	addUint16ToMsg(head->fwdata[i].type, data);

	/* nodename */
	hn = getSlurmHostbyNodeID(head->fwdata[i].node);
	addStringToMsg(hn, data);

	/* msg body */
	if (head->fwdata[i].body.bufUsed) {
	    addMemToMsg(head->fwdata[i].body.buf,
			head->fwdata[i].body.bufUsed, data);
	}
    }

    /* addr/port */
    addUint32ToMsg(head->addr, data);
    addUint16ToMsg(head->port, data);

    return true;
}

bool __packSlurmIOMsg(PS_SendDB_t *data, Slurm_IO_Header_t *ioh, char *body,
		      const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!ioh) {
	mlog("%s: invalid I/O message pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* type (stdout/stderr) */
    addUint16ToMsg(ioh->type, data);
    /* global taskid */
    addUint16ToMsg(ioh->gtid, data);
    /* local taskid (unused) */
    addUint16ToMsg((uint16_t)NO_VAL, data);
    /* msg length */
    addUint32ToMsg(ioh->len, data);
    /* msg data */
    if (ioh->len > 0 && body) addMemToMsg(body, ioh->len, data);

    return true;
}

bool __unpackSlurmIOHeader(char **ptr, Slurm_IO_Header_t **iohPtr,
			   const char *caller, const int line)
{
    Slurm_IO_Header_t *ioh;

    if (!ptr) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!iohPtr) {
	mlog("%s: invalid I/O message pointer from '%s' at %i\n",
		__func__, caller, line);
	return false;
    }

    ioh = umalloc(sizeof(Slurm_IO_Header_t));
    /* type */
    getUint16(ptr, &ioh->type);
    /* global taskid */
    getUint16(ptr, &ioh->gtid);
    /* local taskid */
    getUint16(ptr, &ioh->ltid);
    /* length */
    getUint32(ptr, &ioh->len);
    *iohPtr = ioh;

    return true;
}

bool __unpackReqTerminate(Slurm_Msg_t *sMsg, Req_Terminate_Job_t **reqPtr,
			  const char *caller, const int line)
{

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!reqPtr) {
	mlog("%s: invalid reqPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    Req_Terminate_Job_t *req = ucalloc(sizeof(Req_Terminate_Job_t));

    uint16_t msgVer = sMsg->head.version;
    char **ptr = &sMsg->ptr;

    /* jobid*/
    getUint32(ptr, &req->jobid);

    if (msgVer == SLURM_18_08_PROTO_VERSION) {
	/* pack jobid */
	getUint32(ptr, &req->packJobid);
    }
    /* jobstate */
    getUint32(ptr, &req->jobstate);
    /* user id */
    getUint32(ptr, &req->uid);
    /* nodes */
    req->nodes = getStringM(ptr);

    if (msgVer == SLURM_17_02_PROTO_VERSION) {
	env_t pelogueEnv;

	/* pelogue env */
	envInit(&pelogueEnv);
	getStringArrayM(ptr, &pelogueEnv.vars, &pelogueEnv.cnt);
	envDestroy(&pelogueEnv);
    }

    /* job info */
    uint32_t tmp;
    getUint32(ptr, &tmp);
    /* spank env */
    getStringArrayM(ptr, &req->spankEnv.vars, &req->spankEnv.cnt);
    /* start time */
    getTime(ptr, &req->startTime);
    /* step id */
    getUint32(ptr, &req->stepid);

    *reqPtr = req;
    return true;
}

bool __unpackReqSignalTasks(Slurm_Msg_t *sMsg, Req_Signal_Tasks_t **reqPtr,
			    const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!reqPtr) {
	mlog("%s: invalid reqPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    Req_Signal_Tasks_t *req = ucalloc(sizeof(Req_Terminate_Job_t));

    char **ptr = &sMsg->ptr;
    uint16_t msgVer = sMsg->head.version;
    if (msgVer >= SLURM_17_11_PROTO_VERSION) {
	/* flags */
	getUint16(ptr, &req->flags);
	/* jobid*/
	getUint32(ptr, &req->jobid);
	/* stepid */
	getUint32(ptr, &req->stepid);
	/* signal */
	getUint16(ptr, &req->signal);
    } else {
	uint32_t sigInfo;

	/* jobid*/
	getUint32(ptr, &req->jobid);
	/* stepid */
	getUint32(ptr, &req->stepid);
	/* encoded flags and signal */
	getUint32(ptr, &sigInfo);

	/* extract flags and signal */
	req->flags = sigInfo >> 24;
	req->signal = sigInfo & 0xfff;
    }

    *reqPtr = req;
    return true;
}

static void unpackStepTaskIds(Step_t *step, char **ptr)
{
    uint32_t i, x;

    step->tasksToLaunch = umalloc(step->nrOfNodes * sizeof(uint16_t));
    step->globalTaskIds = umalloc(step->nrOfNodes * sizeof(uint32_t *));
    step->globalTaskIdsLen = umalloc(step->nrOfNodes * sizeof(uint32_t));

    for (i=0; i<step->nrOfNodes; i++) {
	/* num of tasks per node */
	getUint16(ptr, &step->tasksToLaunch[i]);

	/* job global task ids per node */
	getUint32Array(ptr, &(step->globalTaskIds)[i],
			    &(step->globalTaskIdsLen)[i]);
	mdbg(PSSLURM_LOG_PART, "%s: node '%u' tasksToLaunch '%u' "
		"globalTaskIds: ", __func__, i, step->tasksToLaunch[i]);

	for (x=0; x<step->globalTaskIdsLen[i]; x++) {
	    mdbg(PSSLURM_LOG_PART, "%u%s", step->globalTaskIds[i][x],
		 (x+1==step->globalTaskIdsLen[i]) ? "" : ",");
	}
	mdbg(PSSLURM_LOG_PART, "\n");
    }
}

static void unpackStepAddr(Step_t *step, char **ptr)
{
    uint32_t i, addr;
    uint16_t port;

    /* srun ports */
    getUint16(ptr, &step->numSrunPorts);
    if (step->numSrunPorts >0) {
	step->srunPorts = umalloc(step->numSrunPorts * sizeof(uint16_t));
	for (i=0; i<step->numSrunPorts; i++) {
	    getUint16(ptr, &step->srunPorts[i]);
	}
    }

    getUint32(ptr, &addr);
    getUint16(ptr, &port);
}

static void unpackStepIOoptions(Step_t *step, char **ptr)
{
    uint32_t i;

    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	/* stdout options */
	step->stdOut = getStringM(ptr);
	/* stderr options */
	step->stdErr = getStringM(ptr);
	/* stdin options */
	step->stdIn = getStringM(ptr);
	/* I/O Ports */
	getUint16(ptr, &step->numIOPort);
	if (step->numIOPort >0) {
	    step->IOPort = umalloc(sizeof(uint16_t) * step->numIOPort);
	    for (i=0; i<step->numIOPort; i++) {
		getUint16(ptr, &step->IOPort[i]);
	    }
	}
    }
}

bool __unpackReqLaunchTasks(Slurm_Msg_t *sMsg, Step_t **stepPtr,
			    const char *caller, const int line)
{
    uint32_t jobid, stepid, count, i, tmp;

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr, *unused;
    /* jobid/stepid */
    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    Step_t *step = addStep(jobid, stepid);

    uint16_t msgVer = sMsg->head.version, debug;
    if (msgVer >= SLURM_17_11_PROTO_VERSION) {
	/* uid */
	getUint32(ptr, &step->uid);
	/* gid */
	getUint32(ptr, &step->gid);
	/* username */
	step->username = getStringM(ptr);
	/* secondary group ids */
	getUint32Array(ptr, &step->gids, &step->gidsLen);
	/* node offset */
	getUint32(ptr, &step->packNodeOffset);
	/* pack jobid */
	getUint32(ptr, &step->packJobid);
	/* pack number of nodes */
	getUint32(ptr, &step->packNrOfNodes);
	/* pack task counts */
	if (step->packNrOfNodes != NO_VAL) {
	    getUint16Array(ptr, &step->packTaskCounts, &tmp);
	    if (step->packNrOfNodes != tmp) {
		mlog("%s: invalid packTaskCounts len %u : %u\n", __func__, tmp,
		     step->packNrOfNodes);
		goto ERROR;
	    }
	}
	/* pack ntasks */
	getUint32(ptr, &step->packNtasks);
	/* pack offset */
	getUint32(ptr, &step->packOffset);
	/* pack task offset */
	getUint32(ptr, &step->packTaskOffset);
	if (step->packTaskOffset == NO_VAL) step->packTaskOffset = 0;
	/* pack nodelist */
	step->packHostlist = getStringM(ptr);
	/* ntasks */
	getUint32(ptr, &step->np);
	/* ntasks per board/core/socket (unused) */
	getUint16(ptr, &tmp);
	getUint16(ptr, &tmp);
	getUint16(ptr, &tmp);
	/* partition */
	step->partition = getStringM(ptr);
    } else {
	uint32_t mpiJobid;
	uint32_t mpiNnodes;
	uint32_t mpiNtasks;
	uint32_t mpiStepfnodeid;
	uint32_t mpiStepftaskid;
	uint32_t mpiStepid;
	uint32_t packStepid;

	/* unused */
	getUint32(ptr, &mpiJobid);
	getUint32(ptr, &mpiNnodes);
	getUint32(ptr, &mpiNtasks);
	getUint32(ptr, &mpiStepfnodeid);
	getUint32(ptr, &mpiStepftaskid);
	getUint32(ptr, &mpiStepid);
	/* ntasks */
	getUint32(ptr, &step->np);
	/* ntasks per board/core/socket (unused) */
	getUint16(ptr, &tmp);
	getUint16(ptr, &tmp);
	getUint16(ptr, &tmp);
	/* pack jobid */
	getUint32(ptr, &step->packJobid);
	/* pack stepid */
	getUint32(ptr, &packStepid);
	/* uid */
	getUint32(ptr, &step->uid);
	/* partition */
	step->partition = getStringM(ptr);
	/* username */
	step->username = getStringM(ptr);
	/* gid */
	getUint32(ptr, &step->gid);
	/* set pack values to default */
	step->packJobid = NO_VAL;
	step->packNrOfNodes = NO_VAL;
	step->packOffset = NO_VAL;
	step->packAllocID = NO_VAL;
    }

    /* job/step mem limit */
    getUint64(ptr, &step->jobMemLimit);
    getUint64(ptr, &step->stepMemLimit);
    /* num of nodes */
    getUint32(ptr, &step->nrOfNodes);
    if (!step->nrOfNodes) {
	mlog("%s: invalid nrOfNodes %u\n", __func__, step->nrOfNodes);
	goto ERROR;
    }
    /* cpus_per_task */
    getUint16(ptr, &step->tpp);
    /* task distribution */
    getUint32(ptr, &step->taskDist);
    /* node cpus (unused) */
    getUint16(ptr, &tmp);
    /* count of specialized cores */
    getUint16(ptr, &step->jobCoreSpec);
    /* accelerator bind type */
    getUint16(ptr, &tmp);

    /* job credentials */
    if (!(step->cred = extractJobCred(&step->gresList, sMsg, 1))) {
	mlog("%s: extracting job credential failed\n", __func__);
	goto ERROR;
    }

    /* tasks to launch / global task ids */
    unpackStepTaskIds(step, ptr);

    /* srun ports/addr */
    unpackStepAddr(step, ptr);

    /* env */
    getStringArrayM(ptr, &step->env.vars, &step->env.cnt);
    /* spank env */
    getStringArrayM(ptr, &step->spankenv.vars, &step->spankenv.cnt);
    /* cwd */
    step->cwd = getStringM(ptr);
    /* cpu bind */
    getUint16(ptr, &step->cpuBindType);
    step->cpuBind = getStringM(ptr);
    /* mem bind */
    getUint16(ptr, &step->memBindType);
    step->memBind = getStringM(ptr);
    /* args */
    getStringArrayM(ptr, &step->argv, &step->argc);
    /* task flags */
    getUint32(ptr, &step->taskFlags);
    /* I/O options */
    unpackStepIOoptions(step, ptr);
    /* profile (unused) see srun --profile */
    getUint32(ptr, &tmp);
    /* prologue/epilogue */
    step->taskProlog = getStringM(ptr);
    step->taskEpilog = getStringM(ptr);
    /* debug mask */
    getUint16(ptr, &debug);

    /* switch plugin, does not add anything when using "switch/none" */
    if (msgVer >= SLURM_17_11_PROTO_VERSION) {
	/* job info */
	getUint32(ptr, &tmp);
    }

    /* job options (plugin) */
    char jobOpt[512];
    getString(ptr, jobOpt, sizeof(jobOpt));
    if (!!(strcmp(jobOpt, JOB_OPTIONS_TAG))) {
	mlog("%s: invalid job options tag '%s'\n", __func__, jobOpt);
	goto ERROR;
    }

    /* TODO use job options */
    getUint32(ptr, &count);
    for (i=0; i<count; i++) {
	/* type */
	getUint32(ptr, &tmp);
	/* name */
	getString(ptr, jobOpt, sizeof(jobOpt));
	/* value */
	getString(ptr, jobOpt, sizeof(jobOpt));
    }

    /* node alias */
    step->nodeAlias = getStringM(ptr);
    /* host list */
    step->slurmHosts = getStringM(ptr);

    /* I/O open_mode */
    getUint8(ptr, &step->appendMode);
    /* accounting frequency */
    step->acctFreq = getStringM(ptr);
    /* CPU frequency minimal (unused)
     * see srun --cpu-freq */
    getUint32(ptr, &tmp);
    /* CPU frequency maximal (unused)
     * see srun --cpu-freq */
    getUint32(ptr, &tmp);
    /* CPU frequency governor (unused)
     * see srun --cpu-freq */
    getUint32(ptr, &tmp);
    /* directory for checkpoints */
    step->checkpoint = getStringM(ptr);
    /* directory for restarting checkpoints (unused)
     * see srun --restart-dir */
    unused = getStringM(ptr);
    ufree(unused);

    /* jobinfo plugin id */
    getUint32(ptr, &tmp);

    if (msgVer == SLURM_18_08_PROTO_VERSION) {
	/* tres bind */
	step->tresBind = getStringM(ptr);
	/* tres freq */
	step->tresFreq = getStringM(ptr);
	/* x11 */
	getUint16(ptr, &step->x11.x11);
	/* magic cookie */
	step->x11.magicCookie = getStringM(ptr);
	/* x11 host */
	step->x11.host = getStringM(ptr);
	/* x11 port */
	getUint16(ptr, &step->x11.port);
    } else if (msgVer == SLURM_17_11_PROTO_VERSION) {
	/* x11 */
	getUint16(ptr, &step->x11.x11);
	/* magic cookie */
	step->x11.magicCookie = getStringM(ptr);
	/* x11 host */
	step->x11.host = getStringM(ptr);
	/* x11 port */
	getUint16(ptr, &step->x11.port);
    } else if (msgVer == SLURM_17_02_PROTO_VERSION) {
	env_t pelogueEnv;

	/* pelogue env */
	envInit(&pelogueEnv);
	getStringArrayM(ptr, &pelogueEnv.vars, &pelogueEnv.cnt);
	envDestroy(&pelogueEnv);
    }

    *stepPtr = step;
    return true;

ERROR:
    deleteStep(jobid, stepid);
    return false;
}

static void readJobCpuOptions(Job_t *job, char **ptr)
{
    /* cpu group count */
    getUint32(ptr, &job->cpuGroupCount);

    if (job->cpuGroupCount) {
	uint32_t len;

	/* cpusPerNode */
	getUint16Array(ptr, &job->cpusPerNode, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu per node array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpusPerNode);
	    job->cpusPerNode = NULL;
	}

	/* cpuCountReps */
	getUint32Array(ptr, &job->cpuCountReps, &len);
	if (len != job->cpuGroupCount) {
	    mlog("%s: invalid cpu count reps array '%u:%u'\n", __func__,
		    len, job->cpuGroupCount);
	    ufree(job->cpuCountReps);
	    job->cpuCountReps = NULL;
	}
    }
}

bool __unpackReqBatchJobLaunch(Slurm_Msg_t *sMsg, Job_t **jobPtr,
			       const char *caller, const int line)
{
    uint32_t jobid, tmp, count;
    char buf[1024];

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;
    uint16_t msgVer = sMsg->head.version, cpuBindType;

    /* jobid */
    getUint32(ptr, &jobid);

    Job_t *job = addJob(jobid);

    if (msgVer >= SLURM_18_08_PROTO_VERSION) {
	/* pack jobid */
	getUint32(ptr, &job->packJobid);
    }
    /* stepid */
    getUint32(ptr, &tmp);
    if (tmp != SLURM_BATCH_SCRIPT) {
	mlog("%s: batch job should not have stepid '%u'\n", __func__, tmp);
	deleteJob(jobid);
	return false;
    }
    /* uid */
    getUint32(ptr, &job->uid);

    if (msgVer >= SLURM_17_11_PROTO_VERSION) {
	/* gid */
	getUint32(ptr, &job->gid);
	/* username */
	job->username = getStringM(ptr);
	/* gids */
	getUint32Array(ptr, &job->gids, &job->gidsLen);
	/* partition */
	job->partition = getStringM(ptr);
    } else if (msgVer == SLURM_17_02_PROTO_VERSION) {
	/* partition */
	job->partition = getStringM(ptr);
	/* username */
	job->username = getStringM(ptr);
	/* gid */
	getUint32(ptr, &job->gid);
    } else {
	mlog("%s: unsupported protocol version %u\n", __func__, msgVer);
	return false;
    }

    /* ntasks */
    getUint32(ptr, &job->np);
    /* pn_min_memory */
    getUint64(ptr, &job->nodeMinMemory);
    /* open_mode */
    getUint8(ptr, &job->appendMode);
    /* overcommit (overbook) */
    getUint8(ptr, &job->overcommit);
    /* array job id */
    getUint32(ptr, &job->arrayJobId);
    /* array task id */
    getUint32(ptr, &job->arrayTaskId);
    /* acctg freq */
    job->acctFreq = getStringM(ptr);
    /* cpu bind type (unused for jobs) */
    getUint16(ptr, &cpuBindType);
    /* cpus per task */
    getUint16(ptr, &job->tpp);
    /* TODO: restart count */
    getUint16(ptr, (uint16_t *)&tmp);
    /* count of specialized cores */
    getUint16(ptr, &job->jobCoreSpec);

    /* cpusPerNode / cpuCountReps */
    readJobCpuOptions(job, ptr);

    /* node alias */
    job->nodeAlias = getStringM(ptr);
    /* cpu bind string */
    getString(ptr, buf, sizeof(buf));
    /* hostlist */
    job->slurmHosts = getStringM(ptr);
    /* jobscript */
    job->jsData = getStringM(ptr);
    /* work dir */
    job->cwd = getStringM(ptr);
    /* directory for checkpoints */
    job->checkpoint = getStringM(ptr);
    /* directory for restarting checkpoints (unused)
     * see srun --restart-dir */
    char *unused = getStringM(ptr);
    ufree(unused);
    /* std I/O/E */
    job->stdErr = getStringM(ptr);
    job->stdIn = getStringM(ptr);
    job->stdOut = getStringM(ptr);
    /* argv/argc */
    getUint32(ptr, &count);
    getStringArrayM(ptr, &job->argv, &job->argc);
    if (count != job->argc) {
	mlog("%s: mismatching argc %u : %u\n", __func__, count, job->argc);
	goto ERROR;
    }
    /* spank env/envc */
    getStringArrayM(ptr, &job->spankenv.vars, &job->spankenv.cnt);
    /* env/envc */
    getUint32(ptr, &count);
    getStringArrayM(ptr, &job->env.vars, &job->env.cnt);
    if (count != job->env.cnt) {
	mlog("%s: mismatching envc %u : %u\n", __func__, count, job->env.cnt);
	goto ERROR;
    }
    /* TODO use job memory limit */
    getUint64(ptr, &job->memLimit);

    /* job credential */
    if (!(job->cred = extractJobCred(&job->gresList, sMsg, 1))) {
	mlog("%s: extracting job credentail failed\n", __func__);
	goto ERROR;
    }

    /* jobinfo plugin id */
    getUint32(ptr, &tmp);
    /* account (unused) */
    unused = getStringM(ptr);
    ufree(unused);
    /* qos (unused) see srun --qos */
    unused = getStringM(ptr);
    ufree(unused);
    /* reservation name (unused) */
    unused = getStringM(ptr);
    ufree(unused);
    /* profile (unused) see srun --profile */
    getUint32(ptr, &tmp);

    if (msgVer == SLURM_17_02_PROTO_VERSION) {
	env_t pelogueEnv;

	/* pelogue env */
	envInit(&pelogueEnv);
	getStringArrayM(ptr, &pelogueEnv.vars, &pelogueEnv.cnt);
	envDestroy(&pelogueEnv);

	/* reserved ports (unused) */
	unused = getStringM(ptr);
	ufree(unused);
	/* jobpack group number index (unused) */
	getUint32(ptr, &tmp);
    }

    /* TODO
     *          packstr(msg->tres_bind, buffer);
                packstr(msg->tres_freq, buffer);
    */

    *jobPtr = job;
    return true;

ERROR:
    deleteJob(job->jobid);
    return false;
}

bool __packRespPing(PS_SendDB_t *data, Resp_Ping_t *ping,
		    const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!ping) {
	mlog("%s: invalid ping pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* cpu load */
    addUint32ToMsg(ping->cpuload, data);
    /* free memory */
    addUint64ToMsg(ping->freemem, data);

    return true;
}

static void packAccNodeId(PS_SendDB_t *data, int type,
			  AccountDataExt_t *accData, PSnodes_ID_t *nodes,
			  uint32_t nrOfNodes)
{
    PSnodes_ID_t psNodeID;
    int nid;

    psNodeID = PSC_getID(accData->taskIds[type]);

    /* node ID */
    if ((nid = getSlurmNodeID(psNodeID, nodes, nrOfNodes)) < 0) {
	addUint32ToMsg((uint32_t) 0, data);
    } else {
	addUint32ToMsg((uint32_t) nid, data);
    }
    /* task ID */
    addUint16ToMsg((uint16_t) 0, data);
}

static void packAccData_17(PS_SendDB_t *data, SlurmAccData_t *slurmAccData)
{
    if (slurmAccData->empty) {
	int i;
	/* pack empty account data */
	for (i=0; i<6; i++) {
	    addUint64ToMsg(0, data);
	}
	for (i=0; i<5; i++) {
	    addUint32ToMsg(0, data);
	}
	addDoubleToMsg(0, data);
	addUint32ToMsg(0, data);
	addUint64ToMsg(0, data);
	for (i=0; i<4; i++) {
	    addDoubleToMsg(0, data);
	}
	for (i=0; i<6; i++) {
	    addUint32ToMsg((uint32_t) NO_VAL, data);
	    addUint16ToMsg((uint16_t) NO_VAL, data);
	}
	return;
    }

    AccountDataExt_t *accData = slurmAccData->accData;
    /* user cpu sec/usec */
    addUint32ToMsg(accData->rusage.ru_utime.tv_sec, data);
    addUint32ToMsg(accData->rusage.ru_utime.tv_usec, data);

    /* system cpu sec/usec */
    addUint32ToMsg(accData->rusage.ru_stime.tv_sec, data);
    addUint32ToMsg(accData->rusage.ru_stime.tv_usec, data);

    /* max vsize */
    addUint64ToMsg(accData->maxVsize, data);
    /* total vsize (sum of average vsize of all tasks, slurm divides) */
    addUint64ToMsg(accData->avgVsizeTotal, data);

    /* max rss */
    addUint64ToMsg(accData->maxRss, data);
    /* total rss (sum of average rss of all tasks, slurm divides) */
    addUint64ToMsg(accData->avgRssTotal, data);

    /* max/total major page faults */
    addUint64ToMsg(accData->maxMajflt, data);
    addUint64ToMsg(accData->totMajflt, data);

    /* minimum cpu time */
    addUint32ToMsg(accData->minCputime, data);

    /* total cpu time */
    addDoubleToMsg(accData->totCputime, data);

    /* act cpufreq */
    addUint32ToMsg(accData->cpuFreq, data);

    /* energy consumed */
    addUint64ToMsg(0, data);

    /* max/total disk read */
    addDoubleToMsg(accData->maxDiskRead, data);
    addDoubleToMsg(accData->totDiskRead, data);

    /* max/total disk write */
    addDoubleToMsg(accData->maxDiskWrite, data);
    addDoubleToMsg(accData->totDiskWrite, data);

    /* node ids */
    packAccNodeId(data, ACCID_MAX_VSIZE, accData, slurmAccData->nodes,
		  slurmAccData->nrOfNodes);
    packAccNodeId(data, ACCID_MAX_RSS, accData, slurmAccData->nodes,
		  slurmAccData->nrOfNodes);
    packAccNodeId(data, ACCID_MAX_PAGES, accData, slurmAccData->nodes,
		  slurmAccData->nrOfNodes);
    packAccNodeId(data, ACCID_MIN_CPU, accData, slurmAccData->nodes,
		  slurmAccData->nrOfNodes);
    packAccNodeId(data, ACCID_MAX_DISKREAD, accData, slurmAccData->nodes,
		  slurmAccData->nrOfNodes);
    packAccNodeId(data, ACCID_MAX_DISKWRITE, accData, slurmAccData->nodes,
		  slurmAccData->nrOfNodes);
}

bool __packTResData(PS_SendDB_t *data, TRes_t *tres, const char *caller,
		    const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!tres) {
	mlog("%s: invalid tres pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* TRes IDs */
    addUint32ArrayToMsg(tres->ids, tres->count, data);

    /* add empty TRes list */
    addUint32ToMsg(NO_VAL, data);

    /* in max/min values */
    addUint64ArrayToMsg(tres->in_max, tres->count, data);
    addUint64ArrayToMsg(tres->in_max_nodeid, tres->count, data);
    addUint64ArrayToMsg(tres->in_max_taskid, tres->count, data);
    addUint64ArrayToMsg(tres->in_min, tres->count, data);
    addUint64ArrayToMsg(tres->in_min_nodeid, tres->count, data);
    addUint64ArrayToMsg(tres->in_min_taskid, tres->count, data);
    /* in total */
    addUint64ArrayToMsg(tres->in_tot, tres->count, data);

    /* out max/min values */
    addUint64ArrayToMsg(tres->out_max, tres->count, data);
    addUint64ArrayToMsg(tres->out_max_nodeid, tres->count, data);
    addUint64ArrayToMsg(tres->out_max_taskid, tres->count, data);
    addUint64ArrayToMsg(tres->out_min, tres->count, data);
    addUint64ArrayToMsg(tres->out_min_nodeid, tres->count, data);
    addUint64ArrayToMsg(tres->out_min_taskid, tres->count, data);
    /* in total */
    addUint64ArrayToMsg(tres->out_tot, tres->count, data);

    return true;
}

static void convAccDataToTRes(AccountDataExt_t *accData, TRes_t *tres)
{
    TRes_Entry_t entry;

    /* vsize in byte */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxVsize * 1024;
    entry.in_min = accData->maxVsize * 1024;
    entry.in_tot = accData->avgVsizeTotal * 1024;
    TRes_set(tres, TRES_VMEM, &entry);

    /* memory in byte */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxRss * 1024;
    entry.in_min = accData->maxRss * 1024;
    entry.in_tot = accData->avgRssTotal * 1024;
    TRes_set(tres, TRES_MEM, &entry);

    /* pages */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxMajflt;
    entry.in_min = accData->maxMajflt;
    entry.in_tot = accData->totMajflt;
    TRes_set(tres, TRES_PAGES, &entry);

    /* cpu */
    TRes_reset_entry(&entry);
    entry.in_min = accData->minCputime * 1000;
    entry.in_max = accData->minCputime * 1000;
    entry.in_tot = accData->totCputime * 1000;
    TRes_set(tres, TRES_CPU, &entry);

    /* fs disk in byte */
    TRes_reset_entry(&entry);
    entry.in_max = accData->maxDiskRead * 1048576;
    entry.in_min = accData->maxDiskRead * 1048576;
    entry.in_tot = accData->totDiskRead * 1048576;

    entry.out_max = accData->maxDiskWrite * 1048576;
    entry.out_min = accData->maxDiskWrite * 1048576;
    entry.out_tot = accData->totDiskWrite * 1048576;
    TRes_set(tres, TRES_FS_DISK, &entry);
}

bool __packSlurmAccData(PS_SendDB_t *data, SlurmAccData_t *slurmAccData,
			const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!slurmAccData) {
	mlog("%s: invalid accData pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!slurmAccData->type) {
	addUint8ToMsg(0, data);
	return true;
    }

    addUint8ToMsg(1, data);

    if (slurmProto <= SLURM_17_11_PROTO_VERSION) {
	/* pack old accouting data */
	packAccData_17(data, slurmAccData);
	return true;
    }

    AccountDataExt_t *accData = slurmAccData->accData;

    /* user cpu sec/usec */
    addUint32ToMsg(accData->rusage.ru_utime.tv_sec, data);
    addUint32ToMsg(accData->rusage.ru_utime.tv_usec, data);

    /* system cpu sec/usec */
    addUint32ToMsg(accData->rusage.ru_stime.tv_sec, data);
    addUint32ToMsg(accData->rusage.ru_stime.tv_usec, data);

    /* act cpufreq */
    addUint32ToMsg(accData->cpuFreq, data);

    /* energy consumed */
    addUint64ToMsg(0, data);

    TRes_t *tres = TRes_new();

    flog("ADDING TRES DATA!!!\n");

    convAccDataToTRes(accData, tres);

    TRes_print(tres);

    packTResData(data, tres);

    TRes_destroy(tres);

    return true;
}

bool packGresConf(Gres_Conf_t *gres, void *info)
{
    PS_SendDB_t *msg = info;

    addUint32ToMsg(GRES_MAGIC, msg);
    addUint64ToMsg(gres->count, msg);
    addUint32ToMsg(getConfValueI(&Config, "SLURM_CPUS"), msg);
    addUint8ToMsg((gres->file ? 1 : 0), msg);
    addUint32ToMsg(gres->id, msg);
    addStringToMsg(gres->cpus, msg);
    if (slurmProto >= SLURM_18_08_PROTO_VERSION) {
	/* links (unused) */
	addStringToMsg("", msg);
    }
    addStringToMsg(gres->name, msg);
    addStringToMsg(gres->type, msg);

    return false;
}

void addGresData(PS_SendDB_t *msg, int version)
{
    size_t startGresData;
    uint32_t len;
    char *ptr;

    /* add placeholder for gres info size */
    startGresData = msg->bufUsed;
    addUint32ToMsg(0, msg);
    /* add placeholder again for gres info size in pack_mem() */
    addUint32ToMsg(0, msg);

    /* add slurm version */
    addUint16ToMsg(version, msg);

    /* data count */
    addUint16ToMsg(countGresConf(), msg);

    traverseGresConf(packGresConf, msg);

    /* set real gres info size */
    ptr = msg->buf + startGresData;
    len = msg->bufUsed - startGresData - (2 * sizeof(uint32_t));

    *(uint32_t *)ptr = htonl(len);
    ptr += sizeof(uint32_t);
    *(uint32_t *)ptr = htonl(len);
}

bool __packRespNodeRegStatus(PS_SendDB_t *data, Resp_Node_Reg_Status_t *stat,
			     const char *caller, const int line)
{
    uint32_t i;

    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!stat) {
	mlog("%s: invalid stat pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* timestamp */
    addTimeToMsg(stat->now, data);
    /* slurmd_start_time */
    addTimeToMsg(stat->startTime, data);
    /* status */
    addUint32ToMsg(stat->status, data);
    /* features active/avail */
    addStringToMsg(NULL, data);
    addStringToMsg(NULL, data);
    /* node_name */
    addStringToMsg(stat->nodeName, data);
    /* architecture */
    addStringToMsg(stat->arch, data);
    /* cpu spec list */
    addStringToMsg("", data);
    /* os */
    addStringToMsg(stat->sysname, data);
    /* cpus */
    addUint16ToMsg(stat->cpus, data);
    /* boards */
    addUint16ToMsg(stat->boards, data);
    /* sockets */
    addUint16ToMsg(stat->sockets, data);
    /* cores */
    addUint16ToMsg(stat->coresPerSocket, data);
    /* threads */
    addUint16ToMsg(stat->threadsPerCore, data);
    /* real mem */
    addUint64ToMsg(stat->realMem, data);
    /* tmp disk */
    addUint32ToMsg(stat->tmpDisk, data);
    /* uptime */
    addUint32ToMsg(stat->uptime, data);
    /* hash value of the SLURM config file */
    addUint32ToMsg(stat->config, data);
    /* cpu load */
    addUint32ToMsg(stat->cpuload, data);
    /* free memory */
    addUint64ToMsg(stat->freemem, data);
    /* job infos */
    addUint32ToMsg(stat->jobInfoCount, data);
    for (i=0; i<stat->jobInfoCount; i++) {
	addUint32ToMsg(stat->jobids[i], data);
    }
    for (i=0; i<stat->jobInfoCount; i++) {
	addUint32ToMsg(stat->stepids[i], data);
    }

    /* flags */
    addUint16ToMsg(stat->flags, data);

    if (stat->flags & SLURMD_REG_FLAG_STARTUP) {
	/* TODO pack switch node info */
    }

    /* add gres configuration */
    addGresData(data, slurmProto);

    /* TODO: acct_gather_energy_pack(msg->energy, buffer, protocol_version); */
    addUint64ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint64ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint64ToMsg(0, data);
    time_t now = 0;
    addTimeToMsg(now, data);

    /* protocol version */
    addStringToMsg(stat->verStr, data);

    return true;
}

bool __unpackReqFileBcast(Slurm_Msg_t *sMsg, BCast_t **bcastPtr,
			  const char *caller, const int line)
{
    char **ptr = &sMsg->ptr;
    BCast_t *bcast;
    size_t len;

    if (!sMsg) {
	mlog("%s: invalid ptr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    bcast = addBCast();

    /* block number */
    getUint32(ptr, &bcast->blockNumber);
    /* compression */
    getUint16(ptr, &bcast->compress);
    /* last block */
    getUint16(ptr, &bcast->lastBlock);
    /* force */
    getUint16(ptr, &bcast->force);
    /* modes */
    getUint16(ptr, &bcast->modes);
    /* uid | not always the owner of the bcast!  */
    getUint32(ptr, &bcast->uid);
    /* username */
    bcast->username = getStringM(ptr);
    /* gid */
    getUint32(ptr, &bcast->gid);
    /* atime */
    getTime(ptr, &bcast->atime);
    /* mtime */
    getTime(ptr, &bcast->mtime);
    /* file name */
    bcast->fileName = getStringM(ptr);
    /* block length */
    getUint32(ptr, &bcast->blockLen);
    /* uncompressed length */
    getUint32(ptr, &bcast->uncompLen);
    /* block offset */
    getUint64(ptr, &bcast->blockOffset);
    /* file size */
    getUint64(ptr, &bcast->fileSize);
    /* data block */
    bcast->block = getDataM(ptr, &len);
    if (bcast->blockLen != len) {
	mlog("%s: blockLen mismatch: %d/%zd\n", __func__, bcast->blockLen, len);
	deleteBCast(bcast);
	return false;
    }

    *bcastPtr = bcast;

    return true;
}

bool __packSlurmMsg(PS_SendDB_t *data, Slurm_Msg_Header_t *head,
		    PS_DataBuffer_t *body, Slurm_Auth_t *auth,
		    const char *caller, const int line)
{
    uint32_t lastBufLen = 0, msgStart;
    char *ptr;

    if (!data || !head || !body || !auth) {
	mlog("%s: invalid param from '%s' at %i\n", __func__,
	     caller, line);
	return false;
    }

    /* add placeholder for the message length */
    msgStart = data->bufUsed;
    addUint32ToMsg(0, data);

    /* add message header */
    head->bodyLen = body->bufUsed;
    __packSlurmHeader(data, head, caller, line);

    mdbg(PSSLURM_LOG_COMM, "%s: added slurm header (%i) : body len :%i\n",
	    __func__, data->bufUsed, body->bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"msg header");
	lastBufLen = data->bufUsed;
    }

    /* add munge auth string, will *not* be counted to msg header body len */
    __packSlurmAuth(data, auth, caller, line);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm auth (%i)\n",
	    __func__, data->bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"slurm auth");
	lastBufLen = data->bufUsed;
    }

    /* add the message body */
    addMemToMsg(body->buf, body->bufUsed, data);
    mdbg(PSSLURM_LOG_COMM, "%s: added slurm msg body (%i)\n",
	    __func__, data->bufUsed);

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_IO_VERB) {
	printBinaryData(data->buf + lastBufLen, data->bufUsed - lastBufLen,
			"msg body");
    }

    /* set real message length without the uint32 for the length itself! */
    ptr = data->buf + msgStart;
    *(uint32_t *) ptr = htonl(data->bufUsed - sizeof(uint32_t));

    return true;
}

bool __packRespDaemonStatus(PS_SendDB_t *data, Resp_Daemon_Status_t *stat,
			    const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!stat) {
	mlog("%s: invalid stat pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* slurmd_start_time */
    addTimeToMsg(stat->startTime, data);
    /* last slurmctld msg */
    addTimeToMsg(stat->now, data);
    /* debug */
    addUint16ToMsg(stat->debug, data);
    /* cpus */
    addUint16ToMsg(stat->cpus, data);
    /* boards */
    addUint16ToMsg(stat->boards, data);
    /* sockets */
    addUint16ToMsg(stat->sockets, data);
    /* cores */
    addUint16ToMsg(stat->coresPerSocket, data);
    /* threads */
    addUint16ToMsg(stat->threadsPerCore, data);
    /* real mem */
    addUint64ToMsg(stat->realMem, data);
    /* tmp disk */
    addUint32ToMsg(stat->tmpDisk, data);
    /* pid */
    addUint32ToMsg(stat->pid, data);
    /* hostname */
    addStringToMsg(stat->hostname, data);
    /* logfile */
    addStringToMsg(stat->logfile, data);
    /* step list */
    addStringToMsg(stat->stepList, data);
    /* version */
    addStringToMsg(stat->verStr, data);

    return true;
}

bool __packRespLaunchTasks(PS_SendDB_t *data, Resp_Launch_Tasks_t *ltasks,
			   const char *caller, const int line)
{
    uint32_t i;

    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (!ltasks) {
	mlog("%s: invalid ltasks pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    if (slurmProto >= SLURM_17_11_PROTO_VERSION) {
	/* jobid */
	addUint32ToMsg(ltasks->jobid, data);
	/* stepid */
	addUint32ToMsg(ltasks->stepid, data);
    }

    /* return code */
    addUint32ToMsg(ltasks->returnCode, data);
    /* node_name */
    addStringToMsg(ltasks->nodeName, data);
    /* count of pids */
    addUint32ToMsg(ltasks->countPIDs, data);
    /* local pids */
    addUint32ToMsg(ltasks->countLocalPIDs, data);
    for (i=0; i<ltasks->countLocalPIDs; i++) {
	addUint32ToMsg(ltasks->localPIDs[i], data);
    }
    /* global task IDs */
    addUint32ToMsg(ltasks->countGlobalTIDs, data);
    for (i=0; i<ltasks->countGlobalTIDs; i++) {
	addUint32ToMsg(ltasks->globalTIDs[i], data);
    }

    return true;
}

bool __packEnergyData(PS_SendDB_t *data, const char *caller, const int line)
{
    if (!data) {
	mlog("%s: invalid data pointer from '%s' at %i\n", __func__,
		caller, line);
	return false;
    }

    /* node name */
    addStringToMsg(getConfValueC(&Config, "SLURM_HOSTNAME"), data);

    /* we need at least 1 dummy sensor to prevent segfaults in slurmctld */
    addUint16ToMsg(1, data);

    /* dummy sensor data */
    addUint64ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint64ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint64ToMsg(0, data);
    addTimeToMsg(0, data);

    return true;
}

bool __unpackExtRespNodeReg(Slurm_Msg_t *sMsg, Ext_Resp_Node_Reg_t **respPtr,
			    const char *caller, const int line)
{
    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    char **ptr = &sMsg->ptr;

    Ext_Resp_Node_Reg_t *resp = umalloc(sizeof(*resp));

    getUint32(ptr, &resp->count);
    resp->entry = umalloc(sizeof(*resp->entry) * resp->count);

    uint32_t i;
    for (i=0; i<resp->count; i++) {
	getUint64(ptr, &resp->entry[i].allocSec);
	getUint64(ptr, &resp->entry[i].count);
	getUint32(ptr, &resp->entry[i].id);
	resp->entry[i].name = getStringM(ptr);
	resp->entry[i].type = getStringM(ptr);
    }

    *respPtr = resp;

    return true;
}
