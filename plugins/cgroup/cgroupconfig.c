/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "pluginconfig.h"
#include "cgrouplog.h"

#include "cgroupconfig.h"

const ConfDef_t cgConfDef[] =
{
    { "CGROUP_ROOT", false, "path", "/sys/fs/cgroup/",
      "Root directory of all cgroups" },
    { "CGROUP_NAME", false, "string", "psmgmtGrp",
      "Name of psmgmt's cgroup" },
    { "MEM_LIMIT", true, "num", "-1",
      "Limit of psmgmt's memory cgroup's memory usage" },
    { "MEMSW_LIMIT", true, "num", "-1",
      "Limit of psmgmt's memory cgroup's memory+swap usage" },
    { "DEBUG_MASK", true, "mask", "0",
      "Mask to steer debug output" },
    { NULL, false, NULL, NULL, NULL},
};

LIST_HEAD(cgroupConfig);

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
    if (parseConfigFile(cfgName, &cgroupConfig) < 0) {
	cglog(-1, "%s: failed to open '%s'\n", __func__, cfgName);
    }

    traverseConfig(&cgroupConfig, verifyVisitor, cgConfDef);

    setConfigDefaults(&cgroupConfig, cgConfDef);
}
