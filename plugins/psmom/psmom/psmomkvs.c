/*
 * ParaStation
 *
 * Copyright (C) 2011-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomkvs.h"

#include <pwd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include "list.h"
#include "pscommon.h"
#include "psstrbuf.h"

#include "plugin.h"
#include "pluginconfig.h"
#include "pluginlog.h"
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
 * @brief Save detailed job information into an output buffer
 *
 * @param job The job structure which holds all the information to save
 *
 * @return Returns the buffer with the job information
 */
static char *showJob(Job_t *job)
{
    strbuf_t buf = strbufNew(NULL);

    snprintf(line, sizeof(line), "\n## job '%s' ##\n", job->id);
    strbufAdd(buf, line);

    strbufAdd(buf, "\n# psmom attributes #\n");

    snprintf(line, sizeof(line), "user = '%s'\n", job->user);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "nr of procs = '%i'\n", job->nrOfNodes);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "nr of nodes = '%i'\n", job->nrOfUniqueNodes);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "server = '%s'\n", job->server);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "state = '%s'\n", jobState2String(job->state));
    strbufAdd(buf, line);

    if (job->pwbuf) {
	snprintf(line, sizeof(line), "uid = '%i'\n", job->passwd.pw_uid);
	strbufAdd(buf, line);

	snprintf(line, sizeof(line), "gid = '%i'\n", job->passwd.pw_gid);
	strbufAdd(buf, line);
    }

    snprintf(line, sizeof(line), "start time = %s", ctime(&job->start_time));
    strbufAdd(buf, line);

    if (job->end_time > 0) {
	snprintf(line, sizeof(line), "end time = %s", ctime(&job->end_time));
	strbufAdd(buf, line);
    }

    if (job->pid != -1) {
	snprintf(line, sizeof(line), "child pid = '%i'\n", job->pid);
	strbufAdd(buf, line);
    }

    if (job->sid != -1) {
	snprintf(line, sizeof(line), "child sid = '%i'\n", job->pid);
	strbufAdd(buf, line);
    }

    if (job->mpiexec != -1) {
	snprintf(line, sizeof(line), "mpiexec pid = '%i'\n", job->mpiexec);
	strbufAdd(buf, line);
    }

    snprintf(line, sizeof(line), "job cookie = '%s'\n", job->cookie);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "prologue exit = '%i'\n", job->prologueExit);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "epilogue exit = '%i'\n", job->epilogueExit);
    strbufAdd(buf, line);

    if (job->qsubPort != 0) {
	strbufAdd(buf, "interactive = true\n");

	snprintf(line, sizeof(line), "qsub port = '%i'\n", job->qsubPort);
	strbufAdd(buf, line);
    } else {
	strbufAdd(buf, "interactive = false\n");
    }

    /* display saved pbs attributes */
    strbufAdd(buf, "\n# pbs attributes #\n");

    list_t *e;
    list_for_each(e, &job->data.list) {
	Data_Entry_t *entry = list_entry(e, Data_Entry_t, list);
	if (!entry->name || *entry->name == '\0') break;

	if (entry->resource) {
	    snprintf(line, sizeof(line), "%s:%s = %s\n", entry->name,
		     entry->resource, entry->value);
	} else {
	    snprintf(line, sizeof(line), "%s = %s\n", entry->name, entry->value);
	}
	strbufAdd(buf, line);
    }

    /* display known accouting data */
    strbufAdd(buf, "\n# accounting information #\n");

    list_for_each(e, &job->status.list) {
	Data_Entry_t *entry = list_entry(e, Data_Entry_t, list);
	if (!entry->name || *entry->name == '\0') break;

	if (entry->resource) {
	    snprintf(line, sizeof(line), "%s:%s = %s\n", entry->name,
		     entry->resource, entry->value);
	} else {
	    snprintf(line, sizeof(line), "%s = %s\n", entry->name, entry->value);
	}
	strbufAdd(buf, line);
    }

    /* display running job child */
    Child_t *child = findChildByJobid(job->id, -1);
    if (child) {
	time_t max_runtime;

	strbufAdd(buf, "\n# forwarder information #\n");

	snprintf(line, sizeof(line), "pid = '%i'\n", child->pid);
	strbufAdd(buf, line);

	snprintf(line, sizeof(line), "type = '%s'\n",
		childType2String(child->type));
	strbufAdd(buf, line);

	snprintf(line, sizeof(line), "child pid = '%i'\n", child->c_pid);
	strbufAdd(buf, line);

	snprintf(line, sizeof(line), "child sid = '%i'\n", child->c_sid);
	strbufAdd(buf, line);

	snprintf(line, sizeof(line), "start time = %s",
			    ctime(&child->start_time.tv_sec));
	strbufAdd(buf, line);

	if (child->fw_timeout > 0) {
	    max_runtime = child->start_time.tv_sec + child->fw_timeout;
	    snprintf(line, sizeof(line), "timeout = %s", ctime(&max_runtime));
	}
	strbufAdd(buf, line);
    }

    return strbufSteal(buf);
}

/**
 * @brief Show psmom state information
 *
 * @return Returns the buffer with the updated state information
 */
static char *showState(void)
{
    strbuf_t buf = strbufNew("\n");

    updateInfoList(1);
    list_t *e;
    list_for_each(e, &infoData.list) {
	Data_Entry_t *entry = list_entry(e, Data_Entry_t, list);
	if (!entry->name || *entry->name == '\0') break;

	strbufAdd(buf, entry->value);
	strbufAdd(buf, "\n");
    }
    clearDataList(&infoData.list);

    list_for_each(e, &staticInfoData.list) {
	Data_Entry_t *entry = list_entry(e, Data_Entry_t, list);
	if (!entry->name || *entry->name == '\0') break;

	strbufAdd(buf, entry->value);
	strbufAdd(buf, "\n");
    }

    return strbufSteal(buf);
}

/**
 * @brief Show psmom connection information
 *
 * @return Returns the buffer with the updated connection information
 */
static char *showConnectionState(void)
{
    strbuf_t buf = strbufNew(NULL);
    snprintf(line, sizeof(line), "\nsocket\ttype\t%20s\t%20s\tinfo\n",
		"local", "remote");
    strbufAdd(buf, line);

    if (masterSocket != -1) {
	snprintf(line, sizeof(line), "%i\t%s\t%20s\t%20s\t%s\n", masterSocket,
		"UNIX", "0.0.0.0:*", "0.0.0.0:*", masterSocketName);
	strbufAdd(buf, line);
    }

    list_t *c;
    list_for_each(c, &ComList.list) {
	ComHandle_t *com = list_entry(c, ComHandle_t, list);
	char *info = com->jobid ? com->jobid : NULL;
	if (!info) info = com->info ? com->info : "";

	/* print local addr */
	char lport[8];
	if (com->localPort == -1) {
	    snprintf(lport, sizeof(lport), "*");
	} else {
	    snprintf(lport, sizeof(lport), "%i", com->localPort);
	}
	char local[32];
	snprintf(local, sizeof(local), "%s:%s",
		    com->addr[0] ? com->addr : "0.0.0.0", lport);

	/* print remote addr */
	char rport[8];
	if (com->remotePort == -1) {
	    snprintf(rport, sizeof(rport), "*");
	} else {
	    snprintf(rport, sizeof(rport), "%i", com->remotePort);
	}
	char remote[32];
	snprintf(remote, sizeof(remote), "%s:%s",
		    com->remoteAddr[0] ? com->remoteAddr : "0.0.0.0",
		    rport);

	snprintf(line, sizeof(line), "%i\t%s\t%20s\t%20s\t%s\n", com->socket,
		protocolType2String(com->type), local, remote, info);
	strbufAdd(buf, line);
    }

    return strbufSteal(buf);
}

/**
 * @brief Show current forwarders
 *
 * @return Returns the buffer with the updated forwarder information
 */
static char *showForwarder(void)
{
    strbuf_t buf = strbufNew(NULL);

    if (list_empty(&ChildList.list)) {
	strbufAdd(buf, "\nCurrently no running forwarders.\n");
    } else {
	snprintf(line, sizeof(line), "\n%12s\t%19s\t%19s\tPID\tChild PID"
		 "\tChild SID\n", "Type", "Starttime", "Timeout");
	strbufAdd(buf, line);

	list_t *c;
	list_for_each(c, &ChildList.list) {
	    Child_t *child = list_entry(c, Child_t, list);

	    /* format start time */
	    struct tm *ts = localtime(&child->start_time.tv_sec);
	    char start[50];
	    strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", ts);

	    /* format timeout */
	    char timeout[50];
	    formatTimeout(child->start_time.tv_sec, child->fw_timeout, timeout,
			  sizeof(timeout));

	    snprintf(line, sizeof(line), "%12s\t%s\t%s\t%i\t%i\t\t%i\n",
		     childType2String(child->type),
		     start, timeout, child->pid, child->c_pid, child->c_sid);

	    strbufAdd(buf, line);
	}
    }
    return strbufSteal(buf);
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
static char *showAllowedPid(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    pid_t pid;
    if (sscanf(key, "allowed_%i", &pid) != 1) {
	strbufAdd(buf, "\nInvalid pid: not a number\n");
	return strbufSteal(buf);
    }

    pid_t sid = getSIDforPID(pid);

    int found = 0;
    char *reason = NULL;
    list_t *c;
    list_for_each(c, &ChildList.list) {
	Child_t *child = list_entry(c, Child_t, list);
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
    PStask_t *task = PStasklist_find(&managedTasks, PSC_getTID(-1, pid));
    PStask_ID_t psAccLogger;
    if (task) {
	psAccLogger = task->loggertid;
    } else {
	psAccLogger = psAccountGetLoggerByClient(pid);
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
    strbufAdd(buf, line);

    if (found) {
	strbufAdd(buf, "TRUE ");
	strbufAdd(buf, reason);
	strbufAdd(buf, "\n");
    } else {
	strbufAdd(buf, "FALSE\n");
    }
    return strbufSteal(buf);
}

/**
 * @brief Show internal statistic
 *
 * @return Returns the buffer with the updated configuration information
 */
static char *showStatistic(void)
{
    strbuf_t buf = strbufNew("\n# statistic #\n\n");
    snprintf(line, sizeof(line), "executed batch jobs\t\t%u\n", stat_batchJobs);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "executed interactive jobs\t%u\n",
	     stat_interJobs);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "successful batch jobs\t\t%u\n",
	     stat_successBatchJobs);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "successful interactive jobs\t%u\n",
		stat_successInterJobs);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "failed batch jobs\t\t%u\n",
		stat_failedBatchJobs);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "failed interactive jobs\t\t%u\n",
		stat_failedInterJobs);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "successful local prologue\t%u\n",
		stat_lPrologue);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "successful remote prologue\t%u\n",
		stat_rPrologue);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "failed local prologue\t\t%u\n",
		stat_failedlPrologue);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "failed remote prologue\t\t%u\n",
		stat_failedrPrologue);
    strbufAdd(buf, line);

    uint64_t stat_allJobs = stat_batchJobs + stat_interJobs;
    if (stat_allJobs > 0) {
	snprintf(line, sizeof(line), "average nodes\t\t\t%lu\n",
		    stat_numNodes / stat_allJobs);
	strbufAdd(buf, line);

	snprintf(line, sizeof(line), "average processes\t\t%lu\n",
		    stat_numProcs / stat_allJobs);
	strbufAdd(buf, line);
    }

    snprintf(line, sizeof(line), "executed remote jobs\t\t%u\n",
		stat_remoteJobs);
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "psmom start time\t\t%s\n",
		ctime(&stat_startTime));
    strbufAdd(buf, line);

    return strbufSteal(buf);
}

/**
 * @brief Show current configuration
 *
 * Print the current configuration of the plugin to a buffer.
 *
 * @return Returns the buffer with the updated configuration information
 */
static char *showConfig(void)
{
    int maxKeyLen = getMaxKeyLen(confDef);

    strbuf_t buf = strbufNew("\n");
    for (int i = 0; confDef[i].name; i++) {
	char *cName = confDef[i].name;
	char *cVal = getConfValueC(config, cName);

	snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, cVal);
	strbufAdd(buf, line);
    }

    return strbufSteal(buf);
}

/**
 * @brief Show all supported virtual keys
 *
 * @param buf String buffer to add information to
 *
 * @return No return value
 */
static void showVirtualKeys(strbuf_t buf, int example)
{
    strbufAdd(buf, "\n# available keys #\n\n");

    snprintf(line, sizeof(line), "%12s\t%s\n", "config",
	     "show current configuration");
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "%12s\t%s\n", "jobs", "show all jobs");
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "%12s\t%s\n", "job-id",
	     "show detailed information about a selected job");
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "%12s\t%s\n", "state", "show internal state");
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "%12s\t%s\n", "connections",
	     "show all connections");
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "%12s\t%s\n", "rjobs", "show all remote jobs");
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "%12s\t%s\n", "forwarder",
	     "show running forwarder");
    strbufAdd(buf, line);

    snprintf(line, sizeof(line), "%12s\t%s\n", "statistic", "show statistic");
    strbufAdd(buf, line);

    if (example) {
	strbufAdd(buf, "\nExample:\nUse 'plugin psmom show 1001.frontend' or"
		  " 'plugin show psmom 1001'\nto display detailed job"
		  " information about job '1001.frontend'.\n");
    }
}

char *set(char *key, char *value)
{
    strbuf_t buf = strbufNew(NULL);

    /* search in config for given key */
    const ConfDef_t *thisConfDef = getConfigDef(key, confDef);
    if (thisConfDef) {
	int verRes = verifyConfigEntry(confDef, key, value);

	if (!strcmp(key, "PBS_SERVER")) {
	    // do nothing
	} else if (!strcmp(key, "PORT_SERVER")) {
	    // do nothing
	} else if (!strcmp(key, "PORT_MOM")) {
	    // do nothing
	} else if (!strcmp(key, "PORT_RM")) {
	    // do nothing
	} else if (!strcmp(key, "TORQUE_VERSION")) {
	    strbufAdd(buf, "\nInvalid request: changing torque version is not"
		      " possible without a restart\n");
	} else if (!strcmp(key, "DEBUG_MASK")) {
	    int32_t mask;

	    if (sscanf(value, "%i", &mask) != 1) {
		strbufAdd(buf, "\nInvalid debug mask: not a number\n");
	    }
	    maskLogger(mask);
	} else if (verRes) {
	    if (verRes == 1) {
		strbufAdd(buf, "\nInvalid key '");
		strbufAdd(buf, key);
		strbufAdd(buf, "' for cmd set : use 'plugin help psmom' for help.\n");
	    } else if (verRes == 2) {
		strbufAdd(buf, "\nThe key '");
		strbufAdd(buf, key);
		strbufAdd(buf, "' for cmd set has to be numeric.\n");
	    }
	} else {
	    /* save new config value */
	    addConfigEntry(config, key, value);

	    snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	    strbufAdd(buf, line);
	}
    } else if (!strcmp(key, "statistic")) {
	if (!strcmp(value, "0")) {
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

	    strbufAdd(buf, "\nReset statistics\n");
	} else {
	    strbufAdd(buf, "\nInvalid statistic command\n");
	}
    } else if (!strcmp(key, "memdebug")) {
	if (memoryDebug) fclose(memoryDebug);

	if ((memoryDebug = fopen(value, "w+"))) {
	    finalizePluginLogger();
	    initPluginLogger(NULL, memoryDebug);
	    maskPluginLogger(PLUGIN_LOG_MALLOC);
	    strbufAdd(buf, "\nmemory logging to '");
	    strbufAdd(buf, value);
	    strbufAdd(buf, "'\n");
	} else {
	    strbufAdd(buf, "\nopening file '");
	    strbufAdd(buf, value);
	    strbufAdd(buf, "' for writing failed\n");
	}
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd set : use 'plugin help psmom' for help.\n");
    }

    return strbufSteal(buf);
}

char *unset(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    if (getConfValueC(config, key)) {
	unsetConfigEntry(config, confDef, key);
    } else if (!strcmp(key, "memdebug")) {
	if (memoryDebug) {
	    finalizePluginLogger();
	    fclose(memoryDebug);
	    memoryDebug = NULL;
	    initPluginLogger(NULL, psmomlogfile);
	}
	strbufAdd(buf, "Stopped memory debugging\n");
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd unset : use 'plugin help psmom' for help.\n");
    }

    return strbufSteal(buf);
}

char *help(char *key)
{
    int maxKeyLen = getMaxKeyLen(confDef);

    strbuf_t buf = strbufNew(NULL);
    strbufAdd(buf, "\nThe psmom is a complete replacement of the Torque pbs_mom."
	    " Using the psmom plug-in the psid\ntherefore is the only"
	    " daemon needed on the compute nodes to integrate them into"
	    " the batch system.\n");

    strbufAdd(buf, "\n# configuration options #\n\n");
    for (int i = 0; confDef[i].name; i++) {
	char type[10];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%*s %8s  %s\n", maxKeyLen+2,
		 confDef[i].name, type, confDef[i].desc);
	strbufAdd(buf, line);
    }

    showVirtualKeys(buf, 0);

    strbufAdd(buf, "\nUse 'plugin show psmom [key name]' to view the"
	      " current configuration or internal informations.\n"
	      "To change the configuration use 'plugin set psmom"
	      " <name> <value>'.\n"
	      "To unset a configuration value use 'plugin unset psmom <name>'.\n"
	      "To reset the statistic use 'plugin set psmom statistic 0'.\n");

    return strbufSteal(buf);
}

char *show(char *key)
{
    if (!key) {
	/* show all virtual keys */
	strbuf_t buf = strbufNew(NULL);
	showVirtualKeys(buf, 1);
	return strbufSteal(buf);
    }

    /* search in config for given key */
    char *tmp = getConfValueC(config, key);
    if (tmp) {
	strbuf_t buf = strbufNew(NULL);
	strbufAdd(buf, key);
	strbufAdd(buf, " = ");
	strbufAdd(buf, tmp);
	strbufAdd(buf, "\n");

	return strbufSteal(buf);
    }

    /* search for "virtual" values for given key */

    /* show current remote jobs */
    if (!strcmp(key, "rjobs")) return listJobInfos();

    /* show running forwarders */
    if (!strcmp(key, "forwarder")) return showForwarder();

    /* show current connections */
    if (!strcmp(key, "connections")) return showConnectionState();

    /* show state information */
    if (!strcmp(key, "state")) return showState();

    /* show current jobs */
    if (!strcmp(key, "jobs")) return listJobs();

    /* show current config */
    if (!strcmp(key, "config")) return showConfig();

    /* show statistic */
    if (!strcmp(key, "statistic")) return showStatistic();

    /* check for allowed pid */
    if (!strncmp(key, "allowed_", 8)) return showAllowedPid(key);

    /* job-id: show detailed job info */
    Job_t *job = findJobById(key);
    if (!job) job = findJobByShortId(key);
    if (job) return showJob(job);

    strbuf_t buf = strbufNew("\nInvalid key '");
    strbufAdd(buf, key);
    strbufAdd(buf, "' for cmd show : use 'plugin help psmom'.\n");

    return strbufSteal(buf);
}
