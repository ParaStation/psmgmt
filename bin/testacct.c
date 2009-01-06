/*
 *               ParaStation
 *
 * Copyright (C) 2006-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * psaccounter: ParaStation example accounting daemon
 *
 * $Id$
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 * Ralph Krotz <krotz@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <popt.h>

#include "pse.h"
#include "psi.h"
#include "psiinfo.h"
#include "psispawn.h"
#include "pscpu.h"

#include "pscommon.h"

void handleSlotsMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf + sizeof(PStask_ID_t);
    unsigned int numSlots = *(uint32_t *)ptr, slot;

    ptr += sizeof(uint32_t);

    for (slot = 0; slot < numSlots; slot++) {
	PSnodes_ID_t node;
	PSCPU_set_t cpus;

	if (!slot) printf(": "); else printf(", ");

	memcpy(&node,  ptr, sizeof(PSnodes_ID_t));
	ptr += sizeof(PSnodes_ID_t);
	memcpy(cpus,  ptr, sizeof(PSCPU_set_t));
	ptr += sizeof(PSCPU_set_t);

	printf("%d/%s", node, PSCPU_print(cpus));
    }
    printf("\n");
}

char *handleCommonMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    int rank;
    uid_t uid;
    gid_t gid;

    /* logger's TID, this identifies a task uniquely */
    ptr += sizeof(PStask_ID_t);

    /* current rank */
    rank = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* child's uid */
    uid = *(uid_t *)ptr;
    ptr += sizeof(uid_t);

    /* child's gid */
    gid = *(gid_t *)ptr;
    ptr += sizeof(gid_t);

    printf(" rank %d", rank);
    printf(" UID %d GID %d", uid, gid);

    return ptr;
}

char *handleQueueMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = handleCommonMsg(msg);
    int partReqSize;

    /* size of the requested partition */
    partReqSize = *(int32_t *)ptr;
    ptr += sizeof(int32_t);
    printf(" req part-size %d", partReqSize);

    return ptr;
}

char *handleEndMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = handleCommonMsg(msg);
    struct timeval walltime;

    if (msg->header.sender == *(PStask_ID_t *)msg->buf) {
	/* end message from logger process */
	int numChilds;

	/* total number of childs. Only the logger knows this */
	numChilds = *(int32_t *)ptr;
	ptr += sizeof(int32_t);
	/* walltime used by logger */
	memcpy(&walltime, ptr, sizeof(walltime));
	ptr += sizeof(walltime);

	printf(" num childs %d", numChilds);
	printf(" wall %.6f", walltime.tv_sec + 1.0e-6*walltime.tv_usec);
    } else {
	struct rusage rusage;
	int status;

	memcpy(&rusage, ptr, sizeof(rusage));
	ptr += sizeof(rusage);

	/* skip some stuff */
	/* size of max used mem */
	ptr += sizeof(uint64_t);
	/* pagesize */
	ptr += sizeof(int64_t);
	/* size of max used vmem */
	ptr += sizeof(uint64_t);
	/* walltime used by child */
	memcpy(&walltime, ptr, sizeof(walltime));
	ptr += sizeof(walltime);
	/* number of threads */
	ptr += sizeof(uint32_t);
	/* session id of job */
	ptr += sizeof(int32_t);

	/* child's return status */
	status = *(int32_t *)ptr;
	ptr += sizeof(int32_t);

	/* ignore all trailing info, too */

	printf(" user %.6f sys %.6f",
	       rusage.ru_utime.tv_sec + 1.0e-6 * rusage.ru_utime.tv_usec,
	       rusage.ru_stime.tv_sec + 1.0e-6 * rusage.ru_stime.tv_usec);
	printf(" wall %.6f", walltime.tv_sec + 1.0e-6*walltime.tv_usec);
	printf(" exit status %d", WEXITSTATUS(status));
	if (WIFSIGNALED(status)) {
	    printf(" on signal %d", WTERMSIG(status));
	    if (WCOREDUMP(status)) printf(" core dumped");
	}
    }

    return ptr;
}

char *handleDeleteMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = handleCommonMsg(msg);

    return ptr;
}

void handleStartMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = handleCommonMsg(msg);
    int possChilds;

    /* total number of possible childs */
    possChilds = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    printf(" poss childs %d", possChilds);
}

void handleChildMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = handleCommonMsg(msg);
    char *progname;

    progname = ptr;
    ptr += strlen(ptr)+1;

    printf(" prog '%s'", progname);
}

void handleLogMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = handleCommonMsg(msg);
    int maxConnected;
    char *jobID;

    /* total number of childs connected to logger */
    maxConnected = *(int32_t *)ptr;
    ptr += sizeof(int32_t);
    printf(" logger conn childs %d", maxConnected);

    /* job ID (if available) */
    jobID = ptr;
    ptr += strlen(ptr)+1;

    if (*jobID) {
	printf(" jobID '%s'", jobID);
    } else {
	printf(" no jobID");
    }
}

void handleAcctMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    PStask_ID_t sender = msg->header.sender, logger;

    /* logger's TID, this identifies a task uniquely */
    logger = *(PStask_ID_t *)ptr;
    ptr += sizeof(PStask_ID_t);

    {
	int ret = PSI_kill(logger, 0, 1); /* ping the sender */
	if (ret == -1) printf("PSI_kill(%s, 0): %s\n",
			      PSC_printTID(logger), strerror(ret));
    }

    printf("%s: msg from %s:", __func__, PSC_printTID(sender));
    printf(" job %s type ", PSC_printTID(logger));

    switch (msg->type) {
    case PSP_ACCOUNT_QUEUE:
	printf("Q");
	handleQueueMsg(msg);
	break;
    case PSP_ACCOUNT_DELETE:
	printf("D");
	handleDeleteMsg(msg);
	break;
    case PSP_ACCOUNT_START:
	printf("S");
	handleStartMsg(msg);
	break;
    case PSP_ACCOUNT_SLOTS:
	printf("S-N");
	handleSlotsMsg(msg);
	break;
    case PSP_ACCOUNT_CHILD:
	printf("S-C");
	handleChildMsg(msg);
	break;
    case PSP_ACCOUNT_LOG:
	printf("L");
	handleLogMsg(msg);
	break;
    case PSP_ACCOUNT_END:
	printf("E");
	ptr = handleEndMsg(msg);
	break;
    default:
	printf("?");
    }
    printf("\n");
}

void handleSigMsg(DDErrorMsg_t *msg)
{
    char *errstr = strerror(msg->error);

    if (!errstr) errstr = "UNKNOWN";

    printf("%s: msg from %s:", __func__, PSC_printTID(msg->header.sender));
    printf(" task %s: %s\n", PSC_printTID(msg->request), errstr);

    return;
}

void loop(void)
{
    while (1) {
	DDTypedBufferMsg_t msg;

	PSI_recvMsg(&msg);

	switch (msg.header.type) {
	case PSP_CD_ACCOUNT:
	    handleAcctMsg(&msg);
	    break;
	case PSP_CD_SIGRES:
	    handleSigMsg((DDErrorMsg_t *)&msg);
	    break;
	default:
	    printf("Unknown message\n");
	}
    }

    PSE_finalize();
}


int main(int argc, char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */
    int arg_np, rc;

    struct poptOption optionsTable[] = {
	{ "np", '\0', POPT_ARG_INT | POPT_ARGFLAG_ONEDASH,
	  &arg_np, 0, "number of processes to start", "num"},
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL, 0, NULL, NULL}
    };

    optCon = poptGetContext(NULL, argc, (const char **)argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	return 1;
    }

    /* init PSI */
    if (!PSI_initClient(TG_ACCOUNT)) {
	printf("Initialization of PSI failed\n");
	exit(1);
    }

    loop();

    return 0;
}


/*
 * Local Variables:
 *  compile-command: "make psaccounter"
 * End:
 */
