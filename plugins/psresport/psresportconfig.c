/*
 * ParaStation
 *
 * Copyright (C) 2012-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginmalloc.h"
#include "pluginconfig.h"
#include "psresportlog.h"

#include "psresportconfig.h"

const ConfDef_t psresportConfDef[] =
{
    { "RESERVED_PORTS", false, "string", "12000-13000",
      "The reserved port range used by OpenMPI startup phase" },
    { "DEBUG_MASK", true, "num", "0",
      "The debug mask for logging" },
    { NULL, false, NULL, NULL, NULL },
};

LIST_HEAD(psresportConfig);

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

void initConfig(char *cfgName)
{
    if (parseConfigFile(cfgName, &psresportConfig) < 0) {
	mlog("%s: failed to open '%s'\n", __func__, cfgName);
    }

    traverseConfig(&psresportConfig, verifyVisitor, psresportConfDef);

    setConfigDefaults(&psresportConfig, psresportConfDef);
}
