/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "psaccountproc.h"
#include "psaccountlog.h"
#include "psaccountconfig.h"
#include "psaccountscript.h"
#include "psaccounttypes.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "pslog.h"
#include "psserial.h"
#include "pscommon.h"

#include "psaccountinterconnect.h"

/** interconnect monitor script */
static Collect_Script_t *iScript = NULL;

/** interconnect state data */
static psAccountIC_t icData, icBase;

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

	flog("init base values: port %hu XmitData %zu RcvData %zu "
	     "XmitPkts %zu RcvPkts %zu\n", icData.port, icBase.recvBytes,
	     icBase.recvPkts, icBase.sendBytes, icBase.sendPkts);
    }

    icData.recvBytes = new.recvBytes - icBase.recvBytes;
    icData.recvPkts = new.recvPkts - icBase.recvPkts;
    icData.sendBytes = new.sendBytes - icBase.sendBytes;
    icData.sendPkts = new.sendPkts - icBase.sendPkts;
    icData.port = new.port;
    icData.lastUpdate = time(NULL);

    flog("port %hu XmitData %zu RcvData %zu XmitPkts %zu RcvPkts %zu\n",
	 icData.port, icData.recvBytes, icData.recvPkts, icData.sendBytes,
	 icData.sendPkts);
}

psAccountIC_t *IC_getData(void)
{
    return &icData;
}

bool IC_Init(void)
{
    memset(&icData, 0, sizeof(icData));

    char *interScript = getConfValueC(&config, "INTERCONNECT_SCRIPT");

    if (interScript && interScript[0] != '\0') {
	if (!Script_test(interScript, "interconnect")) {
	    flog("invalid interconnect script, cannot continue\n");
	    return false;
	}

	int poll = getConfValueI(&config, "INTERCONNECT_POLL");
	if (poll < 1) {
	    /* interconnect polling is disabled */
	    return true;
	}

	iScript = Script_start("interconn", interScript, parseInterconn, poll);
	if (!iScript) {
	    flog("invalid interconnect script, cannot continue\n");
	    return false;
	}
    }

    return true;
}

void IC_Finalize(void)
{
    if (iScript) Script_finalize(iScript);
}

bool IC_setPoll(uint32_t poll)
{
    if (iScript) return Script_setPollTime(iScript, poll);
    return false;
}

uint32_t IC_getPoll(void)
{
    if (iScript) return iScript->poll;
    return 0;
}
