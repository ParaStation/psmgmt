/*
 * ParaStation
 *
 * Copyright (C) 2022-2024 ParTec AG, Munich
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

#define DEFAULT_POLL_TIME 30

/** filesystem monitor script */
static Collect_Script_t *fsScript;

/** filesystem state data */
static psAccountFS_t fsData, fsBase;

/** script poll interval in seconds */
static int pollTime;

/** additional script environment */
static env_t scriptEnv;

static void parseFilesys(char *data)
{
    static bool isInit = false;
    psAccountFS_t new;

    if (!data || sscanf(data, "readBytes:%lu writeBytes:%lu numReads:%lu "
			"numWrites:%lu", &new.readBytes, &new.writeBytes,
			&new.numReads, &new.numWrites) != 4) {
	flog("cannot parse filesystem data '%s'\n", data ? data : "<null>");
	return;
    }
    new.lastUpdate = time(NULL);

    if (!isInit) {
	memcpy(&fsBase, &new, sizeof(fsBase));
	isInit = true;

	fdbg(PSACC_LOG_FILESYS, "init base values: readBytes %lu "
	     "writeBytes %lu numReads %lu numWrites %lu\n", fsBase.readBytes,
	     fsBase.writeBytes, fsBase.numReads, fsBase.numWrites);
    }

    fsData.readBytes = new.readBytes - fsBase.readBytes;
    fsData.writeBytes = new.writeBytes - fsBase.writeBytes;
    fsData.numReads = new.numReads - fsBase.numReads;
    fsData.numWrites = new.numWrites - fsBase.numWrites;
    fsData.lastUpdate = time(NULL);

    fdbg(PSACC_LOG_FILESYS, "XmitData %lu RcvData %lu XmitPkts %lu "
	 "RcvPkts %lu\n", fsData.readBytes, fsData.writeBytes, fsData.numReads,
	 fsData.numWrites);
}

psAccountFS_t *FS_getData(void)
{
    return &fsData;
}

bool FS_startScript(void)
{
    if (fsScript) return true;

    if (pollTime < 1) pollTime = DEFAULT_POLL_TIME;
    char *fsPath = getConfValueC(config, "FILESYSTEM_SCRIPT");

    fsScript = Script_start("psaccount-filesystem", fsPath, parseFilesys,
			    pollTime, scriptEnv);
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
    scriptEnv = envNew(NULL);

    char *fsPath = getConfValueC(config, "FILESYSTEM_SCRIPT");
    if (!fsPath || fsPath[0] == '\0') return true;

    if (!Script_test(fsPath, "filesytem")) {
	flog("invalid filesytem script, cannot continue\n");
	return false;
    }

    pollTime = getConfValueI(config, "FILESYSTEM_POLL");
    if (pollTime < 1) {
	/* filesytem polling is disabled */
	return true;
    }

    return FS_startScript();
}

void FS_stopScript(void)
{
    fdbg(PSACC_LOG_FILESYS, "\n");
    if (fsScript) Script_finalize(fsScript);
    fsScript = NULL;
}

void FS_cleanup(void)
{
    envDestroy(scriptEnv);
    scriptEnv = NULL;
}

bool FS_setPoll(uint32_t poll)
{
    fdbg(PSACC_LOG_FILESYS, "%i seconds\n", poll);
    pollTime = poll;
    if (fsScript) return Script_setPollTime(fsScript, poll);
    return true;
}

uint32_t FS_getPoll(void)
{
    return pollTime;
}

bool FS_ctlEnv(psAccountCtl_t action, const char *name, const char *val)
{
    switch (action) {
    case PSACCOUNT_SCRIPT_ENV_SET:
	fdbg(PSACC_LOG_FILESYS, "set %s=%s\n", name, val);
	envSet(scriptEnv, name, val);
	break;
    case PSACCOUNT_SCRIPT_ENV_UNSET:
	fdbg(PSACC_LOG_FILESYS, "unset %s\n", name);
	envUnset(scriptEnv, name);
	break;
    default:
	flog("invalid action %i\n", action);
	return false;
    }

    if (fsScript) return Script_ctlEnv(fsScript, action, name, val);
    return true;
}

char *FS_showEnv(const char *name)
{
    return Script_showEnv(scriptEnv, name);
}
