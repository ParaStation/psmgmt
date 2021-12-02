/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>

#include "pscommon.h"
#include "timer.h"
#include "psidcomm.h"
#include "psidtask.h"
#include "psidpartition.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"

#include "psaccounthandles.h"
#include "pspamhandles.h"

#include "psmomspawn.h"
#include "psmomlog.h"
#include "psmomjob.h"
#include "psmomconfig.h"
#include "psmomchild.h"
#include "psmomsignal.h"
#include "psmomcollect.h"
#include "psmomproto.h"
#include "psmompscomm.h"
#include "psmomforwarder.h"
#include "psmomjobinfo.h"
#include "psmom.h"
#include "psmomconv.h"
#include "psmomlocalcomm.h"
#include "psmomkvs.h"

#include "psmomscript.h"

int checkPELogueFileStats(char *filename, int root)
{
    struct stat statbuf;

    if (stat(filename, &statbuf) == -1) {
	return -1;
    }

    if (root) {
	/* readable and executable by root and NOT writable by anyone
	 * besides root */
	if (statbuf.st_uid != 0) {
	    return -2;
	}
	if (!S_ISREG(statbuf.st_mode) ||
	    ((statbuf.st_mode & (S_IRUSR | S_IXUSR)) != (S_IRUSR | S_IXUSR)) ||
	    (statbuf.st_mode & (S_IWGRP | S_IWOTH))) {
	    return -2;
	}
    } else {
	/* readable and executable by root and other  */
	if ((statbuf.st_mode & (S_IROTH | S_IXOTH)) != (S_IROTH | S_IXOTH)) {
	    return -2;
	}
    }
    return 1;
}

void signalPElogue(Job_t *job, char *signal, char *reason)
{
    DDTypedBufferMsg_t msg = {
	.header = {
	    .type = PSP_PLUG_PSMOM,
	    .sender = PSC_getMyTID(),
	    .dest = PSC_getMyTID(),
	    .len = offsetof(DDTypedBufferMsg_t, buf) },
	.type = PSP_PSMOM_PELOGUE_SIGNAL };
    int32_t *finishPtr, i;

    /* Add string including its length mimicking addString */
    uint32_t len = htonl(PSP_strLen(job->id));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "jobID", job->id, PSP_strLen(job->id));

    /* Add string including its length mimicking addString */
    len = htonl(PSP_strLen(signal));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "signal", signal, PSP_strLen(signal));

    /* add space for finish flag */
    finishPtr = (int32_t *)(msg.buf + (msg.header.len
				       - offsetof(DDTypedBufferMsg_t, buf)));

    PSP_putTypedMsgBuf(&msg, "<wild-card>", NULL, sizeof(int32_t));

    /* Add string including its length mimicking addString */
    len = htonl(PSP_strLen(reason));
    PSP_putTypedMsgBuf(&msg, "len", &len, sizeof(len));
    PSP_putTypedMsgBuf(&msg, "reason", reason, PSP_strLen(reason));

    for (i=0; i<job->nrOfUniqueNodes; i++) {
	PSnodes_ID_t id = job->nodes[i].id;

	/* add the individual pelogue finish flag */
	if (job->state == JOB_PROLOGUE) {
	    *finishPtr = job->nodes[i].prologue;
	} else {
	    *finishPtr = job->nodes[i].epilogue;
	}
	msg.header.dest = PSC_getTID(id, 0);

	mdbg(PSMOM_LOG_PSCOM, "%s: send to %i [%s", __func__, id,
	     PSC_printTID(msg.header.sender));
	mdbg(PSMOM_LOG_PSCOM, "->%s]\n", PSC_printTID(msg.header.dest));
	sendMsg(&msg);
    }
}

/**
 * @brief Prepare the forwarder environments.
 *
 * @param info Not used
 *
 * @return No return value
 */
static void prepScriptEnv(void *info)
{
    /* just make sure we don't call any main cleanup routines on exit */
    isMaster = 0;
}

void stopPElogueExecution(Job_t *job)
{
    removePELogueTimeout(job);

    if (job->state == JOB_CANCEL_INTERACTIVE) {
	/* start the epilogue script(s) */
	job->epilogueTrack = job->nrOfUniqueNodes;
	job->state = JOB_EPILOGUE;
	sendPElogueStart(job, false);
	monitorPELogueTimeout(job);
	return;
    }

    if (job->state == JOB_PROLOGUE || job->state == JOB_CANCEL_PROLOGUE) {
	if (job->qsubPort) {
	    /* wait for interactive forwarder to exit to send
	     * job termination */

	    job->state = JOB_CANCEL_PROLOGUE;
	    stopInteractiveJob(job);
	    return;
	}

	job->jobscriptExit = -3;
	job->state = JOB_EXIT;

	/* connect to the pbs_server and send a job obit msg */
	if (job->signalFlag == SIGTERM || job->signalFlag == SIGKILL) {
	    job->prologueExit = 1;
	}

	if (job->prologueExit == 1) {
	    job->jobscriptExit = -2;
	}
	job->end_time = time(NULL);

	sendTMJobTermination(job);
    } else if (job->state == JOB_EPILOGUE || job->state == JOB_CANCEL_EPILOGUE) {

	/* all epilogue scripts finished */
	job->end_time = time(NULL);
	job->state = JOB_EXIT;

	sendTMJobTermination(job);
    }
}

/**
 * @brief Get job's HW-threads
 *
 * Convert the PBS node-list of the job @a job into a list of
 * HW-threads that is understood by ParaStation's psid and can be used
 * as a task's partition.
 *
 * @param job The job to convert the node-list for.
 *
 * @return Return the generated ParaStation list of HW-threads or NULL on
 * error.
 */
static PSpart_HWThread_t *getThreads(Job_t *job)
{
    const char delim_host[] ="+\0";
    char *exec_hosts;
    char *tmp, *nodeStr, *toksave;
    int threads = 0;
    PSpart_HWThread_t *thrdList;

    if (!(exec_hosts = getJobDetail(&job->data, "exec_host", NULL))) {
	mdbg(PSMOM_LOG_WARN, "%s: getting exec_hosts for job '%s' failed\n",
	    __func__, job->id);
	return NULL;
    }

    thrdList = umalloc(job->nrOfNodes * sizeof(*thrdList));
    if (!thrdList) {
	mwarn(errno, "%s: thrdList", __func__);
	return NULL;
    }

    tmp = ustrdup(exec_hosts);
    nodeStr = strtok_r(tmp, delim_host, &toksave);
    while (nodeStr) {
	char *CPUStr = strchr(nodeStr,'/');
	if (CPUStr) {
	    PSnodes_ID_t node;
	    char *endPtr;
	    int16_t id;

	    CPUStr[0] = '\0';
	    CPUStr++;
	    node = getNodeIDbyName(nodeStr);
	    if (node == -1) {;
		mlog("%s: No id for node '%s'\n", __func__, nodeStr);
		ufree(thrdList);
		ufree(tmp);
		return NULL;
	    }
	    id = strtol(CPUStr, &endPtr, 0);
	    if (*endPtr != '\0') {
		mlog("%s: No id for CPU '%s'\n", __func__, CPUStr);
		ufree(thrdList);
		ufree(tmp);
		return NULL;
	    }
	    if (threads >= job->nrOfNodes) {
		mlog("%s: Too many nodes in exec_host list.\n", __func__);
		ufree(thrdList);
		ufree(tmp);
		return NULL;
	    }

	    thrdList[threads].node = node;
	    thrdList[threads].id = id;
	    thrdList[threads].timesUsed = 0;
	    threads++;
	}
	nodeStr = strtok_r(NULL, delim_host, &toksave);
    }
    ufree(tmp);

    return thrdList;
}

static void registerPartition(Job_t *job, int childType)
{
    Child_t *jobChild;

    if (!childType) return;

    jobChild = findChildByJobid(job->id, childType);

    if (!jobChild) {
	mlog("%s: no child found, cannot register partition.\n", __func__);
	return;
    }

    job->resDelegate = PStask_new();
    if (!job->resDelegate)  {
	mlog("%s: cannot create delegate.\n", __func__);
	return;
    }

    job->resDelegate->group = TG_DELEGATE;
    job->resDelegate->tid = PSC_getTID(-1, jobChild->pid);
    mdbg(PSMOM_LOG_VERBOSE, "%s: use child with pid %d / tid %s\n", __func__,
	 jobChild->pid, PSC_printTID(job->resDelegate->tid));
    job->resDelegate->uid = job->passwd.pw_uid;
    job->resDelegate->gid = job->passwd.pw_gid;
    job->resDelegate->nextResID = job->resDelegate->tid + 0x042;
    job->resDelegate->partThrds = getThreads(job);
    if (!job->resDelegate->partThrds) {
	/* we did not find the corresponding batch job */
	mlog("%s: cannot create list of HW-threads for %s\n", __func__,
	     PSC_printTID(job->resDelegate->tid));
	PStask_delete(job->resDelegate);
	job->resDelegate = NULL;
	return;
    }
    job->resDelegate->totalThreads = job->nrOfNodes;
    job->resDelegate->usedThreads = 0;
    job->resDelegate->activeChild = 0;

    PStasklist_enqueue(&managedTasks, job->resDelegate);

    /* Now register the partition at the master */
    PSIDpart_register(job->resDelegate);
}

static void PElogueExit(Job_t *job, int status, bool prologue)
{
    char *peType;
    int *track;
    int *epExit;

    peType = prologue ? "prologue" : "epilogue";
    track = (prologue) ? &job->prologueTrack : &job->epilogueTrack;
    epExit = (prologue) ? &job->prologueExit : &job->epilogueExit;

    /* check if job is in PElogue state */
    if (job->state != JOB_PROLOGUE &&
	job->state != JOB_EPILOGUE &&
	job->state != JOB_CANCEL_PROLOGUE &&
	job->state != JOB_CANCEL_EPILOGUE &&
	job->state != JOB_CANCEL_INTERACTIVE) {
	mlog("%s: %s exit for job '%s' which is in state '%s'\n",
	    __func__, peType, job->id, jobState2String(job->state));
	return;
    }

    if (*track < 0) {
	mlog("%s: %s tracking error for job '%s'\n", __func__, peType,
		job->id);
	return;
    }

    *track = *track -1;

    /* check if PElogue was running on all hosts */
    if (!(*track)) {
	if (*epExit == 0) {
	    *epExit = status;
	}

	if (job->state == JOB_CANCEL_INTERACTIVE) {
	    stopPElogueExecution(job);
	    return;
	}

	if (prologue) {
	    int childType = 0;
	    /* stop execution if prologue failed */
	    if (*epExit != 0 || job->state == JOB_CANCEL_PROLOGUE) {

		mlog("%s: prologue %s: exit '%i', abort job '%s'\n",
		    __func__, !job->signalFlag ? "failed" : "canceled",
		    *epExit, job->id);

		stopPElogueExecution(job);
		return;
	    }

	    /* stop monitoring the PELouge script for timeout */
	    removePELogueTimeout(job);

	    /* execute the actual job */
	    if (job->qsubPort) {
		ComHandle_t *com;

		/* job is interactive */
		if ((com = getJobCom(job, JOB_CON_FORWARD))) {
		    startInteractiveJob(job, com);
		    childType = PSMOM_CHILD_INTERACTIVE;
		} else {
		    mlog("%s: interactive forwarder connection for job '%s'"
			    " not found\n",
			    __func__, job->id);

		    job->prologueExit = 1;
		    job->state = JOB_EXIT;
		    job->jobscriptExit = -2;
		    sendTMJobTermination(job);
		}
	    } else {
		spawnJobScript(job);
		childType = PSMOM_CHILD_JOBSCRIPT;
	    }
	    /* Register partition here */
	    registerPartition(job, childType);
	} else {
	    stopPElogueExecution(job);
	}
    } else if (status != 0 && *epExit == 0 &&
		job->state != JOB_CANCEL_INTERACTIVE) {
	char *reason;

	/* update job state */
	job->state = (prologue) ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
	reason = (prologue) ? "prologue failed" : "epilogue failed";

	/* Cancel the PElogue scripts on all hosts. The signal
	 * SIGTERM will force the forwarder for PElogue scripts
	 * to kill the script. */
	if (job->signalFlag != SIGTERM && job->signalFlag != SIGKILL) {
	    signalPElogue(job, "SIGTERM", reason);
	}
    }

    if (status != 0 && (status > *epExit || status < 0)) {
	*epExit = status;
    }
}

void handlePELogueSignal(DDTypedBufferMsg_t *msg)
{
    struct stat statbuf;
    char buf[100], signal[100], jobid[JOB_NAME_LEN], reason[100];
    char *ptr = msg->buf;
    int isignal;
    int32_t finish;
    Child_t *child;

    /* get jobid */
    getString(&ptr, jobid, sizeof(jobid));

    /* get signal */
    getString(&ptr, signal, sizeof(signal));
    if (!(isignal = string2Signal(signal))) {
	mlog("%s: got invalid signal '%s'\n", __func__, signal);
	return;
    }

    /* get the finish/slient flag */
    getInt32(&ptr, &finish);

    /* find job */
    if (!(child = findChildByJobid(jobid, PSMOM_CHILD_PROLOGUE))) {
	if (!(child = findChildByJobid(jobid, PSMOM_CHILD_EPILOGUE))) {
	    if (finish == -1) {
		mdbg(PSMOM_LOG_WARN, "%s: child for job '%s' not found\n",
		    __func__, jobid);
	    }
	    return;
	}
    }

    /* get the reason for sending the signal */
    getString(&ptr, reason, sizeof(reason));

    /* save the signal we are about to send */
    if (isignal == SIGTERM || isignal == SIGKILL) {
	child->signalFlag = isignal;
    }

    /* send the signal */
    if (child->c_sid != -1) {
	mlog("signal '%s (%i)' to pelogue '%s' - reason '%s' - sid '%i'\n",
		signal, isignal, jobid, reason, child->c_sid);
	psAccountSignalSession(child->c_sid, isignal);
    } else if (child->c_pid != -1) {
	mlog("signal '%s (%i)' to pelogue '%s' - reason '%s' - pid '%i'\n",
		signal, isignal, jobid, reason, child->c_pid);
	kill(child->c_pid, isignal);
    } else if ((child->sharedComm && isValidComHandle(child->sharedComm))) {
	mlog("signal '%s (%i)' to pelogue '%s' - reason '%s' - forwarder\n",
		signal, isignal, jobid, reason);
	WriteDigit(child->sharedComm, CMD_LOCAL_SIGNAL);
	WriteDigit(child->sharedComm, isignal);
	wDoSend(child->sharedComm);
    } else {
	/* verify child is still alive */
	snprintf(buf, sizeof(buf), "/proc/%i", child->pid);
	if (stat(buf, &statbuf) == -1) {
	    mdbg(PSMOM_LOG_WARN, "%s: not sending signal '%s' to job '%s' : "
		    "forwarder already died\n", __func__, signal, jobid);
	} else {
	    mlog("signal '%s (%i)' to pelogue '%s' - reason '%s' - child\n",
		    signal, isignal, jobid, reason);

	    if (isignal == SIGKILL) {
		/* let the forwarder a little time for cleanup before killing it
		 * hard via SIGKILL */
		if (!child->killFlag) {
		    kill(child->pid, SIGTERM);
		    child->killFlag = 1;
		} else {
		    kill(child->pid, isignal);
		}
	    }
	}
    }
}

void handlePELogueFinish(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *rData)
{
    PSnodes_ID_t nodeId = PSC_getID(msg->header.sender);
    char *ptr = rData->buf;
    char buf[300], *peType;
    int32_t res = 1, signalFlag;
    time_t job_start;
    int prologue = msg->type == PSP_PSMOM_PROLOGUE_FINISH ? 1 : 0;

    peType = prologue ? "prologue" : "epilogue";

    /* get jobid */
    getString(&ptr, buf, sizeof(buf));

    Job_t *job = findJobById(buf);
    if (!job) {
	if (!isJobIDinHistory(buf)) {
	    mdbg(PSMOM_LOG_WARN, "%s: '%s' finish message for unknown"
		 " job '%s', ignoring it\n", __func__, peType, buf);
	}
	return;
    }

    /* get job start_time */
    getTime(&ptr, &job_start);

    if (job->start_time != job_start) {
	/* msg is for previous job, ignore */
	mdbg(PSMOM_LOG_WARN, "%s: received '%s' finish from previous"
	    " job '%s', ignoring it\n", __func__, peType, job->id);
	return;
    }

    /* get result */
    getInt32(&ptr, &res);

    Job_Node_List_t *nodeEntry = findJobNodeEntry(job, nodeId);
    if (nodeEntry) {
	if (prologue) {
	    nodeEntry->prologue = res;
	} else {
	    nodeEntry->epilogue = res;
	}
    }

    /* get signal flag */
    getInt32(&ptr, &signalFlag);

    /* on error get errmsg */
    if (res) {
	getString(&ptr, buf, sizeof(buf));

	/* suppress error message if we have killed the pelogue by request */
	if (!signalFlag) {
	    mlog("%s: '%s' for job '%s' node '%s(%i)' failed: %s", __func__,
		 peType, job->id, getHostnameByNodeId(nodeId), nodeId, buf);
	} else {
	    mdbg(PSMOM_LOG_PELOGUE, "%s: '%s' for job '%s' node '%s(%i)' "
		 "failed: %s", __func__, peType, job->id,
		 getHostnameByNodeId(nodeId), nodeId, buf);
	}
    }

    PElogueExit(job, res, prologue);
}

/**
 * @brief Set my node offline.
 *
 * @param server The server to request the offline state.
 *
 * @param jobid The jobid for the offline message.
 *
 * @param prologue Set to 1 if the prologue failed or 0 otherwise.
 *
 * @param host The host to set offline, NULL for my host.
 *
 * @return No return value.
 */
static void PElogueTimeoutAction(char *server, char *jobid, int prologue,
				    const char *host)
{
    char note[128], buf[32];
    int offline;
    struct tm *ts;
    time_t now;

    offline = getConfValueI(&config, "OFFLINE_PELOGUE_TIMEOUT");
    if (!offline) return;

    mlog("%s: %s for job '%s' timed out, setting node '%s' offline\n", __func__,
	    prologue ? "prologue" : "epilogue", jobid, host ? host : "myself");

    /* prepare note */
    now = time(NULL);
    ts = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ts);
    snprintf(note, sizeof(note), "psmom - %s - %s for job '%s' timed out",
		buf, prologue ? "prologue" : "epilogue", jobid);

    setPBSNodeOffline(server, host, note);
}

/**
 * @brief Callback for Pro/Epilogue scripts.
 *
 * @return Always returns 0.
 */
static int callbackPElogue(int fd, PSID_scriptCBInfo_t *info)
{
    int32_t exitStat, signalFlag = 0;
    PElogue_Data_t *data = info ? info->info : NULL;
    char errMsg[300] = {'\0'};
    size_t errLen;

    /* fetch error msg and exit status */
    bool ret = getScriptCBdata(fd, info, &exitStat,
			       errMsg, sizeof(errMsg), &errLen);
    ufree(info);
    if (!ret) {
	mlog("%s: invalid cb data\n", __func__);
	return 0;
    }

    if (!data) {
	mlog("%s: no data\n", __func__);
	return 0;
    }

    /* log error locally and forward to mother superior */
    if (errMsg[0] != '\0' && strlen(errMsg) > 0) {
	mlog("job '%s': %s", data->jobid, errMsg);
    }

    /* do some sanity checks and free the child */
    Child_t *child = findChildByJobid(data->jobid, PSMOM_CHILD_PROLOGUE);
    if (!child) {
	child = findChildByJobid(data->jobid, PSMOM_CHILD_EPILOGUE);
    }
    if (!child) {
	mlog("%s: finding child '%s' failed\n", __func__, data->jobid);
    } else {
	signalFlag = child->signalFlag;
	if (!deleteChild(child->pid)) {
	    mlog("%s: deleting child '%s' failed\n", __func__, data->jobid);
	}
    }

    /* prepare result msg */
    PS_SendDB_t msg;

    if (data->prologue) {
	initFragBuffer(&msg, PSP_PLUG_PSMOM, PSP_PSMOM_PROLOGUE_FINISH);

	/* add to statistic */
	if (data->frontend){
	    if (exitStat) {
		stat_failedlPrologue++;
	    } else {
		stat_lPrologue++;
	    }
	} else {
	    if (exitStat) {
		stat_failedrPrologue++;
	    } else {
		stat_rPrologue++;
	    }
	}

	/* delete temp directory if prologue failed */
	if (exitStat != 0 && data->tmpDir) {
	    removeDir(data->tmpDir, 1);
	}
    } else {
	initFragBuffer(&msg, PSP_PLUG_PSMOM, PSP_PSMOM_EPILOGUE_FINISH);

	/* delete temp directory in epilogue */
	if (data->tmpDir) {
	    removeDir(data->tmpDir, 1);
	}
    }
    setFragDest(&msg, data->mainMom);

    /* add jobid */
    addStringToMsg(data->jobid, &msg);

    /* add start_time */
    addTimeToMsg(data->start_time, &msg);

    /* add result */
    addInt32ToMsg(exitStat, &msg);

    /* add signal flag */
    addInt32ToMsg(signalFlag, &msg);

    /* add error msg */
    if (exitStat) {
	if (!strlen(errMsg)) {
	    mlog("%s: exit without message for '%s'\n", __func__, data->jobid);
	    addStringToMsg("no error msg received", &msg);
	} else {
	    addStringToMsg(errMsg, &msg);
	}
    }

    sendFragMsg(&msg);

    /* pelogue timed out */
    if (exitStat == -4) {
	PElogueTimeoutAction(data->server, data->jobid, data->prologue, NULL);
    }

    ufree(data->dirScripts);
    ufree(data->jobid);
    ufree(data->jobname);
    ufree(data->user);
    ufree(data->group);
    ufree(data->limits);
    ufree(data->queue);
    ufree(data->sessid);
    ufree(data->nameExt);
    ufree(data->resources_used);
    ufree(data->gpus);
    ufree(data->server);
    ufree(data->tmpDir);
    ufree(data);

    return 0;
}

/**
 * @brief Callback handler for the global PElogue timeout.
 *
 * @param timerId The id of my timer that expired.
 *
 * @param data Holds the jobid of the pelogue script.
 *
 * @return No return value.
 */
static void handlePELogueTimeout(int timerId, void *data)
{
    Job_t *job;
    char *jobid = data;
    const char *host;
    char *buf = NULL, tmp[100];
    size_t buflen = 0;
    int i, count = 0;

    /* don't call myself again */
    Timer_remove(timerId);

    if (!(job = findJobById(jobid))) {
	mlog("%s: job '%s' not found\n", __func__, jobid);
	ufree(jobid);
	return;
    }
    if (job->pelogueMonStr) {
	ufree(job->pelogueMonStr);
	job->pelogueMonStr = NULL;
    }

    /* don't break job if it got re-queued */
    if (timerId != job->pelogueMonitorId) {
	mlog("%s: timer of old job, skipping it\n", __func__);
	return;
    }
    job->pelogueMonitorId = -1;

    mlog("%s: global %s timeout for job '%s', stopping job using SIGKILL\n",
	    __func__, job->state == JOB_PROLOGUE ? "prologue" : "epilogue",
	    job->id);

    str2Buf("missing nodeID(s): ", &buf, &buflen);

    for (i=0; i<job->nrOfUniqueNodes; i++) {
	if (job->state == JOB_PROLOGUE) {
	    if (job->nodes[i].prologue == -1) {
		if (count>0) {
		    str2Buf(",", &buf, &buflen);
		}
		snprintf(tmp, sizeof(tmp), "%i", job->nodes[i].id);
		str2Buf(tmp, &buf, &buflen);
		if ((host = getHostnameByNodeId(job->nodes[i].id))) {
		    PElogueTimeoutAction(job->server, job->id, 1, host);
		} else {
		    mlog("%s: get hostname by node ID '%i' failed\n", __func__,
			    job->nodes[i].id);
		}
	    }
	} else {
	    if (job->nodes[i].epilogue == -1) {
		if (count>0) {
		    str2Buf(",", &buf, &buflen);
		}
		snprintf(tmp, sizeof(tmp), "%i", job->nodes[i].id);
		str2Buf(tmp, &buf, &buflen);
		if ((host = getHostnameByNodeId(job->nodes[i].id))) {
		    PElogueTimeoutAction(job->server, job->id, 0, host);
		} else {
		    mlog("%s: get hostname by node ID '%i' failed\n", __func__,
			    job->nodes[i].id);
		}
	    }
	}
	count++;
    }
    mlog("%s: %s\n", __func__, buf);
    ufree(buf);

    signalPElogue(job, "SIGKILL", "global pelogue timeout");
    stopPElogueExecution(job);
}

void removePELogueTimeout(Job_t *job)
{
    if (job->pelogueMonitorId != -1) {
	Timer_remove(job->pelogueMonitorId);
	job->pelogueMonitorId = -1;
    }

    if (job->pelogueMonStr) {
	ufree(job->pelogueMonStr);
	job->pelogueMonStr = NULL;
    }
}

void monitorPELogueTimeout(Job_t *job)
{
    struct timeval pelogueTimer = {1,0};
    int timeout, grace, id;
    char *jobid;

    if (job->state == JOB_PROLOGUE) {
	timeout = getConfValueI(&config, "TIMEOUT_PROLOGUE");
    } else {
	timeout = getConfValueI(&config, "TIMEOUT_EPILOGUE");
    }
    grace = getConfValueI(&config, "TIMEOUT_PE_GRACE");

    pelogueTimer.tv_sec = timeout + (2 * grace);
    jobid = ustrdup(job->id);

    if ((id = Timer_registerEnhanced(&pelogueTimer, handlePELogueTimeout,
		    jobid)) == -1) {
	mlog("%s: register PElogue monitor timer failed\n", __func__);
	ufree(jobid);
    } else {
	job->pelogueMonitorId = id;
	job->pelogueMonStr = jobid;
    }
}

void handlePELogueStart(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *msgData)
{
    char *ptr, ctype[20], buf[300], tmpDir[400] = { '\0' };
    char *dirScripts, *confTmpDir;
    int itype, disPE;
    PElogue_Data_t *data;
    PS_SendDB_t ans;
    bool prologue = msg->type == PSP_PSMOM_PROLOGUE_START ? true : false;

    ptr = msgData->buf;

    /* fetch job hashname */
    getString(&ptr, buf, sizeof(buf));

    /* set temp dir using hashname */
    confTmpDir = getConfValueC(&config, "DIR_TEMP");
    if (confTmpDir) snprintf(tmpDir, sizeof(tmpDir), "%s/%s", confTmpDir, buf);

    /* fetch username */
    getString(&ptr, buf, sizeof(buf));

    if (prologue) {
	struct stat statbuf;

	initFragBuffer(&ans, PSP_PLUG_PSMOM, PSP_PSMOM_PROLOGUE_FINISH);

	snprintf(ctype, sizeof(ctype), "%s", "prologue");
	itype = PSMOM_CHILD_PROLOGUE;

	if (confTmpDir && stat(tmpDir, &statbuf) == -1) {
	    if (mkdir(tmpDir, S_IRWXU == -1)) {
		mdbg(PSMOM_LOG_WARN, "%s: mkdir (%s) failed : %s\n", __func__,
		     tmpDir, strerror(errno));
	    } else {
		struct passwd *spasswd = getpwnam(buf);

		if (!spasswd) {
		    mlog("%s: getpwnam(%s) failed\n", __func__, buf);
		} else {
		    if (chown(tmpDir, spasswd->pw_uid, spasswd->pw_gid) == -1) {
			mlog("%s: chown(%s) failed : %s\n", __func__, tmpDir,
			     strerror(errno));
		    }
		}
	    }
	}
    } else {
	initFragBuffer(&ans, PSP_PLUG_PSMOM, PSP_PSMOM_EPILOGUE_FINISH);

	snprintf(ctype, sizeof(ctype), "%s", "epilogue");
	itype = PSMOM_CHILD_EPILOGUE;

	/* delete temp directory in epilogue */
	if (confTmpDir) {
	    removeDir(tmpDir, 1);
	}
    }
    setFragDest(&ans, msg->header.sender);

    disPE = getConfValueI(&config, "DISABLE_PELOGUE");

    if (disPE == 1) {
	/* no PElogue scripts to run */
	time_t jobStart;

	/* get jobid from received msg */
	char *jobid = getStringM(&ptr);

	/* get start_time */
	getTime(&ptr, &jobStart);

	if (prologue) psPamAddUser(buf, jobid, PSPAM_STATE_PROLOGUE);

	/* add jobid */
	addStringToMsg(jobid, &ans);

	/* add start_time */
	addTimeToMsg(jobStart, &ans);

	/* add result */
	addInt32ToMsg(0, &ans);

	sendFragMsg(&ans);
	ufree(jobid);

	return;
    }

    /* collect all data and start the script */
    dirScripts = getConfValueC(&config, "DIR_SCRIPTS");
    data = umalloc(sizeof(PElogue_Data_t));

    /* build up data struct */
    if (PSC_getMyTID() == msg->header.sender) {
	data->frontend = 1;
    } else {
	data->frontend = 0;
    }

    /* prologue/epilogue flag */
    data->prologue = prologue;

    /* senders task id */
    data->mainMom = msg->header.sender;

    /* the scripts directory */
    data->dirScripts = ustrdup(dirScripts);

    /* set pelogue data structure */
    data->jobid = getStringM(&ptr);
    getTime(&ptr, &data->start_time);
    data->jobname = getStringM(&ptr);
    data->user = getStringM(&ptr);
    data->group = getStringM(&ptr);
    data->limits = getStringM(&ptr);
    data->queue = getStringM(&ptr);
    getInt32(&ptr, &data->timeout);
    data->sessid = getStringM(&ptr);
    data->nameExt = getStringM(&ptr);
    data->resources_used = getStringM(&ptr);
    getInt32(&ptr, &data->exit);
    data->gpus = getStringM(&ptr);
    data->server = getStringM(&ptr);
    data->tmpDir = (confTmpDir != NULL) ? ustrdup(tmpDir) : NULL;

    if (prologue) psPamAddUser(data->user, data->jobid, PSPAM_STATE_PROLOGUE);

    if (!data->frontend) {
	PSnodes_ID_t id = PSC_getID(msg->header.sender);
	mdbg(PSMOM_LOG_JOB, "remote %s for job '%s' ms '%s(%i)' is starting\n",
		ctype, data->jobid, getHostnameByNodeId(id), id);
    } else {
	mdbg(PSMOM_LOG_JOB, "local %s for job '%s' is starting\n",
		ctype, data->jobid);
    }

    /* spawn child to prevent the pelogue script from blocking
     * the psmom/psid */
    pid_t pid = PSID_execFunc(execPElogueForwarder, prepScriptEnv,
			      callbackPElogue, data);
    if (pid == -1) {
	mlog("%s: exec '%s'-script failed\n", __func__, ctype);

	/* add jobid */
	addStringToMsg(data->jobid, &ans);

	/* add start_time */
	addTimeToMsg(data->start_time, &ans);

	/* add result */
	addInt32ToMsg(-2, &ans);

	sendFragMsg(&ans);

	if (data->tmpDir) ufree(data->tmpDir);
	ufree(data->dirScripts);
	ufree(data->jobid);
	ufree(data->jobname);
	ufree(data->user);
	ufree(data->group);
	ufree(data->limits);
	ufree(data->queue);
	ufree(data->sessid);
	ufree(data->nameExt);
	ufree(data->resources_used);
	ufree(data->gpus);
	ufree(data->server);
	ufree(data);
	handleFailedSpawn();
	return;
    }

    addChild(pid, itype, data->jobid);
    mdbg(PSMOM_LOG_PROCESS, "%s: %s [%i] for job %s started\n", __func__,
	 ctype, pid, data->jobid);
}

int handleNodeDown(void *nodeID)
{
    PSnodes_ID_t node = *(PSnodes_ID_t *) nodeID;

    /* check if the node which has gone down is a part of a local job */
    cleanJobByNode(node);

    /* check if the node which has gone down is a mother superior
     * from a remote job */
    cleanJobInfoByNode(node);

    return 1;
}
