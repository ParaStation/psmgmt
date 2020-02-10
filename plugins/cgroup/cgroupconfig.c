/*
 * ParaStation
 *
 * Copyright (C) 2016-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginconfig.h"
#include "cgrouplog.h"

#include "cgroupconfig.h"

#ifndef DEFAULT_CGROUP_ROOT
#define DEFAULT_CGROUP_ROOT "/sys/fs/cgroup/"
#endif

#ifndef DEFAULT_CGROUP_NAME
#define DEFAULT_CGROUP_NAME "psmgmtGrp"
#endif

const ConfDef_t confDef[] =
{
    { "CGROUP_ROOT", false, "path", DEFAULT_CGROUP_ROOT,
      "Root directory of all cgroups" },
    { "CGROUP_NAME", false, "string", DEFAULT_CGROUP_NAME,
      "Name of psmgmt's cgroup" },
    { "MEM_LIMIT", true, "num", "-1",
      "Limit of psmgmt's memory cgroup's memory usage" },
    { "MEMSW_LIMIT", true, "num", "-1",
      "Limit of psmgmt's memory cgroup's memory+swap usage" },
    { "DEBUG_MASK", true, "mask", "0",
      "Mask to steer debug output" },
    { NULL, false, NULL, NULL, NULL},
};

Config_t config;

static bool verifyVisitor(char *key, char *value, const void *info)
{
    const ConfDef_t *confDef = info;
    int res = verifyConfigEntry(confDef, key, value);

    switch (res) {
    case 0:
	break;
    case 1:
	cglog(-1, "Unknown config option '%s'\n", key);
	break;
    case 2:
	cglog(-1, "Option '%s' shall be numeric but is '%s'\n", key, value);
	return true;
    default:
	cglog(-1, "unexpected return %d from verifyConfigEntry()\n", res);
    }
    return false;
}

void initCgConfig(char *cfgName)
{
    if (parseConfigFile(cfgName, &config, false /* trimQuotes */) < 0) {
	cglog(-1, "%s: failed to open '%s'\n", __func__, cfgName);
    }

    traverseConfig(&config, verifyVisitor, confDef);

    setConfigDefaults(&config, confDef);
}
