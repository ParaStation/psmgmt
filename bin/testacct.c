/*
 * ParaStation
 *
 * Copyright (C) 2006-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file psaccounter: ParaStation example accounting daemon
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>    // IWYU pragma: keep
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <popt.h>

#include "psi.h"
#include "psispawn.h"
#include "pscpu.h"

#include "pscommon.h"

static void handleSlotsMsg(DDTypedBufferMsg_t *msg)
{
    PStask_ID_t logger;
    uid_t uid;
    uint16_t numSlots, nBytes;
    int slot;
    size_t used = 0;

    /* logger's TID, this identifies a task uniquely */
    PSP_getTypedMsgBuf(msg, &used, "logger", &logger, sizeof(logger));

    /* child's uid */
    PSP_getTypedMsgBuf(msg, &used, "uid", &uid, sizeof(uid));

    /* number of slots */
    PSP_getTypedMsgBuf(msg, &used, "numSlots", &numSlots, sizeof(numSlots));

    /* size of CPUset part */
    PSP_getTypedMsgBuf(msg, &used, "nBytes", &nBytes, sizeof(nBytes));

    for (slot = 0; slot < numSlots; slot++) {
	PSnodes_ID_t node;
	PSCPU_set_t cpus, setBuf;

	PSP_getTypedMsgBuf(msg, &used, "node", &node, sizeof(node));

	PSP_getTypedMsgBuf(msg, &used, "CPUset", setBuf, nBytes);
	PSCPU_clrAll(cpus);
	PSCPU_inject(cpus, setBuf, nBytes);

	printf("%s%d/%s", slot ? ", " : ": ", node,
	       PSCPU_print_part(cpus, nBytes));
    }
}

static size_t handleCommonMsg(DDTypedBufferMsg_t *msg)
{
    PStask_ID_t logger;
    int32_t rank;
    uid_t uid;
    gid_t gid;
    size_t used = 0;

    /* logger's TID, this identifies a task uniquely */
    PSP_getTypedMsgBuf(msg, &used, "logger", &logger, sizeof(logger));

    /* current rank */
    PSP_getTypedMsgBuf(msg, &used, "rank", &rank, sizeof(rank));

    /* child's uid */
    PSP_getTypedMsgBuf(msg, &used, "uid", &uid, sizeof(uid));

    /* child's gid */
    PSP_getTypedMsgBuf(msg, &used, "gid", &gid, sizeof(gid));

    printf(" rank %d", rank);
    printf(" UID %d GID %d", uid, gid);

    return used;
}

static void handleQueueMsg(DDTypedBufferMsg_t *msg)
{
    size_t used = handleCommonMsg(msg);
    int32_t partReqSize;

    /* size of the requested partition */
    PSP_getTypedMsgBuf(msg, &used, "partReqSize", &partReqSize,
		       sizeof(partReqSize));
    printf(" req part-size %d", partReqSize);
}

static void handleEndMsg(DDTypedBufferMsg_t *msg)
{
    size_t used = handleCommonMsg(msg);
    pid_t pid;
    struct rusage rusage;
    struct timeval wallTm;
    uint64_t pgSize, maxRSS, avgRSS, maxVM, avgVM, avgThd;
    uint32_t extFlg, maxThd;
    int32_t sessID, status;

    if (msg->header.sender == *(PStask_ID_t *)msg->buf) {
	/* end message from logger process */
	int32_t numChild;

	/* total number of children. Only the logger knows this */
	PSP_getTypedMsgBuf(msg, &used, "numChild", &numChild, sizeof(numChild));
	/* walltime used by logger */
	PSP_getTypedMsgBuf(msg, &used, "wallTm", &wallTm, sizeof(wallTm));

	printf(" num children %d", numChild);
	printf(" wall %.6f", wallTm.tv_sec + 1.0e-6*wallTm.tv_usec);

	return;
    }

    PSP_getTypedMsgBuf(msg, &used, "pid", &pid, sizeof(pid));
    printf(" pid %d", pid);

    PSP_getTypedMsgBuf(msg, &used, "rusage", &rusage, sizeof(rusage));
    printf(" user %.6f sys %.6f",
	   rusage.ru_utime.tv_sec + 1.0e-6 * rusage.ru_utime.tv_usec,
	   rusage.ru_stime.tv_sec + 1.0e-6 * rusage.ru_stime.tv_usec);

    /* pagesize */
    PSP_getTypedMsgBuf(msg, &used, "pgSize", &pgSize, sizeof(pgSize));
    printf(" page-size %lu", pgSize);

    /* walltime used by child */
    PSP_getTypedMsgBuf(msg, &used, "wallTm", &wallTm, sizeof(wallTm));
    printf(" wall %.6f", wallTm.tv_sec + 1.0e-6*wallTm.tv_usec);

    /* child's return status */
    PSP_getTypedMsgBuf(msg, &used, "status", &status, sizeof(status));
    printf(" exit status %d", WEXITSTATUS(status));
    if (WIFSIGNALED(status)) {
	printf(" on signal %d", WTERMSIG(status));
	if (WCOREDUMP(status)) printf(" core dumped");
    }

    PSP_getTypedMsgBuf(msg, &used, "extFlg", &extFlg, sizeof(extFlg));
    if (!extFlg) {
	printf(" nothing else");
	return;
    }

    PSP_getTypedMsgBuf(msg, &used, "maxRSS", &maxRSS, sizeof(maxRSS));
    PSP_getTypedMsgBuf(msg, &used, "maxVM", &maxVM, sizeof(maxVM));
    PSP_getTypedMsgBuf(msg, &used, "maxThd", &maxThd, sizeof(maxThd));

    /* session id of job */
    PSP_getTypedMsgBuf(msg, &used, "sessID", &sessID, sizeof(sessID));
    printf(" sessID %d", sessID);

    PSP_getTypedMsgBuf(msg, &used, "avgRSS", &avgRSS, sizeof(avgRSS));
    printf(" RSS %lu/%lu", maxRSS, avgRSS);
    PSP_getTypedMsgBuf(msg, &used, "avgVM", &avgVM, sizeof(avgVM));
    printf(" VM %lu/%lu", maxVM, avgVM);
    PSP_getTypedMsgBuf(msg, &used, "avgThd", &avgThd, sizeof(avgThd));
    printf(" HW-threads %u/%lu", maxThd, avgThd);
}

static void handleStartMsg(DDTypedBufferMsg_t *msg)
{
    size_t used = handleCommonMsg(msg);
    int32_t num;

    PSP_getTypedMsgBuf(msg, &used, "num", &num, sizeof(num));
    printf(" number of children %d", num);
}

static void handleChildMsg(DDTypedBufferMsg_t *msg)
{
    size_t used = handleCommonMsg(msg);

    printf(" prog '%s'", &msg->buf[used]);
}

static void handleLogMsg(DDTypedBufferMsg_t *msg)
{
    size_t used = handleCommonMsg(msg);
    int32_t maxConnected;

    /* total number of children connected to logger */
    PSP_getTypedMsgBuf(msg, &used, "maxConnected", &maxConnected,
		       sizeof(maxConnected));
    printf(" logger conn children %d", maxConnected);

    if (msg->buf[used]) {
	printf(" jobID '%s'", &msg->buf[used]);
    } else {
	printf(" no jobID");
    }
}

static void handleAcctMsg(DDTypedBufferMsg_t *msg)
{
    PStask_ID_t sender = msg->header.sender, logger;
    size_t used = 0;
    int ret;

    /* logger's TID, this identifies a task uniquely */
    PSP_getTypedMsgBuf(msg, &used, "logger", &logger, sizeof(logger));

    logger = *(PStask_ID_t *)msg->buf;

    ret = PSI_kill(logger, 0, true); /* ping the sender */
    if (ret == -1) printf("PSI_kill(%s, 0): %s\n",
			  PSC_printTID(logger), strerror(ret));

    printf("%s: msg from %s:", __func__, PSC_printTID(sender));
    printf(" job %s type ", PSC_printTID(logger));

    switch (msg->type) {
    case PSP_ACCOUNT_QUEUE:
	printf("Q");
	handleQueueMsg(msg);
	break;
    case PSP_ACCOUNT_DELETE:
	printf("D");
	handleCommonMsg(msg);
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
	handleEndMsg(msg);
	break;
    case PSP_ACCOUNT_LOST:
	// ignore
	break;
    default:
	printf("?");
    }
    printf("\n");
}

static void handleSigMsg(DDErrorMsg_t *msg)
{
    char *errstr = strerror(msg->error);

    if (!errstr) errstr = "UNKNOWN";

    printf("%s: msg from %s:", __func__, PSC_printTID(msg->header.sender));
    printf(" task %s: %s\n", PSC_printTID(msg->request), errstr);

    return;
}

static void loop(void)
{
    while (1) {
	DDTypedBufferMsg_t msg;
	PSI_recvMsg((DDBufferMsg_t *)&msg, sizeof(msg), -1, false);

	switch (msg.header.type) {
	case PSP_CD_ACCOUNT:
	    handleAcctMsg(&msg);
	    break;
	case PSP_CD_SIGRES:
	    handleSigMsg((DDErrorMsg_t *)&msg);
	    break;
	default:
	    printf("unexpected message %s\n", PSP_printMsg(msg.header.type));
	}
    }
}

int main(int argc, char *argv[])
{
    poptContext optCon;   /* context for parsing command-line options */
    int rc;

    struct poptOption optionsTable[] = {
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
