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

#include "pluginmalloc.h"
#include "pluginhostlist.h"

#include "psslurmlog.h"

#include "psslurmconfig.h"

const ConfDef_t CONFIG_VALUES[] =
{
    { "SLURM_CONF", 0,
	"file",
	"/etc/slurm/slurm.conf",
	"Configuration file of slurm" },
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
	} else {
	    mlog("%s: unknown node option '%s'", __func__, next);
	    return 0;
	}
	next = strtok_r(NULL, delimiters, &toksave);
    }
    return 1;
}

static int setMyHostDef(char *hn, char *line)
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

    //mlog("%s: hn:%s  host: %s args: %s\n", __func__, hn, hostlist, hostopt);
    host = strtok_r(hostlist, delimiters, &toksave);
    while (host) {
	if ((ptr = strchr(host, '.'))) ptr[0] = '\0';
	if (!(strcmp(host, hn))) {
	    mlog("%s: found my host: %s args: %s\n", __func__, host, hostopt);
	    addConfigEntry(&Config, "SLURM_HOSTNAME", host);
	    ufree(hostlist);
	    if (!(addHostOptions(hostopt))) return 0;
	    return 1;
	}
	host = strtok_r(NULL, delimiters, &toksave);
    }
    ufree(hostlist);
    return 1;
}

int parseSlurmConf()
{
    struct list_head *pos;
    Config_t *config;
    char hn[1024], *hostline;

    if ((gethostname(hn, sizeof(hn))) < 0) {
	mlog("%s: getting hostname failed\n", __func__);
	return 0;
    }

    if (list_empty(&SlurmConfig.list)) return 0;

    list_for_each(pos, &SlurmConfig.list) {
	if (!(config = list_entry(pos, Config_t, list))) return 0;

	/* parse all NodeName entries */
	if (!(strcmp(config->key, "NodeName"))) {
	    hostline = ustrdup(config->value);
	    if (!(setMyHostDef(hn, hostline))) {
		ufree(hostline);
		return 0;
	    }
	    ufree(hostline);
	}
    }
    return 1;
}

int initConfig(char *filename)
{
    char *slurmConfFile;

    if ((parseConfigFile(filename, &Config)) < 0) return 0;
    setConfigDefaults(&Config, CONFIG_VALUES);
    if ((verifyConfig(&Config, CONFIG_VALUES)) != 0) return 0;

    if (!(slurmConfFile = getConfValueC(&Config, "SLURM_CONF"))) return 0;
    if (parseConfigFile(slurmConfFile, &SlurmConfig) < 0) return 0;
    if (!(parseSlurmConf())) return 0;

    return 1;
}
