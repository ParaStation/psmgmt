/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#include <pty.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "timer.h"

#include "psaccounthandles.h"
#include "pspamhandles.h"

#include "pbsdef.h"
#include "psmomcomm.h"
#include "psmomconv.h"
#include "psmomjob.h"
#include "psmomspawn.h"
#include "psmomcollect.h"
#include "psmomacc.h"
#include "psmomlog.h"
#include "psmomsignal.h"
#include "psmomauth.h"
#include "psmom.h"
#include "pluginmalloc.h"
#include "psmomconfig.h"
#include "psmomscript.h"
#include "psmomchild.h"
#include "psmomlocalcomm.h"
#include "psmompbsserver.h"
#include "psmomrecover.h"

#include "psi.h"
#include "pscommon.h"

#include "psmomproto.h"

#define ENABLE_TM_REQUEST false

static char buf[2048];


/**
 * @brief Read all pending data into buffer.
 *
 * @param com The communication handle to use.
 *
 * @return No return value.
 */
static void readAllData(ComHandle_t *com)
{
    unsigned int i;

    /* clear buffer */
    for (i=0; i<sizeof(buf); i++) {
	buf[i] = '\0';
    }

    wReadAll(com, buf, sizeof(buf));
}

/**
 * @brief Send an IS status request to the server.
 *
 * @param com The communication handle to use.
 *
 * @return No return value.
 */
int updateServerState(ComHandle_t *com)
{
    WriteIS(com, IS_STATUS);
    return wDoSend(com);
}

/**
 * @brief Handle a null ping from the server
 *
 * Nothing to do here.
 *
 * @return Returns always 0.
 */
static int handle_IS_NULL(void)
{
    mdbg(PSMOM_LOG_PIS, "%s:\n", __func__);
    return 0;
}

/**
 * @brief Handle a IS hello request.
 *
 * Basicly this is a pingpong between us and the pbs_server.
 *
 * @param com The communication handle to use.
 *
 * @return Returns always 0.
 */
static int handle_IS_HELLO(ComHandle_t *com)
{
    /* return the hello to the server */
    mdbg(PSMOM_LOG_PIS, "%s:\n", __func__);

    WriteIS(com, IS_HELLO);
    return wDoSend(com);
}

/**
 * @brief Receive authencicated server addresses.
 *
 * We only accept request(s) from those ip addresses, or addresses we
 * got from the config-file.
 *
 * @param com The communication handle to use.
 *
 * @return Returns 0.
 */
static int handle_IS_CLUSTER_ADDRS(ComHandle_t *com)
{
    Server_t *serv;
    unsigned long addr;

    if (!(serv = findServer(com))) {
	mlog("%s: server for com handle '%i' not found\n", __func__,
	    com->socket);
	return 0;
    }

    while(ReadDigitUL(com, &addr) == 0) {
	mdbg(PSMOM_LOG_PIS, "%s: received adress:%ld.%ld.%ld.%ld %lu\n",
	    __func__,
	    (addr & 0xff000000) >> 24,
	    (addr & 0x00ff0000) >> 16,
	    (addr & 0x0000ff00) >> 8,
	    (addr & 0x000000ff), addr);
    }

    /* insert into authorized server list */
    addAuthIP(addr);

    if (serv->haveConnection != 1) {
	serv->haveConnection = 1;

	mdbg(PSMOM_LOG_VERBOSE, "%s: connection to server '%s' established\n",
	    __func__, serv->addr);

	/* reset recv queue */
	wEOM(com);

	/* update server information */
	updateServerState(com);
    }
    serv->lastContact = time(NULL);

    mdbg(PSMOM_LOG_VERBOSE, "%s: got IS update from '%s'\n", __func__,
	com->remoteAddr);

    return 0;
}

/**
 * @brief Handle an Inter Server (IS) request.
 *
 * @param com The communication handle to use.
 *
 * @return Returns 0 on success and -1 on error.
 */
static int handle_IS_message(ComHandle_t *com)
{
    Server_t *serv;
    unsigned int version = 0;
    unsigned int cmd = 0;
    int ret = -1;

    if ((serv = findServer(com))) {
	serv->lastContact = time(NULL);
    }

    if ((ret = ReadDigitUI(com, &version)) < 0
	 || version != IS_PROTOCOL_VER) {
	 mlog("%s: invalid protocol version:%i\n", __func__, version);
	 return ret;
    }

    if ((ret = ReadDigitUI(com, &cmd)) < 0){
	 mlog("%s: invalid command:%i\n", __func__, cmd);
	 return ret;
    }

    mdbg(PSMOM_LOG_PIS, "%s: got cmd:%i ver:%i\n", __func__, cmd, version);
    switch (cmd) {
	case IS_NULL:
	    ret = handle_IS_NULL();
	    break;
	case IS_HELLO:
	    ret = handle_IS_HELLO(com);
	    break;
	case IS_CLUSTER_ADDRS:
	    ret = handle_IS_CLUSTER_ADDRS(com);
	    break;
	case IS_UPDATE:
	    mlog("%s: received unexpected IS_UPDATE from '%s'."
		 " I am not a pbs_mom!\n", __func__, com->remoteAddr);
	    return -1;
	case IS_STATUS:
	    mlog("%s: received unexpected IS_STATUS from '%s'."
		" I am not a pbs_mom!\n", __func__, com->remoteAddr);
	    return -1;
	default:
	    mlog("%s: invalid cmd received:%i\n", __func__, cmd);
	    return -1;
    }
    return ret;
}

/**
 * @brief Handle RM close request.
 *
 * Just close the corresponding connection.
 *
 * @param com The communication handle to use.
 *
 * @return Always returns 0.
 */
static int handle_RM_CLOSE(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_PRM, "%s: closing connection\n", __func__);
    wClose(com);
    return 0;
}

/**
 * @brief Handle a RM request message.
 *
 * momctl cycle test -> cycle
 * momctl Clear stale job: job oder all -> clearjob=all
 * momctl diagnose test: -> diag0, diag1, diag2
 *
 * In torque: max_load: The value of this keyword plays a role in scheduling.
 * When the system load average goes above this value the MOM will
 * refuse any new jobs.
 * In psmom this value will be ignored.
 *
 * In torque: ideal_load: The value of this keyword also plays a role in
 * scheduling. After the load average has crossed $max_load, the MOM will
 * only take on new jobs after the load average has gone below this value.
 * In psmom this value will be ignored.
 *
 * @param com The communication handle to use.
 *
 */
static int handle_RM_REQUEST(ComHandle_t *com)
{
    int ret;
    char request[REQUEST_NAME_LEN];
    char *value;

    if ((WriteDigit(com, RM_RSP_OK)) == -1) {
	wEOM(com);
	wClose(com);
	return 0;
    }
    /* update all values in the list */
    updateInfoList(1);

    while ((ReadString(com, request, sizeof(request))) > 0) {
	/* search in both lists */
	if ((value = getValue(&staticInfoData.list, request, "")) == NULL) {
	    value = getValue(&infoData.list, request, "");
	}

	/* send option value */
	if (value) {
	    snprintf(buf, sizeof(buf), "%s", value);
	} else {
	    /* check for momctl cmds */
	    if (!strcmp(request, "diag0")) {
		snprintf(buf, sizeof(buf), "das ist ein test\nund es geht "
		"weiter..\n");
	    } else {
		mdbg(PSMOM_LOG_VERBOSE , "%s: unknown value '%s' requested\n",
			__func__, request);
		snprintf(buf, sizeof(buf), "%s=? %i", request, PBSE_RMUNKNOWN);
	    }
	}

	mdbg(PSMOM_LOG_PRM, "%s: got request '%s' -> %s\n", __func__, request,
	    buf);
	WriteString(com, buf);

    }
    /* free all info data */
    clearDataList(&infoData.list);

    ret = wDoSend(com);
    wEOM(com);
    return ret;
}

/**
 * @brief Handle a RM remote configure request.
 *
 * We don`t support to be configured by "momctl -r FILE".

 * @param com The communication handle to use.
 *
 * @return Always returns 0;
 */
static int handle_RM_CONFIG(ComHandle_t *com)
{
    readAllData(com);
    mdbg(PSMOM_LOG_PRM, "%s: not supported by psmom\n", __func__);
    mlog("%s: not supported, got '%s'\n", __func__, buf);

    /* ignore it and just close the connection */
    wClose(com);
    return 0;
}

/**
 * @brief Handle a shutdown request.
 *
 * The server/"momctl -s" told me to shut down. But we will just close
 * the connection and ignore the request.
 *
 * @param com The communication handle to use.
 *
 * @return Always return 0;
 */
static int handle_RM_SHUTDOWN(ComHandle_t *com)
{
    readAllData(com);
    mdbg(PSMOM_LOG_PRM, "%s: ignoring shutdown req\n", __func__);
    mlog("%s: not supported, got '%s'\n", __func__, buf);

    wClose(com);
    return 0;
}

/**
 * @brief Handle a Resource Manager (RM) request.
 *
 * @param com The communication handle to use.
 *
 * @returns Returns -1 on error, or 0 on success.
 */
static int handle_RM_message(ComHandle_t *com)
{
    unsigned int version = 0;
    unsigned int cmd = 0;
    int ret = -1;

    if ((ReadDigitUI(com, &version)) < 0
	 || version != RM_PROTOCOL_VER) {
	 mlog("%s: invalid protocol version:%i\n", __func__, version);
	 return -1;
    }

    if ((ReadDigitUI(com, &cmd)) < 0) {
	 mlog("%s: invalid command:%i\n", __func__, cmd);
	 return -1;
    }

    /* Resource Monitor request */
    mdbg(PSMOM_LOG_PRM, "%s: got cmd:%i ver:%i\n", __func__, cmd, version);

    switch (cmd) {
	case RM_CMD_CLOSE:
	    ret = handle_RM_CLOSE(com);
	    break;
	case RM_CMD_REQUEST:
	    ret = handle_RM_REQUEST(com);
	    break;
	case RM_CMD_CONFIG:
	    ret = handle_RM_CONFIG(com);
	    break;
	case RM_CMD_SHUTDOWN:
	    ret = handle_RM_SHUTDOWN(com);
	    break;
	default:
	    mlog("%s: unknown cmd:%i\n", __func__, cmd);
	    return -1;
    }
    return ret;
}

void setPBSNodeOffline(char *server, const char *host, char *note)
{
    Data_Entry_t *status;

    if (!note) {
	setPBSNodeState(server, NULL, "offline", host);
	return;
    }

    /* get my node state */
    if (!(status = getPBSNodeState(server, host))) {
	mlog("%s: getting my node status failed\n", __func__);
	setPBSNodeState(server, NULL, "offline", host);
    } else {
	if ((getValue(&status->list, "note", NULL))) {
	    /* node already has a note set, we will not overwrite it */
	    setPBSNodeState(server, NULL, "offline", host);
	} else {
	    setPBSNodeState(server, note, "offline", host);
	}
	clearDataList(&status->list);
	ufree(status);
    }
}

Data_Entry_t *getPBSNodeState(char *server, const char *host)
{
    ComHandle_t *com;
    int serverPort, cmd, tmp, len;
    Server_t *serv;
    char hname[HOST_NAME_LEN];
    const char *hostname;
    Data_Entry_t *data;

    if (!host) {
	if ((gethostname(hname, sizeof(hname))) == -1) {
	    mlog("%s: can't get my hostname\n", __func__);
	    return NULL;
	}
	hostname = hname;
    } else {
	hostname = host;
    }

    serverPort = getConfValueI(&config, "PORT_SERVER");

    if (!(serv = findServerByrAddr(server))) {
	mlog("%s: server object for addr '%s' not found\n", __func__,
		server);
	return NULL;
    }

    /* open a new connection to pbs_server */
    if (!(com = wConnect(serverPort, server, TCP_PROTOCOL))) {
	mlog("%s: connecting to PBS server addr '%s:%i' failed\n", __func__,
		server, serverPort);

	return NULL;
    }

    WriteTM(com, PBS_BATCH_StatusNode);
    WriteString(com, "root");
    WriteString(com, (char *) hostname);
    WriteDigit(com, 0);
    WriteDigit(com, 0);
    wDoSend(com);

    if (!(ReadTM(com, &cmd)) || cmd) {
	readAllData(com);
	mlog("%s: failed '%s'\n", __func__, buf);
	wClose(com);
	return NULL;
    }

    ReadDigitI(com, &tmp);
    ReadDigitI(com, &tmp);
    ReadDigitI(com, &tmp);
    ReadDigitI(com, &tmp);
    ReadString(com, hname, sizeof(hname));

    /* read the state */
    data = umalloc(sizeof(Data_Entry_t));
    INIT_LIST_HEAD(&data->list);

    ReadDigitI(com, &len);
    ReadDataStruct(com, len, &data->list, NULL);

    /* finish command */
    WriteTM(com, PBS_BATCH_Disconnect);
    WriteString(com, "root");
    wDoSend(com);
    wClose(com);
    return data;
}

static void writePBSNodeStateHeader(ComHandle_t *com, const char *host)
{
    WriteTM(com, PBS_BATCH_Manager);
    WriteString(com, "root");
    WriteDigit(com, 2);
    WriteDigit(com, 3);
    WriteString(com, (char *) host);
}

int setPBSNodeState(char *server, char *note, char *state, const char *host)
{
    ComHandle_t *com;
    int serverPort, cmd;
    Server_t *serv;
    Data_Entry_t data;
    char hname[HOST_NAME_LEN];
    const char *hostname;

    if (!host) {
	if ((gethostname(hname, sizeof(hname))) == -1) {
	    mlog("%s: can't get my hostname\n", __func__);
	    return 1;
	}
	hostname = hname;
    } else {
	hostname = host;
    }

    serverPort = getConfValueI(&config, "PORT_SERVER");

    if (!(serv = findServerByrAddr(server))) {
	mlog("%s: server object for addr '%s' not found\n", __func__,
		server);
	return 1;
    }

    /* open a new connection to pbs_server */
    if (!(com = wConnect(serverPort, server, TCP_PROTOCOL))) {
	mlog("%s: connecting to PBS server addr '%s:%i' failed\n", __func__,
		server, serverPort);

	return 1;
    }

    INIT_LIST_HEAD(&data.list);

    /* set note */
    if (note) {
	writePBSNodeStateHeader(com, hostname);
	setEntry(&data.list, "note", "", note);
	WriteDataStruct(com, &data);
	WriteDigit(com, 0);
	wDoSend(com);
	clearDataList(&data.list);

	if (!(ReadTM(com, &cmd)) || cmd) {
	    readAllData(com);
	    mlog("%s: failed '%s'\n", __func__, buf);
	    wClose(com);
	    return 1;
	}
	ReadDigitI(com, &cmd);
	ReadDigitI(com, &cmd);
    }

    /* set state */
    writePBSNodeStateHeader(com, hostname);
    setEntry(&data.list, "state", "", state);
    WriteDataStruct(com, &data);
    WriteDigit(com, 0);
    wDoSend(com);
    clearDataList(&data.list);

    if (!(ReadTM(com, &cmd)) || cmd) {
	readAllData(com);
	mlog("%s: failed '%s'\n", __func__, buf);
	wClose(com);
	return 1;
    }
    ReadDigitI(com, &cmd);
    ReadDigitI(com, &cmd);

    /* finish command */
    WriteTM(com, PBS_BATCH_Disconnect);
    WriteString(com, "root");
    wDoSend(com);
    wClose(com);
    return 0;
}

static int doSendObit(Job_t *job, char *addr, int port)
{
    ComHandle_t *com;

    /* open a new connection to pbs_server */
    if (!(com = wConnect(port, addr, TCP_PROTOCOL))) {
	mdbg(PSMOM_LOG_OBIT, "%s: failed sending job obit(1) for '%s' to "
		"'%s'\n", __func__, job->id, job->server);
	return 0;
    }

    /* send obit of the job */
    if (!com->jobid) com->jobid = ustrdup(job->id);

    mdbg(PSMOM_LOG_PTM, "%s: sending obit job:%s\n", __func__, job->id);
    mdbg(PSMOM_LOG_JOB, "%s job '%s' finished with exit code [%i]\n",
	    (job->qsubPort ? "interactive" : "batch"), job->id,
	    job->jobscriptExit);

    WriteTM(com, PBS_BATCH_JobObit);
    WriteString(com, "pbs_mom");
    WriteString(com, job->id);

    WriteDigit(com, job->jobscriptExit);
    WriteDataStruct(com,  &job->status);
    WriteDigit(com, 0);

    /* send exit status */
    if ((wDoSend(com)) == -1) {
	mdbg(PSMOM_LOG_OBIT, "%s: failed sending job obit(2) for '%s' to "
		"'%s'\n", __func__, job->id, job->server);
	return 0;
    }

    return 1;
}

/**
 * @brief Send a TM job termination message.
 *
 * Inform the PBS server about a job terminination.
 *
 * @param job The job to send the termination message.
 *
 * @return Returns 0 on success and 1 on error.
 */
int sendTMJobTermination(Job_t *job)
{
    struct list_head *pos;
    int serverPort, obitSuccess = 0;
    Server_t *serv;

    /* update used resources for job */
    if (job->state != JOB_WAIT_OBIT) {
	updateJobInfo(job);
    }

    serverPort = getConfValueI(&config, "PORT_SERVER");

    if ((doSendObit(job, job->server, serverPort))) {
	obitSuccess = 1;
    } else {
	list_for_each(pos, &ServerList.list) {
	    if (!(serv = list_entry(pos, Server_t, list))) break;

	    /* we cannot obit a job if the rpp connection
	     * to the PBS server is broken */
	    if (!serv->lastContact) {
		mdbg(PSMOM_LOG_OBIT, "%s: jobid '%s', skipping server '%s', "
			"no rpp connection\n", __func__, job->id, job->server);
		continue;
	    }

	    if (!(doSendObit(job, serv->addr, serverPort))) continue;

	    obitSuccess = 1;
	    break;
	}
    }

    /* setup timer for later retries */
    if (!obitSuccess) {
	if (job->state != JOB_WAIT_OBIT) {
	    setJobObitTimer(job);
	}
	return 1;
    }

    job->state = JOB_EXIT;
    return 0;
}

/**
 * @brief Send a TM error message.
 *
 * @param com The communication handle to use.
 *
 * @param err_code The error code to send.
 *
 * @param err_msg The error message to send.
 *
 * @param clear If set to 1 all recive puffer will be freed.
 *
 * @return Always returns 0.
 */
int send_TM_Error(ComHandle_t *com, int32_t err_code, char *err_msg, int clear)
{
    /* free all receive puffers  */
    if (clear) {
	wEOM(com);
    }

    WriteTM(com, err_code);
    WriteDigit(com, 0);

    if (err_msg != NULL) {
	mdbg(PSMOM_LOG_PTM, "%s: %s\n", __func__, err_msg);
	WriteDigit(com, BATCH_REPLY_CHOICE_Text);
	WriteString(com, err_msg);
    } else {
	mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
	WriteDigit(com, BATCH_REPLY_CHOICE_Text);
	WriteDigit(com, 0);
    }
    wDoSend(com);
    wClose(com);
    return 0;
}

/**
 * @brief Handle a Batch Connect request.
 *
 * @param com The communication handle to use.
 *
 * @returns Returns 0 on success, -1 on error.
 */
static int handle_TM_BConnect(ComHandle_t *com)
{
    char jobid[JOB_NAME_LEN];
    unsigned int x, cmd;
    unsigned int len;
    Job_t *job;

    ReadDigitUI(com, &x);
    ReadDigitUI(com, &cmd);

    switch (cmd) {
	case BATCH_REPLY_CHOICE_NULL:
	    mdbg(PSMOM_LOG_PTM, "%s: closing connection\n", __func__);
	    wClose(com);
	    break;
	case BATCH_REPLY_CHOICE_Status:
	    ReadDigitUI(com, &x);
	    ReadDigitUI(com, &x);

	    ReadString(com, jobid, sizeof(jobid));

	    if ((job = findJobById(jobid)) == NULL) {
		return send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
	    }

	    ReadDigitUI(com, &len);
	    mdbg(PSMOM_LOG_PTM, "%s: reading job data job:%s, len:%i\n",
		__func__, jobid, len);

	    ReadDataStruct(com, len, &job->data.list, DataFilterJob);
	    wClose(com);
	    break;
	default:
	    mlog("%s: unknown batch reply\n", __func__);
	    send_TM_Error(com, PBSE_UNKREQ, NULL, 1);
	    return 0;
    }
    return 0;
}

/**
 * @brief Handle a TM disconnect request.
 *
 * The task manager ask me to close the connection.
 *
 * @param com The communication handle to use.
 *
 * @return Always returns 0;
 */
static int handle_TM_BDisconnect(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
    wClose(com);

    return 0;
}

/**
 * @brief Handle a "batch queue job" request.
 *
 * @param com The communication handle to use.
 */
static int handle_TM_BQueueJob(ComHandle_t *com, char *sender)
{
    char host[HOST_NAME_LEN], jobid[JOB_NAME_LEN];
    char *hashname = NULL, *user = NULL;
    struct passwd *result;
    unsigned int len;
    Job_t *job;

    ReadString(com, jobid, sizeof(jobid));
    ReadString(com, host, sizeof(host));

    mdbg(PSMOM_LOG_PTM, "%s: job queue request of '%s'\n", __func__, jobid);
    mdbg(PSMOM_LOG_JOB, "new job '%s' from '%s' is queued\n", jobid,
	    com->remoteAddr);

    job = addJob(jobid, com->remoteAddr);
    job->state = JOB_QUEUED;

    /* get job information from server */
    ReadDigitUI(com, &len);
    mdbg(PSMOM_LOG_PTM, "%s: host '%s' data len '%i' from %s\n",
	__func__, host, len, com->remoteAddr);

    if ((ReadDataStruct(com, len, &job->data.list, DataFilterJob)) == -1) {
	wReconnect(com);
	return false;
    }
    ReadDigitUI(com, &len);

    /* setup various node infos */
    if (!(setNodeInfos(job))) {
	send_TM_Error(com, PBSE_NOATTR, "Undefined Attribute", 1);
	deleteJob(job->id);
	return false;
    }

    /* make sure all psmom in the job have the same versions */
    if (job->nrOfUniqueNodes > 1) sendPSmomVersion(job);

    /* set hashname */
    if ((hashname = getJobDetail(&job->data, "hashname", NULL))) {
	job->hashname = ustrdup(hashname);
    } else {
	job->hashname = ustrdup(job->id);
    }

    /* set username */
    if ((user = getJobDetail(&job->data, "euser", NULL))) {
	job->user = ustrdup(user);
    } else {
	mlog("%s: invalid username (euser) received from pbs_server\n",
		__func__);
	send_TM_Error(com, PBSE_NOATTR, "Undefined Attribute", 1);
	deleteJob(job->id);
	return false;
    }

    /* save users passwd information */
    job->pwbuf = umalloc(pwBufferSize);
    while ((getpwnam_r(job->user, &job->passwd, job->pwbuf, pwBufferSize,
		&result)) != 0) {
	if (errno == EINTR) continue;
	mlog("%s: getpwnam(%s) failed : %s\n", __func__, job->user,
		strerror(errno));
	send_TM_Error(com, PBSE_BADUSER, "Bad user - no password entry", 1);
	deleteJob(job->id);
	return false;
    }
    if (result == NULL) {
	mlog("%s: getpwnam(%s) failed : %s\n", __func__, job->user,
		strerror(errno));
	send_TM_Error(com, PBSE_BADUSER, "Bad user - no password entry", 1);
	deleteJob(job->id);
	return false;
    }

    /* send reply */
    WriteTM(com, PBS_BATCH_Connect);
    WriteDigit(com, 0);
    WriteDigit(com, BATCH_REPLY_CHOICE_Queue);
    WriteString(com, jobid);

    return wDoSend(com);
}

/**
 * @brief Handle a TM job credential request.
 *
 * Not implemented in Torque yet. Just recongize it.
 *
 * @return Always returns 0.
 */
static int handle_TM_BJobCred(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
    mdbg(PSMOM_LOG_WARN, "%s: not supported\n", __func__);
    return send_TM_Error(com, PBSE_NOSUP, "jobcred not supported", 1);
}

/**
 * @brief Handle a TM hold job request.
 *
 * Server wants me to checkpoint and stop the job. Psmom
 * does not/will not support checkpointing.
 *
 * @return Always returns 0.
 */
static int handle_TM_BHoldJob(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
    mdbg(PSMOM_LOG_WARN, "%s: not supported\n", __func__);
    return send_TM_Error(com, PBSE_NOSUP, "checkpointing not supported", 1);
}

/**
 * @brief Handle a TM message job request.
 *
 * Used to add a message to the job stdout/stderr files.
 *
 * @return Always returns 0.
 */
static int handle_TM_BMessJob(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
    mdbg(PSMOM_LOG_WARN, "%s: not supported\n", __func__);
    return send_TM_Error(com, PBSE_NOSUP, "messaging job not supported", 1);
}

/**
 * @brief Handle a TM modify job request.
 *
 * Used to change job parameter of an existing job.
 *
 * @return Always returns 0.
 */
static int handle_TM_BModifyJob(ComHandle_t *com)
{
    Job_t *job;
    char jobid[JOB_NAME_LEN];
    unsigned int x, len;

    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);

    ReadDigitUI(com, &x);
    ReadDigitUI(com, &x);

    ReadString(com, jobid, sizeof(jobid));

    if ((job = findJobById(jobid)) == NULL) {
	return send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
    }

    ReadDigitUI(com, &len);
    mdbg(PSMOM_LOG_PTM, "%s: reading job data job:%s, len:%i\n",
	__func__, jobid, len);

    ReadDataStruct(com, len, &job->data.list, DataFilterJob);
    wClose(com);

    return 0;
}

/**
 * @brief Handle a TM rerun job request.
 *
 * Used to send jobfiles again to pbs_server.
 *
 * @return Always returns 0.
 */
static int handle_TM_BRerunJob(ComHandle_t *com)
{
    readAllData(com);
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
    mlog("%s: not (yet) supported, got '%s'\n", __func__, buf);
    return send_TM_Error(com, PBSE_NOSUP, "rerunning job not supported", 1);
}

/**
 * @brief Handle a Batch Jobscript request.
 *
 *      Data items are: u int   block sequence number
 *                      u int   file type (stdout, stderr, ...)
 *                      u int   size of data in block
 *                      string  job id
 *                      cnt str data
 */
static int handle_TM_Bjobscript(ComHandle_t *com)
{
    FILE *fp;
    char *jobscript, *jobfiles;
    size_t jlen, size;
    char jobid[JOB_NAME_LEN];
    unsigned int block, file, end;
    int jsWritten;
    Job_t *job;

    ReadDigitUI(com, &block);
    ReadDigitUI(com, &file);
    ReadDigitUL(com, &size);

    mdbg(PSMOM_LOG_PTM, "%s: block:%i, file:%i, size:%zu", __func__,
	block, file, size);

    /* read jobid */
    ReadString(com, jobid, sizeof(jobid));
    mdbg(PSMOM_LOG_PTM, " jobid:%s\n", jobid);

    /* read jobscript */
    if (!(jobscript = ReadStringEx(com, &jlen))) {
	mlog("%s: reading (%i) Jobscript for '%s' failed\n",
		__func__, block, jobid);
	return send_TM_Error(com, PBSE_UNKJOBID, "Invalid jobscript", 1);
    }

    if (jlen != size) {
	mlog("%s: invalid js block '%i' for '%s': toread '%zu' read '%zu'\n",
		__func__, block, jobscript, size, jlen);
	return send_TM_Error(com, PBSE_UNKJOBID, "Invalid jobscript", 1);
    }

    ReadDigitUI(com, &end);
    mdbg(PSMOM_LOG_PTM, " end:%i\n", end);

    if ((job = findJobById(jobid)) == NULL) {
	ufree(jobscript);
	return send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
    }

    if (jlen < 1) {
	ufree(jobscript);
	return send_TM_Error(com, PBSE_UNKJOBID, "Invalid Jobscript", 1);
    }

    /* set jobscript filename */
    if (!job->jobscript) {
	jobfiles = getConfValueC(&config, "DIR_JOB_FILES");
	snprintf(buf, sizeof(buf), "%s/%s", jobfiles, job->hashname);
	job->jobscript = ustrdup(buf);
    }

    if (!(fp = fopen(job->jobscript, "a"))) {
	ufree(jobscript);
	mlog("%s: open file '%s' failed\n", __func__, job->jobscript);
	return send_TM_Error(com, PBSE_UNKJOBID, "Writing Jobscript failed", 1);
    }

    while ((jsWritten = fprintf(fp, "%s", jobscript)) !=
	    (int) strlen(jobscript)) {
	if (errno == EINTR) continue;
	mlog("%s: writing (%i) jobscript '%s' failed : %s\n", __func__, block,
		jobscript, strerror(errno));
	send_TM_Error(com, PBSE_UNKJOBID, "Writing Jobscript failed", 1);
	ufree(jobscript);
	return 1;
    }

    fclose(fp);
    ufree(jobscript);

    WriteTM_Batch_Reply(com, BATCH_REPLY_CHOICE_NULL);
    return wDoSend(com);
}

/**
 * @brief Handle a Batch Ready to Commit request.
 */
static int handle_TM_BRdytoCommit(ComHandle_t *com)
{
    unsigned int end;
    ReadString(com, buf, sizeof(buf));
    ReadDigitUI(com, &end);
    mdbg(PSMOM_LOG_PTM, "%s: y:%s end:%i\n", __func__, buf, end);

    WriteTM_Batch_Reply(com, BATCH_REPLY_CHOICE_RdytoCom);
    WriteString(com, buf);
    return wDoSend(com);
}

/**
 * @brief Handle a TM commit request.
 *
 * Start the commited job.
 *
 */
static int handle_TM_PCommit(ComHandle_t *com)
{
    char jobid[JOB_NAME_LEN], *qsub_port;
    unsigned int end;
    int ret;
    Job_t *job;

    ReadString(com, jobid, sizeof(jobid));
    ReadDigitUI(com, &end);
    mdbg(PSMOM_LOG_PTM, "%s: jobid:%s end:%i\n", __func__, jobid, end);

    if ((job = findJobById(jobid)) == NULL) {
	return send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
    }

    /* prologue time is accounted as job time */
    job->start_time = time(NULL);

    /* init prologue tracking */
    job->prologueTrack = job->nrOfUniqueNodes;

    /* save qsub_port now for interactive jobs, because torque
     * will override it later on with "TRUE" */
    if ((qsub_port = getJobDetail(&job->data, "interactive", NULL))) {
	job->qsubPort = atoi(qsub_port);

	/* open connection to waiting qsub */
	if ((spawnInteractiveJob(job))) {
	    return send_TM_Error(com, PBSE_SYSTEM, "system error occurred", 1);
	}
    }

    /* send "everthing is ok" back to server */
    WriteTM_Batch_Reply(com, BATCH_REPLY_CHOICE_Commit);
    WriteString(com, jobid);
    ret = wDoSend(com);

    /* start the prologue script(s) */
    job->state = JOB_PROLOGUE;
    sendPElogueStart(job, true);
    monitorPELogueTimeout(job);

    /* save job information to disc */
    updateJobInfo(job);
    saveJobInfo(job);

    return ret;
}

int requestJobInformation(Job_t *job)
{
    int serverPort;
    ComHandle_t *com;

    mdbg(PSMOM_LOG_PTM, "%s: Requesting job info for %s\n", __func__,
	    job->id);

    serverPort = getConfValueI(&config, "PORT_SERVER");

    if (!(com = wConnect(serverPort, job->server, TCP_PROTOCOL))) {
	mlog("%s: failed sending job status request for '%s' from '%s'\n",
		__func__, job->id, job->server);
	return 1;
    }

    WriteTM(com, PBS_BATCH_StatusJob);
    WriteString(com, "pbs_mom");
    WriteString(com, job->id);
    WriteDigit(com, 0);
    WriteDigit(com, 0);

    if ((wDoSend(com)) == -1) return 1;
    job->update++;

    return 0;
}

/**
 * @brief Handle a TM job status request.
 *
 * Return the status of one (if id is specified) or all
 * jobs (if id is null).
 */
static int handle_TM_BStatusJob(ComHandle_t *com)
{
    char jobid[JOB_NAME_LEN];
    unsigned int end;
    Job_t *job;

    ReadString(com, jobid, sizeof(jobid));
    ReadDigitUI(com, &end);
    ReadDigitUI(com, &end);

    WriteTM_Batch_Reply(com, BATCH_REPLY_CHOICE_Status);

    if (strlen(jobid) == 0) {
	/* send status for all jobs */
	/* seems that nobody is using this feature at all */
	mlog("%s: send status for all jobs not yet implemented\n", __func__);
	return send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
    } else {
	/* send status for a single job */
	if ((job = findJobById(jobid)) == NULL) {
	    return send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
	}

	switch (job->state) {
	    case JOB_RUNNING:
	    case JOB_EXIT:
	    case JOB_PROLOGUE:
	    case JOB_PRESTART:
	    case JOB_EPILOGUE:
	    case JOB_CANCEL_PROLOGUE:
	    case JOB_CANCEL_EPILOGUE:
	    case JOB_CANCEL_INTERACTIVE:

		/* re-calculate job informations */
		updateJobInfo(job);

		/* store job informations to disc */
		saveJobInfo(job);

		mdbg(PSMOM_LOG_PTM, "%s: sending job status for job '%s'\n",
		    __func__ , jobid);

		WriteDigit(com, 1);
		WriteDigit(com, 2);
		WriteString(com, jobid);

		/* send cput,mem,vmem,walltime */
		WriteDataStruct(com, &job->status);
		break;
	    case JOB_WAIT_OBIT:
		/* will send no job information */
		WriteDigit(com, 0);
		break;
	    default:
		mlog("%s: unknown jobstate: '%i'\n", __func__, job->state);
		WriteDigit(com, 0);
	}

	if ((wDoSend(com)) == -1) return -1;
    }

    /* request job information from pbs_server */
    if (!job->update) {
	if (requestJobInformation(job)) return 1;
    }

    return 0;
}

/**
 * @brief Send a signal to a job.
 */
static int handle_TM_BSignalJob(ComHandle_t *com)
{
    char jobid[JOB_NAME_LEN];
    char signal[200];
    Job_t *job;
    unsigned int end;
    int sig = -1;

    /* read jobid */
    ReadString(com, jobid, sizeof(jobid));

    /* read signal to send */
    ReadString(com, signal, sizeof(signal));

    /* read end of command */
    ReadDigitUI(com, &end);

    /* read extended info */
    if (end != 0) {
	ReadString(com, buf, sizeof(buf));
    }

    if ((job = findJobById(jobid)) == NULL) {
	return send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
    }

    if ((sig = string2Signal(signal)) > 0) {
	job->signalFlag = sig;

	switch (job->state) {
	    case JOB_RUNNING:
		mdbg(PSMOM_LOG_VERBOSE, "%s: job '%s' signal '%s' pid:'%i'\n",
		    __func__, jobid, signal, job->pid);

		if (!signalJob(job, sig, "PBS server")) {
		    mlog("%s: signal '%s' to job '%s' failed\n",
			 __func__, signal, job->id);
		    return send_TM_Error(com, PBSE_SYSTEM,
					 "system error occurred", 1);
		}
		break;
	    case JOB_CANCEL_PROLOGUE:
	    case JOB_CANCEL_EPILOGUE:
	    case JOB_CANCEL_INTERACTIVE:
	    case JOB_EXIT:
	    case JOB_WAIT_OBIT:
		/* nothing to do here, job will be canceled anyway */
		mlog("%s: can't signal '%s' job '%s' in state '%s'\n", __func__,
			signal, job->id, jobState2String(job->state));
		return send_TM_Error(com, PBSE_BADSTATE,
					"request invalid for job state", 1);
		break;
	    case JOB_EPILOGUE:
		/* don't cancel epilogue scripts */
		mlog("%s: not forwarding signal '%s' to job '%s' in epilogue\n",
			__func__, signal, job->id);
		break;
	    case JOB_PROLOGUE:
	    case JOB_PRESTART:
		/* cancel prologue scripts */
		mlog("signal '%s (%i)' to job '%s' in prologue\n",
			signal2String(sig), sig, job->id);
		signalPElogue(job, signal, "PBS server");
		break;
	    case JOB_QUEUED:
	    case JOB_INIT:
		mlog("%s: unexpected signal '%s' for job '%s' which is in"
		     " init/queued state\n", __func__, signal, job->id);
		return send_TM_Error(com, PBSE_BADSTATE,
					"request invalid for job state", 1);
		break;
	    default:
		mlog("%s: unknown job state '%s' for job '%s': can't send"
		     " signal '%s'\n", __func__, jobState2String(job->state),
		     job->id, signal);
	}
    } else {
	mlog("%s: got unknown signal '%s' request\n", __func__, signal);
	return send_TM_Error(com, PBSE_UNKSIG, NULL, 1);
    }

    WriteTM_Batch_Reply(com, BATCH_REPLY_CHOICE_NULL);
    return wDoSend(com);
}

/**
 * @brief Handle a TM Copy Files Request.
 *
 * Copy file structure:
 *
 *   string	    job id   (may be null)
 *   string	    job owner  (may be null)
 *   string	    execution user name
 *   string	    execution group name (may be null)
 *   unsigned int   direction
 *   unsigned int   count of file pairs in set
 *
 *   set of file pairs:
 *	unsigned int flag
 *	string	local path name
 *	string	remote path name (may be null)
 *
*/
static int handle_TM_BCopyFiles(ComHandle_t *com)
{
    char jobid[JOB_NAME_LEN] = {'\0'};
    char jobowner[USER_NAME_LEN] = {'\0'};
    char execuser[USER_NAME_LEN] = {'\0'};
    char execgroup[USER_NAME_LEN] = {'\0'};
    unsigned int direction, count;
    unsigned int flag, i, end;
    char local[HOST_NAME_LEN];
    char remote[HOST_NAME_LEN];
    Copy_Data_t *copyData;
    Copy_Data_files_t *ptr;
    Job_t *job;

    /* find the job */
    ReadString(com, jobid, sizeof(jobid));

    if (!(job = findJobById(jobid))) {
	mlog("%s: copy request for unknown job '%s'\n", __func__, jobid);
	send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
	return -1;
    }

    /* no need to copy anything, since the job was aborted at prologue */
    if (job->prologueExit == 1) {
	readAllData(com);

	WriteTM(com, 0);
	WriteDigit(com, 0);
	WriteDigit(com, 1);
	wDoSend(com);
	return 0;
    }

    ReadString(com, jobowner, sizeof(jobowner));
    ReadString(com, execuser, sizeof(execuser));
    ReadString(com, execgroup, sizeof(execgroup));
    ReadDigitUI(com, &direction);
    ReadDigitUI(com, &count);

    mdbg(PSMOM_LOG_JOB, "copy %i files for job '%s'\n", count, jobid);

    copyData = umalloc(sizeof(Copy_Data_t));
    copyData->jobid = ustrdup(jobid);
    copyData->hashname = ustrdup(job->hashname);
    copyData->jobowner = ustrdup(jobowner);
    copyData->execuser = ustrdup(execuser);
    copyData->execgroup = ustrdup(execgroup);
    copyData->direction = direction;
    copyData->count = count;
    copyData->files = umalloc(sizeof(Copy_Data_files_t *) * count);
    copyData->com = com;

    for (i=0; i<count; i++) {
	ReadDigitUI(com, &flag);
	ReadString(com, local, sizeof(local));
	ReadString(com, remote, sizeof(remote));

	/* add to data structure */
	copyData->files[i] = umalloc(sizeof(Copy_Data_files_t));
	ptr = copyData->files[i];
	ptr->local = ustrdup(local);
	ptr->remote = ustrdup(remote);
	ptr->flag = flag;

	mdbg(PSMOM_LOG_PTM, "%s: copy flag:%i local:%s remote:%s\n", __func__,
	    flag, local, remote);
    }

    /* read the end of structre sign */
    ReadDigitUI(com, &end);

    /* do the copy in a separated process, because it could take some time */
    if ((spawnCopyScript(copyData))) {
	/* forking copy process failed */
	send_TM_Error(com, PBSE_SYSTEM, "system error occurred", 1);
    }

    return 0;
}

/**
 * @brief Handle a TM delete files request.
 *
 * In psmom we are not going to delete any files.
 *
 * @return Always returns 0.
 */
static int handle_TM_BDelFiles(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
    mdbg(PSMOM_LOG_WARN, "%s: not supported\n", __func__);
    readAllData(com);
    mdbg(PSMOM_LOG_WARN, "%s: del cmd buf '%s'\n", __func__, buf);
    return send_TM_Error(com, PBSE_NOSUP, NULL, 1);
}

/**
 * @brief Handle a TM shutdown job request.
 *
 * Shutdown is not even supported by original mom.
 *
 * @return Always returns 0.
 */
static int handle_TM_BShutdown(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
    mdbg(PSMOM_LOG_WARN, "%s: not supported\n", __func__);
    return send_TM_Error(com, PBSE_NOSUP, NULL, 1);
}

/**
 * @brief Handle a TM move jobfile request.
 *
 * Move some job files the server told me to spool or users home.
 * Implemented maybe later.
 *
 * @return Always returns 0.
 */
static int handle_TM_BMoveJobfile(ComHandle_t *com)
{
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);
    mdbg(PSMOM_LOG_WARN, "%s: not supported\n", __func__);
    return send_TM_Error(com, PBSE_NOSUP, NULL, 1);
}

int jobCleanup(Job_t *job, int save)
{
    char *dir;
    int cleanJob, cleanNodes;
    ComHandle_t *com;
    Child_t *child;

    /* make sure all children are dead */
    while ((child = findChildByJobid(job->id, -1)) != NULL) {
	if (child->c_sid > 0) {
	    psAccountSignalSession(child->c_sid, SIGTERM);
	    psAccountSignalSession(child->c_sid, SIGKILL);
	} else {
	    mlog("%s: can't kill child, session id missing\n", __func__);
	}
	deleteChild(child->pid);
    }

    /* cleanup leftover ssh/daemon processes */
    psPamDeleteUser(job->user, job->id);
    afterJobCleanup(job->user);

    /* close leftover file descriptors */
    while ((com = findComHandleByJobid(job->id)) != NULL) {
	wClose(com);
    }

    /* delete jobscript file */
    cleanJob = getConfValueI(&config, "CLEAN_JOBS_FILES");
    if (cleanJob) {
	dir = getConfValueC(&config, "DIR_JOB_FILES");
	snprintf(buf, sizeof(buf), "%s/%s", dir, job->hashname);
	unlink(buf);
    }

    /* delete node file */
    cleanNodes = getConfValueI(&config, "CLEAN_NODE_FILES");
    if (cleanNodes) {
	dir = getConfValueC(&config, "DIR_NODE_FILES");
	snprintf(buf, sizeof(buf), "%s/%s", dir, job->hashname);
	unlink(buf);
	snprintf(buf, sizeof(buf), "%s/%sgpu", dir, job->hashname);
	unlink(buf);
    }

    /* handle account informations */
    dir = getConfValueC(&config, "DIR_JOB_ACCOUNT");
    snprintf(buf, sizeof(buf), "%s/%s", dir, job->hashname);
    if (save) {
	char savePath[100];

	snprintf(savePath, sizeof(savePath), "%s/%s",
		    DEFAULT_DIR_JOB_UNDELIVERED, job->hashname);
	rename(buf, savePath);
    } else {
	unlink(buf);
    }

    /* handle job output/error files */
    dir = getConfValueC(&config, "DIR_SPOOL");
    if (save) {
	char savePath[100];

	snprintf(buf, sizeof(buf), "%s/%s.OU", dir, job->hashname);
	snprintf(savePath, sizeof(savePath), "%s/%s.OU",
		    DEFAULT_DIR_JOB_UNDELIVERED, job->hashname);
	rename(buf, savePath);

	snprintf(buf, sizeof(buf), "%s/%s.ER", dir, job->hashname);
	snprintf(savePath, sizeof(savePath), "%s/%s.ER",
		    DEFAULT_DIR_JOB_UNDELIVERED, job->hashname);
	rename(buf, savePath);
    } else {
	snprintf(buf, sizeof(buf), "%s/%s.OU", dir, job->hashname);
	unlink(buf);
	snprintf(buf, sizeof(buf), "%s/%s.ER", dir, job->hashname);
	unlink(buf);
    }

    /* delete the job structure */
    mdbg(PSMOM_LOG_PROCESS, "%s: deleting job '%s'\n", __func__, job->id);
    if ((deleteJob(job->id)) == 0) {
	send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
	return 1;
    }

    return 0;
}

/**
 * @brief Handle a TM Delete Job Request.
 *
 */
static int handle_TM_BDeleteJob(ComHandle_t *com)
{
    #define MGR_CMD_DELETE 1
    #define MGR_OBJ_JOB 2

    char jobid[JOB_NAME_LEN];
    unsigned int mgr_del, mgr_obj;
    unsigned int i, x;
    Job_t *job;

    ReadDigitUI(com, &mgr_del); // 1=MGR_CMD_DELETE
    ReadDigitUI(com, &mgr_obj); // 2=MGR_OBJ_JOB

    if (mgr_del != MGR_CMD_DELETE || mgr_obj != MGR_OBJ_JOB) {
	mlog("%s: unsupported: wrong MGR_CMD OR MGR_OBJ\n", __func__);
	return send_TM_Error(com, PBSE_NOSUP,
				"Feature/function not supported", 1);
    }

    ReadString(com, jobid, sizeof(jobid));
    ReadDigitUI(com, &i); // aoplp
    ReadDigitUI(com, &x); // extend

    mdbg(PSMOM_LOG_JOB, "deleting job '%s'\n", jobid);

    if (!(job = findJobById(jobid))) {
	mlog("%s: delete request for unknown job '%s'\n", __func__, jobid);
    } else {
	/* kill all processes and remove old files */
	if ((jobCleanup(job, 0))) return 1;
	mdbg(PSMOM_LOG_PTM, "%s: delete '%s'\n", __func__, jobid);
    }

    WriteTM(com, 0);
    WriteDigit(com, 0);
    WriteDigit(com, 1);

    return wDoSend(com);
}

/**
 * @brief Handle a TM (Task Manager) Init request.
 */
static int handle_TM_TInit(ComHandle_t *com, Job_t *job)
{
    unsigned int event, taskid;
    int i, ret;

    ReadDigitUI(com, &event);
    ReadDigitUI(com, &taskid);

    /* request is okay */
    WriteTM(com, TM_SUCCESS);
    WriteDigit(com, event);

    /* nr of nodes in job */
    WriteDigit(com, job->nrOfNodes);

    /* nodeids */
    for (i=0; i<job->nrOfNodes; i++) {
	WriteDigit(com, i);
    }

    /* jobid */
    WriteString(com, job->id);

    /* node */
    WriteDigit(com, -1);

    /* task id */
    WriteDigit(com, taskid);

    ret = wDoSend(com);
    wClose(com);

    mlog("%s: taskid:%i node:%i jobid:%s nrOfNodes:%i event:%i\n",
	__func__, taskid, -1, job->id, job->nrOfNodes, event);

    return ret;
}

/**
 * @brief Handle a TM Tasks Info request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TTasks(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
* @brief Handle a TM Spawn request.
*
* @return No return value.
*/
static int handle_TM_TSpawn(ComHandle_t *com, Job_t *job)
{
    unsigned int event, taskid, nodeNr, argc, i;
    Task_t *task;

    return 0;

    ReadDigitUI(com, &event);
    ReadDigitUI(com, &taskid);
    ReadDigitUI(com, &nodeNr);
    ReadDigitUI(com, &argc);

    task = createTask(job);
    task->argc = argc;
    task->argv = umalloc((argc + 1) * sizeof(char *));
    task->nodeNr = nodeNr;

    /* read all arguments */
    for (i=0; i<argc; i++) {
	ReadString(com, buf, sizeof(buf));
	task->argv[i] = ustrdup(buf);
	//mlog("arg%i:%s\n", i, buf);
    }

    /* read in env */
    while (ReadString(com, buf, sizeof(buf)), __func__) {
	//mlog("env:%s\n", buf);
	//task->env strcpy
    }

    mlog("%s: event:%i taskid:%i nodeNr:%i argc:%i\n",
	__func__, event, taskid, nodeNr, argc);

    /* check if we spawn here */
    if (nodeNr == 0) {
	/* return spawn ok */
	WriteTM(com, TM_SUCCESS);
	WriteDigit(com, event);
	WriteDigit(com, event);
    } else {
       /* TODO: spawn happens on other node */
       /*
       WriteTM(com, TM_ERROR);
       WriteDigit(com, event);
       WriteDigit(com, 17000);
       */
	WriteTM(com, TM_SUCCESS);
	WriteDigit(com, event);
	WriteDigit(com, event);
    }

    /* failed: return
    +2+1    3+999     +2	    5+17000
    P  V    Error   Event	    ERRORNR

     return: spawn ok
     * handle_TM_TSpawn:    event:2 taskid:1 nodeNr:0 argc:3
     +2+1+0 +2+2   +2+1+0+3+3
     P V   Success	event

    +2+1+0+2+2 +2+1+0+3+3+ 2+1+0+4+4 +2+1+0+5+5

    */

    wDoSend(com);

    return 0;
}

/**
 * @brief Handle a TM Signal request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TSignal(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
* @brief Handle a TM Job Obit request.
*
* @return No return value.
*/
static int handle_TM_TObit(ComHandle_t *com, Job_t *job)
{
    unsigned int taskid, event, nodeNr, some;

    ReadDigitUI(com, &event);
    ReadDigitUI(com, &taskid);
    ReadDigitUI(com, &nodeNr);
    ReadDigitUI(com, &some);

    mlog("%s: event:%i taskid:%i nodeNr:%i some:%i\n",
	__func__, event, taskid, nodeNr, some);

    /* check if the task is dead, if yes send out a obit msg */
    WriteTM(com, TM_SUCCESS);
    WriteDigit(com, event);

    /* exit status for task */
    WriteDigit(com, 255);

    wDoSend(com);

    /*
     req:
	 +2+1
	 2+25 1079.michi-ng.localdomain
	 2+32 AADA9E53BFA06DCE6593430FB017C09C
	 3+104

	 +4
	 +1
	 +0

     ret: 2+2+1+0+4+0

     req:
	+2+1
	2+251079.michi-ng.localdomain
	2+32AADA9E53BFA06DCE6593430FB017C09C
	3+104
	+5
	+1
	+1

	+3
     ret: +2+1+0+5+0

     */
    return 0;
}

/**
 * @brief Handle a TM Resources request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TResources(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
 * @brief Handle a TM Postinfo request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TPostinfo(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
 * @brief Handle a TM Getinfo request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TGetinfo(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
 * @brief Handle a TM Getid request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TGetid(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
 * @brief Handle a TM Register request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TRegister(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
 * @brief Handle a TM Reconfig request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TReconfig(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
 * @brief Handle a TM ACK request.
 *
 * Implemented when needed.
 *
 */
static int handle_TM_TACK(ComHandle_t *com, Job_t *job)
{
    mdbg(PSMOM_LOG_PTM, "%s: job '%s'\n", __func__, job->id);
    mdbg(PSMOM_LOG_WARN, "%s: request for job '%s' not supported\n",
	    __func__, job->id);

    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
    return 0;
}

/**
* @brief Handle a TM finalize request.
*
* @return No return value.
*/
static int handle_TM_TFinalize(ComHandle_t *com)
{
    wClose(com);
    return 0;
}

/**
* @brief Handle a TM request over the TM interface.
*
* This is used by the mpiexec of torque and openmpi (to spawn
* the orted and further processes).
*
* @return No return value.
*/
static int handle_TM_Request(ComHandle_t *com)
{
    Job_t *job;
    char jobid[JOB_NAME_LEN];
    char jobcookie[JOB_NAME_LEN];
    unsigned int cmd;
    int ret = -1;

    /* DISABLE TM interface */
    wClose(com);
    return -1;

    /* read jobid */
    ReadString(com, jobid, sizeof(jobid));

    if ((job = findJobById(jobid)) == NULL) {
	send_TM_Error(com, PBSE_UNKJOBID, "Unknown Job Id", 1);
	return -1;
    }

    /* read jobcookie */
    ReadString(com, jobcookie, sizeof(jobcookie));

    if ((strcmp(jobcookie, job->cookie)) != 0) {
	send_TM_Error(com, TM_EBADENVIRONMENT, "Invalid cookie", 1);
	return -1;
    }

    /* read cmd */
    if ((ReadDigitUI(com, &cmd)) < 0){
	 mlog("%s: invalid command:%i\n", __func__, cmd);
	 return -1;
    }

    switch (cmd) {
	case TM_INIT:
	    ret = handle_TM_TInit(com, job);
	    break;
	case TM_TASKS:
	    ret = handle_TM_TTasks(com, job);
	    break;
	case TM_SPAWN:
	    ret = handle_TM_TSpawn(com, job);
	    break;
	case TM_SIGNAL:
	    ret = handle_TM_TSignal(com, job);
	    break;
	case TM_OBIT:
	    ret = handle_TM_TObit(com, job);
	    break;
	case TM_RESOURCES:
	    ret = handle_TM_TResources(com, job);
	    break;
	case TM_POSTINFO:
	    ret = handle_TM_TPostinfo(com, job);
	    break;
	case TM_GETINFO:
	    ret = handle_TM_TGetinfo(com, job);
	    break;
	case TM_GETTID:
	    ret = handle_TM_TGetid(com, job);
	    break;
	case TM_REGISTER:
	    ret = handle_TM_TRegister(com, job);
	    break;
	case TM_RECONFIG:
	    ret = handle_TM_TReconfig(com, job);
	    break;
	case TM_ACK:
	    ret = handle_TM_TACK(com, job);
	    break;
	case TM_FINALIZE:
	    ret = handle_TM_TFinalize(com);
	default:
	    send_TM_Error(com, TM_EUNKNOWNCMD, NULL, 1);
	    mdbg(PSMOM_LOG_WARN, "%s: got unknown TM request '%i' for job "
		    "'%s'\n", __func__, cmd, job->id);
    }

    return ret;
}

static void handleTMError(ComHandle_t *com, unsigned int cmd)
{
    Job_t *job;

    /* error message */
    ReadString(com, buf, sizeof(buf));

    switch(cmd) {
	case PBSE_UNKJOBID:
	    if ((job = findJobById(com->jobid))) {
		if (job->state == JOB_EXIT) {
		    mlog("%s: moving job '%s' undelivered, reason: unknown by "
			    "pbs server\n", __func__, job->id);
		    jobCleanup(job, 1);
		}
	    }
	    break;
	case PBSE_BADCRED:
	    if ((job = findJobById(buf))) {
		if (job->state == JOB_EXIT) {
		    /* resend job obit */
		    setJobObitTimer(job);
		}
	    }
	    readAllData(com);
	    /* fallthrough */
	default:
	    mlog("%s: got error cmd err_code %i err_msg '%s' from %s:%i\n",
		__func__, cmd, buf, com->remoteAddr, com->remotePort);
    }
}

/**
 * @brief Handle a Task Manager (TM) request.
 *
 * Main switch for the TM protocol.
 *
 * @return Returns -1 on error, or 0 on success.
 */
static int handle_TM_message(ComHandle_t *com)
{
    unsigned int version = 0;
    unsigned int cmd = 0;
    char sender[200];
    int rmPort, ret = -1;

    if ((ReadDigitUI(com, &version)) < 0 || version != TM_PROTOCOL_VER) {
	mlog("%s: invalid protocol version:%i from:'%s:%i'\n", __func__,
		version, com->remoteAddr, com->remotePort);
	return -1;
    }

    /* special tm interface */
    rmPort = getConfValueI(&config, "PORT_RM");
    if (com->type == TCP_PROTOCOL && com->localPort == rmPort) {
	if (ENABLE_TM_REQUEST) {
	    return handle_TM_Request(com);
	}
	wClose(com);
	return 0;
    }

    if ((ReadDigitUI(com, &cmd)) < 0){
	 mlog("%s: invalid command:%i\n", __func__, cmd);
	 return -1;
    }

    /* read sender info */
    if (cmd > 0 && cmd < 60) {
	ReadString(com, sender, sizeof(sender));
    } else {
	sender[0] = '\0';
    }

    /* check for error cmds */
    if (cmd > 15000 && cmd < 16000) {
	handleTMError(com, cmd);
	return -1;
    }

    mdbg(PSMOM_LOG_PTM, "%s: got cmd:%i sender:%s ver:%i\n", __func__,
	cmd, sender, version);

    switch (cmd) {
	case PBS_BATCH_Connect:
	    ret = handle_TM_BConnect(com);
	    break;
	case PBS_BATCH_QueueJob:
	    ret = handle_TM_BQueueJob(com, sender);
	    break;
	case PBS_BATCH_JobCred:
	    ret = handle_TM_BJobCred(com);
	    break;
	case PBS_BATCH_HoldJob:
	    ret = handle_TM_BHoldJob(com);
	    break;
	case PBS_BATCH_MessJob:
	    ret = handle_TM_BMessJob(com);
	    break;
	case PBS_BATCH_ModifyJob:
	    ret = handle_TM_BModifyJob(com);
	    break;
	case PBS_BATCH_Rerun:
	    ret = handle_TM_BRerunJob(com);
	    break;
	case PBS_BATCH_RdytoCommit:
	    ret = handle_TM_BRdytoCommit(com);
	    break;
	case PBS_BATCH_StatusJob:
	    ret = handle_TM_BStatusJob(com);
	    break;
	case PBS_BATCH_Commit:
	    ret = handle_TM_PCommit(com);
	    break;
	case PBS_BATCH_Jobscript:
	    ret = handle_TM_Bjobscript(com);
	    break;
	case PBS_BATCH_CopyFiles:
	    ret = handle_TM_BCopyFiles(com);
	    break;
	case PBS_BATCH_DelFiles:
	    ret = handle_TM_BDelFiles(com);
	    break;
	case PBS_BATCH_Shutdown:
	    ret = handle_TM_BShutdown(com);
	    break;
	case PBS_BATCH_MvJobFile:
	    ret = handle_TM_BMoveJobfile(com);
	    break;
	case PBS_BATCH_DeleteJob:
	    ret = handle_TM_BDeleteJob(com);
	    break;
	case PBS_BATCH_SignalJob:
	    ret = handle_TM_BSignalJob(com);
	    break;
	case PBS_BATCH_Disconnect:
	    ret = handle_TM_BDisconnect(com);
	    break;
	default:
	    send_TM_Error(com, PBSE_UNKREQ, NULL, 1);
	    mlog("%s unsupported batch request:%i\n", __func__, cmd);
	    return 0;
    }
    return ret;
}

/**
 * @brief Main protocol switch.
 *
 */
int handleNewData(ComHandle_t *com)
{
    unsigned int proto = 0;
    int ret = -1;

    if ((ret = ReadDigitUI(com, &proto)) < 0) {

	/* connection closed */
	if (ret == -2) {
	    mlog("%s: lost connection, stream:%i\n", __func__, com->socket);
	    wReconnect(com);
	} else if (ret == -3) {
	    return ret;
	} else {
	    if (proto != 0) {
		mlog("%s: invalid protocol '%i' ret '%i' from '%s'\n", __func__,
		    proto, ret, com->remoteAddr);
	    }
	    wReconnect(com);
	}
	return ret;
    }

    ret = -1;

    switch (proto) {
	case RM_PROTOCOL: /* resource manager request */
	    ret = handle_RM_message(com);
	    break;
	case TM_PROTOCOL: /* task manager request */
	    ret = handle_TM_message(com);
	    break;
	case IM_PROTOCOL: /* inter MOM request(s) are not supported by psmom */
	    mlog("%s: unsupported InterMom request from '%s:%i'. I am not a "
		    "pbs mom!\n", __func__, com->remoteAddr, com->remotePort);
	    wClose(com);
	    return 0;
	case IS_PROTOCOL: /* inter server request */
	    ret = handle_IS_message(com);
	    break;
	default:
	    mlog("%s: invalid protocol type '%i' from '%s'\n", __func__,
		proto, com->remoteAddr);
    }

    /* reconnect connection if we received an invalid a cmd */
    if (ret == -1) wReconnect(com);

    return ret;
}

static void doWriteStateStruct(ComHandle_t *com, struct list_head *list)
{
    Data_Entry_t *next;
    struct list_head *pos;

    if (!(list_empty(list))) {

	list_for_each(pos, list) {
	    if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) {
		break;
	    }
	    if (next->value) WriteString(com, next->value);
	}
    }
}

static void writeStateStruct(ComHandle_t *com)
{
    /* update all values in the list */
    updateInfoList(1);
    doWriteStateStruct(com, &infoData.list);
    clearDataList(&infoData.list);

    doWriteStateStruct(com, &staticInfoData.list);
}

int WriteTM_Batch_Reply(ComHandle_t *com, int cmd)
{
    mdbg(PSMOM_LOG_PTM, "%s\n", __func__);

    WriteTM(com, 0);
    WriteDigit(com, 0);
    WriteDigit(com, cmd);

    return 0;
}

int ReadTM(ComHandle_t *com, int *cmd)
{
    unsigned int proto, version;

    ReadDigitUI(com, &proto);
    ReadDigitUI(com, &version);
    ReadDigitI(com, cmd);

    if (proto == TM_PROTOCOL && version == TM_PROTOCOL_VER) {
	return 1;
    }

    return 0;
}

/**
 * @brief Simplify sending an TM msg.
 *
 * @param com The connection handle.
 *
 * @param cmd The TM command to send.
 *
 */
int WriteTM(ComHandle_t *com, int cmd)
{
    if (com->socket < 0) {
	mlog("%s: invalid stream:%i\n", __func__, com->socket);
	return -1;
    }

    mdbg(PSMOM_LOG_PTM, "%s: TM_PROTOCOL:%i, TM_PROTOCOL_VER:%i, command:%i\n",
	       __func__, TM_PROTOCOL, TM_PROTOCOL_VER, cmd);

    WriteDigit(com, TM_PROTOCOL);
    WriteDigit(com, TM_PROTOCOL_VER);
    WriteDigit(com, cmd);

    return 0;
}

/**
 * @brief Init a IS connection.
 *
 * @return Returns 1 on success.
 */
int InitIS(ComHandle_t *com)
{
    /* say hello */
    WriteIS(com, IS_HELLO);
    wDoSend(com);

    /* send update request */
    WriteIS(com, IS_UPDATE);
    wDoSend(com);
    return 1;
}

/**
 * @brief Simplify sending an IS msg.
 *
 * @param com The connection handle.
 *
 * @param cmd The IS command to send.
 *
 */
int WriteIS(ComHandle_t *com, int cmd)
{
    if (com->socket < 0) {
	mlog("%s: invalid stream:%i\n", __func__, com->socket);
	return -1;
    }

    mdbg(PSMOM_LOG_PIS, "%s: IS_PROTOCOL:%i, IS_PROTOCOL_VER:%i, command:%i\n",
	       __func__, IS_PROTOCOL, IS_PROTOCOL_VER, cmd);

    WriteDigit(com, IS_PROTOCOL);
    WriteDigit(com, IS_PROTOCOL_VER);
    WriteDigit(com, cmd);

    if (torqueVer > 2) {
	/* mom_port */
	WriteDigit(com, momPort);

	/* rm_port */
	WriteDigit(com, rmPort);
    }

    switch(cmd) {
	case IS_UPDATE:
	    /* send internal state */
	    WriteDigit(com, 0);
	    break;
	case IS_STATUS:
	    writeStateStruct(com);
	    break;
    }
    return 1;
}
