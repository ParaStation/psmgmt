/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "pmikvs.h"

#include <string.h>

#include "plugin.h"

#include "pluginlog.h"
#include "pluginmalloc.h"

#include "pmilog.h"

FILE *memoryDebug = NULL;

char *set(char *key, char *value)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (key && !strcmp(key, "memdebug")) {
	if (memoryDebug) fclose(memoryDebug);

	memoryDebug = fopen(value, "w+");
	if (memoryDebug) {
	    finalizePluginLogger();
	    initPluginLogger(NULL, memoryDebug);
	    maskPluginLogger(PLUGIN_LOG_MALLOC);
	    str2Buf("\nmemory logging to '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    str2Buf("'\n", &buf, &bufSize);
	} else {
	    str2Buf("\nopening file '", &buf, &bufSize);
	    str2Buf(value, &buf, &bufSize);
	    str2Buf("' for writing failed\n", &buf, &bufSize);
	}
    } else {
	str2Buf("\nInvalid key '", &buf, &bufSize);
	str2Buf(key ? key : "<empty>", &buf, &bufSize);
	str2Buf("' for cmd set : use 'plugin help pspmi' for help.\n", &buf,
		&bufSize);
    }
    return buf;
}

char *show(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    str2Buf("\nInvalid key '", &buf, &bufSize);
    str2Buf(key ? key : "<empty>", &buf, &bufSize);
    str2Buf("' for cmd show : use 'plugin help pspmi'.\n", &buf, &bufSize);

    return buf;
}

char *unset(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    if (key && !strcmp(key, "memdebug")) {
	if (memoryDebug) {
	    finalizePluginLogger();
	    fclose(memoryDebug);
	    memoryDebug = NULL;
	    initPluginLogger(NULL, pmilogfile);
	}
	str2Buf("Stopped memory debugging\n", &buf, &bufSize);
    } else {
	str2Buf("\nInvalid key '", &buf, &bufSize);
	str2Buf(key ? key : "<empty>", &buf, &bufSize);
	str2Buf("' for cmd unset : use 'plugin help pspmi' for help.\n", &buf,
		&bufSize);
    }

    return buf;
}

char *help(char *key)
{
    char *buf = NULL;
    size_t bufSize = 0;

    str2Buf("\nThe pspmi plugin provides the PMI layer which is needed "
	    "for the startup mechanism of MPI2.\n", &buf, &bufSize);

    return buf;
}
