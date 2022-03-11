/*
 * ParaStation
 *
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountfilesystem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pluginconfig.h"
#include "psenv.h"

#include "psaccountconfig.h"
#include "psaccountlog.h"
#include "psaccountscript.h"
#include "psaccounttypes.h"


/** filesystem monitor script */
static Collect_Script_t *fsScript = NULL;

/** filesystem state data */
static psAccountFS_t fsData, fsBase;

/** script poll interval in seconds */
static int pollTime = 0;

/** additional script environment */
static env_t scriptEnv;

static void parseFilesys(char *data)
{
    static bool isInit = false;
    psAccountFS_t new;

    if (sscanf(data, "readBytes:%zu writeBytes:%zu numReads:%zu "
	       "numWrites:%zu", &new.readBytes, &new.writeBytes,
	       &new.numReads, &new.numWrites) != 4) {
	flog("parsing filesystem data '%s' from script failed\n", data);
	return;
    }
    new.lastUpdate = time(NULL);

    if (!isInit) {
	memcpy(&fsBase, &new, sizeof(fsBase));
	isInit = true;

	fdbg(PSACC_LOG_FILESYS, "init base values: readBytes %zu "
	     "writeBytes %zu numReads %zu numWrites %zu\n", fsBase.readBytes,
	     fsBase.writeBytes, fsBase.numReads, fsBase.numWrites);
    }

    fsData.readBytes = new.readBytes - fsBase.readBytes;
    fsData.writeBytes = new.writeBytes - fsBase.writeBytes;
    fsData.numReads = new.numReads - fsBase.numReads;
    fsData.numWrites = new.numWrites - fsBase.numWrites;
    fsData.lastUpdate = time(NULL);

    fdbg(PSACC_LOG_FILESYS, "XmitData %zu RcvData %zu XmitPkts %zu "
	 "RcvPkts %zu\n", fsData.readBytes, fsData.writeBytes, fsData.numReads,
	 fsData.numWrites);
}

psAccountFS_t *FS_getData(void)
{
    return &fsData;
}

bool FS_startScript(void)
{
    if (fsScript) return true;

    if (pollTime < 1) pollTime = 30;
    char *fsPath = getConfValueC(&config, "FILESYSTEM_SCRIPT");

    fsScript = Script_start("psaccount-filesystem", fsPath, parseFilesys,
			    pollTime, &scriptEnv);
    if (!fsScript) {
	flog("invalid filesytem script, cannot continue\n");
	return false;
    }
    fdbg(PSACC_LOG_FILESYS, "filesystem monitor %s interval %i started\n",
	 fsPath, pollTime);

    return true;
}

bool FS_init(void)
{
    memset(&fsData, 0, sizeof(fsData));
    envInit(&scriptEnv);

    char *fsPath = getConfValueC(&config, "FILESYSTEM_SCRIPT");

    if (fsPath && fsPath[0] != '\0') {
	if (!Script_test(fsPath, "filesytem")) {
	    flog("invalid filesytem script, cannot continue\n");
	    return false;
	}

	int poll = getConfValueI(&config, "FILESYSTEM_POLL");
	if (poll < 1) {
	    /* filesytem polling is disabled */
	    return true;
	}
	pollTime = poll;

	return FS_startScript();
    }

    return true;
}

void FS_finalize(void)
{
    if (fsScript) Script_finalize(fsScript);
    fsScript = NULL;
}

bool FS_setPoll(uint32_t poll)
{
    pollTime = poll;
    if (fsScript) return Script_setPollTime(fsScript, poll);
    return true;
}

uint32_t FS_getPoll(void)
{
    return pollTime;
}

bool FS_ctlEnv(psAccountCtl_t action, const char *envStr)
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

    if (fsScript) return Script_ctlEnv(fsScript, action, envStr);
    return true;
}
