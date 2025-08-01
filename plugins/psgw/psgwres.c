/*
 * ParaStation
 *
 * Copyright (C) 2018-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psgwres.h"

#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <timer.h>
#include <unistd.h>

#include "pscommon.h"
#include "psenv.h"
#include "psstrbuf.h"
#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginhelper.h"
#include "pluginmalloc.h"
#include "psidscripts.h"
#include "psidsignal.h"

#include "peloguehandles.h"
#include "psexechandles.h"
#include "psslurmalloc.h"

#include "psgwconfig.h"
#include "psgwlog.h"
#include "psgwpart.h"

static char msgBuf[1024];

/**
 * @brief Prepare environment for the routing script
 *
 * Set various environment variables for the routing script.
 *
 * @param reqPtr Pointer to the request management structure
 *
 * @return No return value
 */
static void prepEnv(void *reqPtr)
{
    PSGW_Req_t *req = Request_verify(reqPtr);
    if (!req) {
	flog("invalid request\n");
	return;
    }

    int cnt = 0;
    for (char **e = envGetArray(req->res->env); e && *e; e++, cnt++) {
	fdbg(PSGW_LOG_DEBUG, "%i: %s\n", cnt, *e);
	putenv(*e);
    }

    char *tmp = getenv("SLURM_SPANK_PSGW_PLUGIN");
    if (!tmp) {
	char *script = getConfValueC(config, "DEFAULT_ROUTE_PLUGIN");
	setenv("SLURM_SPANK_PSGW_PLUGIN", script, 1);
    }

    char buf[1024];
    snprintf(buf, sizeof(buf), "%u", req->numGWstarted);
    setenv("NUM_GATEWAYS", buf, 1);
    for (uint32_t i = 0; i < req->numGWstarted; i++) {
	snprintf(buf, sizeof(buf), "GATEWAY_ADDR_%u", i);
	setenv(buf, req->psgwd[i].addr, 1);
    }

    if (logger_getMask(psgwlogger) & PSGW_LOG_ROUTE) {
	setenv("PSGW_VERBOSE", "1", 1);
    }
}

/**
 * @brief Callback for the psgwd stop script
 *
 * @param id ID of the psgw allocation
 *
 * @param exit The exit code of the stop script
 *
 * @param dest The node ID of the execution host
 *
 * @param uid User ID of the allocation
 *
 * @param output Stdout/Stderr of the stop script
 *
 * @return Always returns 0
 */
static int cbStopPSGWD(uint32_t id, int32_t exit, PSnodes_ID_t dest,
		       uint16_t uid, char *output)
{
    fdbg(PSGW_LOG_PSGWD | PSGW_LOG_DEBUG, "id %u exit %u output %s\n",
	 id, exit, output);
    return 0;
}

/**
 * @brief Stop all psgwd of an allocation
 *
 * @param req The request management structure
 *
 * @return Returns true on success otherwise false is returned
 */
static bool stopPSGWD(PSGW_Req_t *req)
{
    uint32_t id = atoi(req->jobid);
    char *dir = getConfValueC(config, "DIR_ROUTE_SCRIPTS");
    char buf[1024];

    env_t env = envNew(NULL);
    snprintf(buf, sizeof(buf), "%u", req->uid);
    envSet(env, "PSGWD_UID", buf);
    envSet(env, "PSGWD_USER", req->username);

    for (uint32_t i = 0; i < req->numPSGWD; i++) {
	if (req->psgwd[i].pid == -1) continue;

	fdbg(PSGW_LOG_PSGWD | PSGW_LOG_DEBUG, "stopping psgwd %u on node %i\n",
	     i, req->psgwd[i].node);

	snprintf(buf, sizeof(buf), "%u", req->psgwd[i].pid);
	envSet(env, "PSGWD_PID", buf);

	int ret = psExecStartScriptEx(id, "psgwd_stop", dir, env,
				      req->psgwd[i].node, cbStopPSGWD);
	if (ret == -1) {
	    flog("stopping psgwd on node %i failed\n", req->psgwd[i].node);
	    return false;
	}
    }

    return true;
}

/**
 * @brief Callback for the psgw_error script
 *
 * @param id ID of the psgw allocation
 *
 * @param exit The exit code of the psgw_error script
 *
 * @param dest The node ID of the execution host
 *
 * @param uid User ID of the allocation
 *
 * @param output Stdout/Stderr of the psgw_error script
 *
 * @return Always returns 0
 */
static int cbPSGWDerror(uint32_t id, int32_t exit, PSnodes_ID_t dest,
			uint16_t uid, char *output)
{
    if (exit != 0) flog(" failed: id %u exit %u output %s\n", id, exit, output);
    return 0;
}

void writeErrorFile(PSGW_Req_t *req, char *msg, char *file, bool header)
{
    if (envGet(req->env, "SLURM_SPANK_PSGW_QUIET")) {
	return;
    }

    /* write may block in parallel filesystem, execute in separate process */
    pid_t childPID = fork();
    if (!childPID) {
	/* switch to user */
	if (!switchUser(req->username, req->uid, req->gid)) {
	    flog("switching user %s failed\n", req->username);
	    exit(1);
	}

	/* switch to current working directory */
	char *cwd = envGet(req->env, "SLURM_SPANK_PSGW_CWD");
	if (!switchCwd(cwd)) {
	    flog("switching cwd to '%s'\n", cwd);
	    exit(1);
	}

	char path[1024];
	if (!file) {
	    snprintf(path, sizeof(path), "%s/JOB-%s-psgw.err", cwd, req->jobid);
	    file = path;
	}

	FILE *fp;
	if (!(fp = fopen(file, "a"))) {
	    mwarn(errno, "%s: open file '%s' failed :", __func__, file);
	    exit(1);
	}

	if (header) fprintf(fp, "gateway startup failed:\n");
	fprintf(fp, "%s", msg);
	fclose(fp);

	exit(0);
    }

    if (childPID < 0) mwarn(errno, "%s: fork() failed: ", __func__);
}

void __cancelReq(PSGW_Req_t *req, char *reason, const char *func)
{
    /* inform user via error file */
    writeErrorFile(req, reason, NULL, true);

    mlog("%s: %s", func, reason);
    flog("canceling request for jobid %s\n", req->jobid);

    /* kill routing script */
    if (req->routePID != -1) {
	pskill(req->routePID, SIGKILL, 0);
    }

    /* remove timer for route script */
    if (req->timerRouteScript != -1) {
	Timer_remove(req->timerRouteScript);
	req->timerRouteScript = -1;
    }

    if (req->timerPartReq != -1) {
	Timer_remove(req->timerPartReq);
	req->timerPartReq = -1;
    }

    /* kill psgw daemon */
    stopPSGWD(req);

    /* kill prologue on gateway nodes */
    psPelogueSignalPE("psgw", req->jobid, SIGKILL, "psgw error");

    PElogueResource_t *res = req->res;
    if (!res) {
	flog("invalid pelogue resource for jobid %s\n", req->jobid);
	/* start epilogue on gateway nodes */
	if (!startPElogue(req, PELOGUE_EPILOGUE)) {
	    Request_delete(req);
	}
	return;
    }

    /* call script to forward error to slurmctld */
    uint32_t id = atoi(req->jobid);
    int16_t ret = psExecStartScript(id, "psgw_error", res->env,
				    PSC_getID(res->src), cbPSGWDerror);
    if (ret == -1) {
	flog("starting psgw_error script for job %s failed\n", req->jobid);
    }

    /* invoke pelogue callback */
    ret = 0;
    res->cb(res->plugin, res->jobid, ret);
    req->res = NULL;

    /* start epilogue on gateway nodes */
    if (!startPElogue(req, PELOGUE_EPILOGUE)) {
	Request_delete(req);
    }
}

/**
 * @brief Finalize a pending request
 *
 * Check if the routing script and all gateway prologue processes
 * successfully finalized. Invoke the pelogue callback to let the
 * pending slurmctld prologue start.
 *
 * @param req The request to finalize
 */
static void finalizeRequest(PSGW_Req_t *req)
{
    int16_t ret = 1;

    if (req->routeRes == -1) {
	fdbg(PSGW_LOG_DEBUG, "waiting for route script\n");
	return;
    }

    if (req->pelogueState == PSP_PROLOGUE_START) {
	fdbg(PSGW_LOG_DEBUG, "waiting for prologue on gateway nodes\n");
	return;
    }

    /* set additional environment variables */
    char *addEnv = getConfValueC(config, "GATEWAY_ENV");
    PElogueResource_t *res = req->res;

    if (addEnv && strlen(addEnv) > 0) {
	const char delimiters[] = " ";
	char *envCopy = ustrdup(addEnv), *toksave;
	char *next = strtok_r(envCopy, delimiters, &toksave);
	char prefix[] = "_PSSLURM_ENV_"; // @see constant in setPsslurmEnv()

	while (next) {
	    size_t len = strlen(prefix) + strlen(next) + 1;
	    char *new = umalloc(len);
	    snprintf(new, len, "%s%s", prefix, next);
	    envPut(res->env, new);
	    next = strtok_r(NULL, delimiters, &toksave);
	}
	ufree(envCopy);
    }


    /* let pelogue execute prologue */
    res->cb(res->plugin, res->jobid, ret);
    req->res = NULL;
}

/**
 * @brief Callback of the routing script
 *
 * @param exit Routing script's exit-status
 *
 * @param tmdOut Ignored flag of timeout
 *
 * @param iofd File descriptor providing routing script's output
 *
 * @param info Extra information pointing to @ref PSGW_Req_t
 *
 * @return No return value
 */
static void cbRouteScript(int exit, bool tmdOut, int iofd, void *info)
{
    char errMsg[1024];
    size_t errLen = 0;

    getScriptCBdata(iofd, errMsg, sizeof(errMsg), &errLen);
    fdbg(PSGW_LOG_ROUTE, "routing script exit %i\n", exit);

    PSGW_Req_t *req = info;
    req = Request_verify(req);
    if (!req) {
	flog("no request found for script\n");
	return;
    }
    /* remove the timer */
    if (req->timerRouteScript != -1) {
	Timer_remove(req->timerRouteScript);
	req->timerRouteScript = -1;
    }

    /* save result in request */
    req->routeRes = exit;
    req->routePID = -1;

    if (exit != 0) {
	if (errMsg[0] == '\0') {
	    snprintf(msgBuf, sizeof(msgBuf), "routing script failed with exit "
		     "code %i\n", exit);
	}
	cancelReq(req, errMsg);
    } else {
	if (errMsg[0] != '\0') flog("%s\n", errMsg);
	finalizeRequest(req);
    }
}

/**
 * @brief Setup environment for the routing script
 *
 * @param req The request management structure
 *
 * @return Returns true on success or false on error
 */
static bool initRoutingEnv(PSGW_Req_t *req)
{
    PElogueResource_t *res = req->res;
    env_t env = res->env;
    char buf[1024];

    snprintf(buf, sizeof(buf), "%u", req->uid);
    envSet(env, "SLURM_JOB_UID", buf);

    snprintf(buf, sizeof(buf), "%u", req->gid);
    envSet(env, "SLURM_JOB_GID", buf);
    envSet(env, "SLURM_JOB_USER", req->username);

    char *pwBuf = NULL;
    struct passwd *passwd = PSC_getpwnamBuf(req->username, &pwBuf);
    if (!passwd) {
	mwarn(errno, "%s: getpwnam for user %s failed", __func__,
	      req->username);
	return false;
    }
    char *home = passwd->pw_dir;
    envSet(env, "HOME", home);

    char *routeFile = envGet(env, "SLURM_SPANK_PSGW_ROUTE_FILE");
    if (!routeFile) {
	char *cwd = envGet(env, "SLURM_SPANK_PSGW_CWD");
	char *prefix = getConfValueC(config, "DEFAULT_ROUTE_PREFIX");
	snprintf(buf, sizeof(buf), "%s/%s-%s", (cwd ? cwd : home), prefix,
		 req->jobid);
	routeFile = buf;
    }
    free(pwBuf);

    req->routeFile = ustrdup(routeFile);
    envSet(env, "_PSSLURM_ENV_PSP_GW_SERVER", routeFile);

    return true;
}

/**
 * @brief Handle a route script timeout
 *
 * @param timerId The timerId of the elapsed timer
 *
 * @param data The request management structure
 */
static void routeScriptTimeout(int timerId, void *data)
{
    PSGW_Req_t *req = Request_verify(data);

    /* remove the timer */
    Timer_remove(timerId);

    if (!req) {
	flog("no request found for timer %i\n", timerId);
	return;
    }

    req->timerRouteScript = -1;

    snprintf(msgBuf, sizeof(msgBuf), "route script for jobid %s timed out\n",
	     req->jobid);
    cancelReq(req, msgBuf);
}

/**
 * @brief Execute the routing script
 *
 * Start the routing script and pass the IP address/port
 * pairs of the started psgwds. The runtime is monitored
 * by a timer.
 *
 * @param req The request management structure
 *
 * @return Returns true on success and false on error
 */
static bool execRoutingScript(PSGW_Req_t *req)
{
    char exePath[PATH_MAX];
    char *dir = getConfValueC(config, "DIR_ROUTE_SCRIPTS");
    char *script = getConfValueC(config, "ROUTE_SCRIPT");
    int tmRouteScript = getConfValueI(config, "TIMEOUT_ROUTE_SCRIPT");

    snprintf(exePath, sizeof(exePath), "%s/%s", dir, script);

    fdbg(PSGW_LOG_ROUTE, "exec %s timeout %i\n", exePath, tmRouteScript);

    if (!initRoutingEnv(req)) {
	flog("setup of routing environment failed\n");
	return false;
    }

    req->routePID = PSID_execScript(exePath, prepEnv, cbRouteScript, NULL, req);
    if (req->routePID == -1) return false;

    if (tmRouteScript >0) {
	struct timeval timeout = {tmRouteScript, 0};

	req->timerRouteScript =
	    Timer_registerEnhanced(&timeout, routeScriptTimeout, req);
	if (req->timerRouteScript == -1) {
	    flog("register timer for route script failed\n");
	}
    }

    return true;
}

/**
 * @brief Callback of the pelogue executed on the gateway nodes
 *
 * @param jobid The jobid of the allocation
 *
 * @param exit The exit code of the prologue
 *
 * @param timeout Flag to show a timeout while prologue
 * execution occurred.
 *
 * @param result Unused
 *
 * @param info Pointer to the request management structure
 */
static void pelogueCB(char *jobid, int exit, bool timeout,
		      PElogueResList_t *result, void *info)
{
    PSGW_Req_t *req = Request_verify(info);

    if (!req) {
	flog("no request found for jobid %s exit %i timeout %i\n",
	     jobid, exit, timeout);
	return;
    }

    flog("gateway %s for jobid %s exit %i timeout %i\n",
	(req->pelogueState == PSP_PROLOGUE_START ? "prologue" : "epilogue"),
	 jobid, exit, timeout);

    if (req->pelogueState == PSP_PROLOGUE_START) {
	req->pelogueState = PSP_PROLOGUE_FINISH;

	if (exit) {
	    /* prologue failed */
	    strbuf_t failNodes = strbufNew(NULL);
	    for (uint32_t i = 0; i < req->numGWnodes; i++) {
		if (result[i].prologue != PELOGUE_DONE) {
		    if (strbufLen(failNodes)) strbufAdd(failNodes, ",");
		    strbufAdd(failNodes, getHostnameByNodeId(result[i].id));
		}
	    }

	    snprintf(msgBuf, sizeof(msgBuf), "prologue on gateway(s) %s failed,"
		     " jobid %s exit %i timeout %i\n",
		     strbufLen(failNodes) ? strbufStr(failNodes) : "unknown",
		     jobid, exit, timeout);
	    cancelReq(req, msgBuf);
	    strbufDestroy(failNodes);
	} else {
	    finalizeRequest(req);
	}
    } else {
	shutdownForwarder(req->fwdata);
	if (exit) {
	    flog("epilogue on gateway failed, jobid %s exit %i timeout %i\n",
		 jobid, exit, timeout);
	}
	Request_delete(req);
    }
}

bool startPElogue(PSGW_Req_t *req, PElogueType_t type)
{
    bool ret;

    fdbg(PSGW_LOG_DEBUG, "start %s for job %s\n",
	 (type == PELOGUE_PROLOGUE) ? "prologue" : "epilogue", req->jobid);

    if (type == PELOGUE_PROLOGUE) {
	int fwOE = getConfValueI(config, "PELOGUE_LOG_OE");
	ret = psPelogueAddJob("psgw", req->jobid, req->uid, req->gid,
			      req->numGWnodes, req->gwNodes, pelogueCB, req,
			      fwOE);
	if (!ret) {
	    snprintf(msgBuf, sizeof(msgBuf), "adding psgw job %s to pelogue "
		     "failed\n", req->jobid);
	    cancelReq(req, msgBuf);
	    return false;
	}
    }

    ret = psPelogueStartPE("psgw", req->jobid, type, req->env);
    if (!ret) {
	snprintf(msgBuf, sizeof(msgBuf), "starting psgw %s for job %s "
		 "failed\n",
		 (type == PELOGUE_PROLOGUE) ? "prologue" : "epilogue",
		 req->jobid);
	if (type == PELOGUE_PROLOGUE) {
	    cancelReq(req, msgBuf);
	} else {
	    flog("%s", msgBuf);
	}
	return false;
    }

    if (type == PELOGUE_PROLOGUE) {
	req->pelogueState = PSP_PROLOGUE_START;
    } else {
	req->pelogueState = PSP_EPILOGUE_START;
    }

    return true;
}

/**
 * @brief Callback of the psgw start script
 *
 * @param id ID of the psgw allocation
 *
 * @param exit The exit code of the stop script
 *
 * @param dest The node ID of the execution host
 *
 * @param uid User ID of the allocation
 *
 * @param output Stdout/Stderr of the stop script
 *
 * @return Always returns 0
 */
static int cbStartPSGWD(uint32_t id, int32_t exit, PSnodes_ID_t dest,
			uint16_t uid, char *output)
{
    char sid[512];

    snprintf(sid, sizeof(sid), "%u", id);
    PSGW_Req_t *req = Request_find(sid);
    if (!req) {
	flog("no request found for jobid %s\n", sid);
	return 0;
    }

    if (exit) {
	snprintf(msgBuf, sizeof(msgBuf), "psgwd on node %i failed, exit %i,"
		 " output %s\n", dest, exit, output);
	cancelReq(req, msgBuf);
	return 0;
    }

    int pid;
    char addr[25];
    if (sscanf(output, "%24s\npid\t%i", addr, &pid) != 2) {
	snprintf(msgBuf, sizeof(msgBuf), "parsing psgwd output from node %i "
		 "failed, output %s\n", dest, output);
	cancelReq(req, msgBuf);
	return 0;
    }

    uint32_t i;
    bool saved = false;
    for (i=0; i<req->numPSGWD; i++) {
	if (req->psgwd[i].node == dest && !req->psgwd[i].addr) {
	    req->psgwd[i].pid = pid;
	    req->psgwd[i].addr = ustrdup(addr);
	    saved = true;
	    break;
	}
    }

    if (!saved) {
	snprintf(msgBuf, sizeof(msgBuf), "unable to save psgwd address %s in "
		 "request\n", addr);
	cancelReq(req, msgBuf);
	return 0;
    }

    req->numGWstarted++;

    fdbg(PSGW_LOG_PSGWD | PSGW_LOG_DEBUG, "psgwd (%i/%i) on node %i, pid %u "
	 "addr %s\n", req->numGWstarted, req->numPSGWD, dest, pid, addr);

    if (req->numPSGWD == req->numGWstarted) {
	flog("all %i psgwd started, spawning routing script\n",
	     req->numGWstarted);

	/* call script to write the routing file */
	if (!execRoutingScript(req)) {
	    snprintf(msgBuf, sizeof(msgBuf), "executing routing script "
		     "failed\n");
	    cancelReq(req, msgBuf);
	    return 0;
	}
    }

    return 0;
}

/**
 * @brief Initialize the psgwd environment
 *
 * @param req The request management structure
 *
 * @return On success return the just created environment; or NULL in
 * case of error
 */
static env_t initPSGWDEnv(PSGW_Req_t *req)
{
    /* build clean environment for the psgwd */
    env_t env = envNew(NULL);
    char *psgwd = getConfValueC(config, "PSGWD_BINARY");
    envSet(env, "PSGWD_BINARY", psgwd);
    char buf[64];
    snprintf(buf, sizeof(buf), "%u", req->uid);
    envSet(env, "PSGWD_UID", buf);

    envSet(env, "PSGWD_USER", req->username);
    envSet(env, "SLURM_JOB_ID", req->jobid);

    env_t peEnv = req->res->env;

    char *gwEnv = envGet(peEnv, "SLURM_SPANK_PSGW_ENV");
    if (gwEnv) envSet(env, "SLURM_SPANK_PSGW_ENV", gwEnv);

    char *gwBinary = envGet(peEnv, "SLURM_SPANK_PSGWD_BINARY");
    if (gwBinary) envSet(env, "PSGWD_BINARY", gwBinary);

    char *gwDebug = envGet(peEnv, "SLURM_SPANK_PSGWD_DEBUG");
    if (gwDebug) envSet(env, "PSGWD_DEBUG", gwDebug);

    char *cwd = envGet(peEnv, "SLURM_SPANK_PSGW_CWD");
    envSet(env, "PSGWD_CWD", cwd);

    char *ld = envGet(peEnv, "SLURM_SPANK_PSGWD_LD_LIB");
    if (ld) envSet(env, "LD_LIBRARY_PATH", ld);

    for (char **e = envGetArray(peEnv); e && *e; e++) {
	if (strncmp("SLURM_SPANK_PSGWD_PSP_", *e, 22)) continue;
	char *val = strchr(*e, '=');
	if (val) {
	    char *key = *e + 18;
	    *val = '\0';
	    envSet(env, key, val + 1);
	    *val = '=';
	}
    }
    return env;
}

bool startPSGWD(PSGW_Req_t *req)
{
    env_t psgwdEnv = initPSGWDEnv(req);

    char *dir = getConfValueC(config, "DIR_ROUTE_SCRIPTS");
    uint32_t gIdx = 0;
    for (uint32_t i = 0; i < req->numGWnodes; i++) {
	for (uint32_t z = 0; z < req->psgwdPerNode; z++) {
	    if (gIdx >= req->numPSGWD) {
		snprintf(msgBuf, sizeof(msgBuf), "invalid psgwd index %u num "
			 "psgwd %u\n", gIdx, req->numPSGWD);
		cancelReq(req, msgBuf);
		envDestroy(psgwdEnv);
		return false;
	    }
	    req->psgwd[gIdx].node = req->gwNodes[i];

	    fdbg(PSGW_LOG_PSGWD | PSGW_LOG_DEBUG, "starting psgwd %u on "
		 "node %i\n", gIdx, req->psgwd[gIdx].node);

	    char buf[64];
	    snprintf(buf, sizeof(buf), "%u", gIdx);
	    envSet(psgwdEnv, "PSGWD_ID", buf);
	    uint32_t id = atoi(req->jobid);
	    int ret = psExecStartScriptEx(id, "psgwd_start", dir, psgwdEnv,
					  req->psgwd[gIdx].node, cbStartPSGWD);
	    if (ret == -1) {
		snprintf(msgBuf, sizeof(msgBuf), "starting psgwd %u on node %i "
			 "failed\n", gIdx, req->psgwd[gIdx].node);
		cancelReq(req, msgBuf);
		envDestroy(psgwdEnv);
		return false;
	    }
	    gIdx++;
	}
    }

    /* set timeout to start the psgwd */

    envDestroy(psgwdEnv);
    return true;
}

int handleFinAlloc(void *data)
{
    Alloc_t *alloc = data;
    char jobid[128];

    snprintf(jobid, sizeof(jobid), "%u", alloc->id);
    PSGW_Req_t *req = Request_find(jobid);
    if (!req) {
	snprintf(jobid, sizeof(jobid), "%u", alloc->packID);
	if (!(req = Request_find(jobid))) {
	    fdbg(PSGW_LOG_DEBUG, "no request for jobid %u found\n", alloc->id);
	    return 1;
	}
    }

    /* kill psgw daemon */
    stopPSGWD(req);

    /* start the epilogue on gateway nodes */
    if (!startPElogue(req, PELOGUE_EPILOGUE)) {
	Request_delete(req);
    }

    return 1;
}

int handlePElogueRes(void *data)
{
    PElogueResource_t *res = data;
    int numNodes = 0;

    if (!envGet(res->env, "SLURM_PACK_JOB_NODELIST")) {
	/* no pack leader prologue, nothing to do */
	return 0;
    }

    char *psgwNum = envGet(res->env, "SLURM_SPANK_PSGW_NUM");
    if (psgwNum) numNodes = atoi(psgwNum);

    if (!psgwNum || !numNodes) {
        mlog("%s: no psgw used\n", __func__);
        /* no gateway resources needed */
        return 0;
    }

    char *packJobID = envGet(res->env, "SLURM_PACK_JOB_ID");
    if (!packJobID) {
        flog("no job pack ID for job %s found\n", res->jobid);
        return 2;
    }

    mlog("%s: need %i psgw nodes\n", __func__, numNodes);
    PSGW_Req_t *req = Request_add(res, packJobID);
    if (!req) {
	flog("adding request failed\n");
	return 2;
    }

    /* remove error files from previous startup attempts */
    char *cwd = envGet(req->res->env, "SLURM_SPANK_PSGW_CWD");
    if (cwd) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/JOB-%s-psgw.err", cwd, req->jobid);
	unlink(path);
    }

    char *strPsgwdPerNode = envGet(req->res->env, "SLURM_SPANK_PSGWD_PER_NODE");
    if (strPsgwdPerNode) req->psgwdPerNode = atoi(strPsgwdPerNode);

    if (!requestGWnodes(req, numNodes)) {
	flog("requesting gateway nodes failed\n");
	Request_delete(req);
	return 2;
    }

    /* wait for result from master */

    return 1;
}
