/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginconfig.h"
#include "psaccountlog.h"

#include "psaccountconfig.h"

const ConfDef_t confDef[] =
{
    { "SAFE_ACC_UPDATES", true,	"flag", "1",
      "Obsolete! Was: Constantly forward accounting updates" },
    { "TIME_JOBSTART_POLL", true, "num", "1",
      "Poll interval in seconds at the beginning of a job" },
    { "TIME_JOBSTART_WAIT", true, "num", "1",
      "Time in seconds to wait until polling is started" },
    { "TIME_CLIENT_GRACE", true, "num", "60",
      "The grace time for clients in minutes" },
    { "TIME_JOB_GRACE", true, "num", "10",
      "The grace time for jobs in minutes" },
    { "DEBUG_MASK", true, "num", "0",
      "The debug mask for logging" },
    { "FORWARD_INTERVAL", true, "num", "2",
      "Forward every Nth accounting update to global aggregation" },
    { NULL, false, NULL, NULL, NULL },
};

LIST_HEAD(config);

static bool verifyVisitor(char *key, char *value, const void *info)
{
    const ConfDef_t *confDef = info;
    int res = verifyConfigEntry(confDef, key, value);

    switch (res) {
    case 0:
	break;
    case 1:
	mlog("Unknown config option '%s'\n", key);
	break;
    case 2:
	mlog("Option '%s' shall be numeric but is '%s'\n", key, value);
	return true;
    default:
	mlog("unexpected return %d from verifyConfigEntry()\n", res);
    }
    return false;
}

bool initConfig(char *cfgName)
{
    if (parseConfigFile(cfgName, &config) < 0) {
	mlog("%s: failed to open '%s'\n", __func__, cfgName);
	return false;
    }

    if (traverseConfig(&config, verifyVisitor, confDef)) {
	return false;
    }

    setConfigDefaults(&config, confDef);
    return true;
}
