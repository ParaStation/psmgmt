/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountinterconnect.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "psenv.h"
#include "pluginconfig.h"

#include "psaccountconfig.h"
#include "psaccountlog.h"
#include "psaccountscript.h"
#include "psaccounttypes.h"

#define DEFAULT_POLL_TIME 30

/** interconnect monitor script */
static Collect_Script_t *iScript = NULL;

/** interconnect state data */
static psAccountIC_t icData, icBase;

/** script poll interval in seconds */
static int pollTime = 0;

/** additional script environment */
static env_t scriptEnv;

static void parseInterconn(char *data)
{
    static bool isInit = false;
    psAccountIC_t new;

    if (sscanf(data, "RcvData:%zu RcvPkts:%zu Select:%hu XmitData:%zu "
	       "XmitPkts:%zu", &new.recvBytes, &new.recvPkts,
	       &new.port, &new.sendBytes, &new.sendPkts) != 5) {
	flog("parsing interconnect data '%s' from script failed\n",data);
	return;
    }
    new.lastUpdate = time(NULL);

    if (!isInit) {
	memcpy(&icBase, &new, sizeof(icBase));
	isInit = true;

	fdbg(PSACC_LOG_INTERCON, "init base values: port %hu XmitData %zu "
	     "RcvData %zu XmitPkts %zu RcvPkts %zu\n", icBase.port,
	     icBase.recvBytes, icBase.recvPkts, icBase.sendBytes,
	     icBase.sendPkts);
    }

    icData.recvBytes = new.recvBytes - icBase.recvBytes;
    icData.recvPkts = new.recvPkts - icBase.recvPkts;
    icData.sendBytes = new.sendBytes - icBase.sendBytes;
    icData.sendPkts = new.sendPkts - icBase.sendPkts;
    icData.port = new.port;
    icData.lastUpdate = time(NULL);

    fdbg(PSACC_LOG_INTERCON, "port %hu XmitData %zu RcvData %zu XmitPkts %zu "
	 "RcvPkts %zu\n", icData.port, icData.recvBytes, icData.recvPkts,
	 icData.sendBytes, icData.sendPkts);
}

psAccountIC_t *IC_getData(void)
{
    return &icData;
}

bool IC_startScript(void)
{
    if (iScript) return true;

    if (pollTime < 1) pollTime = DEFAULT_POLL_TIME;
    char *interScript = getConfValueC(&config, "INTERCONNECT_SCRIPT");

    iScript = Script_start("psaccount-interconn", interScript, parseInterconn,
			   pollTime, &scriptEnv);
    if (!iScript) {
	flog("failed to start interconnect script, cannot continue\n");
	return false;
    }
    fdbg(PSACC_LOG_INTERCON, "interconnect monitor %s interval %i started\n",
	 interScript, pollTime);

    return true;
}

bool IC_init(void)
{
    memset(&icData, 0, sizeof(icData));
    envInit(&scriptEnv);

    char *interScript = getConfValueC(&config, "INTERCONNECT_SCRIPT");
    if (!interScript || interScript[0] == '\0') return true;

    if (!Script_test(interScript, "interconnect")) {
	flog("invalid interconnect script, cannot continue\n");
	return false;
    }

    int poll = getConfValueI(&config, "INTERCONNECT_POLL");
    if (poll < 1) {
	/* interconnect polling is disabled */
	return true;
    }
    pollTime = poll;

    return IC_startScript();
}

void IC_finalize(void)
{
    if (iScript) Script_finalize(iScript);
    iScript = NULL;
}

bool IC_setPoll(uint32_t poll)
{
    pollTime = poll;
    if (iScript) return Script_setPollTime(iScript, poll);
    return true;
}

uint32_t IC_getPoll(void)
{
    return pollTime;
}

bool IC_ctlEnv(psAccountCtl_t action, const char *envStr)
{
    switch (action) {
	case PSACCOUNT_SCRIPT_ENV_SET:
	    envPut(&scriptEnv, envStr);
	    break;
	case PSACCOUNT_SCRIPT_ENV_UNSET:
	    envUnset(&scriptEnv, envStr);
	    break;
	default:
	    flog("invalid action %i\n", action);
	    return false;
    }

    if (iScript) return Script_ctlEnv(iScript, action, envStr);

    return true;
}
