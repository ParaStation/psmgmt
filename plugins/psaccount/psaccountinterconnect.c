/*
 * ParaStation
 *
 * Copyright (C) 2021 ParTec Cluster Competence Center GmbH, Munich
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
	mlog("%s: parsing interconnect data '%s' from script failed\n",
	     __func__, data);
	return;
    }
    new.lastUpdate = time(NULL);

    if (!isInit) {
	memcpy(&icBase, &new, sizeof(icBase));
	isInit = true;

	mlog("%s: init base values: port %hu XmitData %zu RcvData %zu "
	     "XmitPkts %zu RcvPkts %zu\n", __func__, icData.port,
	     icBase.recvBytes, icBase.recvPkts, icBase.sendBytes,
	     icBase.sendPkts);
    }

    icData.recvBytes = new.recvBytes - icBase.recvBytes;
    icData.recvPkts = new.recvPkts - icBase.recvPkts;
    icData.sendBytes = new.sendBytes - icBase.sendBytes;
    icData.sendPkts = new.sendPkts - icBase.sendPkts;
    icData.port = new.port;
    icData.lastUpdate = time(NULL);

    mlog("%s: port %hu XmitData %zu RcvData %zu XmitPkts %zu RcvPkts %zu\n",
	 __func__, icData.port, icData.recvBytes, icData.recvPkts,
	 icData.sendBytes, icData.sendPkts);
}

bool InterconnInit(void)
{
    char *interScript = getConfValueC(&config, "INTERCONNECT_SCRIPT");
    if (interScript && interScript[0] != '\0') {
	int poll = getConfValueI(&config, "INTERCONNECT_POLL");
	iScript = Script_start("interconn", interScript, parseInterconn, poll);
	if (!iScript) {
	    mlog("%s: invalid interconnect script, cannot continue\n", __func__);
	    return false;
	}
    }

    return true;
}

void InterconnFinalize(void)
{
    if (iScript) Script_finalize(iScript);
}
