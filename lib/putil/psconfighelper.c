/*
 * ParaStation
 *
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psconfighelper.h"

#ifndef BUILD_WITHOUT_PSCONFIG

#include <limits.h>
#include <string.h>
#include <unistd.h>

static char obj[HOST_NAME_MAX + 6];

char * PSCfgHelp_getObject(PSConfig* db, guint flags,
			   logger_t logger, int32_t key)
{
    /* generate local psconfig host object name */
    strncpy(obj, "host:", sizeof(obj));
    gethostname(obj + strlen(obj), sizeof(obj) - strlen(obj));
    obj[sizeof(obj) - 1] = '\0'; //assure object is null terminated

    // check if the host object exists or we have to cut the hostname
    char *nodename = psconfig_get(db, obj, "NodeName", flags, NULL);
    if (!nodename) {
	/* cut hostname and try again */
	char *pos = strchr(obj, '.');
	if (pos) {
	    *pos = '\0';
	    logger_print(logger, key, "%s: cut hostname to '%s'\n", __func__, obj);
	    nodename = psconfig_get(db, obj, "NodeName", flags, NULL);
	}
    }
    if (!nodename) {
	logger_print(logger, key, "%s: no object '%s' with key 'NodeName'\n",
		     __func__, obj);
	return NULL;
    }

    g_free(nodename);
    return obj;
}

#endif /* BUILD_WITHOUT_PSCONFIG */
