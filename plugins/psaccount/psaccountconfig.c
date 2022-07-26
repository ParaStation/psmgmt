/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psaccountconfig.h"

#include <stddef.h>

#include "psaccountlog.h"

const ConfDef_t confDef[] =
{
    { "POLL_INTERVAL", true, "num", "30",
      "General poll interval in seconds (must be > 0 or switch polling off)" },
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
    { "IGNORE_ROOT_PROCESSES", true, "num", "1",
      "Flag ignoring of root's processes" },
    { "ENERGY_PATH", false, "path", "",
      "Path to the nodes energy consumption sensor" },
    { "POWER_PATH", false, "path", "",
      "Path to the nodes power consumption sensor" },
    { "POWER_UNIT", false, "string", "W",
      "Unit of the power consumption sensor" },
    { "ENERGY_SCRIPT", false, "file", "",
      "Absolute path to energy monitoring script" },
    { "ENERGY_SCRIPT_POLL", true, "num", "0",
      "Energy script poll time in seconds" },
    { "INTERCONNECT_SCRIPT", false, "file", "monitor_interconnect.sh",
      "Absolute path to interconnect monitoring script" },
    { "INTERCONNECT_POLL", true, "num", "0",
      "Interconnect script poll time in seconds" },
    { "FILESYSTEM_SCRIPT", false, "file", "",
      "Absolute path to filesystem monitoring script" },
    { "FILESYSTEM_POLL", true, "num", "0",
      "Filesystem script poll time in seconds" },
    { "MONITOR_SCRIPT_PATH", false, "path", PSACCTLIBDIR,
      "Default search path for monitor scripts" },
    { NULL, false, NULL, NULL, NULL },
};

Config_t config;

static bool verifyVisitor(char *key, char *value, const void *info)
{
    const ConfDef_t *cDef = info;
    int res = verifyConfigEntry(cDef, key, value);

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

bool initPSAccConfig(char *cfgName)
{
    if (parseConfigFile(cfgName, &config, false /* trimQuotes */) < 0) {
	mlog("%s: failed to open '%s'\n", __func__, cfgName);
	return false;
    }

    if (traverseConfig(&config, verifyVisitor, confDef)) {
	return false;
    }

    setConfigDefaults(&config, confDef);
    return true;
}
