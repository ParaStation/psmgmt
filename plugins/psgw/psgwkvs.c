/*
 * ParaStation
 *
 * Copyright (C) 2019-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <string.h>

#include "pluginmalloc.h"

#include "psgwconfig.h"
#include "psgwlog.h"

static char line[256];

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;

    /* search in config for given key */
    if (getConfigDef(key, confDef)) {
	int ret = verifyConfigEntry(confDef, key, value);
	if (ret) {
	    switch (ret) {
	    case 1:
		str2Buf("\nInvalid key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set : use 'plugin help psgw' for help.\n",
			&buf, &bufSize);
		break;
	    case 2:
		str2Buf("\nThe key '", &buf, &bufSize);
		str2Buf(key, &buf, &bufSize);
		str2Buf("' for cmd set has to be numeric.\n", &buf, &bufSize);
	    }
	    return buf;
	}

	if (!strcmp(key, "DEBUG_MASK")) {
	    int32_t mask;

	    if (sscanf(value, "%i", &mask) != 1) {
		return str2Buf("\nInvalid debug mask: NAN\n", &buf, &bufSize);
	    }
	    maskLogger(mask);
	}

	/* save new config value */
	addConfigEntry(&config, key, value);

	snprintf(line, sizeof(line), "\nsaved '%s = %s'\n", key, value);
	return str2Buf(line, &buf, &bufSize);
    }

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    return str2Buf("' for cmd set : use 'plugin help psgw' for help.\n",
		   &buf, &bufSize);
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int i = 0;
    char type[10];

    str2Buf("\n# configuration options #\n\n", &buf, &bufSize);

    while (confDef[i].name != NULL) {
	snprintf(type, sizeof(type), "<%s>", confDef[i].type);
	snprintf(line, sizeof(line), "%21s\t%8s    %s\n", confDef[i].name,
		type, confDef[i].desc);
	str2Buf(line, &buf, &bufSize);
	i++;
    }

    snprintf(line, sizeof(line), "%12s\t%s\n", "config",
	    "show current configuration");
    str2Buf(line, &buf, &bufSize);

    return buf;
}

/**
 * @brief Show current configuration.
 *
 * @return Returns the buffer with the updated configuration information.
 */
static char *showConfig(void)
{
    char *buf = NULL;
    size_t bufSize = 0;
    int i = 0;

    str2Buf("\n", &buf, &bufSize);

    while (confDef[i].name != NULL) {
	char *name = confDef[i].name;
	char *val = getConfValueC(&config, name);
	snprintf(line, sizeof(line), "%21s = %s\n", name, val ? val:"<empty>");
	str2Buf(line, &buf, &bufSize);
	i++;
    }

    return buf;
}

/**
 * @brief Show all supported virtual keys.
 *
 * @param buf The buffer to write the information to.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Returns the buffer with the updated forwarder information.
 */
static char *showVirtualKeys(char *buf, size_t *bufSize, bool example)
{
    str2Buf("\n# available keys #\n\n", &buf, bufSize);
    str2Buf("     config\tshow current configuration\n", &buf, bufSize);

    if (example) str2Buf("\nExample:\nUse 'plugin show psgw key config'\n",
			 &buf, bufSize);
    return buf;
}

char *show(char *key)
{
    char *buf = NULL, *tmp;
    size_t bufSize = 0;

    if (!key) return showVirtualKeys(buf, &bufSize, true);

    /* search in config for given key */
    tmp = getConfValueC(&config, key);
    if (tmp) {
	str2Buf(key, &buf, &bufSize);
	str2Buf(" = ", &buf, &bufSize);
	str2Buf(tmp, &buf, &bufSize);
	str2Buf("\n", &buf, &bufSize);

	return buf;
    }

    /* show current config */
    if (!strcmp(key, "config")) return showConfig();

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("'\n", &buf, &bufSize);
    return showVirtualKeys(buf, &bufSize, false);
}

char *unset(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (unsetConfigEntry(&config, confDef, key)) return buf;

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key, &buf, &bufSize);
    str2Buf("' for cmd unset : use 'plugin help psgw' for help.\n",
	    &buf, &bufSize);

    return buf;
}
