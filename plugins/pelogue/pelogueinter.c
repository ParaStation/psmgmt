/*
 * ParaStation
 *
 * Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pelogueinter.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "pscommon.h"
#include "psenv.h"
#include "pluginconfig.h"

#include "pelogueconfig.h"
#include "peloguejob.h"
#include "peloguecomm.h"
#include "peloguelog.h"

bool psPelogueAddPluginConfig(char *name, Config_t configList)
{
    Config_t config = NULL;
    if (!initConfig(&config)) {
	mlog("%s: failed to create config\n", __func__);
	goto ERROR;
    }

    char *val = getConfValueC(configList, "TIMEOUT_PROLOGUE");
    if (!val) {
	mlog("%s: invalid prologue timeout\n", __func__);
	goto ERROR;
    }
    addConfigEntry(config, "TIMEOUT_PROLOGUE", val);

    val = getConfValueC(configList, "TIMEOUT_EPILOGUE");
    if (!val) {
	mlog("%s: invalid epilogue timeout\n", __func__);
	goto ERROR;
    }
    addConfigEntry(config, "TIMEOUT_EPILOGUE", val);

    val = getConfValueC(configList, "TIMEOUT_PE_GRACE");
    if (!val) {
	mlog("%s: invalid grace timeout\n", __func__);
	goto ERROR;
    }
    addConfigEntry(config, "TIMEOUT_PE_GRACE", val);

    char *scriptDir = getConfValueC(configList, "DIR_SCRIPTS");
    if (!scriptDir) {
	mlog("%s: invalid scripts directory\n", __func__);
	goto ERROR;
    }
    addConfigEntry(config, "DIR_SCRIPTS", scriptDir);

    return addPluginConfig(name, config);

ERROR:
    freeConfig(config);
    return false;
}

bool psPelogueDelPluginConfig(char *name)
{
    return delPluginConfig(name);
}

bool psPelogueAddJob(const char *plugin, const char *jobid, uid_t uid,
		     gid_t gid, int numNodes, PSnodes_ID_t *nodes,
		     PElogueJobCb_t *cb, void *info, bool fwStdOE)
{
    return addJob(plugin, jobid, uid, gid, numNodes, nodes, cb, info, fwStdOE);
}

bool psPelogueStartPE(const char *plugin, const char *jobid, PElogueType_t type,
		      int rounds, env_t env)
{
    Job_t *job = findJobById(plugin, jobid);

    if (!job) {
	mlog("%s: no job %s for plugin %s\n", __func__, jobid, plugin);
	return false;
    }
    if (type == PELOGUE_PROLOGUE) {
	job->state = JOB_PROLOGUE;
	job->prologueTrack = job->numNodes;
    } else {
	job->state = JOB_EPILOGUE;
	job->epilogueTrack = job->numNodes;
    }
    sendPElogueStart(job, type, rounds, env);

    return true;
}

bool psPelogueSignalPE(const char *plugin, const char *jobid, int sig,
		      char *reason)
{
    Job_t *job = findJobById(plugin, jobid);

    if (!job) {
	mlog("%s: no job %s for plugin %s\n", __func__, jobid, plugin);
	return false;
    }
    sendPElogueSignal(job, sig, reason);

    return true;
}

void psPelogueDeleteJob(const char *plugin, const char *jobid)
{
    Job_t *job = findJobById(plugin, jobid);

    if (job) deleteJob(job);
}
