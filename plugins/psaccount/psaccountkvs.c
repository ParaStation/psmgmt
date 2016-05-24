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
#include "psaccountconfig.h"
#include "psaccountlog.h"
#include "pluginmalloc.h"
#include "pluginlog.h"
#include "plugin.h"

#include "psaccountkvs.h"

static char line[100];

FILE *memoryDebug = NULL;

/**
 * @brief Show current jobs.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated job information.
 */
static char *showJobs(char *buf, size_t *bufSize)
{
    list_t *pos, *tmp;
    Job_t *job;

    if (list_empty(&JobList.list)) {
	return str2Buf("\nNo current jobs.\n", &buf, bufSize);
    }

    str2Buf("\njobs:\n", &buf, bufSize);

    list_for_each_safe(pos, tmp, &JobList.list) {
	if ((job = list_entry(pos, Job_t, list)) == NULL) break;

	snprintf(line, sizeof(line), "nr Of Children '%i'\n", job->nrOfChilds);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "total Children '%i'\n", job->totalChilds);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "exit Children '%i'\n", job->childsExit);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "complete '%i'\n", job->complete);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "grace '%i'\n", job->grace);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "id '%s'\n", job->jobid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "jobscript '%i'\n", job->jobscript);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "logger '%s'\n",
		    PSC_printTID(job->logger));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "start time %s", ctime(&job->startTime));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "end time %s",
		    job->endTime ? ctime(&job->endTime) : "-\n");
	str2Buf(line, &buf, bufSize);

	if (job->jobscript) {
	    psaccAccountInfo_t accData;

	    if (psAccountGetJobInfo(job->jobscript, &accData)) {
		snprintf(line, sizeof(line), "cputime '%zu' utime '%zu'"
			    " stime '%zu' mem '%zu' vmem '%zu'\n",
			    accData.cputime, accData.utime, accData.stime,
			    accData.mem, accData.vmem);
		str2Buf(line, &buf, bufSize);
	    }
	}

	str2Buf("-\n", &buf, bufSize);
    }

    return buf;
}

/**
 * @brief Show current clients.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated client information.
 */
static char *showClient(char *buf, size_t *bufSize, int detailed)
{
    struct list_head *pos;
    Client_t *client;

    if (list_empty(&AccClientList.list)) {
	return str2Buf("\nNo current clients.\n", &buf, bufSize);
    }

    str2Buf("\nclients:\n", &buf, bufSize);

    list_for_each(pos, &AccClientList.list) {

	if ((client = list_entry(pos, Client_t, list)) == NULL) break;

	snprintf(line, sizeof(line), "taskID '%s'\n",
		    PSC_printTID(client->taskid));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "rank '%i'\n", client->rank);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "logger '%s'\n",
		    PSC_printTID(client->logger));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "account '%i'\n", client->doAccounting);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "type '%s'\n",
		    clientType2Str(client->type));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "uid '%i'\n", client->uid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "gid '%i'\n", client->gid);
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "page size '%zu'\n",
		    client->data.pageSize);

	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "start time %s",
		    ctime(&client->startTime));
	str2Buf(line, &buf, bufSize);

	snprintf(line, sizeof(line), "end time %s",
		    client->endTime ? ctime(&client->endTime) : "-\n");
	str2Buf(line, &buf, bufSize);

	if (detailed) {

	    snprintf(line, sizeof(line), "max mem '%zu'\n",
						client->data.maxRss * pageSize);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "max vmem '%zu'\n",
						client->data.maxVsize);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "cutime '%zu'\n", client->data.cutime);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "cstime '%zu'\n", client->data.cstime);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "cputime '%zu'\n",
			client->data.cputime);
	    str2Buf(line, &buf, bufSize);

	    snprintf(line, sizeof(line), "max threads '%zu'\n",
						client->data.maxThreads);
	    str2Buf(line, &buf, bufSize);
	}

	str2Buf("-\n", &buf, bufSize);
    }

    return buf;
}

/**
 * @brief Show current configuration.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showConfig(char *buf, size_t *bufSize)
{
    char empty[] = "";
    int i;

    str2Buf("\n", &buf, bufSize);

    for (i=0; i<configValueCount; i++) {
	char *name, *val;

	name = CONFIG_VALUES[i].name;
	if (!(val = getConfParamC(name))) {
	    val = empty;
	}
	snprintf(line, sizeof(line), "%21s = %s\n", name, val);
	str2Buf(line, &buf, bufSize);
    }

    return buf;
}

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int ret;
    Config_t *conf;

    /* search in config for given key */
    if ((findConfigDef(key))) {

	if ((ret = verfiyConfOption(key, value)) != 0) {
	    if (ret == 1) {
		str2Buf("\nInvalid key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set : use 'plugin help psaccount' "
			      "for help.\n", &buf, &bufSize);
	    } else if (ret == 2) {
		str2Buf("\nThe key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set has to be numeric.\n", &buf,
			      &bufSize);
	    }
	    return buf;
	}

	/* save new config value */
	if ((conf = getConfObject(key))) {
	    if (conf->value) ufree(conf->value);
	    conf->value = ustrdup(value);
	} else {
	    addConfig(key, value);
	}

	snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	str2Buf(line, &buf, &bufSize);
	return buf;
    }

    if (!(strcmp(key, "memdebug"))) {
	if (memoryDebug) fclose(memoryDebug);

	if ((memoryDebug = fopen(value, "w+"))) {
	    finalizePluginLogger();
	    initPluginLogger(NULL, memoryDebug);
	    maskPluginLogger(PLUGIN_LOG_MALLOC);
	    str2Buf("\nmemory logging to '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    str2Buf("'\n", &buf, &bufSize);
	    return buf;
	} else {
	    str2Buf("\nopening file '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    str2Buf("' for writing failed\n", &buf, &bufSize);
	    return buf;
	}
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("' for cmd set : use 'plugin help psaccount' for help.\n",
	    &buf, &bufSize);

    return buf;
}

char *unset(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;
    const ConfDef_t *confDef;
    Config_t *conf;

    /* search in config for given key */
    if ((conf = getConfObject(key))) {

	if ((confDef = findConfigDef(key)) && confDef->def) {
	    /* reset the config object to its default value */
	    if (conf->value) ufree(conf->value);
	    conf->value = ustrdup(confDef->def);
	} else {
	    /* delete the config object */
	    delConfig(conf);
	}
	return buf;
    }

    if (!(strcmp(key, "memdebug"))) {
	if (memoryDebug) {
	    finalizePluginLogger();
	    fclose(memoryDebug);
	    memoryDebug = NULL;
	    initPluginLogger(NULL, psaccountlogfile);
	}
	return str2Buf("Stopped memory debugging\n", &buf, &bufSize);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("' for cmd unset : use 'plugin help psaccount' for help.\n",
	    &buf, &bufSize);

    return buf;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int i;
    char type[10];

    str2Buf("\n# configuration options #\n\n", &buf, &bufSize);
    for (i=0; i<configValueCount; i++) {
	snprintf(type, sizeof(type), "<%s>", CONFIG_VALUES[i].type);
	snprintf(line, sizeof(line), "%21s\t%8s    %s\n", CONFIG_VALUES[i].name,
		type, CONFIG_VALUES[i].desc);
	str2Buf(line, &buf, &bufSize);
    }

    str2Buf("\nuse show [clients|dclients|jobs|config]\n", &buf, &bufSize);

    return buf;
}

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (!key) {
	str2Buf("use key [clients|dclients|jobs|config]\n", &buf, &bufSize);
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

    /* show current config */
    if (!(strcmp(key, "config"))) {
	return showConfig(buf, &bufSize);
    }

    str2Buf("invalid key, use [clients|dclients|jobs|config]\n",
	    &buf, &bufSize);
    return buf;
}
