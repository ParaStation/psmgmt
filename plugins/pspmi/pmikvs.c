/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pmikvs.h"

#include <string.h>

#include "psstrbuf.h"

#include "plugin.h"
#include "pluginlog.h"

#include "pmilog.h"

FILE *memoryDebug = NULL;

char *set(char *key, char *value)
{
    strbuf_t buf = strbufNew(NULL);

    if (key && !strcmp(key, "memdebug")) {
	if (memoryDebug) fclose(memoryDebug);

	memoryDebug = fopen(value, "w+");
	if (memoryDebug) {
	    finalizePluginLogger();
	    initPluginLogger(NULL, memoryDebug);
	    maskPluginLogger(PLUGIN_LOG_MALLOC);
	    strbufAdd(buf, "\nmemory logging to '");
	    strbufAdd(buf, value);
	    strbufAdd(buf, "'\n");
	} else {
	    strbufAdd(buf, "\nopening file '");
	    strbufAdd(buf, value);
	    strbufAdd(buf, "' for writing failed\n");
	}
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key ? key : "<empty>");
	strbufAdd(buf, "' for cmd set : use 'plugin help pspmi' for help.\n");
    }
    return strbufSteal(buf);
}

char *show(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    strbufAdd(buf, "\nInvalid key '");
    strbufAdd(buf, key ? key : "<empty>");
    strbufAdd(buf, "' for cmd show : use 'plugin help pspmi'.\n");

    return strbufSteal(buf);
}

char *unset(char *key)
{
    strbuf_t buf = strbufNew(NULL);

    if (key && !strcmp(key, "memdebug")) {
	if (memoryDebug) {
	    finalizePluginLogger();
	    fclose(memoryDebug);
	    memoryDebug = NULL;
	    initPluginLogger(NULL, pmilogfile);
	}
	strbufAdd(buf, "memory debugging stopped\n");
    } else {
	strbufAdd(buf, "\nInvalid key '");
	strbufAdd(buf, key ? key : "<empty>");
	strbufAdd(buf, "' for cmd unset : use 'plugin help pspmi' for help.\n");
    }

    return strbufSteal(buf);
}

char *help(char *key)
{
    return strdup("\nThe pspmi plugin provides the PMI layer which is needed "
		  "for the startup mechanism of MPI2.\n");
}
