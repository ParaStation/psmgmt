/*
 * ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>
#include <string.h>

#include "logging.h"
#include "plugin.h"
#include "psstrbuf.h"

#include "pspamuser.h"
#include "pspamssh.h"
#include "pspamlog.h"

char *show(char *key)
{
    char l[80];

    if (!key) {
	strbuf_t buf = strbufNew(NULL);
	snprintf(l, sizeof(l),
		 "\nuse 'plugin %s %s key [users|sessions|debug]'\n",
		 name, __func__);
	strbufAdd(buf, l);
	return strbufSteal(buf);
    }

    /* show current users */
    if (!strcmp(key, "users")) return listUsers();

    /* show current sessions */
    if (!strcmp(key, "sessions")) return listSessions();

    /* show current config */
    strbuf_t buf = strbufNew(NULL);
    if (!strcmp(key, "debug")) {
	snprintf(l, sizeof(l), "\t0x%.4x\n", logger_getMask(pspamlogger));
	strbufAdd(buf, l);
    } else {
	snprintf(l, sizeof(l), "\ninvalid key %s (users, sessions, debug)\n", key);
	strbufAdd(buf, l);
    }
    return strbufSteal(buf);
}

char *set(char *key, char *val)
{
    char l[80];
    strbuf_t buf = strbufNew(NULL);

    if (!strcmp(key, "debug")) {
	int debugMask;

	if (sscanf(val, "%i", &debugMask) != 1) {
	    snprintf(l, sizeof(l), "\ndebugmask '%s' not a number\n", val);
	} else {
	    maskLogger(debugMask);
	    snprintf(l, sizeof(l), "\tdebugMask now 0x%.4x\n", debugMask);
	}
	strbufAdd(buf, l);
    } else {
	snprintf(l, sizeof(l), "\ninvalid key %s (debug)\n", key);
	strbufAdd(buf, l);
    }

    return strbufSteal(buf);
}

char *help(char *key)
{
    return strdup("\nuse show [users|sessions|debug] or set debug\n");
}
