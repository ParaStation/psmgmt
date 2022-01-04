/*
 * ParaStation
 *
 * Copyright (C) 2011-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomkvs.h"

#include <pwd.h>
#include <string.h>
#include <time.h>

#include "list.h"
#include "pscommon.h"

#include "plugin.h"
#include "pluginconfig.h"
#include "pluginlog.h"
#include "pluginmalloc.h"
#include "psidtask.h"

#include "psaccounthandles.h"
#include "pspamhandles.h"

#include "psmomchild.h"
#include "psmomcollect.h"
#include "psmomcomm.h"
#include "psmomconfig.h"
#include "psmomdef.h"
#include "psmomjob.h"
#include "psmomjobinfo.h"
#include "psmomlist.h"
#include "psmomlocalcomm.h"
#include "psmomlog.h"

/* some statistic tracking */
uint32_t stat_batchJobs = 0;
uint32_t stat_interJobs = 0;
uint32_t stat_successBatchJobs = 0;
uint32_t stat_successInterJobs = 0;
uint32_t stat_failedBatchJobs = 0;
uint32_t stat_failedInterJobs = 0;
uint32_t stat_remoteJobs = 0;
uint32_t stat_lPrologue = 0;
uint32_t stat_rPrologue = 0;
uint32_t stat_failedrPrologue = 0;
uint32_t stat_failedlPrologue = 0;
uint64_t stat_numNodes = 0;
uint64_t stat_numProcs = 0;
time_t stat_startTime = 0;

FILE *memoryDebug = NULL;

/* line buffer */
static char line[1024];

void formatTimeout(long start, long timeout, char *buf, size_t bufsize)
{
    struct tm *ts;

    if (timeout > 0) {
	timeout += start;
	ts = localtime(&timeout);
	strftime(buf, bufsize, "%Y-%m-%d %H:%M:%S", ts);
    } else {
	strncpy(buf, "no", bufsize);
    }
}

/**
 * @brief Save detailed job information into an output buffer.
 *
 * @param job The job structure which holds all the information to save.
 *
 * @param buf The buffer to save the information to.
 *
 * @param bufSize A pointer to the current size of the buffer.
 *
 * @return Returns the buffer with the job information.
 */
static char *showJob(Job_t *job, char *buf, size_t *bufSize)
{
    struct list_head *pos;
    Data_Entry_t *next;
    Child_t *child;

    snprintf(line, sizeof(line), "\n## job '%s' ##\n", job->id);
    str2Buf(line, &buf, bufSize);

    str2Buf("\n# psmom attributes #\n", &buf, bufSize);

    snprintf(line, sizeof(line), "user = '%s'\n", job->user);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "nr of procs = '%i'\n", job->nrOfNodes);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "nr of nodes = '%i'\n", job->nrOfUniqueNodes);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "server = '%s'\n", job->server);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "state = '%s'\n", jobState2String(job->state));
    str2Buf(line, &buf, bufSize);

    if (job->pwbuf) {
	snprintf(line, sizeof(line), "uid = '%i'\n", job->passwd.pw_uid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "gid = '%i'\n", job->passwd.pw_gid);
	str2Buf(line, &buf, bufSize);
    }

    snprintf(line, sizeof(line), "start time = %s", ctime(&job->start_time));
    str2Buf(line, &buf, bufSize);

    if (job->end_time > 0) {
	snprintf(line, sizeof(line), "end time = %s", ctime(&job->end_time));
	str2Buf(line, &buf, bufSize);
    }

    if (job->pid != -1) {
	snprintf(line, sizeof(line), "child pid = '%i'\n", job->pid);
	str2Buf(line, &buf, bufSize);
    }

    if (job->sid != -1) {
	snprintf(line, sizeof(line), "child sid = '%i'\n", job->pid);
	str2Buf(line, &buf, bufSize);
    }

    if (job->mpiexec != -1) {
	snprintf(line, sizeof(line), "mpiexec pid = '%i'\n", job->mpiexec);
	str2Buf(line, &buf, bufSize);
    }

    snprintf(line, sizeof(line), "job cookie = '%s'\n", job->cookie);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "prologue exit = '%i'\n", job->prologueExit);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "epilogue exit = '%i'\n", job->epilogueExit);
    str2Buf(line, &buf, bufSize);

    if (job->qsubPort != 0) {
	str2Buf("interactive = true\n", &buf, bufSize);

	snprintf(line, sizeof(line), "qsub port = '%i'\n", job->qsubPort);
	str2Buf(line, &buf, bufSize);
    } else {
	str2Buf("interactive = false\n", &buf, bufSize);
    }

    /* display saved pbs attributes */
    str2Buf("\n# pbs attributes #\n", &buf, bufSize);

    if (!(list_empty(&job->data.list))) {

	list_for_each(pos, &job->data.list) {
	    if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) break;
	    if (!next->name || *next->name == '\0') break;

	    if (next->resource) {
		snprintf(line, sizeof(line), "%s:%s = %s\n", next->name,
			next->resource, next->value);
	    } else {
		snprintf(line, sizeof(line), "%s = %s\n", next->name,
			next->value);
	    }
	    str2Buf(line, &buf, bufSize);
	}
    }

    /* display known accouting data */
    str2Buf("\n# accounting information #\n", &buf, bufSize);

    if (!(list_empty(&job->status.list))) {

	list_for_each(pos, &job->status.list) {
	    if ((next = list_entry(pos, Data_Entry_t, list)) == NULL)  break;
	    if (!next->name || *next->name == '\0') break;

	    if (next->resource) {
		snprintf(line, sizeof(line), "%s:%s = %s\n", next->name,
			next->resource, next->value);
	    } else {
		snprintf(line, sizeof(line), "%s = %s\n", next->name,
			next->value);
	    }
	    str2Buf(line, &buf, bufSize);
	}
    }

    /* display running job child */
    if ((child = findChildByJobid(job->id, -1))) {
	time_t max_runtime;

	str2Buf("\n# forwarder information #\n", &buf, bufSize);

	snprintf(line, sizeof(line), "pid = '%i'\n", child->pid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "type = '%s'\n",
		childType2String(child->type));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "child pid = '%i'\n",
		    child->c_pid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "child sid = '%i'\n",
		    child->c_sid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "start time = %s",
			    ctime(&child->start_time.tv_sec));
	str2Buf(line, &buf, bufSize);

	if (child->fw_timeout > 0) {
	    max_runtime = child->start_time.tv_sec + child->fw_timeout;

	    snprintf(line, sizeof(line), "timeout = %s",
				ctime(&max_runtime));
	}
	str2Buf(line, &buf, bufSize);
    }

    return buf;
}

/**
 * @brief Show psmom state information.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated state information.
 */
static char *showState(char *buf, size_t *bufSize)
{
    struct list_head *pos;
    Data_Entry_t *next;

    str2Buf("\n", &buf, bufSize);

    updateInfoList(1);
    if (!(list_empty(&infoData.list))) {

	list_for_each(pos, &infoData.list) {
	    if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) break;
	    if (!next->name || *next->name == '\0') break;

	    str2Buf(next->value, &buf, bufSize);
	    str2Buf("\n", &buf, bufSize);
	}
    }
    clearDataList(&infoData.list);

    if (!(list_empty(&staticInfoData.list))) {

	list_for_each(pos, &staticInfoData.list) {
	    if ((next = list_entry(pos, Data_Entry_t, list)) == NULL) break;
	    if (!next->name || *next->name == '\0') break;

	    str2Buf(next->value, &buf, bufSize);
	    str2Buf("\n", &buf, bufSize);
	}
    }

    return buf;
}

/**
 * @brief Show psmom connection information.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated connection information.
 */
static char *showConnectionState(char *buf, size_t *bufSize)
{
    struct list_head *pos;
    ComHandle_t *com;
    char *info, local[32], remote[32], lport[8], rport[8];

    snprintf(line, sizeof(line), "\nsocket\ttype\t%20s\t%20s\tinfo\n",
		"local", "remote");
    str2Buf(line, &buf, bufSize);

    if (masterSocket != -1) {
	snprintf(line, sizeof(line), "%i\t%s\t%20s\t%20s\t%s\n", masterSocket,
		"UNIX", "0.0.0.0:*", "0.0.0.0:*", masterSocketName);
	str2Buf(line, &buf, bufSize);
    }

    if (list_empty(&ComList.list)) return buf;

    list_for_each(pos, &ComList.list) {
	if ((com = list_entry(pos, ComHandle_t, list)) == NULL) break;

	info = com->jobid ? com->jobid : NULL;
	if (!info) info = com->info ? com->info : "";

	/* print local addr */
	if (com->localPort == -1) {
	    snprintf(lport, sizeof(lport), "*");
	} else {
	    snprintf(lport, sizeof(lport), "%i", com->localPort);
	}
	snprintf(local, sizeof(local), "%s:%s",
		    com->addr[0] ? com->addr : "0.0.0.0", lport);

	/* print remote addr */
	if (com->remotePort == -1) {
	    snprintf(rport, sizeof(rport), "*");
	} else {
	    snprintf(rport, sizeof(rport), "%i", com->remotePort);
	}
	snprintf(remote, sizeof(remote), "%s:%s",
		    com->remoteAddr[0] ? com->remoteAddr : "0.0.0.0",
		    rport);

	snprintf(line, sizeof(line), "%i\t%s\t%20s\t%20s\t%s\n", com->socket,
		protocolType2String(com->type), local, remote, info);
	str2Buf(line, &buf, bufSize);
    }

    return buf;
}

/**
 * @brief Show current forwarders.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated forwarder information.
 */
static char *showForwarder(char *buf, size_t *bufSize)
{
    Child_t *child;
    list_t *pos, *tmp;
    char start[50], timeout[50];
    struct tm *ts;

    if (list_empty(&ChildList.list)) {
	return str2Buf("\nCurrently no running forwarders.\n", &buf, bufSize);
    }

    snprintf(line, sizeof(line), "\n%12s\t%19s\t%19s\tPID\tChild PID"
		"\tChild SID\n", "Type", "Starttime", "Timeout");
    str2Buf(line, &buf, bufSize);


    list_for_each_safe(pos, tmp, &ChildList.list) {
	if ((child = list_entry(pos, Child_t, list)) == NULL) return buf;

	/* format start time */
	ts = localtime(&child->start_time.tv_sec);
	strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

	/* format timeout */
	formatTimeout(child->start_time.tv_sec, child->fw_timeout, timeout,
			sizeof(timeout));

	snprintf(line, sizeof(line), "%12s\t%s\t%s\t%i\t%i\t\t%i\n",
		 childType2String(child->type),
		 start,
		 timeout,
		 child->pid,
		 child->c_pid,
		 child->c_sid);

	str2Buf(line, &buf, bufSize);
    }

    return buf;
}

/**
 * @brief Get the session ID by a PID.
 *
 * @param pid The pid to get the session id for.
 *
 * @return Return 0 on error or the session id on success.
 */
static pid_t getSIDforPID(pid_t pid)
{
    FILE *fd;
    char buf[200];
    pid_t session;
    static char stat_format[] =
	"%*d "          /* pid */
	"(%*[^)]) "     /* comm */
	"%*c "          /* state */
	"%*u "          /* ppid */
	"%*u "          /* pgrp */
	"%u ";          /* session */


    snprintf(buf, sizeof(buf), "/proc/%i/stat", pid);

    if ((fd = fopen(buf, "r")) == NULL) {
	return 0;
    }

    if ((fscanf(fd, stat_format, &session)) != 1) {
	fclose(fd);
	return 0;
    }
    fclose(fd);

    return session;
}

/**
 * @brief Check a PID for validity.
 *
 * Check if a PID belongs to a running job. If not the PSHC will kill it.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showAllowedPid(char *key, char *buf, size_t *bufSize)
{
    char *reason = NULL;
    list_t *pos, *tmp;
    pid_t pid, sid;
    int found = 0;
    PStask_ID_t psAccLogger;
    PStask_t *task;
    Child_t *child;

    if ((sscanf(key, "allowed_%i", &pid)) != 1) {
	return str2Buf("\nInvalid pid: not a number\n", &buf, bufSize);
    }

    sid = getSIDforPID(pid);

    list_for_each_safe(pos, tmp, &ChildList.list) {
	if ((child = list_entry(pos, Child_t, list)) == NULL) continue;
	if (child->pid == pid) {
	    found = 1;
	    reason = "LOCAL_FORWARDER";
	    goto FINISH;
	}

	if (child->c_pid == pid) {
	    found = 1;
	    reason = "LOCAL_CHILD_PID";
	    goto FINISH;
	}

	if (child->c_sid == sid) {
	    found = 1;
	    reason = "LOCAL_CHILD_SID";
	    goto FINISH;
	}
    }

    /* try to find the logger using the account client list */
    if (!(task = PStasklist_find(&managedTasks, PSC_getTID(-1, pid)))) {
	psAccLogger = psAccountGetLoggerByClient(pid);
    } else {
	psAccLogger = task->loggertid;
    }

    /* check for local job */
    found = showAllowedJobPid(pid, sid, psAccLogger, &reason);
    if (found) goto FINISH;

    /* check for remote job */
    found = showAllowedJobInfoPid(pid, psAccLogger, &reason);
    if (found) goto FINISH;

    /* check if the child is from an SSH session */
    if (psPamFindSessionForPID(pid)) {
	found = 1;
	reason = "SSH";
    }

    /* return the result */
FINISH:

    snprintf(line, sizeof(line), "\n%i ", pid);
    str2Buf(line, &buf, bufSize);

    if (found) {
	str2Buf("TRUE ", &buf, bufSize);
	str2Buf(reason, &buf, bufSize);
	return str2Buf("\n", &buf, bufSize);
    }

    return str2Buf("FALSE\n", &buf, bufSize);
}

/**
 * @brief Show internal statistic.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showStatistic(char *buf, size_t *bufSize)
{
    uint64_t stat_allJobs = stat_batchJobs + stat_interJobs;

    str2Buf("\n# statistic #\n\n", &buf, bufSize);

    snprintf(line, sizeof(line), "executed batch jobs\t\t%u\n",
		stat_batchJobs);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "executed interactive jobs\t%u\n",
		stat_interJobs);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "successful batch jobs\t\t%u\n",
		stat_successBatchJobs);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "successful interactive jobs\t%u\n",
		stat_successInterJobs);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "failed batch jobs\t\t%u\n",
		stat_failedBatchJobs);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "failed interactive jobs\t\t%u\n",
		stat_failedInterJobs);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "successful local prologue\t%u\n",
		stat_lPrologue);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "successful remote prologue\t%u\n",
		stat_rPrologue);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "failed local prologue\t\t%u\n",
		stat_failedlPrologue);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "failed remote prologue\t\t%u\n",
		stat_failedrPrologue);
    str2Buf(line, &buf, bufSize);


    if (stat_allJobs > 0) {
	snprintf(line, sizeof(line), "average nodes\t\t\t%lu\n",
		    stat_numNodes / stat_allJobs);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "average processes\t\t%lu\n",
		    stat_numProcs / stat_allJobs);
	str2Buf(line, &buf, bufSize);
    }

    snprintf(line, sizeof(line), "executed remote jobs\t\t%u\n",
		stat_remoteJobs);
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "psmom start time\t\t%s\n",
		ctime(&stat_startTime));
    str2Buf(line, &buf, bufSize);

    return buf;
}

/**
 * @brief Show current configuration
 *
 * Print the current configuration of the plugin to the buffer @a buf
 * of current length @a length. The buffer might be dynamically
 * extended if required.
 *
 * @param buf Buffer to write information to
 *
 * @param bufSize Size of the buffer
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showConfig(char *buf, size_t *bufSize)
{
    int maxKeyLen = getMaxKeyLen(confDef);
    int i;

    str2Buf("\n", &buf, bufSize);

    for (i = 0; confDef[i].name; i++) {
	char *cName = confDef[i].name;
	char *cVal = getConfValueC(&config, cName);

	snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, cVal);
	str2Buf(line, &buf, bufSize);
    }

    return buf;
}

/**
 * @brief Show all supported virtual keys.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated forwarder information.
 */
static char *showVirtualKeys(char *buf, size_t *bufSize, int example)
{
    char *msg;

    str2Buf("\n# available keys #\n\n", &buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "config",
	    "show current configuration");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "jobs",
	    "show all jobs");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "job-id",
	    "show detailed information about a selected job");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "state",
	    "show internal state");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "connections",
	    "show all connections");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "rjobs",
	    "show all remote jobs");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "forwarder",
	    "show running forwarder");
    str2Buf(line, &buf, bufSize);

    snprintf(line, sizeof(line), "%12s\t%s\n", "statistic",
	    "show statistic");
    str2Buf(line, &buf, bufSize);

    if (example) {
	msg = "\nExample:\nUse 'plugin psmom show 1001.frontend' or"
	    " 'plugin show psmom 1001'\nto display detailed job information"
	    " about job '1001.frontend'.\n";

	str2Buf(msg, &buf, bufSize);
    }

    return buf;
}

char *set(char *key, char *value)
{
    const ConfDef_t *thisConfDef = getConfigDef(key, confDef);
    char *buf = NULL;
    size_t bufSize = 0;

    /* search in config for given key */
    if (thisConfDef) {
	int verRes = verifyConfigEntry(confDef, key, value);

	if (!(strcmp(key, "PBS_SERVER"))) {
	    return NULL;
	} else if (!(strcmp(key, "PORT_SERVER"))) {
	    return NULL;
	} else if (!(strcmp(key, "PORT_MOM"))) {
	    return NULL;
	} else if (!(strcmp(key, "PORT_RM"))) {
	    return NULL;
	} else if (!(strcmp(key, "TORQUE_VERSION"))) {
	    return str2Buf("\nInvalid request: changing torque version is not"
			   " possible without a restart\n", &buf, &bufSize);
	} else if (!(strcmp(key, "DEBUG_MASK"))) {
	    int32_t mask;

	    if ((sscanf(value, "%i", &mask)) != 1) {
		return str2Buf("\nInvalid debug mask: not a number\n", &buf,
				&bufSize);
	    }
	    maskLogger(mask);
	}

	if (verRes) {
	    if (verRes == 1) {
		str2Buf("\nInvalid key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set : use 'plugin help psmom' foor help.\n",
			&buf, &bufSize);
	    } else if (verRes == 2) {
		str2Buf("\nThe key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set has to be numeric.\n", &buf, &bufSize);
	    }
	} else {
	    /* save new config value */
	    addConfigEntry(&config, key, value);

	    snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	    str2Buf(line, &buf, &bufSize);
	}
    } else if (!(strcmp(key, "statistic"))) {
	if (!(strcmp(value, "0"))) {
	    stat_batchJobs = 0;
	    stat_interJobs = 0;
	    stat_successBatchJobs = 0;
	    stat_successInterJobs = 0;
	    stat_failedBatchJobs = 0;
	    stat_failedInterJobs = 0;
	    stat_remoteJobs = 0;
	    stat_lPrologue = 0;
	    stat_rPrologue = 0;
	    stat_failedrPrologue = 0;
	    stat_failedlPrologue = 0;
	    stat_numNodes = 0;
	    stat_numProcs = 0;

	    str2Buf("\nReset statistics\n", &buf, &bufSize);
	} else {
	    str2Buf("\nInvalid statistic command\n", &buf, &bufSize);
	}
    } else if (!(strcmp(key, "memdebug"))) {
	if (memoryDebug) fclose(memoryDebug);

	if ((memoryDebug = fopen(value, "w+"))) {
	    finalizePluginLogger();
	    initPluginLogger(NULL, memoryDebug);
	    maskPluginLogger(PLUGIN_LOG_MALLOC);
	    str2Buf("\nmemory logging to '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    str2Buf("'\n", &buf, &bufSize);
	} else {
	    str2Buf("\nopening file '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    str2Buf("' for writing failed\n", &buf, &bufSize);
	}
    } else {
	str2Buf("\nInvalid key '", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf("' for cmd set : use 'plugin help psmom' for help.\n", &buf,
		&bufSize);
    }

    return buf;
}

char *unset(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (getConfValueC(&config, key)) {
	unsetConfigEntry(&config, confDef, key);
    } else if (!(strcmp(key, "memdebug"))) {
	if (memoryDebug) {
	    finalizePluginLogger();
	    fclose(memoryDebug);
	    memoryDebug = NULL;
	    initPluginLogger(NULL, psmomlogfile);
	}
	str2Buf("Stopped memory debugging\n", &buf, &bufSize);
    } else {
	str2Buf("\nInvalid key '", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf("' for cmd unset : use 'plugin help psmom' for help.\n",
		&buf, &bufSize);
    }

    return buf;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int maxKeyLen = getMaxKeyLen(confDef);
    int i;

    str2Buf("\nThe psmom is a complete replacement of the Torque pbs_mom."
	    " Using the psmom plug-in the psid\ntherefore is the only"
	    " daemon needed on the compute nodes to integrate them into"
	    " the batch system.\n", &buf, &bufSize);

    str2Buf("\n# configuration options #\n\n", &buf, &bufSize);
    for (i = 0; confDef[i].name; i++) {
	char type[10];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%*s %8s  %s\n", maxKeyLen+2,
		 confDef[i].name, type, confDef[i].desc);
	str2Buf(line, &buf, &bufSize);
    }

    buf = showVirtualKeys(buf, &bufSize, 0);

    str2Buf("\nUse 'plugin show psmom [key name]' to view the"
	    " current configuration or internal informations.\n"
	    "To change the configuration use 'plugin set psmom"
	    " <name> <value>'.\n"
	    "To unset a configuration value use 'plugin unset psmom"
	    " <name>'.\n"
	    "To reset the statistic use 'plugin set psmom statistic 0'"
	    ".\n", &buf, &bufSize);

    return buf;
}

char *show(char *key)
{
    Job_t *job;
    char *buf = NULL, *tmp;
    size_t bufSize = 0;

    if (!key) {
	/* show all virtual keys */
	buf = showVirtualKeys(buf, &bufSize, 1);

	return buf;
    }

    /* search in config for given key */
    tmp = getConfValueC(&config, key);
    if (tmp) {
	str2Buf(key, &buf, &bufSize);
	str2Buf(" = ", &buf, &bufSize);
	str2Buf(tmp, &buf, &bufSize);
	str2Buf("\n", &buf, &bufSize);

	return buf;
    }

    /* search for "virtual" values for given key */

    /* show current remote jobs */
    if (!strcmp(key, "rjobs")) {
	return listJobInfos(buf, &bufSize);
    }

    /* show running forwarders */
    if (!strcmp(key, "forwarder")) {
	return showForwarder(buf, &bufSize);
    }

    /* show current connections */
    if (!strcmp(key, "connections")) {
	return showConnectionState(buf, &bufSize);
    }

    /* show state information */
    if (!strcmp(key, "state")) {
	return showState(buf, &bufSize);
    }

    /* show current jobs */
    if (!strcmp(key, "jobs")) {
	return listJobs(buf, &bufSize);
    }

    /* show current config */
    if (!strcmp(key, "config")) {
	return showConfig(buf, &bufSize);
    }

    /* show statistic */
    if (!strcmp(key, "statistic")) {
	return showStatistic(buf, &bufSize);
    }

    /* check for allowed pid */
    if (!strncmp(key, "allowed_", 8)) {
	return showAllowedPid(key, buf, &bufSize);
    }

    /* job-id: show detailed job info */
    if ((job = findJobById(key)) || (job = findJobByShortId(key))) {
	return showJob(job, buf, &bufSize);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("' for cmd show : use 'plugin help psmom'.\n", &buf, &bufSize);

    return buf;
}
