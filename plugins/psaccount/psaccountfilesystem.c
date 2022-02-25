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

#include "psaccountfilesystem.h"

/** filesystem monitor script */
static Collect_Script_t *fsScript = NULL;

/** filesystem state data */
static psAccountFS_t fsData, fsBase;

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

	flog("init base values: readBytes %zu writeBytes %zu "
	     "numReads %zu numWrites %zu\n", fsBase.readBytes,
	     fsBase.writeBytes, fsBase.numReads, fsBase.numWrites);
    }

    fsData.readBytes = new.readBytes - fsBase.readBytes;
    fsData.writeBytes = new.writeBytes - fsBase.writeBytes;
    fsData.numReads = new.numReads - fsBase.numReads;
    fsData.numWrites = new.numWrites - fsBase.numWrites;
    fsData.lastUpdate = time(NULL);

    flog("XmitData %zu RcvData %zu XmitPkts %zu RcvPkts %zu\n",
	 fsData.readBytes, fsData.writeBytes, fsData.numReads,
	 fsData.numWrites);
}

psAccountFS_t *FS_getData(void)
{
    return &fsData;
}

bool FS_Init(void)
{
    memset(&fsData, 0, sizeof(fsData));

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

	fsScript = Script_start("filesystem", fsPath, parseFilesys, poll);
	if (!fsScript) {
	    flog("invalid filesytem script, cannot continue\n");
	    return false;
	}
    }

    return true;
}

void FS_Finalize(void)
{
    if (fsScript) Script_finalize(fsScript);
}

bool FS_setPoll(uint32_t poll)
{
    if (fsScript) return Script_setPollTime(fsScript, poll);
    return false;
}

uint32_t FS_getPoll(void)
{
    if (fsScript) return fsScript->poll;
    return 0;
}
