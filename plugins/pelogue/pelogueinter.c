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

#include "pelogueconfig.h"
#include "peloguejob.h"
#include "peloguecomm.h"
#include "peloguelog.h"

#include "pelogueinter.h"

bool psPelogueAddPluginConfig(char *name, Config_t *configList)
{
    return addPluginConfig(name, configList);
}

bool psPelogueDelPluginConfig(char *name)
{
    return delPluginConfig(name);
}

bool psPelogueAddJob(const char *plugin, const char *jobid, uid_t uid,
		     gid_t gid, int numNodes, PSnodes_ID_t *nodes,
		     PElogueJobCb_t *cb)
{
    if (numNodes > PSC_getNrOfNodes()) {
	mlog("%s: invalid numNodes '%u'\n", __func__, numNodes);
	return false;
    }

    if (!plugin || !jobid) {
	mlog("%s: invalid plugin %s or jobid %s\n", __func__, plugin, jobid);
	return false;
    }

    if (!nodes) {
	mlog("%s: invalid nodes\n", __func__);
	return false;
    }

    if (!cb) {
	mlog("%s: invalid plugin callback\n", __func__);
	return false;
    }

    return !!addJob(plugin, jobid, uid, gid, numNodes, nodes, cb);
}

bool psPelogueStartPE(const char *plugin, const char *jobid, PElogueType_t type,
		      int rounds, env_t *env)
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
