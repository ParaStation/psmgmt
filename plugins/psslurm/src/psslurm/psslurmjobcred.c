/*
 * ParaStation
 *
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmjobcred.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "pscomplist.h"
#include "psmungehandles.h"

#include "psslurmlog.h"
#include "psslurmpack.h"
#include "psslurmpscomm.h"

void freeJobCred(JobCred_t *cred)
{
    if (!cred) return;

    strShred(cred->sig);
    strShred(cred->username);

    ufree(cred->gids);
    ufree(cred->coresPerSocket);
    ufree(cred->socketsPerNode);
    ufree(cred->nodeRepCount);
    ufree(cred->stepHL);
    ufree(cred->jobCoreBitmap);
    ufree(cred->stepCoreBitmap);
    ufree(cred->jobHostlist);
    ufree(cred->jobConstraints);
    ufree(cred->pwGecos);
    ufree(cred->pwShell);
    ufree(cred->pwDir);
    ufree(cred->gidNames);
    ufree(cred->jobMemAlloc);
    ufree(cred->jobMemAllocRepCount);
    ufree(cred->stepMemAlloc);
    ufree(cred->stepMemAllocRepCount);
    ufree(cred->SELinuxContext);
    ufree(cred->jobNodes);
    ufree(cred->jobAccount);
    ufree(cred->jobAliasList);
    ufree(cred->jobComment);
    ufree(cred->jobPartition);
    ufree(cred->jobReservation);
    ufree(cred->jobStderr);
    ufree(cred->jobStdin);
    ufree(cred->jobStdout);
    ufree(cred->cpuArray);
    ufree(cred->cpuArrayRep);
    ufree(cred->jobExtra);
    ufree(cred->jobLicenses);
    ufree(cred->nodeAddr);
    ufree(cred);
}

JobCred_t *extractJobCred(list_t *gresList, Slurm_Msg_t *sMsg)
{
    char *credStart = sMsg->data->unpackPtr, *credEnd, *sigBuf = NULL;
    JobCred_t *cred = NULL;

    if (!unpackJobCred(sMsg, &cred, gresList, &credEnd)) {
	flog("unpacking job credential failed\n");
	goto ERROR;
    }

    mdbg(PSSLURM_LOG_PART, "%s:", __func__);
    for (uint32_t i = 0; i < cred->nodeArraySize; i++) {
	mdbg(PSSLURM_LOG_PART, " coresPerSocket %u", cred->coresPerSocket[i]);
    }
    for (uint32_t i = 0; i < cred->nodeArraySize; i++) {
	mdbg(PSSLURM_LOG_PART, " socketsPerNode %u", cred->socketsPerNode[i]);
    }
    for (uint32_t i = 0; i < cred->nodeArraySize; i++) {
	mdbg(PSSLURM_LOG_PART, " nodeRepCount %u", cred->nodeRepCount[i]);
    }
    mdbg(PSSLURM_LOG_PART, "\n");

    int credLen = credEnd - credStart;

    /* convert Slurm host-list to PSnodes */
    uint32_t count;
    if (!convHLtoPSnodes(cred->jobHostlist, getNodeIDbySlurmHost,
			 &cred->jobNodes, &count)) {
	flog("resolving PS nodeIDs from %s failed\n", cred->jobHostlist);
	goto ERROR;
    }

    if (count != cred->jobNumHosts) {
	flog("wrong size of hostlist %s (%d instead of %d)\n",
	     cred->jobHostlist, count, cred->jobNumHosts);
	goto ERROR;
    }

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_AUTH) {
	flog("cred len %u stepHostlist '%s' jobHostlist '%s' ctime %lu"
	     " sig '%s' pwGecos '%s' pwDir '%s' pwShell '%s' contrains %s\n",
	     credLen, cred->stepHL, cred->jobHostlist, cred->ctime, cred->sig,
	     cred->pwGecos, cred->pwDir, cred->pwShell, cred->jobConstraints);

	/* memory allocation for job/step */
	for (uint32_t i=0; i<cred->jobMemAllocSize; i++) {
	    flog("idx:%u jobMemAlloc %zu jobMemAllocRepCount:%u\n",
		 i, cred->jobMemAlloc[i], cred->jobMemAllocRepCount[i]);
	}
	for (uint32_t i=0; i<cred->stepMemAllocSize; i++) {
	    flog("idx:%u stepMemAlloc %zu stepMemAllocRepCount:%u\n",
		 i, cred->stepMemAlloc[i], cred->stepMemAllocRepCount[i]);
	}
    }

    return cred;

ERROR:
    free(sigBuf);
    ufree(cred);
    return NULL;
}

bool *getCPUsetFromCoreBitmap(uint32_t total, const char *bitmap)
{
    bool *coreMap = ucalloc(total * sizeof(*coreMap));

    const char *bitstr = bitmap;
    if (!strncmp(bitstr, "0x", 2)) bitstr += 2;

    size_t len = strlen(bitstr);

    int count = 0;

    /* parse Slurm bit string in LSB first order */
    while (len--) {
	int cur = (int)bitstr[len];

	if (!isxdigit(cur)) {
	    mlog("%s: invalid character in core map sting '%c'\n", __func__,
		    cur);
	    ufree(coreMap);
	    return NULL;
	}

	if (isdigit(cur)) {
	    cur -= '0';
	} else {
	    cur = toupper(cur);
	    cur -= 'A' - 10;
	}

	for (int32_t i = 1; i <= 8; i *= 2) {
	    if (cur & i) coreMap[count] = true;
	    count++;
	}
    }

    if (logger_getMask(psslurmlogger) & PSSLURM_LOG_PART) {
	flog("cores '%s' coreMap '", bitstr);
	for (uint32_t i = 0; i < total; i++) mlog("%i", coreMap[i]);
	mlog("'\n");
    }

    return coreMap;
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
