/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "psaccount.h"
#include "psaccountinter.h"
#include "psaccountclient.h"
#include "helper.h"

#include "psaccountkvs.h"

#define MALLOC_SIZE 512

char line[100];

/**
 * @brief Save a string into a buffer and let it dynamically grow if needed.
 *
 * @param strSave The string to write to the buffer.
 *
 * @param buffer The buffer to write the string to.
 *
 * @param bufSize The current size of the buffer.
 *
 * @return Returns a pointer to the buffer.
 */
static char *str2Buf(char *strSave, char *buffer, size_t *bufSize)
{
    size_t lenSave, lenBuf;

    if (!buffer) {
        buffer = umalloc(MALLOC_SIZE);
        *bufSize = MALLOC_SIZE;
        buffer[0] = '\0';
    }

    lenSave = strlen(strSave);
    lenBuf = strlen(buffer);

    while (lenBuf + lenSave + 1 > *bufSize) {
        buffer = urealloc(buffer, *bufSize + MALLOC_SIZE);
        *bufSize += MALLOC_SIZE;
    }

    strcat(buffer, strSave);

    return buffer;
}

static char *showJobs(char *buf, size_t *bufSize)
{
    list_t *pos, *tmp;
    Job_t *job;

    if (list_empty(&JobList.list)) {
	return str2Buf("\nNo current jobs.\n", buf, bufSize);
    }

    buf = str2Buf("\njobs:\n", buf, bufSize);

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) break;

	snprintf(line, sizeof(line), "nr Of Children '%i'\n", job->nrOfChilds);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "total Children '%i'\n", job->totalChilds);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "exit Children '%i'\n", job->childsExit);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "complete '%i'\n", job->complete);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "id '%s'\n", job->jobid);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "jobscript '%i'\n", job->jobscript);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "logger '%s'\n",
		    PSC_printTID(job->logger));
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "start time %s", ctime(&job->startTime));
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "end time %s",
		    job->endTime ? ctime(&job->endTime) : "-\n");
	buf = str2Buf(line, buf, bufSize);

	if (job->jobscript) {
	    psaccAccountInfo_t accData;

	    if (psAccountGetJobInfo(job->jobscript, &accData)) {
		snprintf(line, sizeof(line), "cputime '%zu' utime '%zu'"
			    " stime '%zu' mem '%zu' vmem '%zu'\n",
			    accData.cputime, accData.utime, accData.stime,
			    accData.mem, accData.vmem);
		buf = str2Buf(line, buf, bufSize);
	    }
	}

	buf = str2Buf("-\n", buf, bufSize);
    }

    return buf;
}

static char *showClient(char *buf, size_t *bufSize, int detailed)
{
    struct list_head *pos;
    Client_t *client;

    if (list_empty(&AccClientList.list)) {
	return str2Buf("\nNo current clients.\n", buf, bufSize);
    }

    buf = str2Buf("\nclients:\n", buf, bufSize);

    list_for_each(pos, &AccClientList.list) {

	if ((client = list_entry(pos, Client_t, list)) == NULL) break;

	snprintf(line, sizeof(line), "taskID '%s'\n",
		    PSC_printTID(client->taskid));
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "rank '%i'\n", client->rank);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "logger '%s'\n",
		    PSC_printTID(client->logger));
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "account '%i'\n", client->doAccounting);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "type '%s'\n",
		    clientType2Str(client->type));
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "uid '%i'\n", client->uid);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "gid '%i'\n", client->gid);
	buf = str2Buf(line, buf, bufSize);

	snprintf(line, sizeof(line), "page size '%zu'\n", client->pageSize);
	buf = str2Buf(line, buf, bufSize);

	if (detailed) {

	    snprintf(line, sizeof(line), "max mem '%zu'\n",
						client->data.maxRss * pageSize);
	    buf = str2Buf(line, buf, bufSize);

	    snprintf(line, sizeof(line), "max vmem '%zu'\n",
						client->data.maxVsize);
	    buf = str2Buf(line, buf, bufSize);

	    snprintf(line, sizeof(line), "cutime '%zu'\n", client->data.cutime);
	    buf = str2Buf(line, buf, bufSize);

	    snprintf(line, sizeof(line), "cstime '%zu'\n", client->data.cstime);
	    buf = str2Buf(line, buf, bufSize);

	    snprintf(line, sizeof(line), "cputime '%zu'\n",
			client->data.cputime);
	    buf = str2Buf(line, buf, bufSize);

	    snprintf(line, sizeof(line), "max threads '%zu'\n",
						client->data.maxThreads);
	    buf = str2Buf(line, buf, bufSize);
	}

	buf = str2Buf("-\n", buf, bufSize);
    }

    return buf;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;

    buf = str2Buf("use show [clients|dclients|jobs]\n", buf, &bufSize);
    return buf;
}

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (!key) {
	buf = str2Buf("use key [clients|dclients|jobs]\n", buf, &bufSize);
        return buf;
    }

    /* show current clients */
    if (!(strcmp(key, "clients"))) {
        return showClient(buf, &bufSize, 0);
    }

    /* show current clients in detail */
    if (!(strcmp(key, "dclients"))) {
        return showClient(buf, &bufSize, 1);
    }

    /* show current jobs */
    if (!(strcmp(key, "jobs"))) {
        return showJobs(buf, &bufSize);
    }

    buf = str2Buf("invalid key, use [clients|dclients|jobs]\n", buf, &bufSize);
    return buf;
}
