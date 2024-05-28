/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomrecover.h"

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>

#include "list.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psmom.h"
#include "psmomconfig.h"
#include "psmomlist.h"
#include "psmomlog.h"

void recoverJobInfo()
{
    FILE *fp;
    struct dirent *d;
    DIR *dir;
    char accFile[400], value[100] = {'\0'}, buf[101] = {'\0'};
    char *jobid, *servername;
    Job_t *job;
    struct passwd *result;

    char *accPath = getConfValueC(config, "DIR_JOB_ACCOUNT");

    if (!(dir = opendir(accPath))) {
	mlog("%s: opening accounting dir '%s' failed\n", __func__, accPath);
	return;
    }

    while ((d = readdir(dir))) {
	if ((!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))) continue;

	mlog("recovering job '%s'\n", d->d_name);
	snprintf(accFile, sizeof(accFile), "%s/%s", accPath, d->d_name);

	if (!(fp = fopen(accFile, "r"))) {
	    mlog("%s: open job account file '%s' for reading failed\n",
		    __func__, accFile);
	    continue;
	}

	/* read job id */
	if ((fscanf(fp, "%100s\n", buf)) != 1) {
	    mlog("%s: invalid job name for job '%s'\n", __func__, d->d_name);
	    continue;
	}
	jobid = ustrdup(buf);

	/* read server name */
	if ((fscanf(fp, "%100s\n", buf)) != 1) {
	    mlog("%s: invalid server name for job '%s'\n", __func__, d->d_name);
	    continue;
	}
	servername = ustrdup(buf);

	/* re-add the job */
	job = addJob(jobid, servername);

	/* read job hashname */
	if ((fscanf(fp, "%100s\n", buf)) != 1) {
	    mlog("%s: invalid hash name for job '%s'\n", __func__, d->d_name);
	    continue;
	}
	job->hashname = ustrdup(buf);

	/* read job username */
	if ((fscanf(fp, "%100s\n", buf)) != 1) {
	    mlog("%s: invalid user name for job '%s'\n", __func__, d->d_name);
	    continue;
	}
	job->user = ustrdup(buf);

	/* read saved accounting data */
	while (fscanf(fp, "%100s\n", buf) == 1) {
	    char *tmp = strchr(buf, '=');
	    if (!tmp) {
		mlog("%s: ignore invalid account entry '%s'\n", __func__, buf);
		continue;
	    }
	    snprintf(value, sizeof(value), "%s", tmp + 1);
	    tmp[0] = '\0';

	    //mlog("%s: read: '%s' '%s'\n", __func__, buf, value);
	    setEntry(&job->status.list, "resources_used", buf, value);
	}

	/* save users passwd information (needed for copyout phase) */
	job->pwbuf = umalloc(pwBufferSize);
	while ((getpwnam_r(job->user, &job->passwd, job->pwbuf, pwBufferSize,
			&result)) != 0) {
	    if (errno == EINTR) continue;
	    mlog("%s: getpwnam(%s) failed : %s\n", __func__, job->user,
		    strerror(errno));
	    break;
	}

	job->state = JOB_EXIT;
	job->jobscriptExit = -1;
	job->recovered = 1;

	/* release the job if connected to server */
	setJobObitTimer(job);

	fclose(fp);
    }
    closedir(dir);
}

void saveJobInfo(Job_t *job)
{
    list_t *d;
    FILE *fp;
    char accFile[400], *accPath = NULL;

    if (!job) {
	mlog("%s: got invalid job object\n", __func__);
	return;
    }

    if (job->state == JOB_WAIT_OBIT) return;

    accPath = getConfValueC(config, "DIR_JOB_ACCOUNT");
    snprintf(accFile, sizeof(accFile), "%s/%s", accPath, job->hashname);

    if (!(fp = fopen(accFile, "w"))) {
	mlog("%s: open job account file '%s' for writing failed\n", __func__,
	    accFile);
	return;
    }

    /* save job information needed for recover */
    fprintf(fp, "%s\n", job->id);
    fprintf(fp, "%s\n", job->server);
    fprintf(fp, "%s\n", job->hashname);
    fprintf(fp, "%s\n", job->user);

    /* save job account information */
    list_for_each(d, &job->status.list) {
	Data_Entry_t *dEntry = list_entry(d, Data_Entry_t, list);
	if (!dEntry->name || *dEntry->name == '\0') {
	    break;
	}
	if (!strcmp(dEntry->name, "resources_used")) {
	    fprintf(fp, "%s=%s\n", dEntry->resource, dEntry->value);
	}
    }
    fclose(fp);
}
