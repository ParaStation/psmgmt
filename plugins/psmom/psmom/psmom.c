/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmom.h"

#include <dlfcn.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "timer.h"
#include "plugin.h"
#include "pluginconfig.h"
#include "pluginhelper.h"
#include "psidcomm.h"
#include "psidhook.h"
#include "psidplugin.h"
#include "psserial.h"

#include "psaccounthandles.h"
#include "pspamhandles.h"

#include "psmomscript.h"
#include "psmomauth.h"
#include "psmomchild.h"
#include "psmomcollect.h"
#include "psmomcomm.h"
#include "psmomconfig.h"
#include "psmomenv.h"
#include "psmomjob.h"
#include "psmomjobinfo.h"
#include "psmomkvs.h"
#include "psmomlist.h"
#include "psmomlocalcomm.h"
#include "psmomlog.h"
#include "psmompartition.h"
#include "psmompbsserver.h"
#include "psmompscomm.h"
#include "psmomrecover.h"

#define PSMOM_CONFIG_FILE  PLUGINDIR "/psmom.conf"

/** default torque server port (udp/tcp) */
static int serverPort;

/** default mom port (tcp) */
int momPort;

/** default rm port (tcp/udp)
 *
 * the tcp port is used for tm requests e.g. torque mpiexec and openmpi
 * */
int rmPort;

/** generic torque version support */
int torqueVer;

/** resmon protocol version number */
unsigned int RM_PROTOCOL_VER;

/** task manager protocol version number */
unsigned int TM_PROTOCOL_VER;

/** inter-manager (mom) protocol number */
unsigned int IM_PROTOCOL_VER;

/** inter-server protocol number */
unsigned int IS_PROTOCOL_VER;

/** the maximal number of seconds to wait for all jobs to exit. */
static int obitTime;

/** the counter to control the obit timeout */
static int obitTimeCounter = 0;

/** the current debug mask */
static int debugMask;

/** the job cleanup timer */
static int cleanupTimerID = -1;

/** flag which is set to 1 if the initialisation was successful */
static int isInit = 0;

/** flag to which is set to 1 if we should call the plugin unload function */
static int doUnload = 1;

/** flag which will be set to 1 if we are currently in the shutdown process */
int doShutdown = 0;

/** set to the maximal size of a pwnam structure */
long pwBufferSize = 0;

/** set to the home directory of root */
char rootHome[100];

/** psid plugin requirements */
char name[] = "psmom";
int version = 58;
int requiredAPI = 109;
plugin_dep_t dependencies[] = {
    { .name = "psaccount", .version = 30 },
    { .name = "pspam", .version = 5 },
    { .name = NULL, .version = 0 } };

/** the process id of the main psmom process */
static pid_t mainPid = -1;

/* flag to prevent side effects of children */
int isMaster = 1;


/**
 * @brief Wait for all jobs to terminate.
 *
 * @return No return value.
 */
static void cleanupJobs(void)
{
    /* check if we are waiting for jobs to exit */
    obitTimeCounter++;

    if (countJobs() == 0) {
	Timer_remove(cleanupTimerID);
	cleanupTimerID = -1;
	if (doUnload) {
	    PSIDplugin_unload("psmom");
	}
    }

    if (obitTime == obitTimeCounter) {
	mlog("sending SIGKILL to %i remaining jobs\n", countJobs());
	signalAllJobs(SIGKILL, "shutdown");
    }
}

/**
 * @brief Start the shutdown process.
 *
 * Check if we have running jobs left and initialize the shutdown
 * sequence.
 *
 * @return Return true on success and false on error
 */
static bool shutdownJobs(void)
{
    struct timeval cleanupTimer = {1,0};

    mlog("shutdown jobs\n");
    if (countJobs() >0) {
	mlog("sending SIGTERM to %i remaining jobs\n", countJobs());
	signalAllJobs(SIGTERM, "shutdown");

	cleanupTimerID = Timer_register(&cleanupTimer, cleanupJobs);
	if (cleanupTimerID == -1) {
	    mlog("registering cleanup timer failed\n");
	}
	return false;
    }

    /* all jobs are gone */
    if (doUnload) PSIDplugin_unload("psmom");
    return true;
}

/**
* @brief Read some configuration values.
*
* @return No return value.
*/
static void saveConfigValues(void)
{
    obitTime = getConfValueI(config, "TIME_OBIT");
    debugMask = getConfValueI(config, "DEBUG_MASK");

    /* network configuration */
    momPort = getConfValueI(config, "PORT_MOM");
    rmPort = getConfValueI(config, "PORT_RM");
    serverPort = getConfValueI(config, "PORT_SERVER");

    /* torque protocol version */
    torqueVer = getConfValueI(config, "TORQUE_VERSION");
}

static bool initAccountingFunc(void)
{
    void *accHandle = PSIDplugin_getHandle("psaccount");

    /* get psaccount function handles */
    if (!accHandle) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	return false;
    }

    psAccountSetGlobalCollect = dlsym(accHandle, "psAccountSetGlobalCollect");
    if (!psAccountSetGlobalCollect) {
	mlog("%s: loading function psAccountSetGlobalCollect() failed\n",
	     __func__);
	return false;
    }

    psAccountRegisterJob = dlsym(accHandle, "psAccountRegisterJob");
    if (!psAccountRegisterJob) {
	mlog("%s: loading function psAccountRegisterJob() failed\n", __func__);
	return false;
    }

    psAccountUnregisterJob = dlsym(accHandle, "psAccountUnregisterJob");
    if (!psAccountUnregisterJob) {
	mlog("%s: loading function psAccountUnregisterJob() failed\n",
	     __func__);
	return false;
    }

    psAccountGetSessionInfos = dlsym(accHandle, "psAccountGetSessionInfos");
    if (!psAccountGetSessionInfos) {
	mlog("%s: loading function psAccountGetSessionInfos() failed\n",
	     __func__);
	return false;
    }

    psAccountSignalSession = dlsym(accHandle, "psAccountSignalSession");
    if (!psAccountSignalSession) {
	mlog("%s: loading function psAccountSignalSession() failed\n",
	     __func__);
	return false;
    }

    psAccountGetDataByJob = dlsym(accHandle, "psAccountGetDataByJob");
    if (!psAccountGetDataByJob) {
	mlog("%s: loading function psAccountGetDataByJob() failed\n", __func__);
	return false;
    }

    psAccountIsDescendant = dlsym(accHandle, "psAccountIsDescendant");
    if (!psAccountIsDescendant) {
	mlog("%s: loading function psAccountIsDescendant() failed\n",
	     __func__);
	return false;
    }

    psAccountFindDaemonProcs = dlsym(accHandle, "psAccountFindDaemonProcs");
    if (!psAccountFindDaemonProcs) {
	mlog("%s: loading function psAccountFindDaemonProcs() failed\n",
	     __func__);
	return false;
    }

    psAccountGetLoggerByClient = dlsym(accHandle,"psAccountGetLoggerByClient");
    if (!psAccountGetLoggerByClient) {
	mlog("%s: loading function psAccountGetLoggerByClient() failed\n",
		__func__);
	return false;
    }

    psAccountGetPoll = dlsym(accHandle, "psAccountGetPoll");
    if (!psAccountGetPoll) {
	mlog("%s: loading psAccountGetPoll() failed\n", __func__);
	return false;
    }

    psAccountSetPoll = dlsym(accHandle, "psAccountSetPoll");
    if (!psAccountSetPoll) {
	mlog("%s: loading psAccountSetPoll() failed\n", __func__);
	return false;
    }

    /* we want to have periodic updates on used resources */
    if (!psAccountGetPoll(PSACCOUNT_OPT_MAIN)) {
	psAccountSetPoll(PSACCOUNT_OPT_MAIN , 30);
    }

    /* set collect mode in psaccount */
    psAccountSetGlobalCollect(true);

    return true;
}

static bool initPsPAMFunc(void)
{
    void *pspamHandle = PSIDplugin_getHandle("pspam");

    /* get psaccount function handles */
    if (!pspamHandle) {
	mlog("%s: getting pspam handle failed\n", __func__);
	return false;
    }

    psPamAddUser = dlsym(pspamHandle, "psPamAddUser");
    if (!psPamAddUser) {
	mlog("%s: loading function psPamAddUser() failed\n", __func__);
	return false;
    }

    psPamSetState = dlsym(pspamHandle, "psPamSetState");
    if (!psPamSetState) {
	mlog("%s: loading function psPamSetState() failed\n", __func__);
	return false;
    }

    psPamDeleteUser = dlsym(pspamHandle, "psPamDeleteUser");
    if (!psPamDeleteUser) {
	mlog("%s: loading function psPamDeleteUser() failed\n", __func__);
	return false;
    }

    psPamFindSessionForPID = dlsym(pspamHandle, "psPamFindSessionForPID");
    if (!psPamFindSessionForPID) {
	mlog("%s: loading function psPamFindSessionForPID() failed\n",
	     __func__);
	return false;
    }

    return true;
}

/**
 * @brief Cleanup leftover log files.
 *
 * Cleanup log files which may be lying around if the psmom was terminated
 * by force.
 *
 * @return No return value.
 */
static void cleanupLogs(void)
{
    bool cleanJob, cleanNodes, cleanTemp;
    char *dir = NULL;

    /* cleanup jobscript files */
    cleanJob = getConfValueI(config, "CLEAN_JOBS_FILES");
    if (cleanJob) {
	dir = getConfValueC(config, "DIR_JOB_FILES");
	removeDir(dir, false);
    }

    /* cleanup node files */
    cleanNodes = getConfValueI(config, "CLEAN_NODE_FILES");
    if (cleanNodes) {
	dir = getConfValueC(config, "DIR_NODE_FILES");
	removeDir(dir, false);
    }

    /* cleanup temp (scratch) dir */
    cleanTemp = getConfValueI(config, "CLEAN_TEMP_DIR");
    if (cleanTemp) {
	dir = getConfValueC(config, "DIR_TEMP");
	if (dir) removeDir(dir, false);
    }
}

/**
 * @brief Set torque protocol versions.
 *
 * @return Returns 1 on success and 0 on error.
 */
static int setupTorqueVersionSupport(void)
{
    switch (torqueVer) {
	case 2:
	    RM_PROTOCOL_VER = 1;
	    TM_PROTOCOL_VER = 1;
	    IM_PROTOCOL_VER = 1;
	    IS_PROTOCOL_VER = 1;
	    break;
	case 3:
	    RM_PROTOCOL_VER = 2;
	    TM_PROTOCOL_VER = 2;
	    IM_PROTOCOL_VER = 2;
	    IS_PROTOCOL_VER = 2;
	    break;
	default:
	    mlog("Unsupported torque version '%i'\n", torqueVer);
	    return 0;
    }
    return 1;
}

/**
 * @brief We got informed early that the psid shutdown sequence has started.
 *
 * @return Always return 0.
 */
static int handleShutdown(void *data)
{
    /* only take notice of phase 0 */
    if (data && *(int *)data != 0) return 0;

    /* we are not finalized yet, so we can't trigger an unload */
    doUnload = 0;

    /* we have little time, so force shutdown quickly */
    obitTime = 2;

    /* kill all running jobs */
    finalize();

    return 0;
}

/**
 * @brief Test if all configured scripts are existing.
 *
 * @return Returns 1 on error and 0 on success.
 */
static int validateScripts(void)
{
    struct stat st;
    char *script, *dir, filename[400];

    script = getConfValueC(config, "BACKUP_SCRIPT");
    if (script) {
	if (stat(script, &st) == -1) {
	    mwarn(errno, "%s: invalid backup script '%s'", __func__, script);
	    return 1;
	}

	if ((st.st_mode & S_IFREG) != S_IFREG
	    || (st.st_mode & S_IXUSR) != S_IXUSR) {
	    mlog("%s: invalid permissions for backup script '%s'\n",
		 __func__, script);
	    return 1;
	}
    }

    script = getConfValueC(config, "TIMEOUT_SCRIPT");
    if (script) {
	if (stat(script, &st) == -1) {
	    mwarn(errno, "%s: invalid timeout script '%s'", __func__, script);
	    return 1;
	}

	if ((st.st_mode & S_IFREG) != S_IFREG
	    || (st.st_mode & S_IXUSR) != S_IXUSR) {
	    mlog("%s: invalid permissions for timeout script '%s'\n",
		 __func__, script);
	    return 1;
	}
    }

    /* validate prologue/epilogue scripts */
    dir = getConfValueC(config, "DIR_SCRIPTS");
    snprintf(filename, sizeof(filename), "%s/prologue", dir);
    if (checkPELogueFileStats(filename, 1) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 1;
    }
    snprintf(filename, sizeof(filename), "%s/prologue.parallel", dir);
    if (checkPELogueFileStats(filename, 1) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 1;
    }
    snprintf(filename, sizeof(filename), "%s/epilogue", dir);
    if (checkPELogueFileStats(filename, 1) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 1;
    }
    snprintf(filename, sizeof(filename), "%s/epilogue.parallel", dir);
    if (checkPELogueFileStats(filename, 1) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 1;
    }

    return 0;
}

/**
 * @brief Test if all needed directories are existing.
 *
 * @return Returns 1 on error and 0 on success.
 */
static int validateDirs(void)
{
    struct stat st;
    char *dir;

    dir = getConfValueC(config, "DIR_JOB_FILES");
    if (stat(dir, &st) == -1 || (st.st_mode & S_IFDIR) != S_IFDIR) {
	mwarn(errno, "%s: invalid job dir '%s'", __func__, dir);
	return 1;
    }

    dir = getConfValueC(config, "DIR_NODE_FILES");
    if (stat(dir, &st) == -1 || (st.st_mode & S_IFDIR) != S_IFDIR) {
	mwarn(errno, "%s: invalid node files dir '%s'", __func__, dir);
	return 1;
    }

    dir = getConfValueC(config, "DIR_TEMP");
    if (dir && (stat(dir, &st) == -1 || (st.st_mode & S_IFDIR) != S_IFDIR)) {
	mwarn(errno, "%s: invalid temp dir '%s'", __func__, dir);
	return 1;
    }

    dir = getConfValueC(config, "DIR_JOB_ACCOUNT");
    if (stat(dir, &st) == -1 || (st.st_mode & S_IFDIR) != S_IFDIR) {
	mwarn(errno, "%s: invalid account dir '%s'", __func__, dir);
	return 1;
    }

    dir = getConfValueC(config, "DIR_SCRIPTS");
    if (stat(dir, &st) == -1 || (st.st_mode & S_IFDIR) != S_IFDIR) {
	mwarn(errno, "%s: invalid scripts dir '%s'", __func__, dir);
	return 1;
    }

    dir = DEFAULT_DIR_JOB_UNDELIVERED;
    if (stat(dir, &st) == -1 || (st.st_mode & S_IFDIR) != S_IFDIR) {
	mwarn(errno, "%s: invalid undelivered dir '%s'", __func__, dir);
	return 1;
    }

    dir = getConfValueC(config, "DIR_LOCAL_BACKUP");
    if (dir && (stat(dir, &st) == -1 || (st.st_mode & S_IFDIR) != S_IFDIR)) {
	mwarn(errno, "%s: invalid backup dir '%s'", __func__, dir);
	return 1;
    }

    return 0;
}

static int setRootHome(void)
{
    char *pwBuf;
    struct passwd *spasswd = PSC_getpwnamBuf("root", &pwBuf);

    if (!spasswd) {
	mlog("%s: getpwnam(root) failed\n", __func__);
	return 0;
    }

    snprintf(rootHome, sizeof(rootHome), "%s", spasswd->pw_dir);
    free(pwBuf);
    return 1;
}

/**
 * @brief Unregister all hooks and message handlers
 *
 * @param verbose If set to true an error message will be displayed
 * when unregistering a hook or a message handle fails.
 *
 * @return No return value
 */
static void unregisterHooks(int verbose)
{
    /* restore old SPAWNREQ handler */
    PSID_clearMsg(PSP_CD_SPAWNREQ, (handlerFunc_t)handlePSSpawnReq);

    /* unregister hooks */
    if (!PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	if (verbose) mlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_CREATEPART, handleCreatePart)) {
	if (verbose) mlog("unregister 'PSIDHOOK_CREATEPART' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_CREATEPARTNL, handleCreatePartNL)) {
	if (verbose) mlog("unregister 'PSIDHOOK_CREATEPARTNL' failed\n");
    }

    if (!PSIDhook_del(PSIDHOOK_SHUTDOWN, handleShutdown)) {
	if (verbose) mlog("unregister 'PSIDHOOK_SHUTDOWN' failed\n");
    }
}

int initialize(FILE *logfile)
{
    static struct timeval time_now;

    /* flag to prevent side effects of children */
    isMaster = 1;

    /* init the logger (log to syslog) */
    initLogger(name, logfile);

    /* we need to have root privileges, or the pbs_server will refuse our
     * connection attempt.
     */
    if(getuid() != 0) {
	fprintf(stderr, "%s: psmom must have root privileges\n", __func__);
	return 1;
    }

    /* init the configuration */
    if (!initPSMomConfig(PSMOM_CONFIG_FILE)) {
	fprintf(stderr, "%s: init of the configuration failed\n", __func__);
	return 1;
    }

    /* save local config values */
    saveConfigValues();

    /* psmom's communication ignores byte-order */
    setByteOrder(false);

    /* test if all needed directories are there */
    if (validateDirs()) return 1;

    /* test if all configured scripts exists and have the correct permissions */
    if (validateScripts()) return 1;

    if (!initAccountingFunc()) {
	fprintf(stderr, "%s: init accounting functions failed\n", __func__);
	return 1;
    }

    if (!initPsPAMFunc()) {
	fprintf(stderr, "%s: init pspam functions failed\n", __func__);
	return 1;
    }

    gettimeofday(&time_now, NULL);
    srand(time_now.tv_usec);

    /* init all data lists */
    initComList();
    initAuthList();
    initInfoList();
    initChildList();
    initServerList();
    initEnvList();

    isInit = 1;

    /* set debug mask */
    maskLogger(debugMask);

    /* read pwnam buffer value */
    pwBufferSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (pwBufferSize < 1) {
	mlog("%s: getting pw buffer size failed, using 1024\n",
		__func__);
	pwBufferSize = 1024;
    }

    /* cleanup left over log files */
    cleanupLogs();

    /* save root's home directory */
    if (!setRootHome()) return 1;

    /* setup torque version support */
    if (!setupTorqueVersionSupport()) return 1;

    /* init the tcp communication layer */
    if (wBind(momPort, TCP_PROTOCOL) < 0) {
	mlog("Listen on tcp port %d failed\n", momPort);
	return 1;
    }

    /* init the rpp communication layer */
    if (wBind(rmPort, RPP_PROTOCOL) < 0) {
	mlog("listen on rpp port %d failed\n", rmPort);
	return 1;
    }

    /* create master socket */
    openMasterSock();

    /* We'll use fragmented messages between different psmoms */
    initPSComm();

    PSID_registerMsg(PSP_CD_SPAWNREQ, (handlerFunc_t)handlePSSpawnReq);

    /* register needed hooks */
    if (!PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown)) {
	mlog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	goto INIT_ERROR;
    }

    if (!PSIDhook_add(PSIDHOOK_CREATEPART, handleCreatePart)) {
	mlog("register 'PSIDHOOK_CREATEPART' failed\n");
	goto INIT_ERROR;
    }

    if (!PSIDhook_add(PSIDHOOK_CREATEPARTNL, handleCreatePartNL)) {
	mlog("register 'PSIDHOOK_CREATEPARTNL' failed\n");
	goto INIT_ERROR;
    }

    if (!PSIDhook_add(PSIDHOOK_SHUTDOWN, handleShutdown)) {
	mlog("register 'PSIDHOOK_SHUTDOWN' failed\n");
	goto INIT_ERROR;
    }

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PSMOM_LOG_WARN, "timer facility not yet ready\n");
	Timer_init(NULL);
    }

    /* connect to the pbs server(s) */
    if (openServerConnections()) {
	goto INIT_ERROR;
    }

    /* recover lost jobs */
    recoverJobInfo();

    mainPid = getpid();

    stat_startTime = time(NULL);

    mlog("(%i) successfully started\n", version);
    return 0;


INIT_ERROR:
    unregisterHooks(0);
    finalizePSComm();
    return 1;
}

void finalize(void)
{
    if (!isMaster || getpid() != mainPid) {
	return;
    }

    /* set the node down so we don't receive new jobs */
    setPsmomState("down");
    sendStatusUpdate();

    /* not enough time to run any epilogue scripts */
    doShutdown = 1;

    /* kill all running jobs */
    shutdownJobs();
}

void cleanup(void)
{
    if (!isMaster || getpid() != mainPid) {
	return;
    }

    /* make sure all timers are gone */
    if (cleanupTimerID != -1) {
	Timer_remove(cleanupTimerID);
	cleanupTimerID = -1;
    }

    if (!isInit) {
	/* free config values */
	freeConfig(config);
	return;
    }

    /* make sure all left forwarders and children are gone */
    if (countJobs() > 0) {
	signalAllJobs(SIGKILL, "shutdown");
    }

    /* free config values */
    freeConfig(config);

    /* set collect mode in psaccount */
    if (psAccountSetGlobalCollect) psAccountSetGlobalCollect(false);

    mlog("shutdown communication\n");

    /* remove all registered hooks and msg handler */
    unregisterHooks(1);
    finalizePSComm();

    /* close local comm socket */
    closeMasterSock();

    /* close all network connections */
    closeAllConnections();

    mlog("freeing memory\n");

    /* free all malloced memory */
    clearJobList();
    clearChildList();
    clearDataList(&infoData.list);
    clearDataList(&staticInfoData.list);
    clearJobInfoList();
    clearAuthList();
    if (memoryDebug) fclose(memoryDebug);

    mlog("...Bye.\n");
    finalizeLogger();
}
