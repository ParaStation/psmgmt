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
 * psaccounter: ParaStation accounting daemon
 *
 * $Id: test_pse.c 3882 2006-01-02 10:24:47Z eicker $ 
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 * Ralph Krotz <krotz@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>


#include <popt.h>

#include "pse.h"
#include "psi.h"
#include "psiinfo.h"
#include "pscommon.h"

void loop(void)
{
    while (1) {
	DDBufferMsg_t msg;
	char *ptr = msg.buf;
	PStask_ID_t sender, logger;
	int rank, taskSize;
	uid_t uid;
	gid_t gid;
	struct rusage rusage;

	PSI_recvMsg(&msg);

	sender = msg.header.sender;

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

	printf("msg from %s:", PSC_printTID(sender));
	printf(" job %s rank %d", PSC_printTID(logger), rank);
	printf(" UID %d GID %d", uid, gid);
	if (sender == logger) {
	    printf(" size %d\n", taskSize);
	} else {
	    printf(" user %.6f sys %.6f\n",
		   rusage.ru_utime.tv_sec + rusage.ru_utime.tv_usec * 1.0e-6,
		   rusage.ru_stime.tv_sec + rusage.ru_stime.tv_usec * 1.0e-6);
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
 *  compile-command: "make psacounter"
 * End
 */
