/*
 * ParaStation
 *
 * Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psresportconfig.h"

#include <stdbool.h>
#include <stddef.h>

#include "psresportlog.h"

const ConfDef_t confDef[] =
{
    { "RESERVED_PORTS", false, "string", "12000-13000",
      "The reserved port range used by OpenMPI startup phase" },
    { "DEBUG_MASK", true, "num", "0",
      "The debug mask for logging" },
    { NULL, false, NULL, NULL, NULL },
};

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

Config_t config = NULL;

void initPSResPortConfig(char *cfgName)
{
    initConfig(&config);

    if (parseConfigFile(cfgName, config, false /*trimQuotes*/) < 0) {
	mlog("%s: failed to open '%s'\n", __func__, cfgName);
    }

    traverseConfig(config, verifyVisitor, confDef);

    setConfigDefaults(config, confDef);
}
