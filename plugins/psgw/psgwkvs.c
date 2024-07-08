/*
 * ParaStation
 *
 * Copyright (C) 2019-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "psstrbuf.h"

#include "pluginconfig.h"

#include "psgwconfig.h"
#include "psgwlog.h"

static char line[256];

char *set(char *key, char *value)
{
    strbuf_t buf = strbufNew(NULL);

    /* search in config for given key */
    if (getConfigDef(key, confDef)) {
	int ret = verifyConfigEntry(confDef, key, value);
	if (ret) {
	    switch (ret) {
	    case 1:
		strbufAdd(buf, "\nInvalid key '");
		strbufAdd(buf, key);
		strbufAdd(buf, "' for cmd set : use 'plugin help psgw' for help.\n");
		break;
	    case 2:
		strbufAdd(buf, "\nThe key '");
		strbufAdd(buf, key);
		strbufAdd(buf, "' for cmd set has to be numeric.\n");
	    }
	} else if (!strcmp(key, "DEBUG_MASK")) {
	    int32_t mask;

	    if (sscanf(value, "%i", &mask) != 1) {
		strbufAdd(buf, "\nInvalid debug mask: NAN\n");
	    } else {
		maskLogger(mask);
	    }
	} else {
	    /* save new config value */
	    addConfigEntry(config, key, value);

	    snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	    strbufAdd(buf, line);
	}
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd set : use 'plugin help psgw' for help.\n");
    }
    return strbufSteal(buf);
}

char *help(void)
{
    strbuf_t buf = strbufNew("\n# configuration options #\n\n");

    for (int i = 0; confDef[i].name; i++) {
	char type[10];
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%21s\t%8s    %s\n", confDef[i].name,
		type, confDef[i].desc);
	strbufAdd(buf, line);
    }

    snprintf(line, sizeof(line), "%12s\tshow current configuration\n", "config");
    strbufAdd(buf, line);

    return strbufSteal(buf);
}

/**
 * @brief Show current configuration.
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showConfig(void)
{
    strbuf_t buf = strbufNew("\n");

    for (int i = 0; confDef[i].name; i++) {
	char *name = confDef[i].name;
	char *val = getConfValueC(config, name);
	snprintf(line, sizeof(line), "%21s = %s\n", name, val ? val:"<empty>");
	strbufAdd(buf, line);
    }

    return strbufSteal(buf);
}

/**
 * @brief Show all supported virtual keys
 *
 * This function will consume the string buffer @a buf, i.e. it will
 * steal the representing string from @a buf and destroy the string
 * buffer itself.
 *
 * @param buf String buffer to write the information to
 *
 * @return Returns the buffer with the updated virtual key information
 */
static char *showVirtualKeys(strbuf_t buf, bool example)
{
    strbufAdd(buf, "\n# available keys #\n\n");
    strbufAdd(buf, "     config\tshow current configuration\n");

    if (example) strbufAdd(buf, "\nExample:\nUse 'plugin show psgw key config'\n");

    return strbufSteal(buf);
}

char *show(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    if (!key) return showVirtualKeys(buf, true);

    /* search in config for given key */
    char *tmp = getConfValueC(config, key);
    if (tmp) {
	strbufAdd(buf, key);
	strbufAdd(buf, " = ");
	strbufAdd(buf, tmp);
	strbufAdd(buf, "\n");
    } else if (!strcmp(key, "config")) {
	/* show current config */
	return showConfig();
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "'\n");
	return showVirtualKeys(buf, false);
    }

    return strbufSteal(buf);
}

char *unset(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    if (!unsetConfigEntry(config, confDef, key)) {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key);
	strbufAdd(buf, "' for cmd unset : use 'plugin help psgw' for help.\n");
    }

    return strbufSteal(buf);
}
