/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pscommon.h"

#include "peloguejob.h"
#include "peloguecomm.h"
#include "peloguelog.h"

#include "pelogueinter.h"

int psPelogueAddPluginConfig(char *name, Config_t *configList)
{
    return addPluginConfig(name, configList);
}

int psPelogueDelPluginConfig(char *name)
{
    return delPluginConfig(name);
}

int psPelogueAddJob(const char *plugin, const char *jobid, uid_t uid, gid_t gid,
		    int nrOfNodes, PSnodes_ID_t *nodes,
		    Pelogue_JobCb_Func_t *pluginCallback)
{
    if (nrOfNodes > PSC_getNrOfNodes()) {
	mlog("%s: invalid nrOfNodes '%u'\n", __func__, nrOfNodes);
	return 1;
    }

    if (!plugin || !jobid) {
	mlog("%s: invalid plugin '%s' or jobid '%s'\n", __func__,
		plugin, jobid);
	return 1;
    }

    if (!nodes) {
	mlog("%s: invalid nodes\n", __func__);
	return 1;
    }

    if (!pluginCallback) {
	mlog("%s: invalid plugin callback\n", __func__);
	return 1;
    }

    addJob(plugin, jobid, uid, gid, nrOfNodes, nodes, pluginCallback);
    return 0;
}

void psPelogueDeleteJob(const char *plugin, const char *jobid)
{
    Job_t *job = findJobById(plugin, jobid);

    if (!job) return;

    deleteJob(job);
}

int psPelogueStartPE(const char *plugin, const char *jobid, PElogueType_t type,
		     int rounds, env_t *env)
{
    Job_t *job = findJobById(plugin, jobid);

    if (!job) {
	mlog("%s: job '%s' for plugin '%s' not found\n", __func__, jobid,
		plugin);
	return 0;
    }
    if (type == PELOGUE_PROLOGUE) {
	job->state = JOB_PROLOGUE;
	job->prologueTrack = job->nrOfNodes;
    } else {
	job->state = JOB_EPILOGUE;
	job->epilogueTrack = job->nrOfNodes;
    }
    sendPElogueStart(job, type, rounds, env);

    return 1;
}

int psPelogueSignalPE(const char *plugin, const char *jobid, int signal,
		      char *reason)
{
    Job_t *job = findJobById(plugin, jobid);

    if (!job) {
	mlog("%s: no job %s for plugin %s\n", __func__, jobid, plugin);
	return 0;
    }

    sendPElogueSignal(job, signal, reason);

    return 1;
}
