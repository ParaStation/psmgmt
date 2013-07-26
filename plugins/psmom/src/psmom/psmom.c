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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <signal.h>
#include <popt.h>
#include <netinet/in.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <dlfcn.h>

#include "timer.h"
#include "pbsdef.h"
#include "psidplugin.h"
#include "psidhook.h"
#include "plugin.h"
#include "helper.h"

#include "psmomscript.h"
#include "psmomcomm.h"
#include "psmomjob.h"
#include "psmomproto.h"
#include "psmomclient.h"
#include "psmomsignal.h"
#include "psmomlog.h"
#include "psmomcollect.h"
#include "psmomconfig.h"
#include "psmomchild.h"
#include "psmompscomm.h"
#include "psmomlocalcomm.h"
#include "psmompartition.h"
#include "psmompsaccfunc.h"
#include "psmompbsserver.h"
#include "psmomjobinfo.h"
#include "psmomssh.h"
#include "psmomrecover.h"
#include "psmomenv.h"
#include "psmomkvs.h"

#include "psmom.h"

#define DEFAULT_PSMOM_CONFIG_FILE   "/opt/parastation/plugins/psmom.conf"

/** default torque server port (udp/tcp) */
int serverPort;

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

handlerFunc_t oldSpawnReqHandler = NULL;


/** psid plugin requirements */
char name[] = "psmom";
int version = 57;
int requiredAPI = 109;
plugin_dep_t dependencies[2];

/** the process id of the main psmom process */
pid_t mainPid = -1;


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
	sendSignaltoJob(NULL, SIGKILL, "shutdown");
    }
}

/**
 * @brief Start the shutdown process.
 *
 * Check if we have running jobs left and initialize the shutdown
 * sequence.
 *
 * @return returns 1 on success and 0 on error.
 */
static int shutdownJobs()
{
    struct timeval cleanupTimer = {1,0};

    mlog("shutdown jobs\n");
    if (countJobs() >0) {
	mlog("sending SIGTERM to %i remaining jobs\n", countJobs());
	sendSignaltoJob(NULL, SIGTERM, "shutdown");

	if ((cleanupTimerID = Timer_register(&cleanupTimer,
							cleanupJobs)) == -1) {
	    mlog("registering cleanup timer failed\n");
	}
	return 0;
    }

    /* all jobs are gone */
    if (doUnload) {
	PSIDplugin_unload("psmom");
    }
    return 1;
}

/**
* @brief Read some configuration values.
*
* @return No return value.
*/
static void saveConfigValues()
{
    getConfParamI("TIME_OBIT", &obitTime);
    getConfParamI("DEBUG_MASK", &debugMask);

    /* network configuration */
    getConfParamI("PORT_MOM", &momPort);
    getConfParamI("PORT_RM", &rmPort);
    getConfParamI("PORT_SERVER", &serverPort);

    /* torque protocol version */
    getConfParamI("TORQUE_VERSION", &torqueVer);
}

void startPsmom()
{
    /* we depend on the psaccount plugin */
    dependencies[0].name = "psaccount";
    dependencies[0].version = 21;
    dependencies[1].name = NULL;
    dependencies[1].version = 0;
}

void stopPsmom()
{
    /* release the logger */
    logger_finalize(psmomlogger);
}

static int initAccountingFunc()
{
    void *accHandle = NULL;

    /* get psaccount function handles */
    if (!(accHandle = PSIDplugin_getHandle("psaccount"))) {
	mlog("%s: getting psaccount handle failed\n", __func__);
	return 1;
    }

    if (!(psAccountSetGlobalCollect = dlsym(accHandle,
	    "psAccountSetGlobalCollect"))) {
	mlog("%s: loading function psAccountSetGlobalCollect() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountRegisterMOMJob = dlsym(accHandle,
	    "psAccountRegisterMOMJob"))) {
	mlog("%s: loading function psAccountRegisterMOMJob() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountUnregisterMOMJob = dlsym(accHandle,
	    "psAccountUnregisterMOMJob"))) {
	mlog("%s: loading function psAccountUnregisterMOMJob() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountGetSessionInfos = dlsym(accHandle,
	    "psAccountGetSessionInfos"))) {
	mlog("%s: loading function psAccountGetSessionInfos() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountsendSignal2Session = dlsym(accHandle,
	    "psAccountsendSignal2Session"))) {
	mlog("%s: loading function psAccountsendSignal2Session() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountSignalAllChildren = dlsym(accHandle,
	    "psAccountSignalAllChildren"))) {
	mlog("%s: loading function psAccountSignalAllChildren() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountGetJobInfo = dlsym(accHandle, "psAccountGetJobInfo"))) {
	mlog("%s: loading function psAccountGetJobInfo() failed\n", __func__);
	return 1;
    }

    if (!(psAccountisChildofParent = dlsym(accHandle,
					    "psAccountisChildofParent"))) {
	mlog("%s: loading function psAccountisChildofParent() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountFindDaemonProcs = dlsym(accHandle,
					    "psAccountFindDaemonProcs"))) {
	mlog("%s: loading function psAccountFindDaemonProcs() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountgetLoggerByClientPID = dlsym(accHandle,
					    "psAccountgetLoggerByClientPID"))) {
	mlog("%s: loading function psAccountgetLoggerByClientPID() failed\n",
		__func__);
	return 1;
    }

    if (!(psAccountreadProcStatInfo = dlsym(accHandle,
					    "psAccountreadProcStatInfo"))) {
	mlog("%s: loading function psAccountreadProcStatInfo() failed\n",
		__func__);
	return 1;
    }

    /* set collect mode in psaccount */
    psAccountSetGlobalCollect(1);

    return 0;
}

/**
 * @brief Cleanup leftover log files.
 *
 * Cleanup log files which may be lying around if the psmom was terminated
 * by force.
 *
 * @return No return value.
 */
static void cleanupLogs()
{
    int cleanJob, cleanNodes, cleanTemp;
    char *dir = NULL;

    /* cleanup jobscript files */
    getConfParamI("CLEAN_JOBS_FILES", &cleanJob);
    if (cleanJob) {
	dir = getConfParamC("DIR_JOB_FILES");
	removeDir(dir, 0);
    }

    /* cleanup node files */
    getConfParamI("CLEAN_NODE_FILES", &cleanNodes);
    if (cleanNodes) {
	dir = getConfParamC("DIR_NODE_FILES");
	removeDir(dir, 0);
    }

    /* cleanup temp (scratch) dir */
    getConfParamI("CLEAN_TEMP_DIR", &cleanTemp);
    if (cleanTemp) {
	if ((dir = getConfParamC("DIR_TEMP"))) {
	    removeDir(dir, 0);
	}
    }
}

/**
 * @brief Set torque protocol versions.
 *
 * @return Returns 1 on success and 0 on error.
 */
static int setupTorqueVersionSupport()
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
static int validateScripts()
{
    struct stat st;
    char *script, *dir, filename[400];

    if ((script = getConfParamC("BACKUP_SCRIPT"))) {
	if ((stat(script, &st)) == -1) {
	    mwarn(errno, "%s: invalid backup script '%s'", __func__, script);
	    return 1;
	}

	if (((st.st_mode & S_IFREG) != S_IFREG) ||
		((st.st_mode & S_IXUSR) != S_IXUSR)) {
	    mlog("%s: invalid permissions for backup script '%s'\n",
		    __func__, script);
	    return 1;
	}
    }

    if ((script = getConfParamC("TIMEOUT_SCRIPT"))) {
	if ((stat(script, &st)) == -1) {
	    mwarn(errno, "%s: invalid timeout script '%s'", __func__, script);
	    return 1;
	}

	if (((st.st_mode & S_IFREG) != S_IFREG) ||
		((st.st_mode & S_IXUSR) != S_IXUSR)) {
	    mlog("%s: invalid permissions for timeout script '%s'\n",
		    __func__, script);
	    return 1;
	}
    }

    /* validate prologue/epilogue scripts */
    dir = getConfParamC("DIR_SCRIPTS");
    snprintf(filename, sizeof(filename), "%s/prologue", dir);
    if ((checkPELogueFileStats(filename, 1)) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 1;
    }
    snprintf(filename, sizeof(filename), "%s/prologue.parallel", dir);
    if ((checkPELogueFileStats(filename, 1)) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 1;
    }
    snprintf(filename, sizeof(filename), "%s/epilogue", dir);
    if ((checkPELogueFileStats(filename, 1)) == -2) {
	mlog("%s: invalid permissions for '%s'\n", __func__, filename);
	return 1;
    }
    snprintf(filename, sizeof(filename), "%s/epilogue.parallel", dir);
    if ((checkPELogueFileStats(filename, 1)) == -2) {
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
static int validateDirs()
{
    struct stat st;
    char *dir;

    dir = getConfParamC("DIR_JOB_FILES");
    if ((stat(dir, &st)) == -1 || ((st.st_mode & S_IFDIR) != S_IFDIR)) {
	mwarn(errno, "%s: invalid job dir '%s'", __func__, dir);
	return 1;
    }

    dir = getConfParamC("DIR_NODE_FILES");
    if ((stat(dir, &st)) == -1 || ((st.st_mode & S_IFDIR) != S_IFDIR)) {
	mwarn(errno, "%s: invalid node files dir '%s'", __func__, dir);
	return 1;
    }

    if ((dir = getConfParamC("DIR_TEMP"))) {
	if ((stat(dir, &st)) == -1 || ((st.st_mode & S_IFDIR) != S_IFDIR)) {
	    mwarn(errno, "%s: invalid temp dir '%s'", __func__, dir);
	    return 1;
	}
    }

    dir = getConfParamC("DIR_JOB_ACCOUNT");
    if ((stat(dir, &st)) == -1 || ((st.st_mode & S_IFDIR) != S_IFDIR)) {
	mwarn(errno, "%s: invalid account dir '%s'", __func__, dir);
	return 1;
    }

    dir = getConfParamC("DIR_SCRIPTS");
    if ((stat(dir, &st)) == -1 || ((st.st_mode & S_IFDIR) != S_IFDIR)) {
	mwarn(errno, "%s: invalid scripts dir '%s'", __func__, dir);
	return 1;
    }

    dir = DEFAULT_DIR_JOB_UNDELIVERED;
    if ((stat(dir, &st)) == -1 || ((st.st_mode & S_IFDIR) != S_IFDIR)) {
	mwarn(errno, "%s: invalid undelivered dir '%s'", __func__, dir);
	return 1;
    }

    if ((dir = getConfParamC("DIR_LOCAL_BACKUP"))) {
	if ((stat(dir, &st)) == -1 || ((st.st_mode & S_IFDIR) != S_IFDIR)) {
	    mwarn(errno, "%s: invalid backup dir '%s'", __func__, dir);
	    return 1;
	}
    }
    return 0;
}

static int setRootHome()
{
    struct passwd *spasswd;

    if (!(spasswd = getpwnam("root"))) {
	mlog("%s: getpwnam(root) failed\n", __func__);
	return 0;
    }

    snprintf(rootHome, sizeof(rootHome), "%s", spasswd->pw_dir);
    return 1;
}

/**
 * @brief Unregister all hooks and message handler.
 *
 * @param verbose If set to true an error message will be displayed
 * when unregistering a hook or a message handle fails.
 *
 * @return No return value.
 */
static void unregisterHooks(int verbose)
{
    /* unregister psmom msg */
    PSID_clearMsg(PSP_CC_PSMOM);

    /* unregister msg drop handler */
    PSID_clearDropper(PSP_CC_PSMOM);

    /* restore old SPAWNREQ handler */
    if (oldSpawnReqHandler) {
	PSID_registerMsg(PSP_CD_SPAWNREQ, (handlerFunc_t) oldSpawnReqHandler);
    }

    /* unregister hooks */
    if (!(PSIDhook_del(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	if (verbose) mlog("unregister 'PSIDHOOK_NODE_DOWN' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_CREATEPART, handleCreatePart))) {
	if (verbose) mlog("unregister 'PSIDHOOK_CREATEPART' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_CREATEPARTNL, handleCreatePartNL))) {
	if (verbose) mlog("unregister 'PSIDHOOK_CREATEPARTNL' failed\n");
    }

    if (!(PSIDhook_del(PSIDHOOK_SHUTDOWN, handleShutdown))) {
	if (verbose) mlog("unregister 'PSIDHOOK_SHUTDOWN' failed\n");
    }
}

int initialize(void)
{
    static struct timeval time_now;

    /* flag to prevent side effects of children */
    isMaster = 1;

    /* init the logger (log to syslog) */
    initLogger("psmom", NULL);

    /* we need to have root privileges, or the pbs_server will refuse our
     * connection attempt.
     */
    if(getuid() != 0) {
	fprintf(stderr, "%s: psmom must have root privileges\n", __func__);
	return 1;
    }

    /* init the configuration */
    if (!(initConfig(DEFAULT_PSMOM_CONFIG_FILE))) {
	fprintf(stderr, "%s: init of the configuration failed\n", __func__);
	return 1;
    }

    /* save local config values */
    saveConfigValues();

    /* test if all needed directories are there */
    if (validateDirs()) return 1;

    /* test if all configured scripts exists and have the correct permissions */
    if (validateScripts()) return 1;

    if ((initAccountingFunc())) {
	fprintf(stderr, "%s: init accounting functions failed\n", __func__);
	return 1;
    }

    gettimeofday(&time_now, NULL);
    srand(time_now.tv_usec);

    /* init all data lists */
    initComList();
    initJobList();
    initClientList();
    initInfoList();
    initChildList();
    initServerList();
    initJobInfoList();
    initSSHList();
    initEnvList();

    isInit = 1;

    /* set debug mask */
    maskLogger(debugMask);

    /* read pwnam buffer value */
    if ((pwBufferSize = sysconf(_SC_GETPW_R_SIZE_MAX)) < 1) {
	mlog("%s: getting pw buffer size failed, using 1024\n",
		__func__);
	pwBufferSize = 1024;
    }

    /* cleanup left over log files */
    cleanupLogs();

    /* save the home directory of root */
    if (!(setRootHome())) {
	return 1;
    }

    /* setup torque version support */
    if (!(setupTorqueVersionSupport())) {
	return 1;
    }

    /* init the tcp communication layer */
    if ((wBind(momPort, TCP_PROTOCOL)) < 0) {
	mlog("Listen on tcp port %d failed\n", momPort);
	return 1;
    }

    if ((wBind(rmPort, TCP_PROTOCOL)) < 0) {
	mlog("Listen on tcp port %d failed\n", rmPort);
	return 1;
    }

    /* init the rpp communication layer */
    if ((wBind(rmPort, RPP_PROTOCOL)) < 0) {
	mlog("listen on rpp port %d failed\n", rmPort);
	return 1;
    }

    /* create master socket */
    openMasterSock();

    /* register inter psmom msg */
    PSID_registerMsg(PSP_CC_PSMOM, (handlerFunc_t) handlePSMsg);

    /* register handler for dropped msgs */
    PSID_registerDropper(PSP_CC_PSMOM, (handlerFunc_t) handleDroppedMsg);

    oldSpawnReqHandler = PSID_registerMsg(PSP_CD_SPAWNREQ,
				(handlerFunc_t) handlePSSpawnReq);

    /* register needed hooks */
    if (!(PSIDhook_add(PSIDHOOK_NODE_DOWN, handleNodeDown))) {
	mlog("register 'PSIDHOOK_NODE_DOWN' failed\n");
	goto INIT_ERROR;
    }

    if (!(PSIDhook_add(PSIDHOOK_CREATEPART, handleCreatePart))) {
	mlog("register 'PSIDHOOK_CREATEPART' failed\n");
	goto INIT_ERROR;
    }

    if (!(PSIDhook_add(PSIDHOOK_CREATEPARTNL, handleCreatePartNL))) {
	mlog("register 'PSIDHOOK_CREATEPARTNL' failed\n");
	goto INIT_ERROR;
    }

    if (!(PSIDhook_add(PSIDHOOK_SHUTDOWN, handleShutdown))) {
	mlog("register 'PSIDHOOK_SHUTDOWN' failed\n");
	goto INIT_ERROR;
    }

    /* make sure timer facility is ready */
    if (!Timer_isInitialized()) {
	mdbg(PSMOM_LOG_WARN, "timer facility not ready, trying to initialize"
	    " it\n");
	Timer_init(NULL);
    }

    /* connect to the pbs server(s) */
    if ((openServerConnections())) {
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
	clearConfig();
	return;
    }

    /* make sure all left forwarders and children are gone */
    if (countJobs() > 0) {
	sendSignaltoJob(NULL, SIGKILL, "shutdown");
    }

    /* free config values */
    clearConfig();

    /* set collect mode in psaccount */
    if (psAccountSetGlobalCollect) psAccountSetGlobalCollect(0);

    mlog("shutdown communication\n");

    /* remove all registered hooks and msg handler */
    unregisterHooks(1);

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
    clearSSHList();
    /*
    clearClientList();
    */
    if (memoryDebug) fclose(memoryDebug);

    mlog("...Bye.\n");
}
