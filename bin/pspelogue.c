/*
 * ParaStation
 *
 * Copyright (C) 2017-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <popt.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#ifndef BUILD_WITHOUT_PSCONFIG
#include <glib.h>
#include <psconfig.h>
#endif

#include "pscommon.h"
#include "psenv.h"
#include "pscomplist.h"
#include "psserial.h"
#include "pspluginprotocol.h"

#include "psi.h"
#include "psiinfo.h"

#include "peloguetypes.h"

typedef struct {
    PSnodes_ID_t id;
    char *hostname;
} PS_NodeList_t;

/** job ID this pspelogue is handling */
static char *jobID = NULL;

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

/** use psid to resolve hostnames */
static int psidResolve = 0;

#ifndef BUILD_WITHOUT_PSCONFIG
/** use psconfig to resolve hostnames */
static int psconfigRes = 0;

static PSConfig* config = NULL;
#endif

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

/** pspelogue protocol version */
static uint16_t protoVer = 3;

/** complete nodelist */
static PS_NodeList_t *nodeList = NULL;

/** number of entries in nodeList */
static PSnodes_ID_t numNodeList = 0;

/** Flag to forward stdout/stderr of prologue scripts */
static int fwPrologueOE = 0;

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
    { "fwPrologueOE", '\0', POPT_ARG_NONE,
      &fwPrologueOE, 0, "forward stdout/stderr of prologue", NULL},
    { "filter", 'f', POPT_ARG_STRING,
      &addFilter, 0, "add additional environment filter", NULL},
    { "psid_resolve", '\0', POPT_ARG_NONE,
      &psidResolve, 0, "use psid to resolve hostnames", NULL},
#ifndef BUILD_WITHOUT_PSCONFIG
    { "psconfig_resolve", '\0', POPT_ARG_NONE,
      &psconfigRes, 0, "use psconfig to resolve hostnames", NULL},
#endif
    POPT_TABLEEND
};

static char *resolveNode(PSnodes_ID_t node)
{
    in_addr_t nodeIP;
    struct sockaddr_in nodeAddr;
    int rc;
    static char nodeStr[NI_MAXHOST];

    /* get ip-address of node */
    rc = PSI_infoUInt(-1, PSP_INFO_NODE, &node, &nodeIP, false);
    if (rc || nodeIP == INADDR_ANY) {
	snprintf(nodeStr, sizeof(nodeStr), "<unknown>(%d)", node);
	return nodeStr;
    }

    /* get hostname */
    nodeAddr = (struct sockaddr_in) {
	.sin_family = AF_INET,
	.sin_port = 0,
	.sin_addr = { .s_addr = nodeIP } };
    rc = getnameinfo((struct sockaddr *)&nodeAddr, sizeof(nodeAddr), nodeStr,
		     sizeof(nodeStr), NULL, 0, NI_NAMEREQD | NI_NOFQDN);
    if (rc) {
	snprintf(nodeStr, sizeof(nodeStr), "%s", inet_ntoa(nodeAddr.sin_addr));
    } else {
	char *ptr = strchr (nodeStr, '.');
	if (ptr) *ptr = '\0';
    }

    return nodeStr;
}

/**
 * @brief Get ParaStation node ID by hostname
 *
 * @param host The hostname to resolve
 *
 * @return Returns the requested node ID or -1 on error
 */
static PSnodes_ID_t resolveHost(const char *host)
{
    PSnodes_ID_t i;

    if (!host) return -1;

    for (i=0; i<numNodeList; i++) {
	if (!strcmp(host, nodeList[i].hostname)) return nodeList[i].id;
    }
    return -1;
}

#ifndef BUILD_WITHOUT_PSCONFIG

static PSnodes_ID_t psconfigResHost(const char *host)
{
    if (!config) return -1;

    GError *err = NULL;
    char tmp[255];
    sprintf(tmp, "host:%s", host);
    gchar* string = psconfig_get(config, tmp, "Psid.NodeId",
		    PSCONFIG_FLAG_INHERIT | PSCONFIG_FLAG_FOLLOW, &err);
    if (!string) {
	fprintf(stderr, "%s", err->message);
	exit(1);
    }
    return atoi(string);
}

#endif

/**
 * @brief Initialize host resolve table
 */
static void initHostTable(void)
{
    PSnodes_ID_t i, nrOfNodes = PSC_getNrOfNodes();

    nodeList = malloc(sizeof(*nodeList) * nrOfNodes);
    if (!nodeList) {
	fprintf(stderr, "no memory for nodeList\n");
	exit(1);
    }

    for (i=0; i<nrOfNodes; i++) {
	nodeList[i].id = i;
	nodeList[i].hostname = strdup(resolveNode(i));
	numNodeList++;
    }
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

    if (psidResolve) initHostTable();
}

static void timeoutHandler(int sig)
{
    fprintf(stderr, "%s: timeout(%i) receiving pelogue result for job %s\n",
	   __func__, pelogueTimeout + recvTimeout + graceTime, jobID);
    exit(1);
}

/**
 * @brief Handle a PSP_CD_UNKNOWN response
 *
 * @param answer The msg to handle
 */
void handleRespUnknown(DDBufferMsg_t *answer)
{
    size_t used = 0;
    PStask_ID_t dest;
    int16_t type;

    /* original dest */
    PSP_getMsgBuf(answer, &used, "dest", &dest, sizeof(dest));

    /* original type */
    PSP_getMsgBuf(answer, &used, "type", &type, sizeof(type));

    fprintf(stderr, "%s: delivery of message with type %i to %s job %s "
	    "failed\n", __func__, type, PSC_printTID(dest), jobID);

    fprintf(stderr, "%s: please make sure the plugin 'pelogue' is loaded on"
	    " node %i\n", __func__, PSC_getID(answer->header.sender));
}

/**
 * @brief Handle a pelogue response message
 */
static void handlePElogueResp(DDTypedBufferMsg_t *msg, PS_DataBuffer_t *data)
{
    int32_t exit_status;
    uint8_t timeout;

    /* jobid */
    char *handledID = getStringM(data);
    /* exit status */
    getInt32(data, &exit_status);
    /* timeout */
    getUint8(data, &timeout);

    if (debug) printf("%s: answer is jobid %s exit %i timeout %u\n", __func__,
		      handledID, exit_status, timeout);

    if (strcmp(handledID, jobID)) {
	fprintf(stderr, "%s: answer for invalid jobid %s, expected %s\n",
		__func__, handledID, jobID);
	exit(1);
    }
    free(handledID);

    if (exit_status) {
	fprintf(stderr, "%s: pelogue failed: jobid %s exit status %i "
		"timeout %u\n", __func__, jobID, exit_status, timeout);
	exit(1);
    }
}

/**
 * @brief Set receive timeout using SIGALRM
 */
static void setTimeout(void)
{
    sigset_t sigset;

    /* unblock SIGALRM */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) == -1) {
	fprintf(stderr, "%s: unblock SIGALRM with sigprocmask() failed\n",
		__func__);
	exit(1);
    }

    /* register SIGALRM sighandler */
    if (PSC_setSigHandler(SIGALRM, timeoutHandler) == SIG_ERR) {
	fprintf(stderr, "%s:  register SIGALRM sighandler for job %s failed\n",
		__func__, jobID);
	exit(1);
    }
    alarm(pelogueTimeout + recvTimeout + graceTime);
}

/**
 * @brief Receive and handle response from pelogue
 */
static void handleResponse(void)
{
    DDTypedBufferMsg_t answer;

    if (debug) printf("%s: ...done, waiting for answer ...\n", __func__);

    /* recv answer */
    setTimeout();

    if (PSI_recvMsg((DDMsg_t *)&answer, sizeof(answer))<0) {
	fprintf(stderr, "%s: PSI_recvMsg() for job %s failed\n",
		__func__, jobID);
	exit(1);
    }

    alarm(0);
    PSC_setSigHandler(SIGALRM, SIG_DFL);

    gettimeofday(&time_now, NULL);
    timersub(&time_now, &time_start, &time_diff);

    /* verify msg */
    switch (answer.header.type) {
    case PSP_CC_MSG:
    case PSP_PLUG_PELOGUE:
	break;
    case PSP_CD_UNKNOWN:
	handleRespUnknown((DDBufferMsg_t *)&answer);
	exit(1);
	break;
    default:
	fprintf(stderr, "%s: received unexpected msg type %hd:%d job %s\n",
		__func__, answer.header.type, answer.type, jobID);
	exit(1);
    }

    switch (answer.type) {
    case PSP_PELOGUE_RESP:
	break;
    default:
	fprintf(stderr, "%s: received unexpected msg type %hd:%d job %s\n",
		__func__, answer.header.type, answer.type, jobID);
	exit(1);
    }

    if (debug) printf("%s: received answer at %f diff %f from %s job %s\n",
		      __func__, time_now.tv_sec + 1e-6 * time_now.tv_usec,
		      time_diff.tv_sec + 1e-6 * time_diff.tv_usec,
		      PSC_printTID(answer.header.sender), jobID);

    recvFragMsg(&answer, handlePElogueResp);
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
		   PSnodes_ID_t *nodes, env_t env)
{
    PS_SendDB_t msg;
    initFragBuffer(&msg, PSP_PLUG_PELOGUE, PSP_PELOGUE_REQ);
    PStask_ID_t dest = PSC_getTID(nodes[0], 0);
    setFragDest(&msg, dest);

    /* protocol version */
    addUint16ToMsg(protoVer, &msg);
    /* add my name */
    addStringToMsg("pspelogue", &msg);
    /* prologue flag */
    if (epilogue) {
	addUint8ToMsg(PELOGUE_EPILOGUE, &msg);
    } else {
	addUint8ToMsg(PELOGUE_PROLOGUE, &msg);
    }
    /* timeout */
    addUint32ToMsg(pelogueTimeout, &msg);
    /* grace time */
    addUint32ToMsg(graceTime, &msg);
    /* jobid */
    addStringToMsg(jobid, &msg);
    /* uid */
    addUint32ToMsg(atoi(sUid), &msg);
    /* gid */
    addUint32ToMsg(atoi(sGid), &msg);
    /* nodelist */
    addInt16ArrayToMsg(nodes, nrOfNodes, &msg);
    /* environment */
    addStringArrayToMsg(envGetArray(env), &msg);
    /* fwPrologueOE */
    addUint16ToMsg(fwPrologueOE, &msg);

    /* send prologue request to mother superior */
    gettimeofday(&time_start, NULL);
    if (debug) printf("%s: send request to %s at %f job %s\n", __func__,
		      PSC_printTID(dest),
		      time_start.tv_sec + 1e-6 * time_start.tv_usec, jobID);

    int ret = sendFragMsg(&msg);

    if (ret == -1) {
	fprintf(stderr, "%s: send request to %s for job %s failed\n", __func__,
		PSC_printTID(dest), jobID);
	exit(1);
    }
}

int main(const int argc, const char *argv[], char *envp[])
{
    char *filter[] = { "SLURM_SPANK_*", "_SLURM_SPANK_OPTION_*", "SLURM_JOBID",
		       "SLURM_JOB_ID", "SLURM_JOB_NODELIST", "SLURM_SUBMIT_DIR",
		       "SLURM_PACK_JOB_ID", "SLURM_PACK_JOB_NODELIST",
		       "_PSSLURM_*", NULL, NULL };
    size_t numFilter = sizeof(filter)/sizeof(*filter);

    init(argc, argv);

    filter[numFilter-2] = addFilter;

    /* make sure we have all the infos we need */
    jobID = getenv("SLURM_JOB_ID");
    if (!jobID) {
	fprintf(stderr, "%s: invalid SLURM_JOB_ID\n", argv[0]);
	exit(1);
    }

    char *slurmHosts = getenv("SLURM_PACK_JOB_NODELIST");
    if (!slurmHosts) {
	slurmHosts = getenv("SLURM_JOB_NODELIST");
	if (!slurmHosts) {
	    fprintf(stderr, "%s: invalid SLURM_JOB_NODELIST for job %s\n",
		    argv[0], jobID);
	    exit(1);
	}
    }

    char *sUid = getenv("SLURM_JOB_UID");
    if (!sUid) {
	fprintf(stderr, "%s: invalid SLURM_JOB_UID for job %s\n",
		argv[0], jobID);
	exit(1);
    }

    char *sGid = getenv("SLURM_JOB_GID");
    if (!sGid) {
	fprintf(stderr, "%s: invalid SLURM_JOB_GID for job %s\n",
		argv[0], jobID);
	exit(1);
    }

    /* build and filter environment */
    env_t env = envConstruct(envp, filter);
    envSet(&env, "SLURM_USER", getenv("SLURM_JOB_USER"));
    envSet(&env, "SLURM_UID", getenv("SLURM_JOB_UID"));

    /* remove new "SPANK_" prefix from already prefixed variables
     * pspelogue needs to forward (juwels:#9228) */
    for (uint32_t i = 0; envp[i]; i++) {
	if (!strncmp("SPANK_SLURM_SPANK_", envp[i], 18)) {
	    envPut(&env, envp[i] + 6);
	}
    }

    /* convert Slurm hostlist into PS IDs */
    bool resolved = false;
    PSnodes_ID_t *nodes = NULL;
    uint32_t nrOfNodes = 0;
    #ifndef BUILD_WITHOUT_PSCONFIG
    if (psconfigRes) {
	resolved = true;

	gettimeofday(&time_start, NULL);

	/* open psconfig database */
	config = psconfig_new();
	/* resolve hostnames using psconfig */
	convHLtoPSnodes(slurmHosts, psconfigResHost, &nodes, &nrOfNodes);
	psconfig_unref(config);
	config = NULL;

	gettimeofday(&time_now, NULL);
	timersub(&time_now, &time_start, &time_diff);

	if (verbose) {
	    printf("psconfig resolve for job %s finished in %.3f seconds on %u "
		   "nodes\n", jobID, time_diff.tv_sec + 1e-6 * time_diff.tv_usec,
		   nrOfNodes);
	}
    }
    #endif

    if (!resolved) {
	if (psidResolve) {
	    convHLtoPSnodes(slurmHosts, resolveHost, &nodes, &nrOfNodes);
	} else {
	    convHLtoPSnodes(slurmHosts, PSI_resolveNodeID, &nodes, &nrOfNodes);
	}
    }

    if (!nrOfNodes) {
	fprintf(stderr, "%s: no nodes for job %s found\n", argv[0], jobID);
	exit(1);
    }

    if (verbose) {
	printf("parallel pelogue for job %s nodes %s starting\n", jobID,
	       slurmHosts);
    }

    /* send pelogue start request */
    sendPElogueReq(jobID, sUid, sGid, nrOfNodes, nodes, env);
    envDestroy(&env);

    /* receive and handle result */
    handleResponse();

    free(nodes);

    if (verbose) {
	printf("parallel pelogue for job %s finished in %.3f seconds on %u "
	       "nodes\n", jobID, time_diff.tv_sec + 1e-6 * time_diff.tv_usec,
	       nrOfNodes);
    }

    return 0;
}
