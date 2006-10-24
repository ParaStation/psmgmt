/*
 *               ParaStation
 *
 * Copyright (C) 2006 Cluster Competence Center GmbH, Munich
 *
 * $Id: test_pse.c 3882 2006-01-02 10:24:47Z eicker $
 *
 *
 */
/**
 * \file
 * psaccounter: ParaStation example accounting daemon
 *
 * $Id: test_pse.c 3882 2006-01-02 10:24:47Z eicker $ 
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 * Ralph Krotz <krotz@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidspawn.c 4393 2006-10-16 22:07:59Z eicker $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <popt.h>

#include "pse.h"
#include "psi.h"
#include "psiinfo.h"
#include "psispawn.h"

#include "pscommon.h"

void handleAcctMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    PStask_ID_t sender = msg->header.sender, logger;
    int rank, taskSize;
    uid_t uid;
    gid_t gid;
    struct rusage rusage;

    /* logger's TID, this identifies a task uniquely */
    logger = *(PStask_ID_t *)ptr;
    ptr += sizeof(PStask_ID_t);

    /* current rank */
    rank = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* childs uid */
    uid = *(uid_t *)ptr;
    ptr += sizeof(uid_t);

    /* childs gid */
    gid = *(gid_t *)ptr;
    ptr += sizeof(gid_t);

    /* total number of childs. Only the logger knows this */
    taskSize = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* actual rusage structure */
    memcpy(&rusage, ptr, sizeof(rusage));
    ptr += sizeof(rusage);

    printf("msg from %s: type %s", PSC_printTID(sender),
	   msg->type == PSP_ACCOUNT_QUEUE ? "Q" :
	   msg->type == PSP_ACCOUNT_START ? "S" :
	   msg->type == PSP_ACCOUNT_DELETE ? "D" :
	   msg->type == PSP_ACCOUNT_END ? "E" : "?");
    printf(" job %s rank %d", PSC_printTID(logger), rank);
    printf(" UID %d GID %d", uid, gid);
    if (sender == logger) {
	printf(" size %d\n", taskSize);
    } else {
	printf(" user %.6f sys %.6f\n",
	       rusage.ru_utime.tv_sec + 1.0e-6 * rusage.ru_utime.tv_usec,
	       rusage.ru_stime.tv_sec + 1.0e-6 * rusage.ru_stime.tv_usec);
    }
}

void handleSigAns(PStask_ID_t tid, int error)
{
    char *errstr = strerror(error);

    if (!errstr) errstr = "UNKNOWN";

    printf("signal to %s ret %d: %s\n", PSC_printTID(tid), error, errstr);
}

void loop(void)
{
    while (1) {
	DDTypedBufferMsg_t msg;
	PStask_ID_t sender;
	int ret;

	PSI_recvMsg(&msg);

	sender = msg.header.sender;

	switch (msg.header.type) {
	case PSP_CD_ACCOUNT:
	    handleAcctMsg(&msg);

	    ret = PSI_kill(sender, 0); /* ping the sender */
	    if (ret == -2) break;
	    if (ret == -1) ret = errno;
	    handleSigAns(sender, ret);
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
 * End
 */
