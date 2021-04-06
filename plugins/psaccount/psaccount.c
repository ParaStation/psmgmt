/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
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
#include "psaccountenergy.h"

#include "timer.h"
#include "plugin.h"
#include "pluginconfig.h"
#include "psidnodes.h"
#include "psidutil.h"
#include "psidcomm.h"

#define PSACCOUNT_CONFIG "psaccount.conf"

/** psid plugin requirements */
char name[] = "psaccount";
int version = 29;
int requiredAPI = 118;
plugin_dep_t dependencies[] = {
    { .name = NULL, .version = 0 } };

/** the ID of the main timer */
static int mainTimerID = -1;

/** the main timer which calls periodicMain() to do all the work */
static struct timeval mainTimer = {-1,0};

/**
 * @brief Main loop doing all the work.
 *
 * @return No return value.
 */
static void periodicMain(void)
{
    static int cleanCtr = 0;

    /* cleanup old jobs */
    if (cleanCtr++ == 4) {
	cleanupJobs();
	cleanupClients();
	cleanCtr = 0;
    }

    if (!mainTimer.tv_sec) return;

    /* update node energy/power consumption */
    energyUpdate();

    /* update proc snapshot */
    if (haveActiveClients()) {
	updateProcSnapshot();

	/* update all accounting data */
	updateClients(NULL);
    }
}

bool setMainTimer(int poll)
{
    if (poll < 0) return false;
    if (poll == mainTimer.tv_sec) return true;
    if (mainTimerID != -1) {
	Timer_remove(mainTimerID);
	mainTimerID = -1;
    }

    mainTimer.tv_sec = poll;
    if (poll) mainTimerID = Timer_register(&mainTimer, periodicMain);

    /* Also push the timer setting into the configuration */
    char valStr[32];
    snprintf(valStr, sizeof(valStr), "%d", poll);
    addConfigEntry(&config, "POLL_INTERVAL", valStr);

    return true;
}

int getMainTimer(void)
{
    return mainTimer.tv_sec;
}

int initialize(void)
{
    struct utsname uts;
    char configfn[200];

    /* init logging facility */
    initLogger(false);

    /* init all lists */
    initProc();

    /* init the config facility */
    snprintf(configfn, sizeof(configfn), "%s/%s", PLUGINDIR, PSACCOUNT_CONFIG);
    if (!initPSAccConfig(configfn)) return 1;

    /* init logging facility */
    int debugMask = getConfValueI(&config, "DEBUG_MASK");
    maskLogger(debugMask);

    /* init energy facility */
    if (!energyInit()) {
	mlog("%s: failed to initialize energy monitoring\n", __func__);
	return 1;
    }

    /* read plattform version */
    if (uname(&uts)) {
	mwarn(errno, "%s: uname()", __func__);
	return 1;
    } else if (strcmp(uts.sysname, "Linux")) {
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

    if (!initAccComm()) return 1;

    if (!Timer_isInitialized()) {
	mdbg(PSACC_LOG_VERBOSE, "timer facility not ready, trying to initialize"
		" it\n");
	Timer_init(NULL);
    }

    PSID_adjustLoginUID(getuid());

    /* register periodic timer */
    int poll = getConfValueI(&config, "POLL_INTERVAL");
    if (!setMainTimer(poll)) {
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
    if (mainTimerID != -1) Timer_remove(mainTimerID);

    energyFinalize();
    finalizeAccComm();

    if (memoryDebug) fclose(memoryDebug);

    /* cleanup allocated lists/memory */
    finalizeJobs();
    clearAllClients();
    finalizeProc();
    freeConfig(&config);
}
