/*
 * ParaStation
 *
 * Copyright (C) 2017-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <popt.h>

#include "psenv.h"
#include "pshostlist.h"
#include "psnodes.h"
#include "psserial.h"
#include "pspluginprotocol.h"

#include "psi.h"
#include "psiinfo.h"

typedef enum {
    PSP_PROLOGUE_START,     /**< prologue script start */
    PSP_PROLOGUE_FINISH,    /**< result from prologue */
    PSP_EPILOGUE_START,     /**< epilogue script start */
    PSP_EPILOGUE_FINISH,    /**< result from epilogue script */
    PSP_PELOGUE_SIGNAL,     /**< send a signal to a PElogue script */
    PSP_PELOGUE_REQ,        /**< remote pelogue request */
    PSP_PELOGUE_RESP,       /**< remote pelogue response */
} PSP_PELOGUE_t;

typedef enum {
    PELOGUE_PROLOGUE = 1,  /**< prologue */
    PELOGUE_EPILOGUE,      /**< epilogue */
} PElogueType_t;

/** timeout for prologue/epilogue scripts */
static int pelogueTimeout = 300;

/** additional time to wait for the pelogue result */
static int recvTimeout = 60;

/** pelogue grace time */
static int graceTime = 60;

/** flag for debug messages */
static int debug = 0;

/** flag for help */
static int help = 0;

/** flag for verbosity */
static int verbose = 0;

/** start parallel epilogue */
static int epilogue = 0;

/** timer to measure the pelogue phase */
static struct timeval time_start;

/** timer to measure the pelogue phase */
static struct timeval time_now;

/** timer to measure the pelogue phase */
static struct timeval time_diff;

/** additional environment filter */
static char *addFilter = NULL;

/** popt command line option table */
static struct poptOption optionsTable[] = {
    { "debug", 'd', POPT_ARG_NONE,
      &debug, 0, "enable debug messages", NULL},
    { "gtime", 'g', POPT_ARG_INT,
      &graceTime, 0, "grace time in seconds (default:60)", NULL},
    { "rtime", 'r', POPT_ARG_INT,
      &recvTimeout, 0, "receive timeout in seconds (default:60)", NULL},
    { "ptime", 'p', POPT_ARG_INT,
      &pelogueTimeout, 0, "pelogue timeout in seconds (default:300)", NULL},
    { "help", 'h', POPT_ARG_NONE,
      &help, 0, "display help", NULL},
    { "verbose", 'v', POPT_ARG_NONE,
      &verbose, 0, "be verbose", NULL},
    { "epilogue", 'e', POPT_ARG_NONE,
      &epilogue, 0, "start parallel epilogue", NULL},
    { "filter", 'f', POPT_ARG_STRING,
      &addFilter, 0, "add additional environment filter", NULL},
    POPT_TABLEEND
};

/**
 * @brief Convert a Slurm hostlist to PS node IDs
 *
 * @param slurmHosts The Slurm hostlist to convert
 *
 * @param nrOfNodes The number of converted nodes
 *
 * @param nodes The PS nodelist holding the result
 */
static void getNodesFromSlurmHL(char *slurmHosts, uint32_t *nrOfNodes,
				PSnodes_ID_t **nodes)
{
    const char delimiters[] =", \n";
    char *next, *saveptr;
    char *hostlist = expandHostList(slurmHosts, nrOfNodes);
    int i = 0;

    if (!hostlist || !*nrOfNodes) return;

    *nodes = malloc((sizeof(*nodes) + sizeof(**nodes)) * *nrOfNodes);
    if (!nodes) exit(1);

    next = strtok_r(hostlist, delimiters, &saveptr);

    while (next) {
	(*nodes)[i] = PSI_resolveNodeID(next);
	i++;
	next = strtok_r(NULL, delimiters, &saveptr);
    }
    free(hostlist);
}

static void init(const int argc, const char *argv[])
{
    const char **dup_argv;
    int rc = 0, dup_argc;
    static poptContext optCon;

    PSC_initLog(stderr);

    if (!PSI_initClient(TG_SERVICE)) {
      printf("%s: PSI initClient failed\n", __func__);
      exit(1);
    }

    if (!initSerial(0, PSI_sendMsg)) {
      printf("%s: initSerial() failed\n", __func__);
      exit(1);
    }

    /* create context for parsing */
    poptDupArgv(argc, argv, &dup_argc, &dup_argv);

    optCon = poptGetContext(NULL, dup_argc, dup_argv,
			    optionsTable, POPT_CONTEXT_POSIXMEHARDER);

    while ((rc = poptGetNextOpt(optCon)) >= 0) { };

    if (rc < -1) {
	/* an error occurred during option processing */
	fprintf(stderr, "%s: %s\n", poptBadOption(optCon,
		POPT_BADOPTION_NOALIAS), poptStrerror(rc));
	exit(1);
    }

    if (help) {
	poptPrintHelp(optCon, stdout, 0);
	exit(0);
    }
}

static void timeoutHandler(int sig)
{
    fprintf(stderr, "%s: timeout(%u) receiving pelogue result\n",
	   __func__, pelogueTimeout + recvTimeout + graceTime);
    exit(1);
}

/**
 * @brief Handle a PSP_CD_UNKNOWN response
 *
 * @param answer The msg to handle
 */
void handleRespUnknown(DDTypedBufferMsg_t *answer)
{
    size_t used = 0;
    PStask_ID_t dest;
    int16_t type;

    /* original dest */
    PSP_getMsgBuf((DDBufferMsg_t *) answer, &used, __func__, "dest",
		  &dest, sizeof(dest));

    /* original type */
    PSP_getMsgBuf((DDBufferMsg_t *) answer, &used, __func__, "type",
		  &type, sizeof(type));

    fprintf(stderr, "%s: delivery of message with type %i to %s failed\n",
	    __func__, type, PSC_printTID(dest));

    fprintf(stderr, "%s: please make sure the plugin 'pelogue' is loaded on"
	    " node %i\n", __func__, PSC_getID(answer->header.sender));
}

/**
 * @brief Receive and handle a pelogue response message
 *
 * @param expJobID The expected jobid for the received msg
 */
static void handlePElogueResult(char *expJobID)
{
    DDTypedBufferMsg_t answer;
    char *ptr = answer.buf;
    char *jobid;
    int32_t exit_status;
    uint8_t timeout;

    if (debug) {
	printf("%s: ...done, waiting for answer ...\n", __func__);
    }

    /* recv answer */
    signal(SIGALRM, timeoutHandler);
    alarm(pelogueTimeout + recvTimeout + graceTime);

    if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer))<0) {
	fprintf(stderr, "%s: PSI_recvMsg() failed\n", __func__);
	exit(1);
    }

    alarm(0);

    gettimeofday(&time_now, NULL);
    timersub(&time_now, &time_start, &time_diff);

    /* verify msg */
    switch (answer.header.type) {
	case PSP_CC_MSG:
	    if (answer.type != PSP_PELOGUE_RESP) {
		fprintf(stderr, "%s: received unexpected msg type %u:%u\n",
			__func__, answer.header.type, answer.type);
		exit(1);
	    }
	    break;
	case PSP_CD_UNKNOWN:
	    handleRespUnknown(&answer);
	    exit(1);
	    break;
	default:
	    fprintf(stderr, "%s: received unexpected msg type %u:%u\n",
		    __func__, answer.header.type, answer.type);
	    exit(1);
    }

    /* jobid */
    jobid = getStringM(&ptr);
    /* exit status */
    getInt32(&ptr, &exit_status);
    /* timeout */
    getUint8(&ptr, &timeout);

    if (debug) {
	printf("%s: received answer jobid %s exit %i timeout %u time %f "
	       "diff %f\n", __func__, jobid, exit_status, timeout,
	       time_now.tv_sec + 1e-6 * time_now.tv_usec,
	       time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
    }

    if (!!strcmp(jobid, expJobID)) {
	fprintf(stderr, "%s: answer for invalid jobid %s, expected %s\n",
		__func__, jobid, expJobID);
	exit(1);
    }

    if (exit_status) {
	fprintf(stderr, "%s: prologue failed with exit status %u timeout %u\n",
		__func__, exit_status, timeout);
	exit(1);
    }
}

/**
 * @brief Send a pelogue request message
 *
 * @param jobid The jobid to start the pelogue for
 *
 * @param sUid The userid of the job
 *
 * @param sGid The groupid of the job
 *
 * @param nrOfNodes The number of participating nodes
 *
 * @param nodes The PS nodelist of participating nodes
 *
 * @param env The environment for the pelogue
 */
void sendPElogueReq(char *jobid, char *sUid, char *sGid, uint32_t nrOfNodes,
		   PSnodes_ID_t *nodes, env_t *env)
{
    char tmp[128];
    uint32_t i;
    int ret;
    PS_SendDB_t msg;

    initFragBuffer(&msg, PSP_CC_PLUG_PELOGUE, PSP_PELOGUE_REQ);
    setFragDest(&msg, PSC_getTID(nodes[0], 0));

    /* add my name */
    addStringToMsg("pspelogue", &msg);
    /* prologue flag */
    if (epilogue) {
	addUint8ToMsg(PELOGUE_EPILOGUE, &msg);
    } else {
	addUint8ToMsg(PELOGUE_PROLOGUE, &msg);
    }
    /* timeout */
    snprintf(tmp, sizeof(tmp), "%u", pelogueTimeout);
    addStringToMsg(tmp, &msg);
    /* grace time */
    snprintf(tmp, sizeof(tmp), "%u", graceTime);
    addStringToMsg(tmp, &msg);
    /* jobid */
    addStringToMsg(jobid, &msg);
    /* uid */
    addUint32ToMsg(atoi(sUid), &msg);
    /* gid */
    addUint32ToMsg(atoi(sGid), &msg);
    /* nodelist */
    addInt16ArrayToMsg(nodes, nrOfNodes, &msg);
    /* environment */
    /*
     * TODO:
    addStringArrayM(ptr, &req->pelogueEnv.vars, &req->pelogueEnv.cnt);
    */
    addUint32ToMsg(env->cnt, &msg);
    for (i=0; i<env->cnt; i++) {
	addStringToMsg(env->vars[i], &msg);
    }

    /* send prologue request to mother superior */
    PStask_ID_t tid = PSC_getTID(nodes[0], 0);

    gettimeofday(&time_start, NULL);

    if (debug) {
	printf("%s: sending message to %s time %f\n", __func__,
	       PSC_printTID(tid),
	       time_start.tv_sec + 1e-6 * time_start.tv_usec);
    }

    ret = sendFragMsg(&msg);

    if (ret == -1) {
	fprintf(stderr, "%s sending of pelogue request to %s failed\n",
		__func__, PSC_printTID(tid));
	exit(1);
    }
}

int main(const int argc, const char *argv[], char *envp[])
{
    char *slurmHosts, *jobid, *sUid, *sGid;
    uint32_t nrOfNodes, envc = 0;
    PSnodes_ID_t *nodes = NULL;
    env_t env, clone;
    char *filter[5] = { "SLURM_SPANK_*", "SLURM_JOBID", "SLURM_JOB_ID",
			NULL, NULL };

    init(argc, argv);

    filter[3] = addFilter;

    /* make sure we have all the infos we need */
    if (!(slurmHosts = getenv("SLURM_JOB_NODELIST"))) {
	fprintf(stderr, "%s: invalid SLURM_JOB_NODELIST\n", __func__);
	exit(1);
    }

    if (!(jobid = getenv("SLURM_JOB_ID"))) {
	fprintf(stderr, "%s: invalid SLURM_JOB_ID\n", __func__);
	exit(1);
    }

    if (!(sUid = getenv("SLURM_JOB_UID"))) {
	fprintf(stderr, "%s: invalid SLURM_JOB_UID\n", __func__);
	exit(1);
    }

    if (!(sGid = getenv("SLURM_JOB_GID"))) {
	fprintf(stderr, "%s: invalid SLURM_JOB_GID\n", __func__);
	exit(1);
    }

    /* build and filter environment */
    while (envp[envc]) envc++;
    env.vars = envp;
    env.cnt = env.size = envc;
    envClone(&env, &clone, filter);
    envSet(&clone, "SLURM_USER", getenv("SLURM_JOB_USER"));
    envSet(&clone, "SLURM_UID", getenv("SLURM_JOB_UID"));

    /* convert Slurm hostlist into PS IDs */
    getNodesFromSlurmHL(slurmHosts, &nrOfNodes, &nodes);

    if (verbose) printf("parallel pelogue for job %s started\n", jobid);

    /* send pelogue start request */
    sendPElogueReq(jobid, sUid, sGid, nrOfNodes, nodes, &clone);

    /* receive and handle result */
    handlePElogueResult(jobid);

    if (verbose) {
	printf("parallel pelogue for job %s finished in %.3f seconds\n",
	       jobid, time_diff.tv_sec + 1e-6 * time_diff.tv_usec);
    }

    /* TODO: create psid delegate (partition) */

    return 0;
}
