/*
 * ParaStation
 *
 * Copyright (C) 2016-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "plugin.h"
#include "timer.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "psidutil.h"

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "cgrouplog.h"
#include "cgroupconfig.h"

#define CGROUP_CONFIG "cgroup.conf"

/** psid plugin requirements */
char name[] = "cgroup";
int version = 1;
int requiredAPI = 115;
plugin_dep_t dependencies[] = { { NULL, 0 } };

/** Root of the cgroup hierarchy */
static char *cgroupRoot = NULL;

/** Name of psmgmt's private cgroup */
static char *cgroupName = NULL;

/** Memory limit of psmgmt's memory cgroup */
static long memLim = -1;

/** Memory+Swap limit of psmgmt's memory cgroup */
static long memSwLim = -1;

/** cgroup to use */
static char *myCgroup = NULL;

/** Actual pid-file to jail processes */
static char *tasksFile = NULL;

static bool enforceLimit(char *lName, long lmt)
{
    if (!myCgroup) {
	cglog(-1, "%s: no local cgroup defined!\n", __func__);
	return false;
    }

    char fName[PATH_MAX];
    snprintf(fName, sizeof(fName), "%s/%s", myCgroup, lName);

    FILE *fp = fopen(fName, "w");
    if (!fp) {
	cgwarn(-1, errno, "%s: cannot open '%s'", __func__, fName);
	return false;
    }
    if (fprintf(fp, "%ld\n", lmt) < 0) {
	cgwarn(-1, errno, "%s: failed to set %ld to %s", __func__, lmt, lName);
	fclose(fp);
	return false;
    }
    fclose(fp);

    return true;
}

static bool enforceAllLimits(void)
{
    return enforceLimit("memory.limit_in_bytes", memLim)
	&& enforceLimit("memory.memsw.limit_in_bytes", memSwLim);
}

/**
 * @brief Setup private cgroup
 *
 * Setup the private cgroup according to the settings found in @ref
 * cgroupRoot, @ref cgroupName, @ref memLim and @ref memSwLim.
 *
 * @return On success true is returned. Or false if an error occurred.
 */
static bool initCgroup(void)
{
    /* check if cgroup is there */
    struct stat sb;
    if (stat(cgroupRoot, &sb) < 0) {
	cgwarn(-1, errno, "%s: cannot stat cgroup '%s'", __func__, cgroupRoot);
	return false;
    }

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s/%s", cgroupRoot, "memory");
    if (stat(tmp, &sb) < 0) {
	cgwarn(-1, errno, "%s: no memory group in '%s'", __func__, cgroupRoot);
	return false;
    }

    /* create subgroup */
    snprintf(tmp, sizeof(tmp), "%s/%s/%s", cgroupRoot, "memory", cgroupName);
    myCgroup = ustrdup(tmp);
    if (mkdir(myCgroup, S_IRWXU | S_IRWXG | S_IRWXO) < 0 && errno != EEXIST) {
	cgwarn(-1, errno, "%s: cannot create cgroup '%s'", __func__, myCgroup);
	return false;
    }

    /* stat the tasks file */
    snprintf(tmp, sizeof(tmp), "%s/%s", myCgroup, "tasks");
    tasksFile = ustrdup(tmp);
    if (stat(tasksFile, &sb) < 0) {
	cgwarn(-1, errno, "%s: cannot stat() '%s'", __func__, tasksFile);
	return false;
    }

    enforceAllLimits();

    return true;
}

static int jailProcess(void *info)
{
    pid_t pid = *(pid_t *)info;

    cglog(CG_LOG_VERBOSE, "%s: called for %d\n", __func__, pid);

    if (!tasksFile) {
	cglog(-1, "%s: no tasks file!\n", __func__);
	return -1;
    }

    FILE *fp = fopen(tasksFile, "w");
    if (!fp) {
	cgwarn(-1, errno, "%s: cannot open '%s'", __func__, tasksFile);
	return -1;
    }
    if (fprintf(fp, "%d\n", pid) < 0) {
	cgwarn(-1, errno, "%s: failed to add %d", __func__, pid);
	fclose(fp);
	return -1;
    }
    fclose(fp);

    return 0;
}

/**
 * Cleanup processes jailed in cgroup
 *
 * Cleanup all processes currently jailed in the cgroup. For this,
 * SIGKILL is sent to each process found in the cgroup's tasks file.
 *
 * @return Return the number of signals sent, i.e. the number of
 * processes still resident in the cgroup. Or -1 if an error occurred.
 */
static int cleanupProcesses(void)
{
    int pid, cnt = 0;

    if (!tasksFile) {
	cglog(-1, "%s: no tasks file!\n", __func__);
	return -1;
    }

    FILE *fp = fopen(tasksFile, "r");
    if (!fp) {
	cgwarn(-1, errno, "%s: cannot open '%s'", __func__, tasksFile);
	return -1;
    }

    while (fscanf(fp, "%i", &pid) != -1) {
	cglog(CG_LOG_VERBOSE, "%s: cleanup %d\n", __func__, pid);
	kill(pid, SIGKILL);
	cnt++;
    }

    fclose(fp);

    return cnt;
}

/**
 * @brief Unregister all hooks and message handler.
 *
 * @param verbose If set to true an error message will be displayed
 * when unregistering a hook or a message handle fails.
 *
 * @return No return value.
 */
static void unregisterHooks(bool verbose)
{
    if (!(PSIDhook_del(PSIDHOOK_JAIL_CHILD, jailProcess))) {
	if (verbose) cglog(-1, "removing PSIDHOOK_JAIL_CHILD failed\n");
    }
}

int initialize(FILE *logfile)
{
    int debugMask;
    char configFile[PATH_MAX];

    /* init logging facility */
    initCgLogger(name, logfile);

    /* init the config facility */
    snprintf(configFile, sizeof(configFile), "%s/%s", PLUGINDIR, CGROUP_CONFIG);

    initCgConfig(configFile);

    /* adapt the debug mask */
    debugMask = getConfValueI(&config, "DEBUG_MASK");
    maskCgLogger(debugMask);
    cglog(CG_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, debugMask);

    cgroupRoot = ustrdup(getConfValueC(&config, "CGROUP_ROOT"));
    if (!cgroupRoot) {
	cglog(-1, "%s: CGROUP_ROOT not found in '%s'\n", __func__, configFile);
	return 1;
    }
    cglog(CG_LOG_VERBOSE, "%s: cgroupRoot set to '%s'\n", __func__, cgroupRoot);

    cgroupName = ustrdup(getConfValueC(&config, "CGROUP_NAME"));
    if (!cgroupName) {
	cglog(-1, "%s: CGROUP_NAME not found in '%s'\n", __func__, configFile);
	return 1;
    }
    cglog(CG_LOG_VERBOSE, "%s: cgroupName set to '%s'\n", __func__, cgroupName);

    memLim = getConfValueL(&config, "MEM_LIMIT");
    cglog(CG_LOG_VERBOSE, "%s: memLim set to %ld\n", __func__, memLim);

    memSwLim = getConfValueL(&config, "MEMSW_LIMIT");
    cglog(CG_LOG_VERBOSE, "%s: memSwLim set to %ld\n", __func__, memSwLim);

    if (!initCgroup()) return 1;

    if (!(PSIDhook_add(PSIDHOOK_JAIL_CHILD, jailProcess))) {
	cglog(-1, "%s: register PSIDHOOK_JAIL_CHILD failed\n", __func__);
	goto INIT_ERROR;
    }

    cglog(-1, "(%i) successfully started\n", version);

    return 0;

INIT_ERROR:
    unregisterHooks(false);
    return 1;
}

/** Timer used to finalize the plugin */
static int cgTimer = -1;

void finalize(void)
{
    if (cleanupProcesses() <= 0) {
	PSIDplugin_unload(name);
    } else if (cgTimer == -1) {
	/* Wait 2 seconds, then try again */
	struct timeval timeout = {2, 0};

	cgTimer = Timer_register(&timeout, finalize);
    }
}

void cleanup(void)
{
    if (myCgroup && rmdir(myCgroup) < 0) {
	cgwarn(-1, errno, "%s: rmdir(%s)", __func__, myCgroup);
    }

    if (cgTimer > -1) {
	Timer_remove(cgTimer);
	cgTimer = -1;
    }
    unregisterHooks(true);
    freeConfig(&config);
    if (cgroupRoot) ufree(cgroupRoot);
    if (cgroupName) ufree(cgroupName);
    if (myCgroup) ufree(myCgroup);
    if (tasksFile) ufree(tasksFile);

    cgroupRoot = cgroupName = myCgroup = tasksFile = NULL;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int maxKeyLen = getMaxKeyLen(confDef);
    int i;

    str2Buf("\tJail all psid's client processes into a single cgroup\n\n",
	    &buf, &bufSize);
    str2Buf("\tcgroup's status is displayed under key 'status'\n\n",
	    &buf, &bufSize);
    str2Buf("# configuration options #\n", &buf, &bufSize);

    for (i = 0; confDef[i].name; i++) {
	char type[10], line[160];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%*s %10s  %s\n",
		 maxKeyLen+2, confDef[i].name, type, confDef[i].desc);
	str2Buf(line, &buf, &bufSize);
    }

    return buf;
}

char *set(char *key, char *val)
{
    const ConfDef_t *thisConfDef = getConfigDef(key, confDef);

    if (!thisConfDef) return ustrdup("\nUnknown option\n");

    if (verifyConfigEntry(confDef, key, val))
	return ustrdup("\nIllegal value\n");

    if (!strcmp(key, "MEM_LIMIT")) {
	addConfigEntry(&config, key, val);
	memLim = getConfValueL(&config, key);
	cglog(CG_LOG_VERBOSE, "%s: memLim set to %ld\n", __func__, memLim);

	enforceAllLimits();
    } else if (!strcmp(key, "MEMSW_LIMIT")) {
	addConfigEntry(&config, key, val);
	memSwLim = getConfValueL(&config, key);
	cglog(CG_LOG_VERBOSE, "%s: memSwLim set to %ld\n", __func__, memSwLim);

	enforceAllLimits();
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	addConfigEntry(&config, key, val);
	dbgMask = getConfValueI(&config, key);
	maskCgLogger(dbgMask);
	cglog(CG_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, dbgMask);
    } else {
	return ustrdup("\nPermission denied\n");
    }

    return NULL;
}

char *unset(char *key)
{
    if (!strcmp(key, "MEM_LIMIT")) {
	unsetConfigEntry(&config, confDef, key);
	memLim = getConfValueL(&config, key);
	cglog(CG_LOG_VERBOSE, "%s: memLim set to %ld\n", __func__, memLim);

	enforceAllLimits();
    } else if (!strcmp(key, "MEMSW_LIMIT")) {
	unsetConfigEntry(&config, confDef, key);
	memSwLim = getConfValueL(&config, key);
	cglog(CG_LOG_VERBOSE, "%s: memSwLim set to %ld\n", __func__, memSwLim);

	enforceAllLimits();
    } else if (!strcmp(key, "DEBUG_MASK")) {
	int dbgMask;
	unsetConfigEntry(&config, confDef, key);
	dbgMask = getConfValueI(&config, key);
	maskCgLogger(dbgMask);
	cglog(CG_LOG_VERBOSE, "%s: debugMask set to %#x\n", __func__, dbgMask);
    } else {
	return ustrdup("Permission denied\n");
    }

    return NULL;
}


static void showLimit(char *lName, char **buf, size_t *bufSize)
{
    size_t limit;

    str2Buf("\t", buf, bufSize);
    str2Buf(lName, buf, bufSize);
    str2Buf(" = ", buf, bufSize);

    if (!myCgroup) {
	str2Buf("<unknown cgroup>\n", buf, bufSize);
	return;
    }

    char fName[PATH_MAX];
    snprintf(fName, sizeof(fName), "%s/%s", myCgroup, lName);

    FILE *fp = fopen(fName, "r");
    if (!fp) {
	str2Buf("<unknown limit>\n", buf, bufSize);
	return;
    }
    int ret;
    if ((ret=fscanf(fp, "%zu", &limit)) != 1) {
	str2Buf("<unknown>", buf, bufSize);
    } else {
	char limitStr[32];

	snprintf(limitStr, sizeof(limitStr), "%zu", limit);
	str2Buf(limitStr, buf, bufSize);
    }
    fclose(fp);

    str2Buf("\n", buf, bufSize);
}

static void showPIDs(char **buf, size_t *bufSize)
{
    FILE *fp;
    bool none = true;
    int pid;

    str2Buf("\tjailed pids = ", buf, bufSize);
    if (!tasksFile || !(fp = fopen(tasksFile, "r")) ) {
	str2Buf(" <unknown>", buf, bufSize);
	return;
    }

    while (fscanf(fp, "%i", &pid) != -1) {
	char pidStr[16];

	snprintf(pidStr, sizeof(pidStr), "%s%d", none ? "" : " ", pid);
	str2Buf(pidStr, buf, bufSize);
	none = false;
    }
    fclose(fp);
    if (none) str2Buf(" <none>", buf, bufSize);

    str2Buf("\n", buf, bufSize);
}

/**
 * @brief Show status
 *
 * Provide an overview on the current status of the cgroup. This
 * includes information on the active settings of the memory resources
 * and the processes jailed into the cgroup.
 *
 * @param buf Buffer to write the information to
 *
 * @param bufSize Current size of the buffer
 *
 * @return Returns the buffer with the requested information.
 */
static char *showStatus(char **buf, size_t *bufSize)
{
    str2Buf("\n", buf, bufSize);
    showLimit("memory.limit_in_bytes", buf, bufSize);
    showLimit("memory.memsw.limit_in_bytes", buf, bufSize);
    showPIDs(buf, bufSize);

    return *buf;
}

char *show(char *key)
{
    char *buf = NULL, *val;
    size_t bufSize = 0;

    if (!key) {
	/* Show the whole configuration */
	int maxKeyLen = getMaxKeyLen(confDef);
	int i;

	str2Buf("\n", &buf, &bufSize);
	for (i = 0; confDef[i].name; i++) {
	    char *cName = confDef[i].name, line[160];
	    val = getConfValueC(&config, cName);

	    snprintf(line, sizeof(line), "%*s = %s\n", maxKeyLen+2, cName, val);
	    str2Buf(line, &buf, &bufSize);
	}
    } else if (!(strcmp(key, "status"))) {
	return showStatus(&buf, &bufSize);
    } else if ((val = getConfValueC(&config, key))) {
	str2Buf("\n", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf(" = ", &buf, &bufSize);
	str2Buf(val, &buf, &bufSize);
	str2Buf("\n", &buf, &bufSize);
    }

    return buf;
}
