/*
 * ParaStation
 *
 * Copyright (C) 2010-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountcomm.h"
#include "psaccountjob.h"
#include "psaccountclient.h"
#include "psaccountconfig.h"
#include "psaccounthistory.h"
#include "psaccountkvs.h"

#include "timer.h"
#include "plugin.h"
#include "psidnodes.h"
#include "psidutil.h"
#include "psidcomm.h"

#define PSACCOUNT_CONFIG "psaccount.conf"

/** psid plugin requirements */
char name[] = "psaccount";
int version = 26;
int requiredAPI = 118;
plugin_dep_t dependencies[] = {
    { .name = NULL, .version = 0 } };

/** the ID of the main timer */
static int mainTimerID = -1;

/** the main timer which calls periodicMain() to do all the work */
static struct timeval mainTimer = {30,0};

/* Forward declaration */
static void setMainTimer(int sec);

/**
 * @brief Main loop doing all the work.
 *
 * @return No return value.
 */
static void periodicMain(void)
{
    static int cleanup = 0;
    int poll = PSIDnodes_acctPollI(PSC_getMyID());

    /* cleanup old jobs */
    if (cleanup++ == 4) {
	cleanupJobs();
	cleanupClients();
	cleanup = 0;
    }

    /* check if config changed */
    if (poll >= 0 && poll != mainTimer.tv_sec) setMainTimer(poll ? poll : 30);

    if (!poll) return;

    /* update proc snapshot */
    if (haveActiveClients()) {
	updateProcSnapshot();

	/* update all accounting data */
	updateClients(NULL);
    }

}

/**
 * @brief Update the main timer configuration.
 *
 * @param sec The new value of the timer in seconds.
 *
 * @return No return value.
 */
static void setMainTimer(int sec)
{
    if (mainTimerID != -1) Timer_remove(mainTimerID);

    mainTimer.tv_sec = sec;
    mainTimerID = Timer_register(&mainTimer, periodicMain);
}

int initialize(void)
{
    int poll, debugMask;
    struct utsname uts;
    char configfn[200];

    /* init logging facility */
    initLogger(false);

    /* init all lists */
    initProc();

    /* init the config facility */
    snprintf(configfn, sizeof(configfn), "%s/%s", PLUGINDIR, PSACCOUNT_CONFIG);
    if (!initConfig(configfn)) return 1;

    /* init logging facility */
    debugMask = getConfValueI(&config, "DEBUG_MASK");
    maskLogger(debugMask);

    /* read plattform version */
    if (uname(&uts)) {
	mwarn(errno, "%s: uname()", __func__);
	return 1;
    } else if (!!strcmp(uts.sysname, "Linux")) {
	mlog("%s: accounting will only work on Linux platforms\n", __func__);
	return 1;
    }

    /* check if system's clock ticks can be determined */
    if (sysconf(_SC_CLK_TCK) < 1) {
	mlog("%s: reading clock ticks failed\n", __func__);
	return 1;
    }

    /* check if system's page size can be determined */
    if (sysconf(_SC_PAGESIZE) < 1) {
	mlog("%s: reading page size failed\n", __func__);
	return 1;
    }

    /* read deamon version */
    if (PSIDnodes_getDmnProtoV(PSC_getMyID()) < 406) {
	mlog("%s: need daemon protocol version >= 406, exiting\n", __func__);
	return 1;
    }

    if (!initAccComm()) return 1;

    if (!Timer_isInitialized()) {
	mdbg(PSACC_LOG_VERBOSE, "timer facility not ready, trying to initialize"
		" it\n");
	Timer_init(NULL);
    }

    PSID_adjustLoginUID(getuid());

    /* register periodic timer */
    poll = PSIDnodes_acctPollI(PSC_getMyID());
    if (poll >= 0) setMainTimer(poll ? poll : 30);
    if (mainTimerID == -1) {
	mlog("registering main timer for poll = %d failed\n", poll);
	return 1;
    }

    /* update proc snapshot */
    updateProcSnapshot();

    mlog("(%i) successfully started\n", version);
    return 0;
}

void cleanup(void)
{
    /* remove all timer */
    Timer_remove(mainTimerID);

    finalizeAccComm();

    if (memoryDebug) fclose(memoryDebug);

    /* cleanup allocated lists/memory */
    finalizeJobs();
    clearAllClients();
    finalizeProc();
    freeConfig(&config);
}
