/*
 * ParaStation
 *
 * Copyright (C) 2011-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomenv.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pscommon.h"
#include "pluginconfig.h"
#include "pluginmalloc.h"

#include "psmom.h"
#include "psmomconfig.h"
#include "psmomlog.h"

Env_t EnvList;

void initEnvList()
{
    INIT_LIST_HEAD(&EnvList.list);
}

static Env_t *findEnvEntry(char *envStr)
{
    struct list_head *pos;
    Env_t *env;
    size_t len;
    char *val;

    if (list_empty(&EnvList.list)) return NULL;

    if (!(val = strchr(envStr, '='))) return NULL;
    len = strlen(envStr) - (strlen(val) - 1);

    list_for_each(pos, &EnvList.list) {
	if ((env = list_entry(pos, Env_t, list)) == NULL) return NULL;

	if (!strncmp(env->var, envStr, len)) {
	    return env;
	}
    }
    return NULL;
}

Env_t *addEnv(char *envStr)
{
    Env_t *env = findEnvEntry(envStr);


    /* use old entry */
    if (env) {
	ufree(env->var);
	if (!(env->var = ustrdup(envStr))) {
	    mlog("%s: out of memory\n", __func__);
	    exit(1);
	}
	return env;
    }

    /* create new entry */
    env = (Env_t *) umalloc(sizeof(Env_t));

    if (!(env->var = ustrdup(envStr))) {
	mlog("%s: out of memory\n", __func__);
	exit(1);
    }

    list_add_tail(&(env->list), &EnvList.list);

    return env;
}

char *getEnvValue(char *name)
{
    struct list_head *pos;
    Env_t *env;
    size_t len;
    char *val;

    len = strlen(name);

    if (list_empty(&EnvList.list)) return NULL;

    list_for_each(pos, &EnvList.list) {
	if ((env = list_entry(pos, Env_t, list)) == NULL) return NULL;

	if (!strncmp(env->var, name, len)) {
	    if (!(val = strchr(env->var, '='))) return NULL;
	    val++;
	    return val;
	}
    }
    return NULL;
}

/**
 * @brief Count the entries in the environment list.
 */
static int countEnv(void)
{
    int count = 0;
    struct list_head *pos;
    list_for_each(pos, &EnvList.list) count++;

    return count;
}

char **getEnvArray(int* count)
{
    char **env;
    int envTotal;
    struct list_head *pos;
    Env_t *item;

    if (list_empty(&EnvList.list)) return NULL;
    if ((envTotal = countEnv()) < 1) return NULL;

    env = umalloc((envTotal + 1) * sizeof(char *));
    *count = 0;

    list_for_each(pos, &EnvList.list) {
	if ((item = list_entry(pos, Env_t, list)) == NULL) break;

	if (envTotal == *count) break;
	env[(*count)++] = ustrdup(item->var);
    }
    env[*count] = NULL;
    return env;
}

void setEnvVars()
{
    struct list_head *pos;
    Env_t *env;

    if (list_empty(&EnvList.list)) return;

    list_for_each(pos, &EnvList.list) {
	if ((env = list_entry(pos, Env_t, list)) == NULL) break;

	/* write env string to environment */
	if ((putenv(env->var)) != 0) {
	    mlog("%s: out of memory", __func__);
	    exit(1);
	}
    }
    return;
}

static bool environmentVisitor(char *key, char *value, const void *info)
{
    if (!strcmp(key, "JOB_ENV")) addEnv(value);
    return false;
}

void setupPBSEnv(Job_t *job, int interactive)
{
    int portRM, end = 0;
    char *next, *tmp, *nodes, *exec_host, *value, *toksave, *pStart;
    char *variList, *exec_gpus, *confTmpDir, *rPtr;
    const char delim_host[] ="+";
    const char delim_nodes[] = ",:";
    char name[400];
    size_t len;

    /* set PBS environment vars */
    if (!(tmp = getJobDetail(&job->data, "Variable_List", NULL))) {
	mlog("%s: job detail variList not found\n", __func__);
	exit(1);
    }

    variList = ustrdup(tmp);
    pStart = variList;
    next = pStart;

    while (1) {
	if (*next == '\\' && (*(next+1) == ',' || *(next+1) == '\n'
	    || *(next+1) == '\\')) {
	    /* remove quoting */
	    rPtr = next;
	    while (*rPtr != '\0') {
		*rPtr = *(rPtr + 1);
		rPtr++;
	    }
	    next += 1;
	    continue;
	} else if (*next == ',' || *next == '\n' || *next == '\0') {
	    if (*next == '\0') end = 1;
	    *next = '\0';

	    if ((value = strchr(pStart,'='))) {
		value++;
		len = strlen(pStart) - strlen(value) - 1;
	    } else {
		len = strlen(pStart) -1;
	    }
	    strncpy(name, pStart, len);
	    name[len] = '\0';

	    /* skip "PBS_O_" */
	    tmp = pStart + 6;

	    if (!strcmp(name, "PBS_O_HOME")) {
		addEnv(tmp);
	    } else if (!strcmp(name, "PBS_O_USER")) {
		addEnv(tmp);
	    } else if (!strcmp(name, "PBS_O_SHELL")) {
		addEnv(tmp);
	    } else if (!strcmp(name, "PBS_O_PATH")) {
		addEnv(tmp);
	    } else if (!strcmp(name, "PBS_O_LOGNAME")) {
		addEnv(tmp);
	    }
	    addEnv(pStart);

	    if (end) break;
	    pStart = ++next;
	} else {
	    next++;
	}
    }
    ufree(variList);

    snprintf(name, sizeof(name), "PBS_NUM_NODES=%d", job->nrOfUniqueNodes);
    addEnv(name);

    if ((next = getJobDetail(&job->data, "Resource_List", "nodes"))) {
	int setPPN = 0;

	nodes = ustrdup(next);

	next = strtok_r(nodes + 1, delim_nodes, &toksave);
	while (next) {
	    if ((value = strchr(next,'='))) {
		value++;
	    }

	    /* processes per node */
	    if (!strncmp(next, "ppn=", 4)) {
		snprintf(name, sizeof(name), "PBS_NUM_PPN=%s", value);
		addEnv(name);
		setPPN = 1;
		break;
	    }
	    next = strtok_r(NULL, delim_nodes, &toksave);
	}

	if (!setPPN) {
	    snprintf(name, sizeof(name), "PBS_NUM_PPN=1");
	    addEnv(name);
	}
	ufree(nodes);
    }
    if ((next = getJobDetail(&job->data, "euser", NULL))) {
	snprintf(name, sizeof(name), "USER=%s", next);
	addEnv(name);
    }
    if ((next = getJobDetail(&job->data, "egroup", NULL))) {
	snprintf(name, sizeof(name), "GROUP=%s", next);
	addEnv(name);
    }
    if ((next = getJobDetail(&job->data, "Job_Name", NULL))) {
	snprintf(name, sizeof(name), "PBS_JOBNAME=%s", next);
	addEnv(name);
    }
    if ((next = getJobDetail(&job->data, "queue", NULL))) {
	snprintf(name, sizeof(name), "PBS_QUEUE=%s", next);
	addEnv(name);
    }

    if ((exec_host = getJobDetail(&job->data, "exec_host", NULL))) {
	int oldmask;
	FILE *fp;
	char filename[200];
	char *nodefiles, *ehost;
	struct stat statbuf;

	/* set umask */
	oldmask = umask(0133);

	nodefiles = getConfValueC(config, "DIR_NODE_FILES");
	snprintf(filename, sizeof(filename), "%s/%s", nodefiles, job->hashname);

	if (stat(filename, &statbuf) == -1) {
	    if (!(fp = fopen(filename, "w"))) {
		mlog("%s: open nodefile '%s' for writing failed\n",
			__func__, filename);
		exit(1);
	    }

	    ehost = ustrdup(exec_host);
	    next = strtok_r(ehost, delim_host, &toksave);
	    while (next) {
		if ((value = strchr(next,'/'))) {
		    value[0] = '\0';
		    fprintf(fp, "%s\n", next);
		}
		next = strtok_r(NULL, delim_host, &toksave);
	    }
	    ufree(ehost);
	    fclose(fp);

	    if ((chmod(filename, 0400)) == -1) {
		mlog("%s: chmod(%s) failed : %s\n", __func__, filename,
			strerror(errno));
	    } else if ((chown(filename, job->passwd.pw_uid, job->passwd.pw_gid))
			    == -1) {
		mlog("%s: chown(%s) failed : %s\n", __func__, filename,
			strerror(errno));
	    }
	}
	snprintf(name, sizeof(name), "PBS_NODEFILE=%s", filename);
	addEnv(name);

	/* reset umask */
	umask(oldmask);
    }

    snprintf(name, sizeof(name), "PBS_JOBID=%s", job->id);
    addEnv(name);

    addEnv("ENVIRONMENT=BATCH");

    if (interactive) {
	addEnv("PBS_ENVIRONMENT=PBS_INTERACTIVE");
    } else {
	addEnv("PBS_ENVIRONMENT=PBS_BATCH");
    }

    /* add psmom plugin version */
    snprintf(name, sizeof(name), "PBS_VERSION=psmom-%i", version);
    addEnv(name);

    /* use ps node id for PBS_NODENUM */
    snprintf(name, sizeof(name), "PBS_NODENUM=%i", PSC_getMyID());
    addEnv(name);

    addEnv("PBS_TASKNUM=1");
    addEnv("PBS_VNODENUM=0");

    portRM = getConfValueI(config, "PORT_RM");
    snprintf(name, sizeof(name), "PBS_MOMPORT=%i", portRM);
    addEnv(name);

    /* add job cookie */
    snprintf(name, sizeof(name), "PBS_JOBCOOKIE=%s", job->cookie);
    addEnv(name);

    if ((exec_gpus = getJobDetail(&job->data, "exec_gpus", NULL))) {
	int oldmask;
	FILE *fp;
	char filename[200];
	char *nodefiles, *ehost;
	struct stat statbuf;

	/* set umask */
	oldmask = umask(0133);

	nodefiles = getConfValueC(config, "DIR_NODE_FILES");
	snprintf(filename, sizeof(filename), "%s/%sgpu", nodefiles, job->id);

	if (stat(filename, &statbuf) == -1) {
	    if (!(fp = fopen(filename, "w"))) {
		mlog("%s: open nodefile-gpu '%s' failed\n", __func__,
			filename);
		exit(1);
	    }

	    ehost = ustrdup(exec_gpus);
	    next = strtok_r(ehost, delim_host, &toksave);
	    while (next) {
		if ((value = strchr(next,'/'))) {
		    strcpy(value, value+1);
		    fprintf(fp, "%s\n", next);
		}
		next = strtok_r(NULL, delim_host, &toksave);
	    }
	    ufree(ehost);
	    fclose(fp);
	}
	snprintf(name, sizeof(name), "PBS_GPUFILE=%s", filename);
	addEnv(name);

	/* reset umask */
	umask(oldmask);
    }

    /* setup tmp dir */
    confTmpDir = getConfValueC(config, "DIR_TEMP");
    if (confTmpDir) {
	char tmpDir[256];

	snprintf(tmpDir, sizeof(tmpDir), "%s/%s", confTmpDir, job->hashname);
	snprintf(name, sizeof(name), "TMPDIR=%s", tmpDir);
	addEnv(name);
    }

    /* set additional env vars from config */
    traverseConfig(config, environmentVisitor, NULL);
}
