/*
 * ParaStation
 *
 * Copyright (C) 2019-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pspmixconfig.h"

#include <stdbool.h>
#include <stddef.h>

#include "pspmixlog.h"

const ConfDef_t confDef[] =
{
    { "DEBUG_MASK", true, "mask", "0",
      "Mask to steer debug output" },
    { "SERVER_KILL_WAIT", true, "seconds", "5",
      "Time to wait after shutdown command before killing the PMIx server" },
    { "KILL_JOB_ON_SERVERFAIL", true, "bool", "0",
      "Flag whether to terminate jobs using a failed PMIx Server" },
    { NULL, false, NULL, NULL, NULL},
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

void initPSPMIxConfig(char *cfgName)
{
    if (parseConfigFile(cfgName, &config, false /* trimQuotes */) < 0) {
	mlog("%s: failed to open '%s'\n", __func__, cfgName);
    }

    traverseConfig(&config, verifyVisitor, confDef);

    setConfigDefaults(&config, confDef);
}

/* vim: set ts=8 sw=4 tw=0 sts=4 noet :*/
