/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
 */
/**
 * \file
 * pbsnodefile.c: slurm plugin for setting up parastation environment
 *
 * $Id$
 *
 * \author
 * Peter Niessen <niessen@par-tec.com>
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <slurm/slurm.h>
#include <slurm/spank.h>

#include "pstask.h"
#include "psi.h"
#include "psprotocol.h"
#include "pspluginprotocol.h"
#include "plugincomm.h"
#include "pluginfrag.h"
#include "pluginmalloc.h"
#include "psslurmmsg.h"

#define MAX_NODES 300
#define MAX_TASKLIST_ENTRYS 8
#define ENV_BUF_SIZE 1024
#define MAX_NF_PATH 512
#define PSI_RECV_TIMEOUT 600

#define ENV_NODELIST "SLURM_NODELIST"
#define ENV_CPUS_PER_TASK "SLURM_CPUS_PER_TASK"
#define ENV_TASKS_PER_NODE "SLURM_TASKS_PER_NODE"

#define ENV_STEP_NODELIST "SLURM_STEP_NODELIST"
#define ENV_STEP_CPUS_PER_TASK "SLURM_STEP_CPUS_PER_TASK"
#define ENV_STEP_TASKS_PER_NODE "SLURM_STEP_TASKS_PER_NODE"

#define DEBUG 0

SPANK_PLUGIN (parastation, 31);

static const char *sCtx2String(int context)
{
    switch (context) {
	case S_CTX_ERROR:
	    return "error";
	case S_CTX_LOCAL:
	    return "local";
	case S_CTX_REMOTE:
	    return "remote";
	case S_CTX_ALLOCATOR:
	    return "allocator";
	case S_CTX_SLURMD:
	    return "slurmd";
	case S_CTX_JOB_SCRIPT:
	    return "jobscript";
    }
    return "unknown";
}

static void showContext(const char *func)
{
    slurm_info("%s: context '%s' uid '%i'", func, sCtx2String(spank_context()),
	    getuid());
}

static int getPbsTasksPerNode(spank_t sp, char *sTasksPerNode,
	char *sCpusPerTasks, uint *pbs_tasklist_num)
{
    char tasklist[ENV_BUF_SIZE]; /* SLURM_TASKS_PER_NODE */
    char pbs_tasklist[MAX_TASKLIST_ENTRYS][MAX_NODES]; /* parsed tasklist with the tasks per node */
    char str_cpus_per_task[ENV_BUF_SIZE];  /* SLURM_CPUS_PER_TASK */
    char *strtok_p, *save_p = NULL, *token;
    int n_tasklist = 0, i_tasklist;
    unsigned int i;
    uint i_node, tpn_sscanf, nodes_sscanf, cpus_task;
    spank_err_t rc;

    for (i = 0; i < MAX_NODES; i++) {
	pbs_tasklist_num[i] = 0;
    }

    rc = spank_getenv (sp, sCpusPerTasks, str_cpus_per_task,
	    sizeof(str_cpus_per_task));
    if (ESPANK_SUCCESS != rc) {
	if (DEBUG) slurm_debug ("%s: %s was not set.", __func__, sCpusPerTasks);
	cpus_task = 1;
    } else {
	if (sscanf (str_cpus_per_task, "%u", &cpus_task) != 1) {
	    cpus_task = 1;
	}
    }

    rc = spank_getenv (sp, sTasksPerNode, tasklist, sizeof(tasklist));
    if (ESPANK_SUCCESS != rc) {
	slurm_error ("%s: could not get %s:", __func__, sTasksPerNode);
	slurm_error ("%s", spank_strerror (rc));
	return -1;
    }

    /* first, find out how many tasks we have per node */
    strtok_p = tasklist;
    while ((token = strtok_r (strtok_p, ",", &save_p))) {
	if (n_tasklist >=  MAX_TASKLIST_ENTRYS) {
	    slurm_error ("%s: unsupported large tasklist.", __func__);
	    return -1;
	}
	strncpy (pbs_tasklist[n_tasklist], token, sizeof(pbs_tasklist[n_tasklist]));
	strtok_p = save_p;
	n_tasklist++;
    }

    /* now, pbs_tasklist contains the compacted particles of each task, */
    /* we have to match them in pbs_tasklist_num onto the nodes.        */
    i_node = 0;
    for (i_tasklist = 0; i_tasklist < n_tasklist; i_tasklist++) {

	/* check if it's a compact list */
	if (2 == sscanf (pbs_tasklist[i_tasklist], "%u(x%u)",
		    &tpn_sscanf, &nodes_sscanf)) {
	    /* loop over each node and copy the information into the next
	       node entry */
	    for (i = 0; i < nodes_sscanf; i++) {
		if (i_node >=  MAX_NODES) {
		    slurm_error ("%s: max supported nodes %i", __func__,
				    MAX_NODES);
		    return -1;
		}
		pbs_tasklist_num[i_node] = tpn_sscanf * cpus_task;
		i_node++;
	    }
	    continue;
	}
	/* check if it's a single number */
	if (1 == sscanf (pbs_tasklist[i_tasklist], "%u", &tpn_sscanf)) {
	    if (i_node >=  MAX_NODES) {
		slurm_error ("%s: max supported nodes %i", __func__, MAX_NODES);
		return -1;
	    }
	    pbs_tasklist_num[i_node] = tpn_sscanf * cpus_task;
	    i_node++;
	    continue;
	}
    }
    return 0;
}

/* FIXME: not available in plugin api */
extern char *hostlist_pop(hostlist_t hl);

/**
 * @brief Choose the next host to use.
 *
 * If we generate the hostlist for a srun job step it is important for SCR
 * to keep the rang assigment stable. For this we compare the nodelist of the
 * job with the nodelist of the jobstep. If we find nodes missing in the jobstep
 * nodelist, we will fill the gasp using the last nodes instead of the next node
 * in the list.
 */
static char *getNextHost(int jobStepID, hostlist_t *hl, hostlist_t *comphl)
{
    static char *nextHost = NULL, *nextCompHost = NULL, *saveHost = NULL, *tmp;

    if (jobStepID >= 0) {
	nextHost = saveHost ? saveHost : slurm_hostlist_shift(*hl);
	nextCompHost = slurm_hostlist_shift(*comphl);

	if (!nextHost || !nextCompHost) return NULL;

	if (!!strcmp(nextCompHost, nextHost)) {
	    saveHost = nextHost;
	    if (!(tmp = hostlist_pop(*hl))) {
		tmp = saveHost;
		saveHost = NULL;
	    }
	    return tmp;
	} else {
	    saveHost = NULL;
	    return nextHost;
	}
    } else {
	return slurm_hostlist_shift(*hl);
    }

    return NULL;
}

static int writePbsNodefile(spank_t sp, int jobStepID, char *sNodelist,
	char *pbs_filename, uint *pbs_tasklist_num)
{
    char nodelist[ENV_BUF_SIZE];
    FILE *pbs_nodefile;
    char *next_host;
    spank_err_t rc; /* to store the spank call return codes */
    uint i, i_node = 0;
    hostlist_t hl, comphl = NULL;

    rc = spank_getenv (sp, sNodelist, nodelist, sizeof(nodelist));
    if (ESPANK_SUCCESS != rc) {
	slurm_error ("%s: could not get %s:", __func__, sNodelist);
	slurm_error ("%s", spank_strerror (rc));
	return -1;
    }
    hl = slurm_hostlist_create (nodelist);

    if (jobStepID >= 0) {
	rc = spank_getenv (sp, ENV_NODELIST, nodelist, sizeof(nodelist));
	if (ESPANK_SUCCESS != rc) {
	    slurm_error ("%s: could not get %s:", __func__, ENV_NODELIST);
	    slurm_error ("%s", spank_strerror (rc));
	    return -1;
	}
	comphl = slurm_hostlist_create(nodelist);
    }

    /* make sure that /tmp/slurmd exists by saying mkdir -p */
    /* i.e. by ignoring the errors if the directory exists */
    mkdir ("/tmp/slurmd", S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);

    if (!(pbs_nodefile = fopen (pbs_filename, "w"))) {
	slurm_error ("%s: could not open PBS_NODEFLE %s", __func__,
			pbs_filename);
	return -1;
    }

    /* expand the node list in compressed form: j3c[001-004,050,060]     */
    /* -> j3c001,jc3002, ...                                             */
    while ((next_host = getNextHost(jobStepID, &hl, &comphl))) {
	if (i_node >=  MAX_NODES) {
	    slurm_error ("%s: max supported nodes %i", __func__, MAX_NODES);
	    return -1;
	}

	for (i=0; i < pbs_tasklist_num[i_node]; i++) {
	    fprintf (pbs_nodefile, "%s\n", next_host);
	}
	i_node++;
    }

    /* clean up */
    fclose (pbs_nodefile);
    slurm_hostlist_destroy (hl);
    if (jobStepID >= 0) slurm_hostlist_destroy(comphl);

    return 0;
}

static int sendPSQueueJob(spank_t sp, PS_DataBuffer_t *data, char *jobid)
{
    char hostlist[ENV_BUF_SIZE];
    spank_err_t rc;
    uint32_t tmp;

    /* add jobid */
    addStringToMsg(jobid, data);

    /* add uid */
    rc = spank_get_item(sp, S_JOB_UID, &tmp);
    if (rc != ESPANK_SUCCESS) {
	slurm_error("%s: getting uid failed: %s\n", __func__,
		    spank_strerror(rc));
	return 0;
    }
    addUint32ToMsg(tmp, data);

    /* add gid */
    rc = spank_get_item(sp, S_JOB_GID, &tmp);
    if (rc != ESPANK_SUCCESS) {
	slurm_error("%s: getting gid failed: %s\n", __func__,
		    spank_strerror(rc));
	return 0;
    }
    addUint32ToMsg(tmp, data);

    /* add hostlist to message */
    rc = spank_getenv(sp, ENV_NODELIST, hostlist, sizeof(hostlist));
    if (ESPANK_SUCCESS != rc) {
	slurm_error("%s: getting nodelist failed: %s\n", __func__,
		    spank_strerror(rc));
	return 0;
    }
    addStringToMsg(hostlist, data);

    /* send the message to psslurm */
    if ((sendFragMsg(data, PSC_getMyTID(), PSP_CC_PLUG_PSSLURM,
			    PSP_QUEUE)) == -1) {
	slurm_error("%s: sending PSI message failed : %s\n", __func__,
		    spank_strerror(errno));
	return 0;
    }

    return 1;
}

static void signalHandler(int sig)
{
    if (sig == SIGALRM) {
	slurm_error("%s: timeout (%i sec) receiving PSI msg\n",
			__func__, PSI_RECV_TIMEOUT);
	PSI_exitClient();
    }
}

static int sendPSPElogueStart(spank_t sp, PS_DataBuffer_t *data, char *jobid,
				int prologue)
{
    DDTypedBufferMsg_t answer;
    char **env, *ptr, buf[100];
    int count, i;
    spank_err_t rc;
    int32_t type, res;

    /* add jobid */
    addStringToMsg(jobid, data);

    /* add environment */
    rc = spank_get_item(sp, S_JOB_ENV, &env);
    if (ESPANK_SUCCESS != rc) {
	slurm_error("%s: getting environment failed: %s\n", __func__,
		    spank_strerror(rc));
	return 0;
    }

    count = 0;
    while (env[count]) count++;
    addInt32ToMsg(count, data);

    for (i=0; i<count; i++) {
	addStringToMsg(env[i], data);
    }

    /* send the message to psslurm */
    type = prologue ? PSP_PROLOGUE_START : PSP_EPILOGUE_START;
    if ((sendFragMsg(data, PSC_getMyTID(), PSP_CC_PLUG_PSSLURM, type)) == -1) {
	slurm_error("%s: sending PSI message failed : %s\n", __func__,
		    spank_strerror(errno));
	return 0;
    }


    signal(SIGALRM, signalHandler);
    alarm(PSI_RECV_TIMEOUT);

    /* wait till pelogue completes */
    if ((PSI_recvMsg((DDMsg_t *) &answer, sizeof(answer))) == -1) {
	slurm_error("%s: receiving PSI msg failed : %s\n", __func__,
			strerror(errno));
	return 0;
    }

    alarm(0);
    signal(SIGALRM, SIG_DFL);

    ptr = answer.buf;
    getString(&ptr, buf, sizeof(buf));
    getInt32(&ptr, &res);

    if (!!strcmp(jobid, buf)) {
	slurm_error("%s: invalid jobid '%s' should be '%s'\n", __func__,
		    buf, jobid);
	return 0;
    }

    /* job has finished, we need to delete it in psslurm */
    if (!prologue) {
	data->bufUsed = 0;

	addStringToMsg(jobid, data);
	sendFragMsg(data, PSC_getMyTID(), PSP_CC_PLUG_PSSLURM, PSP_DELETE);
    }

    if (res != 0) return 0;

    return 1;
}

static int startPSPrologue(spank_t sp)
{
    PS_DataBuffer_t data = { .buf = NULL};
    char sjobid[100];
    spank_err_t rc;
    uint32_t jobid;

    /* connect to local psid */
    if (!(PSI_initClient(TG_SERVICE))) {
	slurm_error ("%s: connecting to local psid failed\n", __func__);
	return 0;
    }
    setFragMsgFunc(PSI_sendMsg);

    /* get jobid */
    rc = spank_get_item(sp, S_JOB_ID, &jobid);
    if (rc != ESPANK_SUCCESS) {
	slurm_error("%s: getting jobid failed: %s\n", __func__,
		    spank_strerror(rc));
	return 0;
    }
    snprintf(sjobid, sizeof(sjobid), "%i", jobid);

    /* queue a new job */
    if (!(sendPSQueueJob(sp, &data, sjobid))) {
	ufree(data.buf);
	return 0;
    }
    data.bufUsed = 0;

    /* start the prologue */
    if (!(sendPSPElogueStart(sp, &data, sjobid, 1))) {
	ufree(data.buf);
	return 0;
    }
    ufree(data.buf);

    return 1;
}

int slurm_spank_init (spank_t sp, int ac, char **av)
{
    char pbs_filename[MAX_NF_PATH]; /* the file name for PBS_NODEFILE */
    char *sNodelist, *sTasksPerNode, *sCpusPerTasks;
    uint pbs_tasklist_num[MAX_NODES];
    int jobid, jobStepID, nodeid;
    spank_err_t rc; /* to store the spank call return codes */

    /* only run in remote context, otherwise getting the job id, etc. */
    /* will not work. */
    if (!spank_remote (sp)) return 0;

    if (DEBUG) showContext(__func__);

    if ((rc = spank_get_item(sp, S_JOB_ID, &jobid) != ESPANK_SUCCESS)
	    || jobid < 0) {
	slurm_error ("%s: could not get jobid '%i'", __func__, jobid);
	slurm_error ("%s", spank_strerror (rc));
	return -1;
    }

    if ((rc = spank_get_item(sp, S_JOB_STEPID, &jobStepID)) != ESPANK_SUCCESS) {
	slurm_error ("%s: could not get job step id", __func__);
	slurm_error ("%s", spank_strerror (rc));
	return -1;
    }

    if (jobStepID >= 0) {
	sNodelist = ENV_STEP_NODELIST;
	sTasksPerNode = ENV_STEP_TASKS_PER_NODE;
	sCpusPerTasks = ENV_STEP_CPUS_PER_TASK;
	snprintf (pbs_filename, sizeof(pbs_filename),
		"/tmp/slurmd/pbs_nodefile.%u.%u", jobid, jobStepID);
    } else {
	sNodelist = ENV_NODELIST;
	sTasksPerNode = ENV_TASKS_PER_NODE;
	sCpusPerTasks = ENV_CPUS_PER_TASK;
	snprintf (pbs_filename, sizeof(pbs_filename),
		"/tmp/slurmd/pbs_nodefile.%u", jobid);
    }

    /* get the number of tasks per node */
    if ((getPbsTasksPerNode(sp, sTasksPerNode, sCpusPerTasks,
		    pbs_tasklist_num)) == -1) {
	return -1;
    }

    /* write the nodefile */
    if ((writePbsNodefile(sp, jobStepID, sNodelist, pbs_filename,
		    pbs_tasklist_num)) == -1) {
	return -1;
    }

    /* need to use spank_setenv() because we are in remote context */
    if (spank_setenv (sp, "PBS_NODEFILE", pbs_filename, 1) != ESPANK_SUCCESS) {
	slurm_error ("%s: paraspank could not set the PBS_NODEFILE variable",
		__func__);
	return -1;
    }

    if ((rc = spank_get_item(sp, S_JOB_NODEID, &nodeid)) != ESPANK_SUCCESS) {
	slurm_error ("%s: could not get node id", __func__);
	slurm_error ("%s", spank_strerror (rc));
	return -1;
    }

    /* start prologue if we are the mother superior for this job */
    if (!nodeid) {
	if (!(startPSPrologue(sp))) return -1;
    }

    return 0;
}

int slurm_spank_slurmd_init (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_job_prolog (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_init_post_opt (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_local_user_init (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_user_init (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_task_init_privileged (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_task_post_fork (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_task_exit (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_job_epilog (spank_t sp, int ac, char **av)
{
    if (DEBUG) showContext(__func__);
    return 0;
}

int slurm_spank_exit (spank_t sp, int ac, char **av)
{
    PS_DataBuffer_t data = { .buf = NULL};
    int nodeid, jobid, jobStepID;
    char sjobid[100], pbs_filename[MAX_NF_PATH]; /* the file name for PBS_NODEFILE */
    spank_err_t rc;

    if (!spank_remote (sp)) return 0;

    if (DEBUG) showContext(__func__);

    /* only run in remote context, otherwise getting the job id, etc. */
    /* will not work. */
    if (!spank_remote (sp)) return 0;


    rc = spank_get_item (sp, S_JOB_ID, &jobid);
    if (ESPANK_SUCCESS != rc || jobid < 0) {
	slurm_error ("%s: could not get jobid '%i'", __func__, jobid);
	slurm_error ("%s", spank_strerror (rc));
	return -1;
    }

    if ((rc = spank_get_item(sp, S_JOB_STEPID, &jobStepID)) != ESPANK_SUCCESS) {
	slurm_error ("%s: could not get job step id", __func__);
	slurm_error ("%s", spank_strerror (rc));
	return -1;
    }

    if ((rc = spank_get_item(sp, S_JOB_NODEID, &nodeid)) != ESPANK_SUCCESS) {
	slurm_error ("%s: could not get node id", __func__);
	slurm_error ("%s", spank_strerror (rc));
	return -1;
    }

    /* start epilogue if we are the mother superior for this job */
    if (!nodeid) {
	snprintf(sjobid, sizeof(sjobid), "%i", jobid);
	if (!(sendPSPElogueStart(sp, &data, sjobid, 0))) {
	    ufree(data.buf);
	    return 0;
	}
	ufree(data.buf);

	/* close connection to local psid */
	PSI_exitClient();
    }

    if (jobStepID >= 0) {
	snprintf (pbs_filename, sizeof(pbs_filename),
		"/tmp/slurmd/pbs_nodefile.%u.%u", jobid, jobStepID);
    } else {
	snprintf (pbs_filename, sizeof(pbs_filename),
		"/tmp/slurmd/pbs_nodefile.%u", jobid);
    }

    if ((remove(pbs_filename)) == -1) {
	slurm_error("%s: remove '%s' failed: %s\n", __func__, pbs_filename,
		strerror(errno));
	return -1;
    }

    return 0;
}
