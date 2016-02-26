/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "psmomlog.h"
#include "pluginmalloc.h"

#include "psmomconfig.h"

static int isInit = 0;

const ConfDef_t CONFIG_VALUES[] =
{
    { "PBS_SERVER", 0,
	"string",
	NULL,
	"Address/Hostname of the PBS Server" },
    { "PORT_SERVER", 1,
	"num",
	"15001",
	"Server-port of the PBS server" },
    { "PORT_MOM", 1,
	"num",
	"15002",
	"MOM-port (for batch requests)" },
    { "PORT_RM", 1,
	"num",
	"15003",
	"RM-port (for rm requests)" },
    { "TIME_OBIT", 1,
	"sec",
	"10",
	"Number of seconds to wait for jobs to exit" },
    { "TIME_OBIT_RESEND", 1,
	"sec",
	"45",
	"Interval for trying to resend job obit messages in seconds" },
    { "TIME_KEEP_ALIVE", 1,
	"sec",
	"0",
	"Keep alive interval in seconds" },
    { "TIME_UPDATE", 1,
	"sec",
	"45",
	"Number of seconds between a status update is send to the pbs_server" },
    { "DIR_NODE_FILES", 0,
	"path",
	SPOOL_DIR "/nodefiles",
	"Directory to store node-files" },
    { "DIR_JOB_FILES", 0,
	"path",
	SPOOL_DIR "/jobs",
	"Directory to store jobfiles" },
    { "DIR_SCRIPTS", 0,
	"path",
	SPOOL_DIR "/scripts",
	"Directory to search for prologue/epilogue scripts" },
    { "DIR_SPOOL", 0,
	"path",
	SPOOL_DIR "/temp",
	"Directory for the job output and error files" },
    { "DIR_TEMP", 0,
	"path",
	NULL,
	"Directory for the job specific scratch space" },
    { "DIR_JOB_ACCOUNT", 0,
	"path",
	SPOOL_DIR "/account",
	"Directory to store accounting data" },
    { "DIR_LOCAL_BACKUP", 0,
	"path",
	SPOOL_DIR "/backup",
	"Directory for local backups" },
    { "DISABLE_PELOGUE", 1,
	"bool",
	"0",
	"Disable pro-/epilogue scripts" },
    { "DISABLE_PAM", 1,
	"bool",
	"1",
	"Disable the use of PAM when starting new user sessions" },
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
    { "TIMEOUT_COPY", 1,
	"sec",
	"0",
	"Number of seconds to allow the copy process to run" },
    { "TIMEOUT_CHILD_CONNECT", 1,
	"sec",
	"10",
	"Number of seconds until a child must connect to the mother"
	    " superior" },
    { "TIMEOUT_CHILD_GRACE", 1,
	"sec",
	"30",
	"Number of seconds until a deadly signal will be send when terminating"
	    " a child" },
    { "TIMEOUT_BACKUP", 1,
	"sec",
	"0",
	"Number of seconds until a backup process will be terminated" },
    { "CLEAN_JOBS_FILES", 1,
	"bool",
	"1",
	"Auto clean the job-files directory" },
    { "CLEAN_NODE_FILES", 1,
	"bool",
	"1",
	"Auto clean the node-files directory" },
    { "CLEAN_TEMP_DIR", 1,
	"bool",
	"1",
	"Clean scratch space directory on startup" },
    { "REPORT_FS_SIZE", 0,
	"path",
	NULL,
	"Total and available disc space for a partition is reported" },
    { "CMD_COPY", 0,
	"string",
	"/bin/cp",
	"Command for the local copy-out process" },
    { "OPT_COPY", 0,
	"string",
	"-rp",
	"Options for the local copy-out process" },
    { "COPY_REWRITE", 0,
	"string",
	NULL,
	"Rewrite the destination for the copy-out process" },
    { "SET_ARCH", 0,
	"string",
	NULL,
	"Set architecture of localhost" },
    { "SET_OPSYS", 0,
	"string",
	NULL,
	"Set the operating system of localhost" },
    { "TORQUE_VERSION", 1,
	"num",
	"3",
	"The torque protocol version support (2|3)" },
    { "JOB_ENV", 0,
	"string",
	NULL,
	"Set additional environment variables" },
    { "JOB_UMASK", 1,
	"num",
	NULL,
	"Set default umask for job stdout/error files" },
    { "ENFORCE_BATCH_START", 1,
	"bool",
	"1",
	"Enforce jobs to use the Batchsystem, only admin user may use mpiexec "
	    "directly" },
    { "WARN_USER_DAEMONS", 1,
	"bool",
	"1",
	"Warn if leftover user daemons are found" },
    { "KILL_USER_DAEMONS", 1,
	"bool",
	"0",
	"Kill leftover user daemons" },
    { "DEBUG_MASK", 1,
	"num",
	"0x018000",
	"The debug mask for logging" },
    { "RLIMITS_SOFT", 0,
	"list",
	NULL,
	"Set soft resource limits for user processes" },
    { "RLIMITS_HARD", 0,
	"list",
	NULL,
	"Set hard resource limits for user processes" },
    { "OFFLINE_PELOGUE_TIMEOUT", 1,
	"bool",
	"1",
	"Set my node offline if a pro/epilogue script timed out" },
    { "LOG_ACCOUNT_RECORDS", 1,
	"bool",
	"0",
	"Write accounting records to the end of the stderror file" },
    { "BACKUP_SCRIPT", 0,
	"string",
	NULL,
	"Script which can backup output/error/node files" },
    { "OPT_BACKUP_COPY", 0,
	"string",
	"-rpl",
	"Options for local backup copy command" },
    { "TIMEOUT_SCRIPT", 0,
	"string",
	NULL,
	"Script which is called when a prologue/epilogue timeout occurs" },
};

const int configValueCount = sizeof(CONFIG_VALUES) / sizeof (CONFIG_VALUES[0]);

const ConfDef_t *findConfigDef(char *name)
{
    int i;

    for (i=0; i<configValueCount; i++) {
	if (!(strcmp(name, CONFIG_VALUES[i].name))) {
	    return &CONFIG_VALUES[i];
	}
    }
    return NULL;
}

Config_t *addConfig(char *key, char *value)
{
    Config_t *config;

    config = (Config_t *) umalloc(sizeof(Config_t));
    config->key = ustrdup(key);
    config->value = ustrdup(value);
    list_add_tail(&(config->list), &ConfigList.list);

    return config;
}

Config_t ConfigList;

int initConfig(char *cfgName)
{
    FILE *fp;
    char *line = NULL, *linebuf = NULL, *value, *tmp;
    size_t len = 0, keylen = 0;
    char key[100], *pkey;
    int read, i, ret;
    Config_t *config;

    /* init config list */
    INIT_LIST_HEAD(&ConfigList.list);

    if (!(fp = fopen(cfgName, "r"))) {
	char cwd[200];
	if (!getcwd(cwd, sizeof(cwd))) {
	    cwd[0] = '\0';
	}
	mlog("%s: error opening config cwd:%s file:%s\n", __func__, cwd,
	    cfgName);
	return 0;
    }

    while ((read = getline(&linebuf, &len, fp)) != -1) {
	line = linebuf;

	/* skip comments and empty lines */
	if (read == 0 || line[0] == '\n' || line[0] == '#'
	    || line[0] == '\0') continue;

	/* remove trailing comments */
	if ((tmp = strchr(line, '#'))) {
	    tmp[0] = '\0';
	}

	/* remove trailing whitespaces */
	len = strlen(line);
	while (line[len-1] == ' ' || line[len-1] == '\n') {
	    line[len-1] = '\0';
	    len = strlen(line);
	}

	/* remove proceeding whitespaces */
	while (line[0] == ' ') {
	    line++;
	}

	/* find the key and the value */
	if ((value = strchr(line,'=')) && strlen(value) > 1) {
	    value++;
	    keylen = strlen(line) - strlen(value) - 1;
	} else if (value && strlen(value) == 1) {
	    keylen = strlen(line) -1;
	    value = NULL;
	} else {
	    keylen = strlen(line);
	}
	strncpy(key, line, keylen);
	key[keylen] = '\0';
	pkey = key;

	/* remove trailing whitespaces from key */
	while (pkey[keylen-1] == ' ') {
	    pkey[keylen-1] = '\0';
	    keylen = strlen(key);
	}

	/* remove proceeding whitespaces from value */
	while (value && value[0] == ' ') {
	    value++;
	}

	if ((ret = verfiyConfOption(pkey, value)) != 0) {
	    if (ret == 1) {
		mlog("%s: unknown config option '%s'\n", __func__, pkey);
	    } else if (ret == 2) {
		mlog("%s: the config option '%s' has to be numeric\n", __func__,
			pkey);
	    }
	    return 0;
	}

	config = (Config_t *) umalloc(sizeof(Config_t));
	config->key = ustrdup(pkey);
	config->value = (!value) ? ustrdup("") : ustrdup(value);

	list_add_tail(&(config->list), &ConfigList.list);
    }
    if (linebuf) ufree(linebuf);
    fclose(fp);

    /* set missing default values */
    for (i=0; i<configValueCount; i++) {
	if (!(getConfParam(CONFIG_VALUES[i].name))) {

	    if (CONFIG_VALUES[i].def) {
		addConfig(CONFIG_VALUES[i].name, CONFIG_VALUES[i].def);
	    }
	}
    }

    isInit = 1;
    return 1;
}

int verfiyConfOption(char *name, char *value)
{
    long testNum;
    const ConfDef_t *def;

    if (!(def = findConfigDef(name))) return 1;

    if (def->isNum) {
	if ((sscanf(value, "%li", &testNum)) != 1) {
	    return 2;
	}
    }

    return 0;
}

void delConfig(Config_t *conf)
{
    if (conf->key) ufree(conf->key);
    if (conf->value) ufree(conf->value);
    list_del(&conf->list);
    ufree(conf);
}

void clearConfig()
{
    list_t *pos, *tmp;
    Config_t *config;

    if (!isInit) return;
    if (list_empty(&ConfigList.list)) return;

    list_for_each_safe(pos, tmp, &ConfigList.list) {
	if ((config = list_entry(pos, Config_t, list)) == NULL) {
	    return;
	}
	delConfig(config);
    }
    isInit = 0;
}

void getConfParamL(char *name, long *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(name))) {
	return;
    }

    if ((sscanf(val, "%li", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

void getConfParamI(char *name, int *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(name))) {
	return;
    }

    if ((sscanf(val, "%i", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

void getConfParamU(char *name, unsigned int *value)
{
    char *val;

    *value = -1;
    if (!isInit) return;
    if (!name) return;
    if (!(val = getConfParam(name))) {
	return;
    }

    if ((sscanf(val, "%u", value)) != 1) {
	mlog("%s: option '%s' is not a number\n", __func__, name);
	*value = -1;
    }
}

char *getConfParamC(char *name)
{
    char *val;

    if (!isInit) return NULL;
    if (!(val = getConfParam(name))) {
	return NULL;
    }
    return val;
}

Config_t *getConfObject(char *name)
{
    struct list_head *pos;
    Config_t *config;

    if (!isInit) return NULL;
    if (!name || list_empty(&ConfigList.list)) return NULL;

    list_for_each(pos, &ConfigList.list) {
	if ((config = list_entry(pos, Config_t, list)) == NULL) return NULL;

	if (!(strcmp(config->key, name))) {
	    return config;
	}
    }
    return NULL;
}

char *getConfParam(char *name)
{
    struct list_head *pos;
    Config_t *config;

    if (!isInit) return NULL;
    if (!name || list_empty(&ConfigList.list)) return NULL;

    list_for_each(pos, &ConfigList.list) {
	if ((config = list_entry(pos, Config_t, list)) == NULL) {
	    return NULL;
	}

	if (!(strcmp(config->key, name))) {
	    return config->value;
	}
    }
    return NULL;
}
