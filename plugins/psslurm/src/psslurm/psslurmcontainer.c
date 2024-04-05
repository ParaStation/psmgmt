/*
 * ParaStation
 *
 * Copyright (C) 2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psslurmcontainer.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "pscommon.h"
#include "psenv.h"
#include "psstrv.h"
#include "pluginconfig.h"
#include "pluginhelper.h"
#include "pluginjson.h"
#include "pluginmalloc.h"
#include "psidscripts.h"

#include "slurmcommon.h"
#include "psslurmlog.h"
#include "psslurmconfig.h"

#define JSON_CONFIG "config.json"
#define JSON_ENV "/process/env"
#define JSON_CONFIG_ROOTFS "/root/path"
#define JSON_TMP "/tmp/psslurm/job-%j"
#define JSON_JOBSCRIPT JSON_TMP "/jobscript"
#define JSON_STDIN JSON_TMP "/stdin"
#define JSON_STDOUT JSON_TMP "/stdout"
#define JSON_STDERR JSON_TMP "/stderr"

#define DEFAULT_SPOOL_DIR "/var/run/psid"

/**
 * @brief Replace symbols in container commands
 *
 * @param line Line holding the command with symbols to replace
 *
 * @param ct Slurm container holding values to use for replacement
 *
 * @return Returns the line extended with the replaced symbols on success.
 * Otherwise NULL is returned.
 */
static char *replaceSymbols(const char *line, Slurm_Container_t *ct)
{
    if (!line) return NULL;

    const char *ptr = line;
    char *next = strchr(ptr, '%');
    if (!next) return ustrdup(line);

    char *buf = NULL;
    size_t bufSize = 0;

    while (next) {
	char tmp[1024];
	char *symbol = next + 1;
	size_t len = next - ptr;
	strn2Buf(ptr, len, &buf, &bufSize);

	switch (symbol[0]) {
	case '@':
	    /* TODO: arguments */
	    flog("ignoring unsupported pattern '@' in oci.conf\n");
	    break;
	case 'b':
	    /* container bundle */
	    str2Buf(ct->bundle, &buf, &bufSize);
	    break;
	case 'e':
	    /* TODO: environment file */
	    flog("ignoring unsupported pattern 'e' in oci.conf\n");
	    break;
	case 'j':
	    /* jobid */
	    snprintf(tmp, sizeof(tmp), "%u", ct->jobid);
	    str2Buf(tmp, &buf, &bufSize);
	    break;
	case 'm':
	    /* spool directory */
	    if (ct->spoolDir) str2Buf(ct->spoolDir, &buf, &bufSize);
	    break;
	case 'n':
	    /* node name */
	    ;char *hn = getConfValueC(Config, "SLURM_HOSTNAME");
	    if (hn && hn[0] != '\0') str2Buf(hn, &buf, &bufSize);
	    break;
	case 'p':
	    /* pid of task (rank) */
	    snprintf(tmp, sizeof(tmp), "%i", ct->rankPID);
	    str2Buf(tmp, &buf, &bufSize);
	    break;
	case 'r':
	    /* rootfs */
	    if (ct->rootfs) str2Buf(ct->rootfs, &buf, &bufSize);
	    break;
	case 's':
	    /* stepid */
	    snprintf(tmp, sizeof(tmp), "%u", ct->stepid);
	    str2Buf(tmp, &buf, &bufSize);
	    break;
	case 't':
	    /* taskid (rank) */
	    snprintf(tmp, sizeof(tmp), "%i", ct->rank);
	    str2Buf(tmp, &buf, &bufSize);
	    break;
	case 'U':
	    /* user ID */
	    snprintf(tmp, sizeof(tmp), "%u", ct->uid);
	    str2Buf(tmp, &buf, &bufSize);
	    break;
	case 'u':
	    /* username */
	    str2Buf(ct->username, &buf, &bufSize);
	    break;
	default:
	    flog("error: unknown symbol '%c' to replace\n", symbol[0]);
	}

	ptr = next + 2;
	next = strchr(ptr, '%');
    }

    str2Buf(ptr, &buf, &bufSize);
    fdbg(PSSLURM_LOG_DEBUG, "orig '%s' result: '%s'\n", line, buf);

    return buf;
}

static bool readConfig(Slurm_Container_t *ct)
{
    if (!ct || !ct->bundle) {
	flog("error: invalid call\n");
	return false;
    }

    char *cPath = PSC_concat(ct->bundle, "/", JSON_CONFIG);
    if (!cPath) {
	flog("error: out of memory\n");
	return false;
    }

    ct->configObj = jsonFromFile(cPath);
    if (!ct->configObj) {
	flog("failed to parse %s\n", cPath);
	free(cPath);
	return false;
    }
    free(cPath);

    return true;
}

static void initContainerCmds(Slurm_Container_t *ct)
{
    Container_CMDs_t *cmds = &ct->cmds;

    char *runtime = getConfValueC(SlurmOCIConfig, "RunTimeQuery");
    cmds->query = replaceSymbols(runtime, ct);

    runtime = getConfValueC(SlurmOCIConfig, "RunTimeCreate");
    cmds->create = replaceSymbols(runtime, ct);

    runtime = getConfValueC(SlurmOCIConfig, "RunTimeStart");
    cmds->start = replaceSymbols(runtime, ct);

    runtime = getConfValueC(SlurmOCIConfig, "RunTimeKill");
    cmds->kill = replaceSymbols(runtime, ct);

    runtime = getConfValueC(SlurmOCIConfig, "RunTimeDelete");
    cmds->delete = replaceSymbols(runtime, ct);

    runtime = getConfValueC(SlurmOCIConfig, "RunTimeRun");
    cmds->run = replaceSymbols(runtime, ct);

    runtime = getConfValueC(SlurmOCIConfig, "RunTimeEnvExclude");
    cmds->envExclude = replaceSymbols(runtime, ct);
}

static bool initSpoolDir(Slurm_Container_t *ct)
{
    /* create root spool directory */
    char *spool = getConfValueC(SlurmOCIConfig, "MountSpoolDir");
    if (spool && spool[0] != '\0') {
	ct->spoolDir = replaceSymbols(spool, ct);
    } else {
	ct->spoolDir = ustrdup(DEFAULT_SPOOL_DIR);
    }
    mkDir(ct->spoolDir, 0755, -1, -1);

    /* create job specific spool directory */
    if (ct->stepid == SLURM_BATCH_SCRIPT) {
        ct->spoolJobDir = replaceSymbols("%m/oci-job%j-batch", ct);
    } else if (ct->stepid == SLURM_INTERACTIVE_STEP) {
        ct->spoolJobDir = replaceSymbols("%m/oci-job%j-interactive", ct);
    } else {
        ct->spoolJobDir = replaceSymbols("%m/oci-job%j-%s", ct);
    }

    if (!ct->spoolJobDir) {
	flog("invalid spool job directory\n");
	return false;
    }
    mkDir(ct->spoolJobDir, 0755, ct->uid, ct->gid);

    return true;
}

static bool initRootFS(Slurm_Container_t *ct)
{
    const char *rootfs = jsonGetString(ct->configObj, JSON_CONFIG_ROOTFS);
    if (!rootfs) {
	flog("could not find rootfs in %s\n", JSON_CONFIG);
	return false;
    }

    /* ensure rootfs is an absolute path */
    if (rootfs[0] != '/') {
	ct->rootfs = PSC_concat(ct->bundle, "/", rootfs);
	if (!ct->rootfs) {
	    flog("error: out of memory\n");
	    return false;
	}
    } else {
	ct->rootfs = ustrdup(rootfs);
    }

    struct stat sbuf;
    if (stat(ct->rootfs, &sbuf) == -1) {
	mwarn(errno, "%s: invalid rootfs directory %s:",
	      __func__, ct->rootfs);
	return false;
    }

    flog("using container rootfs: %s\n", ct->rootfs);

    return true;
}

/**
 * @brief Merge environment from job/task with container environment
 *
 * @param ct Container to modify
 *
 * @param env Environment to merge (might be NULL for current env)
 */
static void mergeEnv(Slurm_Container_t *ct, env_t env)
{
    /* merge container and task environment */
    int len = jsonArrayLen(ct->configObj, JSON_ENV);
    for (int i = 0; i < len; i++) {
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/[%i]", JSON_ENV, i);
	const char *cEnv = jsonGetString(ct->configObj, path);
	if (cEnv) {
	    env ? envAdd(env, cEnv) : putenv(ustrdup(cEnv));
	}
    }

    /* write merged environment to container configuration */
    jsonPutArrayP(ct->configObj, "/process", "env", NULL);
    jsonWalkPath(ct->configObj, JSON_ENV);

    if (!env) {
	extern char **environ;
	for (int i = 0; environ[i] != NULL; i++) {
	    jsonPutString(ct->configObj, NULL, environ[i]);
	}
    } else {
	for (char **e = envGetArray(env); e && *e; e++) {
	    jsonPutString(ct->configObj, NULL, *e);
	}
    }
}

Slurm_Container_t *Container_new(const char *bundle, uint32_t jobid,
				 uint32_t stepid, const char *username,
				 uid_t uid, gid_t gid)
{
    if (!jsonIsAvail()) {
	flog("error: no json-c support available\n");
	return NULL;
    }

    if (!bundle) {
	flog("error: invalid bundle\n");
	return NULL;
    }

    if (!username) {
	flog("error: invalid username\n");
	return NULL;
    }

    Slurm_Container_t *ct = ucalloc(sizeof(*ct));
    ct->jobid = jobid;
    ct->stepid = stepid;
    ct->uid = uid;
    ct->gid = gid;
    ct->bundle = ustrdup(bundle);
    ct->username = ustrdup(username);
    ct->rank = -1;
    ct->rankPID = -1;

    /* customize container commands */
    initContainerCmds(ct);

    /* read json configuration */
    if (!readConfig(ct)) {
	flog("error: failed to read json configuration\n");
	Container_destroy(ct);
	return NULL;
    }

    /* test for additional container environment */
    const char *envStr = jsonGetString(ct->configObj, JSON_ENV);
    if (!envStr) {
	fdbg(PSSLURM_LOG_CONTAIN, "warning: could not find container "
	     "environment %s\n", JSON_ENV);
    }

    /* initialize rootfs */
    if (!initRootFS(ct)) {
	flog("error: setup of rootfs directory failed\n");
	Container_destroy(ct);
	return NULL;
    }

    /* initialize spool directory  */
    if (!initSpoolDir(ct)) {
	flog("error: setup of spool directory failed\n");
	Container_destroy(ct);
	return NULL;
    }

    /* adjust common json configuration */

    /* set correct rootfs path */
    jsonPutStringP(ct->configObj, "/root/", "path", ct->rootfs);

    /* disable runtime hooks */
    char *disHooks = getConfValueC(SlurmOCIConfig, "DisableHooks");
    if (disHooks && disHooks[0] != '\0' &&
	jsonExists(ct->configObj, "/hooks")) {

	const char delimiters[] =" ,\n";
	char *toksave, *dup = ustrdup(disHooks);
	char *next = strtok_r(dup, delimiters, &toksave);
	while (next) {
	    char path[PATH_MAX];
	    snprintf(path, sizeof(path), "/hooks/%s", next);
	    if (jsonExists(ct->configObj, path)) {
		fdbg(PSSLURM_LOG_CONTAIN, "disable hook %s\n", path);
		jsonDel(ct->configObj, "/hooks/", next);
	    }
	    next = strtok_r(NULL, delimiters, &toksave);
	}
	ufree(dup);
    }

    return ct;
}

/**
 * @brief Request additional bind mount from container runtime
 *
 * @param ct Containter to receive additional mount
 *
 * @param dest Destination inside container
 *
 * @param source Source from outside container
 *
 **/
static void bindMount(Slurm_Container_t *ct, const char *dest,
		      const char *source)
{
    /* don't attempt to bind mount /dev/null */
    if (!source || source[0] == '\0' || !strcmp(source, "/dev/null")) return;

    char *rsource = replaceSymbols(source, ct);
    char *rdest = replaceSymbols(dest, ct);
    fdbg(PSSLURM_LOG_CONTAIN, "bind mount src %s dest %s\n", rsource, rdest);

    jsonPutStringP(ct->configObj, "/mounts/[]/", "destination", rdest);
    jsonPutString(ct->configObj, "source", rsource);
    jsonPutString(ct->configObj, "type", "none");

    jsonPutArray(ct->configObj, "options");
    jsonPutStringP(ct->configObj, "options", NULL, "bind");

    ufree(rsource);
    ufree(rdest);
}

/**
 * @brief Mount spool directory inside container
 *
 * @param ct Containter to receive additional mount
 *
 * @param newSpool Updated spool directory
 */
static void mountSpoolDir(Slurm_Container_t *ct, const char *newSpool)
{
    char *spool = getConfValueC(SlurmOCIConfig, "MountSpoolDir");
    if (spool && spool[0] != '\0') bindMount(ct, spool, newSpool);
}

void Container_jobInit(Job_t *job)
{
    Slurm_Container_t *ct = job->ct;

    /* mount I/O files */
    bindMount(ct, JSON_STDIN, job->stdIn);
    bindMount(ct, JSON_STDOUT, job->stdOut);
    bindMount(ct, JSON_STDERR, job->stdErr);

    /* mount jobscript */
    bindMount(ct, JSON_JOBSCRIPT, job->jobscript);
    jsonPutString(ct->configObj, NULL, "ro");

    /* mount spool directory */
    mountSpoolDir(ct, ct->spoolJobDir);

    /* let runtime start jobscript */
    jsonPutArrayP(ct->configObj, "/process", "args", NULL);
    jsonWalkPath(ct->configObj, "/process/args");
    char *jsPath = replaceSymbols(JSON_JOBSCRIPT, ct);
    jsonPutString(ct->configObj, NULL, jsPath);
    ufree(jsPath);
    for (uint32_t i = 0; i < job->argc; i++) {
	fdbg(PSSLURM_LOG_CONTAIN, "arg(%i) %s\n", i, job->argv[i]);
	jsonPutString(ct->configObj, NULL, job->argv[i]);
    }

    /* merge current job environment with container environment */
    mergeEnv(ct, job->env);

    /* no tty for jobscript */
    jsonPutBoolP(ct->configObj, "/process/", "terminal", false);

    /* write final container configuration for jobscript */
    jsonWriteFile(ct->configObj, ct->spoolJobDir, JSON_CONFIG);

    /* switch bundle so new configuration will be used */
    ufree(ct->bundle);
    ct->bundle = ct->spoolJobDir;

    fdbg(PSSLURM_LOG_CONTAIN, "success for %s\n", ct->bundle);
}

bool Container_taskInit(Slurm_Container_t *ct, PStask_t *task, bool tty)
{
    ct->rank = task->rank;
    ct->rankPID = PSC_getPID(task->tid);

    /* set tty for interactive step */
    jsonPutBoolP(ct->configObj, "/process/", "terminal", tty);

    /* create task specific spool directory */
    char *tmp = PSC_concat(ct->spoolJobDir, "/task-%t/");
    char *taskSpoolDir = replaceSymbols(tmp, ct);
    free(tmp);
    if (!mkDir(taskSpoolDir, 0755, ct->uid, ct->gid)) {
	flog("mkDir(%s) failed\n", taskSpoolDir);
	return false;
    }
    fdbg(PSSLURM_LOG_CONTAIN, "rank %i spool %s\n", ct->rank, taskSpoolDir);

    /* let runtime mount spool directory */
    mountSpoolDir(ct, taskSpoolDir);

    /* merge current task environment with container environment */
    mergeEnv(ct, NULL);

    /* save task arguments to container configuration */
    jsonPutArrayP(ct->configObj, "/process", "args", NULL);
    jsonWalkPath(ct->configObj, "/process/args");

    for (uint32_t i = 0; i < task->argc; i++) {
	fdbg(PSSLURM_LOG_CONTAIN, "arg(%i) %s\n", i, task->argv[i]);
	jsonPutString(ct->configObj, NULL, task->argv[i]);
    }

    /* write final container configuration for task */
    jsonWriteFile(ct->configObj, taskSpoolDir, JSON_CONFIG);

    /* switch bundle so new configuration will be used */
    ufree(ct->bundle);
    ct->bundle = taskSpoolDir;

    fdbg(PSSLURM_LOG_CONTAIN, "success for %s\n", ct->bundle);
    return true;
}

static int doExec(void *info)
{
    strv_t *argV = info;

    if (strvSize(argV) <= 2) {
	flog("invalid argument vector for container command\n");
	exit(1);
    }

    char **argvP = strvStealArray(argV);
    fdbg(PSSLURM_LOG_CONTAIN, "executing %s\n", argvP[2]);
    execv(argvP[0], argvP);

    /* execve() failed */
    flog("executing %s failed\n", argvP[2]);
    exit(1);
}

/**
 * @brief Execute runtime command
 */
static int execRuntimeCmd(Slurm_Container_t *ct, char *cmd, bool doFork)
{
    char *runtime = replaceSymbols(getConfValueC(SlurmOCIConfig, cmd), ct);
    if (!runtime) {
	flog("invalid runtime command %s\n", cmd);
	return -1;
    }

    strv_t argV;
    strvInit(&argV, NULL, 0);
    strvAdd(&argV, "/bin/sh");
    strvAdd(&argV, "-c");
    strvAdd(&argV, runtime);

    fdbg(PSSLURM_LOG_CONTAIN, "%s\n", runtime);
    if (!doFork) doExec(&argV);

    struct timeval timeout = { .tv_sec = 3, .tv_usec = 0};
    int ret = PSID_execFunc(doExec, NULL, NULL, &timeout, &argV);
    ufree(runtime);
    strvDestroy(&argV);

    return ret;
}

__attribute__ ((noreturn))
void Container_run(Slurm_Container_t *ct)
{
    /* TODO add support for start + exec */
    if (!ct->cmds.run) {
	flog("unsupported container startup, use RunTimeRun in oci.conf\n");
	exit(1);
    }

    /* start task in container */
    execRuntimeCmd(ct, "RunTimeRun", false);

    /* never reached if successful */
    exit(1);
}

void Container_stop(Slurm_Container_t *ct)
{
    /* stop container */
    execRuntimeCmd(ct, "RunTimeKill", true);

    /* TODO: use RunTimeQuery and ensure container was killed
     * before removing it */

    /* delete container */
    execRuntimeCmd(ct, "RunTimeDelete", true);
}

bool Container_destroy(Slurm_Container_t *ct)
{
    if (!ct) {
	flog("error: invalid container\n");
	return false;
    }

    if (!getConfValueU(SlurmOCIConfig, "DisableCleanup")) {
	if (ct->spoolJobDir) removeDir(ct->spoolJobDir, true);
    }

    /* remove leftover bind mounts from container rootfs */
    char *tmpDir = PSC_concat(ct->rootfs, JSON_TMP);
    char *tmpDirR = replaceSymbols(tmpDir, ct);
    struct stat sbuf;
    if (!stat(tmpDirR, &sbuf)) removeDir(tmpDirR, true);
    ufree(tmpDir);
    ufree(tmpDirR);

    Container_CMDs_t *cmds = &ct->cmds;
    ufree(cmds->query);
    ufree(cmds->create);
    ufree(cmds->start);
    ufree(cmds->kill);
    ufree(cmds->delete);
    ufree(cmds->run);
    ufree(cmds->envExclude);

    strShred(ct->bundle);
    strShred(ct->rootfs);
    strShred(ct->username);
    strShred(ct->spoolDir);
    strShred(ct->spoolJobDir);

    jsonDestroy(ct->configObj);

    ufree(ct);

    return true;
}
