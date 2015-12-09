/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include "pluginmalloc.h"
#include "pluginhostlist.h"

#include "psslurmlog.h"
#include "psslurmgres.h"

#include "psslurmconfig.h"

const ConfDef_t CONFIG_VALUES[] =
{
    { "SLURM_CONF", 0,
	"file",
	"/etc/slurm/slurm.conf",
	"Configuration file of slurm" },
    { "SLURM_GRES_CONF", 0,
	"file",
	"/etc/slurm/gres.conf",
	"Gres configuration file of slurm" },
    { "DIR_SCRIPTS", 0,
	"path",
	SPOOL_DIR "/scripts",
	"Directory to search for prologue/epilogue scripts" },
    { "DIR_JOB_FILES", 0,
	"path",
	SPOOL_DIR "/jobs",
	"Directory to store jobfiles" },
    { "DIR_TEMP", 0,
	"path",
	NULL,
	"Directory for the job specific scratch space" },
    { "TIMEOUT_PROLOGUE", 1,
	"sec",
	"300",
	"Number of seconds to allow the prologue scripts to run" },
    { "TIMEOUT_EPILOGUE", 1,
	"sec",
	"300",
	"Number of seconds to allow the epilogue scripts to run" },
    { "TIMEOUT_PE_GRACE", 1,
	"sec",
	"60",
	"Number of seconds until the local PE-logue timeout will be enforced" },
    { "TIMEOUT_CHILD_CONNECT", 1,
	"sec",
	"10",
	"Number of seconds until a child must connect to the mother"
	    " superior" },
    { "CLEAN_TEMP_DIR", 1,
	"bool",
	"1",
	"Clean scratch space directory on startup" },
    { "OFFLINE_PELOGUE_TIMEOUT", 1,
	"bool",
	"1",
	"Set my node offline if a pro/epilogue script timed out" },
    { "TIMEOUT_SCRIPT", 0,
	"string",
	NULL,
	"Script which is called when a prologue/epilogue timeout occurs" },
    { "ENFORCE_BATCH_START", 1,
	"bool",
	"1",
	"Enforce jobs to use the Batchsystem, only admin user may use mpiexec "
	    "directly" },
    { "PELOGUE_ENV_FILTER", 0,
	"list",
	"SLURM_*",
	"Positive filter which will allow forwarding of selected "
	"environment variables to prologue/epilogue." },
    { "RLIMITS_SOFT", 0,
	"list",
	NULL,
	"Set soft resource limits for user processes" },
    { "RLIMITS_HARD", 0,
	"list",
	NULL,
	"Set hard resource limits for user processes" },
    { "DIST_START", 0,
	"bool",
	"0",
	"Distribute mpiexec service processes at startup" },
    { "MEMBIND_DEFAULT", 0,
	"string",
	"local",
	"Default value to be used for memory binding (none|local)" },
    { NULL, 0, NULL, NULL, NULL },
};

static int addHostOptions(char *options)
{
    char *toksave, *next;
    const char delimiters[] =" \n";

    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!(strncasecmp(next, "Sockets=", 8))) {
	    addConfigEntry(&Config, "SLURM_SOCKETS", next+8);
	} else if (!(strncasecmp(next, "CoresPerSocket=", 15))) {
	    addConfigEntry(&Config, "SLURM_CORES_PER_SOCKET", next+15);
	} else if (!(strncasecmp(next, "ThreadsPerCore=", 15))) {
	    addConfigEntry(&Config, "SLURM_THREADS_PER_CORE", next+15);
	} else if (!(strncasecmp(next, "CPUs=", 5))) {
	    addConfigEntry(&Config, "SLURM_CPUS", next+5);
	} else if (!(strncasecmp(next, "Feature=", 8))) {
	    addConfigEntry(&Config, "SLURM_FEATURE", next+8);
	} else if (!(strncasecmp(next, "Gres=", 5))) {
	    addConfigEntry(&Config, "SLURM_GRES", next+5);
	} else if (!(strncasecmp(next, "State=", 6))) {
	    addConfigEntry(&Config, "SLURM_STATE", next+6);
	} else if (!(strncasecmp(next, "Procs=", 6))) {
	    addConfigEntry(&Config, "SLURM_PROCS", next+6);
	} else if (!(strncasecmp(next, "Weight=", 7))) {
	    addConfigEntry(&Config, "SLURM_WEIGHT", next+7);
	} else if (!(strncasecmp(next, "RealMemory=", 11))) {
	    addConfigEntry(&Config, "SLURM_REAL_MEMORY", next+11);
	} else if (!(strncasecmp(next, "Boards=", 7))) {
	    addConfigEntry(&Config, "SLURM_BOARDS", next+7);
	} else {
	    mlog("%s: unknown node option '%s'\n", __func__, next);
	    return 0;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }
    return 1;
}

static int parseGresOptions(char *options)
{
    char *toksave, *next;
    const char delimiters[] =" \n";
    char *name, *count, *file, *cpus;

    name = count = file = cpus = NULL;
    next = strtok_r(options, delimiters, &toksave);
    while (next) {
	if (!(strncasecmp(next, "Name=", 5))) {
	    name = ustrdup(next+5);
	} else if (!(strncasecmp(next, "Count=", 6))) {
	    count = ustrdup(next+6);
	} else if (!(strncasecmp(next, "File=", 5))) {
	    file = ustrdup(next+5);
	} else if (!(strncasecmp(next, "CPUs=", 5))) {
	    cpus = ustrdup(next+5);
	} else {
	    mlog("%s: unknown gres option '%s'\n", __func__, next);
	    return 0;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }

    addGresConf(name, count, file, cpus);
    ufree(name);
    ufree(count);
    ufree(file);
    ufree(cpus);
    return 1;
}

static int setMyHostDef(char *hn, char *line, int gres)
{
    char *hostopt, *hostrange, *hostlist, *host, *toksave, *ptr;
    uint32_t numHosts;
    const char delimiters[] =", \n";

    if (!(hostopt = strchr(line, ' '))) {
	mlog("%s: invalid node definition '%s'\n", __func__, line);
	return 0;
    }
    hostopt[0] = '\0';
    hostopt++;
    hostrange = line;

    if (!(hostlist = expandHostList(hostrange, &numHosts))) {
	mlog("%s: expanding NodeName '%s' failed\n", __func__, hostrange);
	return 0;
    }

    /*
    mlog("%s: gres:%i hn:%s  host: %s args: %s\n", __func__, gres, hn,
    	    hostlist, hostopt);
    */

    host = strtok_r(hostlist, delimiters, &toksave);
    while (host) {
	if ((ptr = strchr(host, '.'))) ptr[0] = '\0';
	if (gres) {
	    if (!(strcmp(host, hn))) {
		if (!(parseGresOptions(hostopt))) return 0;
	    }
	} else {
	    if (!(strcmp(host, "DEFAULT"))) {
		mlog("%s: found the default host definition\n", __func__);
		addHostOptions(hostopt);
	    }
	    if (!(strcmp(host, hn))) {
		mlog("%s: found my host: %s args: %s\n", __func__, host,
			hostopt);
		addConfigEntry(&Config, "SLURM_HOSTNAME", host);
		ufree(hostlist);
		if (!(addHostOptions(hostopt))) return 0;
		return 1;
	    }
	}
	host = strtok_r(NULL, delimiters, &toksave);
    }
    ufree(hostlist);
    return 1;
}

int parseSlurmConf(char *hn, struct list_head *confList, int gres)
{
    struct list_head *pos;
    Config_t *config;
    char *hostline, *tmp;

    list_for_each(pos, confList) {
	if (!(config = list_entry(pos, Config_t, list))) return 0;

	/* parse all NodeName entries */
	if (!(strcmp(config->key, "NodeName"))) {
	    hostline = ustrdup(config->value);
	    if (!(setMyHostDef(hn, hostline, gres))) {
		ufree(hostline);
		return 0;
	    }
	    ufree(hostline);
	} else if (gres && !(strcmp(config->key, "Name"))) {
	    tmp = umalloc(strlen(config->value) +6 + 1);
	    snprintf(tmp, strlen(config->value) +6, "Name=%s", config->value);
	    //mlog("%s: Gres single name '%s'\n", __func__, tmp);
	    parseGresOptions(tmp);
	    ufree(tmp);
	}
    }
    return 1;
}

/* TODO: identify and check important values */
static int verifySlurmConf()
{
    if (!getConfValueC(&Config, "SLURM_CPUS")) {
	mlog("%s: invalid SLURM_CPUS\n", __func__);
	return 0;
    }
    if (!getConfValueC(&Config, "SLURM_HOSTNAME")) {
	mlog("%s: invalid SLURM_HOSTNAME\n", __func__);
	return 0;
    }
    if (!getConfValueC(&Config, "SLURM_SOCKETS")) {
	mlog("%s: invalid SLURM_SOCKETS\n", __func__);
	return 0;
    }
    if (!getConfValueC(&Config, "SLURM_CORES_PER_SOCKET")) {
	mlog("%s: invalid SLURM_CORES_PER_SOCKET\n", __func__);
	return 0;
    }
    if (!getConfValueC(&Config, "SLURM_THREADS_PER_CORE")) {
	mlog("%s: invalid SLURM_THREADS_PER_CORE\n", __func__);
	return 0;
    }
    if (!getConfValueC(&Config, "SLURM_BOARDS")) {
	/* set default boards */
	addConfigEntry(&Config, "SLURM_BOARDS", "1");
    }

    return 1;
}

int initConfig(char *filename)
{
    char *slurmConfFile;
    char hn[1024];
    struct stat sbuf;

    if ((gethostname(hn, sizeof(hn))) < 0) {
	mlog("%s: getting my hostname failed\n", __func__);
	return 0;
    }

    /* parse plugin config file */
    if ((parseConfigFile(filename, &Config)) < 0) return 0;
    setConfigDefaults(&Config, CONFIG_VALUES);
    if ((verifyConfig(&Config, CONFIG_VALUES)) != 0) return 0;

    /* parse slurm config file */
    if (!(slurmConfFile = getConfValueC(&Config, "SLURM_CONF"))) return 0;
    if (parseConfigFileQ(slurmConfFile, &SlurmConfig) < 0) return 0;
    if (!(parseSlurmConf(hn, &SlurmConfig.list, 0))) return 0;
    if (!(verifySlurmConf())) return 0;

    /* parse optional slurm gres config file */
    if (!(slurmConfFile = getConfValueC(&Config, "SLURM_GRES_CONF"))) return 0;
    if (stat(slurmConfFile, &sbuf) == -1) return 1;
    if (parseConfigFileQ(slurmConfFile, &SlurmGresTmp) < 0) return 0;

    INIT_LIST_HEAD(&SlurmGresConfig.list);
    if (!(parseSlurmConf(hn, &SlurmGresTmp.list, 1))) return 0;
    freeConfig(&SlurmGresTmp);

    return 1;
}
