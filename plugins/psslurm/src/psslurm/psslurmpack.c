/*
 * ParaStation
 *
 * Copyright (C) 2016-2018 ParTec Cluster Competence Center GmbH, Munich
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
#ifdef MIN_SLURM_PROTO_1605
    getUint64(ptr, &gres->countAlloc);
#else
    getUint32(ptr, &gres->countAlloc);
#endif
    getUint32(ptr, &gres->nodeCount);
    gres->nodeInUse = getBitString(ptr);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error: '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	releaseGresCred(gres);
	return NULL;
    }

#ifdef MIN_SLURM_PROTO_1605
    mdbg(PSSLURM_LOG_GRES, "%s: index '%i' pluginID '%u' gresCountAlloc '%lu'"
#else
    mdbg(PSSLURM_LOG_GRES, "%s: index '%i' pluginID '%u' gresCountAlloc '%u'"
#endif
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
#ifdef MIN_SLURM_PROTO_1605
    getUint64(ptr, &gres->countAlloc);
    gres->typeModel = getStringM(ptr);
#else
    getUint32(ptr, &gres->countAlloc);
#endif
    getUint32(ptr, &gres->nodeCount);

    if (magic != GRES_MAGIC) {
	mlog("%s: magic error '%u' : '%u'\n", __func__, magic, GRES_MAGIC);
	releaseGresCred(gres);
	return NULL;
    }

    mdbg(PSSLURM_LOG_GRES, "%s: index '%i' pluginID '%u' "
#ifdef MIN_SLURM_PROTO_1605
	    "gresCountAlloc '%lu' nodeCount '%u'\n", __func__, index,
#else
	    "gresCountAlloc '%u' nodeCount '%u'\n", __func__, index,
#endif
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
#ifdef MIN_SLURM_PROTO_1605
	gres->countStepAlloc = umalloc(sizeof(uint64_t) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    getUint64(ptr, &(gres->countStepAlloc)[i]);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' gres_cnt_step_alloc "
		    "'%lu'\n", __func__, i, gres->countStepAlloc[i]);
	}
#else
	gres->countStepAlloc = umalloc(sizeof(uint32_t) * gres->nodeCount);
	for (i=0; i<gres->nodeCount; i++) {
	    getUint32(ptr, &(gres->countStepAlloc)[i]);
	    mdbg(PSSLURM_LOG_GRES, "%s: node '%u' gres_cnt_step_alloc "
		    "'%u'\n", __func__, i, gres->countStepAlloc[i]);
	}
#endif
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
    JobCred_t *cred;
    char **ptr = &sMsg->ptr;

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

    cred = ucalloc(sizeof(JobCred_t));

    /* jobid / stepid */
    getUint32(ptr, &cred->jobid);
    getUint32(ptr, &cred->stepid);
    /* uid */
    getUint32(ptr, &cred->uid);

    /* gres job/step allocations */
    unpackGres(ptr, gresList, cred->jobid, cred->stepid, cred->uid);

    /* count of specialized cores */
    getUint16(ptr, &cred->jobCoreSpec);
    /* job/step memory limit */
#ifdef SLURM_PROTOCOL_1702
    getUint64(ptr, &cred->jobMemLimit);
    getUint64(ptr, &cred->stepMemLimit);
#else
    getUint32(ptr, &cred->jobMemLimit);
    getUint32(ptr, &cred->stepMemLimit);
#endif
#ifdef MIN_SLURM_PROTO_1605
    /* job constraints */
    cred->jobConstraints = getStringM(ptr);
#endif
    /* hostlist */
    cred->hostlist = getStringM(ptr);
    if (!cred->hostlist) {
	mlog("%s: empty hostlist in credential\n", __func__);
	goto ERROR;
    }
    /* time */
    getTime(ptr, &cred->ctime);
    /* core/socket maps */
    getUint32(ptr, &cred->totalCoreCount);

#ifdef SLURM_PROTOCOL_1702
    char *bitStr;
    size_t listSize;

    bitStr = getBitString(ptr);
    cred->jobCoreBitmap = NULL;
    listSize = 0;
    hexBitstr2List(bitStr, &cred->jobCoreBitmap, &listSize);
    ufree(bitStr);

    bitStr = getBitString(ptr);
    cred->stepCoreBitmap = NULL;
    listSize = 0;
    hexBitstr2List(bitStr, &cred->stepCoreBitmap, &listSize);
    ufree(bitStr);
#else
    cred->jobCoreBitmap = getStringM(ptr);
    cred->stepCoreBitmap = getStringM(ptr);
#endif
    getUint16(ptr, &cred->coreArraySize);

    mdbg(PSSLURM_LOG_PART, "%s: totalCoreCount '%u' coreArraySize '%u' "
	    "jobCoreBitmap '%s' stepCoreBitmap '%s'\n",
	    __func__, cred->totalCoreCount, cred->coreArraySize,
	    cred->jobCoreBitmap, cred->stepCoreBitmap);

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

    getUint32(ptr, &cred->jobNumHosts);
    cred->jobHostlist = getStringM(ptr);
    *credEnd = *ptr;

    cred->sig = getStringM(ptr);

    *credPtr = cred;

    return true;

ERROR:
    freeJobCred(cred);
    return false;
}

bool __unpackBCastCred(Slurm_Msg_t *sMsg, BCast_t *bcast, char **credEnd,
		       const char *caller, const int line)
{
    char **ptr = &sMsg->ptr;
    time_t ctime;
    char *nodes;

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!bcast) {
	mlog("%s: invalid bcast from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!credEnd) {
	mlog("%s: invalid credEnd from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    /* creation time */
    getTime(ptr, &ctime);
    /* expiration time */
    getTime(ptr, &bcast->expTime);
    /* jobid */
    getUint32(ptr, &bcast->jobid);
    /* nodes */
    nodes = getStringM(ptr);
    ufree(nodes);

    *credEnd = *ptr;
    /* signature */
    bcast->sig = getStringML(ptr, &bcast->sigLen);

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
#ifdef MIN_SLURM_PROTO_1605
    /* index */
    addUint16ToMsg(head->index, data);
#endif
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
#ifdef MIN_SLURM_PROTO_1605
	/* tree width */
	addUint16ToMsg(head->treeWidth, data);
#endif
    }

    /* return list */
    addUint16ToMsg(head->returnList, data);
    for (i=0; i<head->returnList; i++) {
	/* error */
	addUint32ToMsg(head->fwdata[i].error, data);

	/* msg type */
	addUint16ToMsg(head->fwdata[i].type, data);

	/* nodename */
	hn = getHostnameByNodeId(head->fwdata[i].node);
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
    Req_Terminate_Job_t *req;
    char **ptr = &sMsg->ptr;

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    if (!reqPtr) {
	mlog("%s: invalid reqPtr from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    req = ucalloc(sizeof(Req_Terminate_Job_t));

    /* jobid*/
    getUint32(ptr, &req->jobid);
#ifndef SLURM_PROTOCOL_1702
    /* stepid */
    getUint32(ptr, &req->stepid);
#endif
    /* jobstate */
#ifdef MIN_SLURM_PROTO_1605
    getUint32(ptr, &req->jobstate);
#else
    getUint16(ptr,(uint16_t) &req->jobstate);
#endif
    /* user id */
    getUint32(ptr, &req->uid);
#ifdef SLURM_PROTOCOL_1702
    uint32_t tmp;

    /* nodes */
    req->nodes = getStringM(ptr);
    /* pelogue env */
    getStringArrayM(ptr, &req->pelogueEnv.vars, &req->pelogueEnv.cnt);
    /* job info */
    getUint32(ptr, &tmp);
    /* spank env */
    getStringArrayM(ptr, &req->spankEnv.vars, &req->spankEnv.cnt);
    /* start time */
    getTime(ptr, &req->startTime);
    /* step id */
    getUint32(ptr, &req->stepid);
#endif

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

#ifndef SLURM_PROTOCOL_1702
    uint16_t userManagedIO;
    getUint16(ptr, &userManagedIO);
    if (userManagedIO) step->taskFlags |= LAUNCH_USER_MANAGED_IO;
#endif
    if (!(step->taskFlags & LAUNCH_USER_MANAGED_IO)) {
	/* stdout options */
	step->stdOut = getStringM(ptr);
	/* stderr options */
	step->stdErr = getStringM(ptr);
	/* stdin options */
	step->stdIn = getStringM(ptr);
#ifndef SLURM_PROTOCOL_1702
	uint8_t bufferedIO, labelIO;
	/* buffered I/O = default (unbufferd = RAW) */
	getUint8(ptr, &bufferedIO);
#ifdef SLURM_PROTOCOL_1605
	/* flag now stands for unbuffered IO */
	if (!bufferedIO) step->taskFlags |= LAUNCH_BUFFERED_IO;
#else
	if (bufferedIO) step->taskFlags |= LAUNCH_BUFFERED_IO;
#endif /* end SLURM_PROTOCOL_1605 */
	/* label I/O = sourceprintf */
	getUint8(ptr, &labelIO);
	if (labelIO) step->taskFlags |= LAUNCH_LABEL_IO;
#endif
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
    char **ptr = &sMsg->ptr;
    Step_t *step;
    uint16_t debug;
    uint32_t jobid, stepid, count, i, tmp;
    char jobOpt[512];

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    /* jobid/stepid */
    getUint32(ptr, &jobid);
    getUint32(ptr, &stepid);

    step = addStep(jobid, stepid);

#ifdef SLURM_PROTOCOL_1702
    /* unused and to be removed in 1711  */
    uint32_t mpiJobid;
    uint32_t mpiNnodes;
    uint32_t mpiNtasks;
    uint32_t mpiStepfnodeid;
    uint32_t mpiStepftaskid;
    uint32_t mpiStepid;

    getUint32(ptr, &mpiJobid);
    getUint32(ptr, &mpiNnodes);
    getUint32(ptr, &mpiNtasks);
    getUint32(ptr, &mpiStepfnodeid);
    getUint32(ptr, &mpiStepftaskid);
    getUint32(ptr, &mpiStepid);
#endif
    /* ntasks */
    getUint32(ptr, &step->np);
#ifdef MIN_SLURM_PROTO_1605
    /* ntasks per board/core/socket */
    getUint16(ptr, &step->ntasksPerBoard);
    getUint16(ptr, &step->ntasksPerCore);
    getUint16(ptr, &step->ntasksPerSocket);
#endif
#if SLURM_PROTOCOL_1702
    /* currently unused */
    uint32_t packJobid;
    uint32_t packStepid;

    /* pack jobid */
    getUint32(ptr, &packJobid);
    /* pack stepid */
    getUint32(ptr, &packStepid);
#endif
    /* uid */
    getUint32(ptr, &step->uid);
    /* partition */
    step->partition = getStringM(ptr);
    /* username */
    step->username = getStringM(ptr);
    /* gid */
    getUint32(ptr, &step->gid);
    /* job/step mem limit */
#if SLURM_PROTOCOL_1702
    getUint64(ptr, &step->jobMemLimit);
    getUint64(ptr, &step->stepMemLimit);
#else
    getUint32(ptr, &step->jobMemLimit);
    getUint32(ptr, &step->stepMemLimit);
#endif
    /* num of nodes */
    getUint32(ptr, &step->nrOfNodes);
    if (!step->nrOfNodes) {
	mlog("%s: invalid nrOfNodes %u\n", __func__, step->nrOfNodes);
	goto ERROR;
    }
    /* cpus_per_task */
    getUint16(ptr, &step->tpp);
    /* task distribution */
#ifdef MIN_SLURM_PROTO_1605
    getUint32(ptr, &step->taskDist);
#else
    getUint16(ptr, &step->taskDist);
#endif
    /* node cpus */
    getUint16(ptr, &step->nodeCpus);
    /* count of specialized cores */
    getUint16(ptr, &step->jobCoreSpec);
#ifdef MIN_SLURM_PROTO_1605
    /* accel bind type */
    getUint16(ptr, &step->accelBindType);
#endif

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
#ifdef SLURM_PROTOCOL_1702
    /* task flags */
    getUint32(ptr, &step->taskFlags);
#else
    /* task flags */
    getUint16(ptr, (uint16_t *) &step->taskFlags);
    /* multi prog */
    uint16_t multiProg;
    getUint16(ptr, &multiProg);
    if (multiProg) step->taskFlags |= LAUNCH_MULTI_PROG;
#endif
    /* I/O options */
    unpackStepIOoptions(step, ptr);

    /* profile */
    getUint32(ptr, &step->profile);
    /* prologue/epilogue */
    step->taskProlog = getStringM(ptr);
    step->taskEpilog = getStringM(ptr);
    /* debug mask */
    getUint16(ptr, &debug);

    /* switch plugin, does not add anything when using "switch/none" */

    /* job options (plugin) */
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
    /* hostlist */
    step->slurmHosts = getStringM(ptr);

    /* I/O open_mode */
    getUint8(ptr, &step->appendMode);
#ifndef SLURM_PROTOCOL_1702
    /* pty */
    uint8_t pty;
    getUint8(ptr, &pty);
    if (pty) step->taskFlags |= LAUNCH_PTY;
#endif
    /* acctg freq */
    step->acctFreq = getStringM(ptr);
#ifdef MIN_SLURM_PROTO_1605
    /* cpu freq min/max/gov */
    getUint32(ptr, &step->cpuFreqMin);
    getUint32(ptr, &step->cpuFreqMax);
    getUint32(ptr, &step->cpuFreqGov);
#else
    /* cpu freq */
    getUint32(ptr, &step->cpuFreq);
#endif
    step->checkpoint = getStringM(ptr);
    step->restart = getStringM(ptr);
    /* jobinfo plugin id */
    getUint32(ptr, &tmp);
#ifdef SLURM_PROTOCOL_1702
    /* pelogue env */
    getStringArrayM(ptr, &step->pelogueEnv.vars, &step->pelogueEnv.cnt);
#endif

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
    char **ptr = &sMsg->ptr;
    Job_t *job;
    uint32_t jobid, tmp, count;
    char buf[1024];

    if (!sMsg) {
	mlog("%s: invalid sMsg from '%s' at %i\n", __func__, caller, line);
	return false;
    }

    /* jobid */
    getUint32(ptr, &jobid);
    /* stepid */
    getUint32(ptr, &tmp);
    if (tmp != SLURM_BATCH_SCRIPT) {
	mlog("%s: batch job should not have stepid '%u'\n", __func__, tmp);
	return false;
    }

    job = addJob(jobid);

    /* uid */
    getUint32(ptr, &job->uid);
    /* partition */
    job->partition = getStringM(ptr);
    /* username */
    job->username = getStringM(ptr);
    /* gid */
    getUint32(ptr, &job->gid);
    /* ntasks */
    getUint32(ptr, &job->np);
#ifdef SLURM_PROTOCOL_1702
    /* pn_min_memory */
    getUint64(ptr, &job->nodeMinMemory);
#else
    /* pn_min_memory */
    getUint32(ptr, &job->nodeMinMemory);
#endif
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
    /* cpu bind type */
    getUint16(ptr, &job->cpuBindType);
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
    /* checkpoint dir */
    job->checkpoint = getStringM(ptr);
    /* restart dir */
    job->restart = getStringM(ptr);
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
#ifdef SLURM_PROTOCOL_1702
    getUint64(ptr, &job->memLimit);
#else
    getUint32(ptr, &job->memLimit);
#endif

    /* job credential */
    if (!(job->cred = extractJobCred(&job->gresList, sMsg, 1))) {
	mlog("%s: extracting job credentail failed\n", __func__);
	goto ERROR;
    }

    /* jobinfo plugin id */
    getUint32(ptr, &tmp);
#ifdef MIN_SLURM_PROTO_1605
    /* TODO: account */
    job->account = getStringM(ptr);
    /* TODO: qos */
    job->qos = getStringM(ptr);
    /* TODO: resv name */
    job->resvName = getStringM(ptr);
#endif
#ifdef SLURM_PROTOCOL_1702
    /* TODO: profile */
    getUint32(ptr, &job->profile);
    /* TODO: pelogue env */
    getStringArrayM(ptr, &job->pelogueEnv.vars, &job->pelogueEnv.cnt);
    /* TODO: resv ports */
    job->resvPorts = getStringM(ptr);
    /* TODO: group number */
    getUint32(ptr, &job->groupNum);
#endif

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
#ifdef SLURM_PROTOCOL_1702
    addUint64ToMsg(ping->freemem, data);
#else
    addUint32ToMsg((uint32_t) ping->freemem, data);
#endif

    return true;
}

static void packAccNodeId(PS_SendDB_t *data, int type,
			  AccountDataExt_t *accData, PSnodes_ID_t *nodes,
			  uint32_t nrOfNodes)
{
    PSnodes_ID_t psNodeID;
    int nid;

    psNodeID = PSC_getID(accData->taskIds[type]);

    if ((nid = getSlurmNodeID(psNodeID, nodes, nrOfNodes)) < 0) {
	addUint32ToMsg((uint32_t) 0, data);
    } else {
	addUint32ToMsg((uint32_t) nid, data);
    }
    addUint16ToMsg((uint16_t) 0, data);
}

bool __packSlurmAccData(PS_SendDB_t *data, SlurmAccData_t *slurmAccData,
			const char *caller, const int line)
{
    AccountDataExt_t *accData = slurmAccData->accData;
    int i;

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

    if (slurmAccData->empty) {
	/* pack empty account data */
	for (i=0; i<6; i++) {
	    addUint64ToMsg(0, data);
	}
	for (i=0; i<5; i++) {
	    addUint32ToMsg(0, data);
	}
#ifdef MIN_SLURM_PROTO_1605
	addDoubleToMsg(0, data);
#else
	addUint32ToMsg(0, data);
#endif
	addUint32ToMsg(0, data);
#ifdef MIN_SLURM_PROTO_1605
	addUint64ToMsg(0, data);
#else
	addUint32ToMsg(0, data);
#endif
	for (i=0; i<4; i++) {
	    addDoubleToMsg(0, data);
	}
	for (i=0; i<6; i++) {
	    addUint32ToMsg((uint32_t) NO_VAL, data);
	    addUint16ToMsg((uint16_t) NO_VAL, data);
	}
	return true;
    }

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
#ifdef MIN_SLURM_PROTO_1605
    addDoubleToMsg(accData->totCputime, data);
#else
    addUint32ToMsg(accData->totCputime, data);
#endif

    /* act cpufreq */
    addUint32ToMsg(accData->cpuFreq, data);

    /* energy consumed */
#ifdef MIN_SLURM_PROTO_1605
    addUint64ToMsg(0, data);
#else
    addUint32ToMsg(0, data);
#endif

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

    return true;
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
#ifdef MIN_SLURM_PROTO_1605
    /* features active/avail */
    addStringToMsg(NULL, data);
    addStringToMsg(NULL, data);
#endif
    /* node_name */
    addStringToMsg(stat->nodeName, data);
    /* architecture */
    addStringToMsg(stat->arch, data);
#ifdef MIN_SLURM_PROTO_1605
    /* cpu spec list */
    addStringToMsg("", data);
#endif
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
#ifdef SLURM_PROTOCOL_1702
    addUint64ToMsg(stat->realMem, data);
#else
    addUint32ToMsg((uint32_t) stat->realMem, data);
#endif
    /* tmp disk */
    addUint32ToMsg(stat->tmpDisk, data);
    /* uptime */
    addUint32ToMsg(stat->uptime, data);
    /* hash value of the SLURM config file */
    addUint32ToMsg(stat->config, data);
    /* cpu load */
    addUint32ToMsg(stat->cpuload, data);
    /* free memory */
#ifdef SLURM_PROTOCOL_1702
    addUint64ToMsg(stat->freemem, data);
#else
    addUint32ToMsg((uint32_t) stat->freemem, data);
#endif
    /* job infos */
    addUint32ToMsg(stat->jobInfoCount, data);
    for (i=0; i<stat->jobInfoCount; i++) {
	addUint32ToMsg(stat->jobids[i], data);
    }
    for (i=0; i<stat->jobInfoCount; i++) {
	addUint32ToMsg(stat->stepids[i], data);
    }

    /*
    pack16(msg->startup, buffer);
    if (msg->startup)
	switch_g_pack_node_info(msg->switch_nodeinfo, buffer);
	*/
    /* TODO switch stuff */
    addUint16ToMsg(0, data);

    /* add gres configuration */
    addGresData(data, SLURM_CUR_PROTOCOL_VERSION);

    /* TODO: acct_gather_energy_pack(msg->energy, buffer, protocol_version); */
#ifdef MIN_SLURM_PROTO_1605
    addUint64ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint64ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint64ToMsg(0, data);
#else
    addUint32ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint32ToMsg(0, data);
    addUint32ToMsg(0, data);
#endif
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
#ifdef SLURM_PROTOCOL_1702
    getUint32(ptr, &bcast->blockNumber);
#else
    getUint16(ptr, &bcast->blockNumber);
#endif
    /* compression */
#ifdef MIN_SLURM_PROTO_1605
    getUint16(ptr, &bcast->compress);
#endif
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
#ifdef MIN_SLURM_PROTO_1605
    /* uncompressed length */
    getUint32(ptr, &bcast->uncompLen);
#ifdef SLURM_PROTOCOL_1702
    /* block offset */
    getUint64(ptr, &bcast->blockOffset);
#else
    getUint32(ptr, &bcast->blockOffset);
#endif
    /* file size */
    getUint64(ptr, &bcast->fileSize);
#endif
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
#ifdef SLURM_PROTOCOL_1702
    addUint64ToMsg(stat->realMem, data);
#else
    addUint32ToMsg((uint32_t) stat->realMem, data);
#endif
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
