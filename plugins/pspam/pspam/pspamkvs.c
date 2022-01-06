/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <string.h>

#include "logging.h"
#include "plugin.h"
#include "pluginmalloc.h"

#include "pspamuser.h"
#include "pspamssh.h"
#include "pspamlog.h"

char *show(char *key)
{
    char *buf = NULL, l[80];
    size_t bufSize = 0;

    if (!key) {
	snprintf(l, sizeof(l),
		 "\nuse 'plugin %s %s key [users|sessions|debug]'\n",
		 name, __func__);
	return str2Buf(l, &buf, &bufSize);
    }

    /* show current users */
    if (!strcmp(key, "users")) {
	return listUsers(buf, &bufSize);
    }

    /* show current sessions */
    if (!strcmp(key, "sessions")) {
	return listSessions(buf, &bufSize);
    }

    /* show current config */
    if (!strcmp(key, "debug")) {
	snprintf(l, sizeof(l), "\t0x%.4x\n", logger_getMask(pspamlogger));
	return str2Buf(l, &buf, &bufSize);
    }
    snprintf(l, sizeof(l), "\ninvalid key %s (users, sessions, debug)\n", key);
    return str2Buf(l, &buf, &bufSize);
}

char *set(char *key, char *val)
{
    char *buf = NULL, l[80];
    size_t bufSize = 0;

    if (!strcmp(key, "debug")) {
	int debugMask;

	if ((sscanf(val, "%i", &debugMask)) != 1) {
	    snprintf(l, sizeof(l), "\ndebugmask '%s' not a number\n", val);
	} else {
	    maskLogger(debugMask);
	    snprintf(l, sizeof(l), "\tdebugMask now 0x%.4x\n", debugMask);
	}
	str2Buf(l, &buf, &bufSize);
    } else {
	snprintf(l, sizeof(l), "\ninvalid key %s (debug)\n", key);
	str2Buf(l, &buf, &bufSize);
    }

    return buf;
}

char *help(void)
{
    char *buf = NULL;
    size_t bufSize = 0;

    str2Buf("\nuse show [users|sessions|debug] or set debug\n", &buf, &bufSize);

    return buf;
}
