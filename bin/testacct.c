/*
 *               ParaStation
 *
 * Copyright (C) 2006-2007 ParTec Cluster Competence Center GmbH, Munich
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
static char vcid[] __attribute__(( unused )) = "$Id$";
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

#include "pscommon.h"

void handleSlotsMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf + sizeof(PStask_ID_t);
    unsigned int numSlots = *(uint16_t *)ptr, slot;

    ptr += sizeof(uint16_t);

    for (slot = 0; slot < numSlots; slot++) {
	struct in_addr slotIP;
	int cpu;

	if (!slot) printf(": "); else printf(", ");

	slotIP.s_addr = *(uint32_t *)ptr;
	ptr += sizeof(uint32_t);
	cpu = *(int16_t *)ptr;
	ptr += sizeof(int16_t);

	printf("%s/%d", inet_ntoa(slotIP), cpu);
    }

    printf("\n");
}

void handleAcctMsg(DDTypedBufferMsg_t *msg)
{
    char *ptr = msg->buf;
    PStask_ID_t sender = msg->header.sender, logger;
    int rank, taskSize, status;
    struct in_addr senderIP;
    uid_t uid;
    gid_t gid;
    struct rusage rusage;

    /* logger's TID, this identifies a task uniquely */
    logger = *(PStask_ID_t *)ptr;
    ptr += sizeof(PStask_ID_t);

    {
	int ret = PSI_kill(logger, 0, 1); /* ping the sender */
	if (ret == -1) printf("PSI_kill(%s, 0): %s\n",
			      PSC_printTID(logger), strerror(ret));
    }

    /* current rank */
    rank = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* child's uid */
    uid = *(uid_t *)ptr;
    ptr += sizeof(uid_t);

    /* child's gid */
    gid = *(gid_t *)ptr;
    ptr += sizeof(gid_t);

    /* total number of childs. Only the logger knows this */
    taskSize = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* sender's IP address */
    senderIP.s_addr = *(uint32_t *)ptr;
    ptr += sizeof(uint32_t);

    printf("%s: msg from %s: type %s", __func__, PSC_printTID(sender),
	   msg->type == PSP_ACCOUNT_QUEUE ? "Q" :
	   msg->type == PSP_ACCOUNT_START ? "S" :
	   msg->type == PSP_ACCOUNT_SLOTS ? "S-N" :
	   msg->type == PSP_ACCOUNT_DELETE ? "D" :
	   msg->type == PSP_ACCOUNT_END ? "E" : "?");
    printf(" job %s", PSC_printTID(logger));
    if (msg->type != PSP_ACCOUNT_SLOTS) {
	printf(" rank %d", rank);
	printf(" UID %d GID %d", uid, gid);
	printf(" sender at %s", inet_ntoa(senderIP));
    }

    switch (msg->type) {
    case PSP_ACCOUNT_START:
	printf(" size %d\n", taskSize);
	break;
    case PSP_ACCOUNT_SLOTS:
	handleSlotsMsg(msg);
	break;
    case PSP_ACCOUNT_END:
	/* actual rusage structure */
	memcpy(&rusage, ptr, sizeof(rusage));
	ptr += sizeof(rusage);

	/* exit status */
	status = *(int32_t *)ptr;
	ptr += sizeof(int32_t);

	printf(" program '%s'", ptr);
	printf(" user %.6f sys %.6f",
	       rusage.ru_utime.tv_sec + 1.0e-6 * rusage.ru_utime.tv_usec,
	       rusage.ru_stime.tv_sec + 1.0e-6 * rusage.ru_stime.tv_usec);
	printf(" exit status %d", WEXITSTATUS(status));
	if (WIFSIGNALED(status)) {
	    printf(" on signal %d", WTERMSIG(status));
	    if (WCOREDUMP(status)) printf(" core dumped");
	}
    default:
	printf("\n");
    }
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
 * End
 */
