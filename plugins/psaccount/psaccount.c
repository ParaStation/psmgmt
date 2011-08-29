/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "psaccountcollect.h"
#include "psaccountlog.h"
#include "psaccountproc.h"
#include "psaccountcomm.h"
#include "psaccountjob.h"
#include "psaccountinter.h"

#include "timer.h"
#include "plugin.h"
#include "psidnodes.h"

#include "psaccount.h"

/** psid plugin requirements */
char name[] = "psaccount";
int version = 016;
int requiredAPI = 101;
plugin_dep_t dependencies[1];

/** the linux system clock ticks */
int clockTicks = -1;

/** the linux system page size */
int pageSize = -1;

/** save default handler for accouting msgs */
handlerFunc_t oldAccountHanlder = NULL;

/** the ID of the main timer */
static int mainTimerID = -1;

/** the main timer which calls periodicMain() to do all the work */
struct timeval mainTimer = {30,0};


/**
 * remarks TODO:
 *
 * -> psaccount fragt alle psaccount skripte nach fehlenden infos
 * -> psmom muss warten bis psaccount alle Info hat.
 */

/**
 * @brief Update the main timer configuration.
 *
 * @param sec The new value of the timer in seconds.
 *
 * @return No return value.
 */
static void setMainTimer(int sec)
{
    if (mainTimerID != -1) {
	Timer_remove(mainTimerID);
    }
    mainTimer.tv_sec = sec;
    mainTimerID = Timer_register(&mainTimer, periodicMain);
}

void periodicMain(void)
{
    int poll;

    /* cleanup old jobs */
    cleanupJobs();

    /* update proc snapshot */
    if ((haveActiveAccClients())) {
	updateProcSnapshot(0);

	/* update all accounting data */
	updateAllAccClients(NULL);
    }

    /* check if config changed */
    if ((poll = PSIDnodes_acctPollI(PSC_getMyID())) > 0) {
	if (poll != mainTimer.tv_sec) {
	    setMainTimer(poll);
	}
    }
}

void accountStart()
{
    /* we have no dependencies */
    dependencies[0].name = NULL;
    dependencies[0].version = 0;
}

void accountStop()
{
    /* nothing to do here */
}

int initialize(void)
{
    int poll;
    struct utsname uts;

    /* init all lists */
    initAccClientList();
    initProcList();
    initJobList();

    /* init logging facility */
    initLogger(false);
    maskLogger(0x00000);

    /* read plattform version */
    if (uname(&uts)) {
	mlog("%s: uname failed\n", __func__);
	return 1;
    } else {
	if (!!strcmp(uts.sysname, "Linux")) {
	mlog("%s: accounting will only work on Linux platforms\n", __func__);
	    return 1;
	}
    }

    /* read system clock ticks */
    if ((clockTicks = sysconf(_SC_CLK_TCK)) < 1) {
	mlog("%s: reading clock ticks failed\n", __func__);
	return 1;
    }

    /* read system page size */
    if ((pageSize = sysconf(_SC_PAGESIZE)) < 1) {
	mlog("%s: reading page size failed\n", __func__);
	return 1;
    }

    /* read deamon version */
    if (PSIDnodes_getDmnProtoV(PSC_getMyID()) < 406) {
	mlog("%s: need daemon protocol version >= 406, exiting\n", __func__);
	return 1;
    }

    /* register periodic timer */
    if ((poll = PSIDnodes_acctPollI(PSC_getMyID())) > 0) {
	mainTimer.tv_sec = poll;
    }

    if (!Timer_isInitialized()) {
	mdbg(LOG_VERBOSE, "timer facility not ready, trying to initialize"
		" it\n");
	Timer_init(NULL);
    }

    if ((mainTimerID = Timer_register(&mainTimer, periodicMain)) == -1) {
	mlog("registering main timer failed\n");
	return 1;
    }

    /* register account msg */
    oldAccountHanlder = PSID_registerMsg(PSP_CD_ACCOUNT, (handlerFunc_t) handlePSMsg);
    PSID_registerMsg(PSP_CC_PLUGIN_ACCOUNT, (handlerFunc_t) handleInterAccount);

    /* update proc snapshot */
    updateProcSnapshot(0);

    mlog("plugin started successfully\n");
    return 0;
}

void cleanup(void)
{
    /* TODO: do we need to forward all known information to psmom
     * before we are forced to exit?
     */

    /* remove all timer */
    Timer_remove(mainTimerID);
    if (jobTimerID != -1) Timer_remove(jobTimerID);

    /* unregister account msg */
    PSID_clearMsg(PSP_CC_PLUGIN_ACCOUNT);
    PSID_clearMsg(PSP_CD_ACCOUNT);
    if (oldAccountHanlder) {
	PSID_registerMsg(PSP_CD_ACCOUNT, oldAccountHanlder);
    }

    /* cleanup allocated lists/memory */
    clearAllJobs();
    clearAllAccClients();
    clearAllProcSnapshots();
}
