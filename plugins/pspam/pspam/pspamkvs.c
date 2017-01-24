/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <string.h>

#include "pluginmalloc.h"
#include "plugin.h"

#include "pspamuser.h"
#include "pspamssh.h"
#include "pspamlog.h"

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (!key) {
	str2Buf("use key [users|sessions|debug]\n", &buf, &bufSize);
	return buf;
    }

    /* show current users */
    if (!strcmp(key, "users")) {
	return listUsers(buf, &bufSize);
    }

    /* show current sessions */
    if (!strcmp(key, "dclients")) {
	return listSessions(buf, &bufSize);
    }

    /* show current config */
    if (!strcmp(key, "debug")) {
	char l[80];
	snprintf(l, sizeof(l), "\t%.16x\n", logger_getMask(pspamlogger));
	return str2Buf(l, &buf, &bufSize);
    }

    str2Buf("invalid key, use [users|sessions|debug]\n", &buf, &bufSize);
    return buf;
}

char *set(char *key, char *val)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (!strcmp(key, "debug")) {
	char l[80];
	int debugMask;

	if ((sscanf(val, "%i", &debugMask)) != 1) {
	    snprintf(l, sizeof(l), "\tdebugmask %s not a number\n", val);
	} else {
	    maskLogger(debugMask);
	    snprintf(l, sizeof(l), "\tdebugMask now %x\n", debugMask);
	}
	str2Buf(l, &buf, &bufSize);
    } else {
	str2Buf("\nInvalid key '", &buf, &bufSize);
	str2Buf(key, &buf, &bufSize);
	str2Buf("' for cmd set : use 'plugin help pspam' for help.\n",
		&buf, &bufSize);
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
