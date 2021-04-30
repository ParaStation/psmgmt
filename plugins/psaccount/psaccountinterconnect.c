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

#include "pluginconfig.h"
#include "pluginforwarder.h"
#include "pluginmalloc.h"
#include "pslog.h"
#include "psserial.h"
#include "pscommon.h"

#include "psaccountinterconnect.h"

/** interconnect monitor script */
static Collect_Script_t *iScript = NULL;

static void parseInterconn(char *data)
{
    unsigned long long power, energy;

    if (sscanf(data, "power:%llu energy:%llu", &power, &energy) != 2) {
	mlog("%s: parsing energy data '%s' from script failed\n",
	     __func__, data);
	return;
    }
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
    if (iScript) {
	Script_finalize(iScript);
    }
}
