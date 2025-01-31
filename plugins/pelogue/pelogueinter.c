/*
 * ParaStation
 *
 * Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pelogueinter.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

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
	flog("failed to create config\n");
	goto ERROR;
    }

    char *val = getConfValueC(configList, "TIMEOUT_PROLOGUE");
    if (!val) {
	flog("invalid prologue timeout\n");
	goto ERROR;
    }
    addConfigEntry(config, "TIMEOUT_PROLOGUE", val);

    val = getConfValueC(configList, "TIMEOUT_EPILOGUE");
    if (!val) {
	flog("invalid epilogue timeout\n");
	goto ERROR;
    }
    addConfigEntry(config, "TIMEOUT_EPILOGUE", val);

    val = getConfValueC(configList, "TIMEOUT_PE_GRACE");
    if (!val) {
	flog("invalid grace timeout\n");
	goto ERROR;
    }
    addConfigEntry(config, "TIMEOUT_PE_GRACE", val);

    val = getConfValueC(configList, "DIR_PROLOGUE");
    if (val) addConfigEntry(config, "DIR_PROLOGUE", val);

    val = getConfValueC(configList, "DIR_EPILOGUE");
    if (val) addConfigEntry(config, "DIR_EPILOGUE", val);

    val = getConfValueC(configList, "DIR_EPILOGUE_FINALIZE");
    if (val) addConfigEntry(config, "DIR_EPILOGUE_FINALIZE", val);

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
		      env_t env)
{
    Job_t *job = findJobById(plugin, jobid);
    if (!job) {
	flog("no job %s for plugin %s\n", jobid, plugin);
	return false;
    }

    if (type == PELOGUE_PROLOGUE) {
	job->state = JOB_PROLOGUE;
	job->prologueTrack = job->numNodes;
    } else {
	job->state = JOB_EPILOGUE;
	job->epilogueTrack = job->numNodes;
    }
    sendPElogueStart(job, type, env);

    return true;
}

bool psPelogueSignalPE(const char *plugin, const char *jobid, int sig,
		      char *reason)
{
    Job_t *job = findJobById(plugin, jobid);
    if (!job) {
	flog("no job %s for plugin %s\n", jobid, plugin);
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

bool psPelogueCallPE(PElogueAction_t peAction, Config_t conf, env_t env)
{
    char *script = getMasterScript();
    if (!script) {
	flog("no masterscript?!\n");
	return false;
    }
    char *ddir = getDDir(peAction, conf);
    if (!ddir) {
	flog("no .d directory for action %s\n", getPEActStr(peAction));
	return false;
    }

    char *argv[3] = { script, ddir, NULL };
    execve(script, argv, envGetArray(env));

    return false;
}
