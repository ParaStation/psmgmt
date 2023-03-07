/*
 * ParaStation
 *
 * Copyright (C) 2018-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "jailconfig.h"

#include <stdbool.h>
#include <stddef.h>

#include "jaillog.h"

#ifndef DEFAULT_JAIL_SCRIPT
#define DEFAULT_JAIL_SCRIPT "jail.sh"
#endif

#ifndef DEFAULT_JAIL_TERM_SCRIPT
#define DEFAULT_JAIL_TERM_SCRIPT "jail-term.sh"
#endif

const ConfDef_t confDef[] =
{
    { "JAIL_SCRIPT", false, "path", DEFAULT_JAIL_SCRIPT,
      "(Relative or absolute) path to jailing script" },
    { "JAIL_TERM_SCRIPT", false, "path", DEFAULT_JAIL_TERM_SCRIPT,
      "(Relative or absolute) path to jail terminate script" },
    { "DEBUG_MASK", true, "mask", "0",
      "Mask to steer debug output" },
    { NULL, false, NULL, NULL, NULL},
};

static bool verifyVisitor(char *key, char *value, const void *info)
{
    const ConfDef_t *cDef = info;
    int res = verifyConfigEntry(cDef, key, value);

    switch (res) {
    case 0:
	break;
    case 1:
	jlog(-1, "Unknown config option '%s'\n", key);
	break;
    case 2:
	jlog(-1, "Option '%s' shall be numeric but is '%s'\n", key, value);
	return true;
    default:
	jlog(-1, "unexpected return %d from verifyConfigEntry()\n", res);
    }
    return false;
}

Config_t config = NULL;

void initJailConfig(char *cfgName)
{
    initConfig(&config);
    if (parseConfigFile(cfgName, config, false /* trimQuotes */) < 0) {
	jlog(-1, "%s: failed to open '%s'\n", __func__, cfgName);
    }

    traverseConfig(config, verifyVisitor, confDef);

    setConfigDefaults(config, confDef);
}
