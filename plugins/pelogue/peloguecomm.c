/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include "peloguelog.h"
#include "pelogueconfig.h"
#include "peloguechild.h"
#include "pelogueforwarder.h"
#include "peloguescript.h"
#include "peloguejob.h"
#include "pelogueforwarder.h"
#include "peloguekvs.h"

#include "pspluginprotocol.h"
#include "pscommon.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidscripts.h"
#include "psaccounthandles.h"
#include "pluginfrag.h"
#include "pluginmalloc.h"
#include "pluginhelper.h"
#include "pluginforwarder.h"

#include "peloguecomm.h"

static void sendFragMsgToHostList(Job_t *job, PS_DataBuffer_t *data,
				    int32_t type, int myself)
{
    PStask_ID_t myTID = PSC_getMyTID(), dest;
    int i;

    for (i=0; i<job->nrOfNodes; i++) {

	if ((dest = PSC_getTID(job->nodes[i].id, 0)) == -1) {
	    mlog("%s: skipping invalid node id '%u'\n", __func__,
		    job->nodes[i].id);
	    continue;
	}

	/* skip sending to myself if requested */
	if (!myself && myTID == dest) continue;

	mdbg(PELOGUE_LOG_PSIDCOM, "%s: send to %i [%i->%i]\n", __func__,
		job->nodes[i].id, myTID, dest);
	sendFragMsg(data, dest, PSP_CC_PLUG_PELOGUE, type);
    }
}

int sendPElogueStart(Job_t *job, bool prologue, env_t *env)
{
    PS_DataBuffer_t data = { .buf = NULL};
    int32_t timeout, type;
    uint32_t i;

    if (prologue) {
	timeout = getConfParamI(job->plugin, "TIMEOUT_PROLOGUE");
	type = PSP_PROLOGUE_START;
	job->state = JOB_PROLOGUE;
    } else {
	timeout = getConfParamI(job->plugin, "TIMEOUT_EPILOGUE");
	type = PSP_EPILOGUE_START;
	job->state = JOB_EPILOGUE;
    }

    /* add PElogue data structure */

    /* add plugin */
    addStringToMsg(job->plugin, &data);

    /* add job id */
    addStringToMsg(job->id, &data);

    /* add user id */
    addInt32ToMsg(job->uid, &data);

    /* add group id */
    addInt32ToMsg(job->gid, &data);

    /* add timeout */
    addInt32ToMsg(timeout, &data);

    /* add scriptname */
    addStringToMsg(job->scriptname, &data);

    /* add start time */
    job->start_time = time(NULL);
    addTimeToMsg(job->start_time, &data);

    /* add environment */
    addInt32ToMsg(env->cnt, &data);

    for (i=0; i<env->cnt; i++) {
	addStringToMsg(env->vars[i], &data);
    }

    /* start global timeout monitoring */
    monitorPELogueTimeout(job);

    /* send the message to all hosts in the job */
    sendFragMsgToHostList(job, &data, type, 1);
    ufree(data.buf);

    return 1;
}

static void manageTempDir(const char *plugin, const char *jobid, int create,
		    uid_t uid, gid_t gid)
{
    char *confTmpDir, tmpDir[400] = {'\0'};
    struct stat statbuf;

    /* set temp dir using hashname */
    if ((confTmpDir = getConfParamC(plugin, "DIR_TEMP"))) {
	snprintf(tmpDir, sizeof(tmpDir), "%s/%s", confTmpDir, jobid);
    }

    if (create) {
	if (confTmpDir && (stat(tmpDir, &statbuf) == -1)) {
	    if ((mkdir(tmpDir, S_IRWXU) == -1)) {
		mdbg(PELOGUE_LOG_WARN, "%s: mkdir (%s) failed : %s\n", __func__,
			tmpDir, strerror(errno));
	    } else {
		if ((chown(tmpDir, uid, gid)) == -1) {
		    mlog("%s: chown(%s) failed : %s\n", __func__, tmpDir,
			    strerror(errno));
		}
	    }
	}
    } else {
	/* delete temp directory in epilogue */
	if (confTmpDir) removeDir(tmpDir, 1);
    }
}

static void destroyPElogueData(PElogue_Data_t *pedata)
{
    envDestroy(&pedata->env);
    ufree(pedata->jobid);
    ufree(pedata->plugin);
    ufree(pedata->scriptname);
    ufree(pedata->dirScripts);
    ufree(pedata);
}

int fwCallback(int32_t wstat, Forwarder_Data_t *fwdata)
{
    DDTypedBufferMsg_t msgRes;
    PElogue_Data_t *pedata = fwdata->userData;
    Child_t *child = pedata->child;
    char errBuf[50];
    int ret, exit_status, signalFlag;

    signalFlag = child->signalFlag;

    if (wstat == -4) {
	exit_status = wstat;
	/* timeout */
    } else if ((WIFEXITED(wstat))) {
	exit_status = WEXITSTATUS(wstat);
    } else if ((WIFSIGNALED(wstat))) {
	exit_status = WTERMSIG(wstat) + 0x100;
    } else {
	exit_status = 1;
    }

    /* let other plugins get information about completed pelogue */
    pedata->exit = exit_status;
    PSIDhook_call(PSIDHOOK_PELOGUE_FINISH, pedata);

    /* pelogue timed out, let local plugin decide to take some action, e.g. set
     * my node offline*/
    if (exit_status == -4) {

    }

    if (!(deleteChild(pedata->plugin, pedata->jobid))) {
	mlog("%s: deleting child '%s' failed\n", __func__, fwdata->jobID);
    }

    /* send result to mother superior */
    msgRes = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PLUG_PELOGUE,
       .sender = PSC_getMyTID(),
       .dest = pedata->mainPelogue,
       .len = sizeof(msgRes.header) },
       .buf = {'\0'} };

    if (pedata->prologue) {
	msgRes.type = PSP_PROLOGUE_FINISH;

	/* add to statistic */
	if (pedata->frontend){
	    if (exit_status != 0) {
		stat_failedlProlog++;
	    } else {
		stat_lProlog++;
	    }
	} else {
	    if (exit_status != 0) {
		stat_failedrProlog++;
	    } else {
		stat_rProlog++;
	    }
	}

	/* delete temp directory if prologue failed */
	if (exit_status != 0) {
	    manageTempDir(pedata->plugin, pedata->jobid, 0, pedata->uid,
			    pedata->gid);
	}
    } else {
	msgRes.type = PSP_EPILOGUE_FINISH;

	/* delete temp directory in epilogue */
	manageTempDir(pedata->plugin, pedata->jobid, 0, pedata->uid,
			pedata->gid);
    }
    msgRes.header.len += sizeof(msgRes.type);

    /* add plugin */
    addStringToMsgBuf(&msgRes, pedata->plugin);

    /* add jobid */
    addStringToMsgBuf(&msgRes, pedata->jobid);

    /* add start_time */
    addTimeToMsgBuf(&msgRes, pedata->start_time);

    /* add result */
    addInt32ToMsgBuf(&msgRes, exit_status);

    /* add signal flag */
    addInt32ToMsgBuf(&msgRes, signalFlag);

    /* add error msg */
    if (exit_status != 0) {
	snprintf(errBuf, sizeof(errBuf), "exit [%i]", exit_status);
	addStringToMsgBuf(&msgRes, errBuf);
    } else {
	addStringToMsgBuf(&msgRes, "");
    }

    ret = sendMsg(&msgRes);
    mlog("%s: local %s exit '%i' job '%s' to '%s' ret '%i'\n", __func__,
	    pedata->prologue ? "prologue" : "epilogue",
	    exit_status, pedata->jobid, PSC_printTID(pedata->mainPelogue), ret);

    if (ret == -1 && errno != EWOULDBLOCK) {
	mwarn(errno, "%s: sendMsg() failed: ", __func__);
    }

    /* cleanup */
    destroyPElogueData(pedata);

    return 0;
}

static void handlePELogueStart(DDTypedBufferMsg_t *msg,
				PS_DataBuffer_t *recvData)
{
    char *ptr = recvData->buf, ctype[20], fname[100], *tmp, *dirScripts;
    int32_t envSize;
    int i, disPE, timeout;
    PELOGUE_child_types_t itype;
    PElogue_Data_t *data;
    DDTypedBufferMsg_t msgRes;
    time_t job_start;
    int prologue = msg->type == PSP_PROLOGUE_START ? 1 : 0;
    Forwarder_Data_t *fwdata;

    /* prepare result msg */
    msgRes = (DDTypedBufferMsg_t) {
       .header = (DDMsg_t) {
       .type = PSP_CC_PLUG_PELOGUE,
       .sender = PSC_getMyTID(),
       .dest = msg->header.sender,
       .len = sizeof(msgRes.header) },
       .buf = {'\0'} };

    data = umalloc(sizeof(PElogue_Data_t));
    data->exit = 0;
    data->prologue = prologue;

    /* get plugin */
    data->plugin = getStringM(&ptr);

    /* get jobid */
    data->jobid = getStringM(&ptr);

    /* get uid */
    getInt32(&ptr, (int32_t *)&data->uid);

    /* get gid */
    getInt32(&ptr, (int32_t *)&data->gid);

    /* get timeout */
    getInt32(&ptr, &timeout);

    /* get scriptname */
    data->scriptname = getStringM(&ptr);

    /* get job start time */
    getTime(&ptr, &data->start_time);

    /* create/destroy temp dir */
    manageTempDir(data->plugin, data->jobid, prologue, data->uid, data->gid);

    if (PSC_getMyTID() == msg->header.sender) {
	data->frontend = 1;
    } else {
	data->frontend = 0;
    }

    if (prologue) {
	msgRes.type = PSP_PROLOGUE_FINISH;
	itype = PELOGUE_CHILD_PROLOGUE;
	snprintf(ctype, sizeof(ctype), "%s %s",
		    data->frontend ? "local" : "remote", "prologue");
    } else {
	msgRes.type = PSP_EPILOGUE_FINISH;
	itype = PELOGUE_CHILD_EPILOGUE;
	snprintf(ctype, sizeof(ctype), "%s %s",
		    data->frontend ? "local" : "remote", "epilogue");
    }
    msgRes.header.len += sizeof(msgRes.type);

    disPE = getConfParamI(data->plugin, "DISABLE_PELOGUE");

    if (disPE == 1) {
	int32_t exitVal = 0;
	mlog("%s: fixmeeee!!!\n", __func__);
	exit(1);

	/* no PElogue scripts to run */

	/* get start_time */
	getTime(&ptr, &job_start);

	/* add jobid */
	addStringToMsgBuf(&msgRes, data->jobid);

	/* add start_time */
	addTimeToMsgBuf(&msgRes, job_start);

	/* add result */
	addInt32ToMsgBuf(&msgRes, exitVal);

	if ((sendMsg(&msgRes)) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg() to '%s' failed ", __func__,
		    PSC_printTID(msgRes.header.sender));
	}

	return;
    }

    /* collect all data and start the script */
    dirScripts = getConfParamC(data->plugin, "DIR_SCRIPTS");

    /* get environment */
    envInit(&data->env);
    getInt32(&ptr, &envSize);
    for (i=0; i<envSize; i++) {
	tmp = getStringM(&ptr);
	envPut(&data->env, tmp);
	ufree(tmp);
    }

    /* prologue/epilogue flag */
    data->prologue = prologue;

    /* senders task id */
    data->mainPelogue = msg->header.sender;

    /* the scripts directory */
    data->dirScripts = ustrdup(dirScripts);


    if (!data->frontend) {
	/* TODO :::: LOG IN PSMOM/PSSLURM
	 *
	PSnodes_ID_t id = PSC_getID(msg->header.sender);
	mdbg(PSMOM_LOG_JOB, "remote %s for job '%s' ms '%s(%i)' is starting\n",
		ctype, data->jobid, getHostnameByNodeId(id), id);
	*/
    } else {
	/* TODO :::: LOG IN PSMOM/PSSLURM
	mdbg(PSMOM_LOG_JOB, "local %s for job '%s' is starting\n",
		ctype, data->jobid);
	*/
    }

    fwdata = ForwarderData_new();
    snprintf(fname, sizeof(fname), "%sforwarder", data->plugin);
    fwdata->pTitle = ustrdup(fname);
    fwdata->jobID = ustrdup(data->jobid);
    fwdata->userData = data;
    fwdata->graceTime = 3;
    fwdata->killSession = psAccountSignalSession;
    fwdata->callback = fwCallback;
    fwdata->childRerun = 1;
    fwdata->childFunc = execPElogueScript;
    fwdata->timeoutChild = timeout;

    if (!startForwarder(fwdata)) {
	int32_t exitVal = -2;

	mlog("%s: exec '%s'-script failed\n", __func__, ctype);

	addStringToMsgBuf(&msgRes, data->plugin);
	addStringToMsgBuf(&msgRes, data->jobid);

	/* add start_time */
	addTimeToMsgBuf(&msgRes, data->start_time);

	/* add result */
	addInt32ToMsgBuf(&msgRes, exitVal);

	if ((sendMsg(&msgRes)) == -1 && errno != EWOULDBLOCK) {
	    mwarn(errno, "%s: sendMsg() to '%s' failed ", __func__,
		    PSC_printTID(msgRes.header.sender));
	}

	destroyPElogueData(data);

	/* TODO FORWARD ERROR BACK TO PSMOM/PSSLURM AND LET IT HANDLE IT */
	//handleFailedSpawn();
	return;
    }

    data->child = addChild(data->plugin, data->jobid, fwdata, itype);
    mdbg(PELOGUE_LOG_PROCESS, "%s: %s for job '%s:%s' started\n",
	    __func__, ctype, data->plugin, data->jobid);
}

static void handlePELogueFinish(DDTypedBufferMsg_t *msg, char *msgData)
{
    PSnodes_ID_t nodeId = PSC_getID(msg->header.sender);
    char *ptr, plugin[100], buf[300], peType[100];
    Job_t *job;
    int32_t res = 1, signalFlag = 0;
    time_t job_start;
    PElogue_Res_List_t *nodeEntry;
    int prologue = msg->type == PSP_PROLOGUE_FINISH ? 1 : 0;

    ptr = msg->buf;
    snprintf(peType, sizeof(peType), "%s %s",
		nodeId == PSC_getMyID() ? "local" : "remote",
		prologue ? "prologue" : "epilogue");

    /* get plugin */
    getString(&ptr, plugin, sizeof(plugin));

    /* get jobid */
    getString(&ptr, buf, sizeof(buf));

    if (!(job = findJobByJobId(plugin, buf))) {
	if (!(isJobIDinHistory(buf))) {
	    mdbg(PELOGUE_LOG_WARN, "%s: '%s' finish message for unknown"
		    " job '%s', ignoring it\n", __func__, peType, buf);
	}
	return;
    }

    /* get job start_time */
    getTime(&ptr, &job_start);

    if (job->start_time != job_start) {
	/* msg is for previous job, ignore */
	mdbg(PELOGUE_LOG_WARN, "%s: received '%s' finish from previous"
	    " job '%s', ignoring it\n", __func__, peType, job->id);
	return;
    }

    /* get result */
    getInt32(&ptr, &res);

    if ((nodeEntry = findJobNodeEntry(job, nodeId))) {
	if (prologue) {
	    nodeEntry->prologue = res;
	} else {
	    nodeEntry->epilogue = res;
	}
    }

    /* get signal flag */
    getInt32(&ptr, &signalFlag);

    /* on error get errmsg */
    if (res != 0) {

	getString(&ptr, buf, sizeof(buf));

	/* suppress error message if we have killed the pelogue by request */
	if (!signalFlag) {
	    mlog("%s: '%s' for job '%s' node '%s(%i)' failed: %s\n", __func__,
		    peType, job->id, getHostnameByNodeId(nodeId), nodeId, buf);
	} else {
	    mdbg(PELOGUE_LOG_WARN, "%s: '%s' for job '%s' node '%s(%i)' "
		    "failed: %s\n", __func__, peType, job->id,
		    getHostnameByNodeId(nodeId), nodeId, buf);
	}
    }

    PElogueExit(job, res, prologue);
}

static void handlePELogueSignal(DDTypedBufferMsg_t *msg)
{
    char *ptr, plugin[JOB_NAME_LEN], jobid[JOB_NAME_LEN], reason[100];
    int32_t finish, signal;
    Child_t *child;
    Forwarder_Data_t *fwdata;

    ptr = msg->buf;

    /* get plugin */
    getString(&ptr, plugin, sizeof(plugin));

    /* get jobid */
    getString(&ptr, jobid, sizeof(jobid));

    /* get signal */
    getInt32(&ptr, &signal);

    /* get the finish/slient flag */
    getInt32(&ptr, &finish);

    /* find job */
    if (!(child = findChild(plugin, jobid))) {
	if (finish == -1) {
	    mdbg(PELOGUE_LOG_WARN, "%s: child for job '%s' not found\n",
		__func__, jobid);
	}
	return;
    }
    fwdata = child->fwdata;

    /* get the reason for sending the signal */
    getString(&ptr, reason, sizeof(reason));

    /* save the signal we are about to send */
    if (signal == SIGTERM || signal == SIGKILL) {
	child->signalFlag = signal;
    }

    /* send the signal */
    if (fwdata->cSid > 0) {
	mlog("signal '%i' to pelogue '%s' - reason '%s' - sid '%i'\n",
		signal, jobid, reason, fwdata->cSid);
	psAccountSignalSession(fwdata->cSid, signal);
    } else if (fwdata->cPid > 0) {
	mlog("signal '%i' to pelogue '%s' - reason '%s' - pid '%i'\n",
		signal, jobid, reason, fwdata->cPid);
	kill(fwdata->cPid, signal);
    } else if ((signal == SIGTERM || signal == SIGKILL) &&
		fwdata->tid != -1) {
	kill(PSC_getPID(fwdata->tid), SIGTERM);
    } else {
	mlog("%s: not sending signal '%i' to job '%s' : "
		"invalid forwarder data\n", __func__, signal, jobid);
    }
}

void handlePelogueMsg(DDTypedBufferMsg_t *msg)
{
    char cover[128];

    snprintf(cover, sizeof(cover), "[%s->", PSC_printTID(msg->header.sender));
    snprintf(cover+strlen(cover), sizeof(cover)-strlen(cover), "%s]",
	     PSC_printTID(msg->header.dest));

    mdbg(PELOGUE_LOG_COMM, "%s: type: %i %s\n", __func__, msg->type, cover);

    switch (msg->type) {
	case PSP_PROLOGUE_START:
	    recvFragMsg(msg, handlePELogueStart);
	    break;
	case PSP_PROLOGUE_FINISH:
	    handlePELogueFinish(msg, NULL);
	    break;
	case PSP_EPILOGUE_START:
	    recvFragMsg(msg, handlePELogueStart);
	    break;
	case PSP_EPILOGUE_FINISH:
	    handlePELogueFinish(msg, NULL);
	    break;
	case PSP_PELOGUE_SIGNAL:
	    handlePELogueSignal(msg);
	    break;
	default:
	    mlog("%s: onknown type %i %s\n", __func__, msg->type, cover);
    }
}

static char *msg2Str(PSP_PELOGUE_t type)
{
    switch(type) {
	case PSP_PROLOGUE_START:
	    return "PROLOGUE_START";
	case PSP_PROLOGUE_FINISH:
	    return "PROLOGUE_FINISH";
	case PSP_EPILOGUE_START:
	    return "EPILOGUE_START";
	case PSP_EPILOGUE_FINISH:
	    return "EPILOGUE_FINISH";
	case PSP_PELOGUE_SIGNAL:
	    return "PELOGUE_SIGNAL";
    }
    return NULL;
}

static bool nodeDownVisitor(Job_t *job, const void *info)
{
    PSnodes_ID_t id = *(PSnodes_ID_t *)info;
    int i;

    if (job->state != JOB_PROLOGUE && job->state != JOB_EPILOGUE) return false;
    if (!findChild(job->plugin, job->id)) return false;

    for (i=0; i<job->nrOfNodes; i++) {
	if (job->nodes[i].id == id) {
	    const char *hname = getHostnameByNodeId(id);

	    mlog("%s: node %s(%i) running job '%s' jstate '%s' is down\n",
		 __func__, hname, id, job->id, jobState2String(job->state));

	    if (job->state == JOB_PROLOGUE) {
		job->nodes[i].prologue = 2;
		job->state = JOB_CANCEL_PROLOGUE;
	    } else {
		job->nodes[i].epilogue = 2;
		job->state = JOB_CANCEL_EPILOGUE;
	    }

	    /* stop pelogue scripts on all nodes */
	    signalPElogue(job, SIGTERM, "node down");
	    stopPElogueExecution(job);
	    break;
	}
    }
    return false;
}

int handleNodeDown(void *nodeID)
{
    traverseJobs(nodeDownVisitor, nodeID);
    return 1;
}

static void handleDroppedStartMsg(DDTypedBufferMsg_t *msg)
{
    PS_Frag_Msg_Header_t *rhead;
    char *ptr = msg->buf;
    char plugin[300], jobid[300];
    Job_t *job;

    /* fragmented message header */
    rhead = (PS_Frag_Msg_Header_t *) ptr;
    ptr += sizeof(PS_Frag_Msg_Header_t);

    /* ignore follow up messages */
    if (rhead->fragNum) return;

    /* get plugin */
    getString(&ptr, plugin, sizeof(plugin));

    /* get jobid */
    getString(&ptr, jobid, sizeof(jobid));

    if (!(job = findJobByJobId(plugin, jobid))) {
	mlog("%s: plugin '%s' job '%s' not found\n", __func__,
		plugin, jobid);
	return;
    }

    job->state = PSP_PROLOGUE_START ? JOB_CANCEL_PROLOGUE : JOB_CANCEL_EPILOGUE;
    stopPElogueExecution(job);
}

void handleDroppedMsg(DDTypedBufferMsg_t *msg)
{
    const char *hname;
    PSnodes_ID_t nodeId;

    /* get hostname for message destination */
    nodeId = PSC_getID(msg->header.dest);
    hname = getHostnameByNodeId(nodeId);

    mlog("%s: msg type '%s (%i)' to host '%s(%i)' got dropped\n", __func__,
	    msg2Str(msg->type), msg->type, hname, nodeId);

    switch (msg->type) {
	case PSP_PROLOGUE_START:
	case PSP_EPILOGUE_START:
	    handleDroppedStartMsg(msg);
	    break;
	case PSP_PROLOGUE_FINISH:
	case PSP_EPILOGUE_FINISH:
	case PSP_PELOGUE_SIGNAL:
	    /* nothing we can do here */
	    break;
	default:
	    mlog("%s: unknown msg type '%i'\n", __func__, msg->type);
    }
}
