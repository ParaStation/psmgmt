/*
 * ParaStation
 *
 * Copyright (C) 2001-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psienv.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

static char** environment = NULL;
static int sizeOfEnv = 0;

void clearPSIEnv(void)
{
    for (int i = 0; i < sizeOfEnv; i++) free(environment[i]);
    free(environment);
    environment = NULL;
    sizeOfEnv = 0;
}

int setPSIEnv(const char *name, const char *value, int overwrite)
{
    if (!name || !value) return -1;

    if (getPSIEnv(name)) {
	if (!overwrite) return 0;

	unsetPSIEnv(name);
    }

    char *envStr = malloc(strlen(name) + strlen(value) + 2);
    if (!envStr) return -1;
    sprintf(envStr, "%s=%s", name, value);

    int ret = putPSIEnv(envStr);

    free(envStr);

    return ret;
}

void unsetPSIEnv(const char *name)
{
    size_t len = strlen(name);

    int i;
    for (i = 0; i < sizeOfEnv; i++) {
	if (environment[i] && !strncmp(environment[i], name, len)
	    && environment[i][len] == '=') {
	    /* the environment names are the same, including the length */
	    break;
	}
    }

    if (i < sizeOfEnv) {
	/* the name is found => delete it */
	free(environment[i]);
	environment[i] = NULL;
    }
}

#define ENVCHUNK 5

int putPSIEnv(const char *string)
{
    /* search for the name in string */
    char *beg = strchr(string,'=');
    if (!beg) return -1;

    size_t len = ((size_t)beg) - ((size_t)string);

    int i;
    for (i = 0; i < sizeOfEnv; i++) {
	if (environment[i] && !strncmp(environment[i], string, len)
	    && environment[i][len] == '=') {
	    /* the environment names are the same, including the length */
	    break;
	}
    }

    if (i < sizeOfEnv) {
	/* the name is found => replace it */
	free(environment[i]);
    } else {
	/* Look for a free place */
	for (i = 0; i < sizeOfEnv && environment[i]; i++);
	if (i == sizeOfEnv) {
	    /* no free place found => extend the environment */
	    char** new_environ = realloc(environment,
					 sizeof(char*)*(sizeOfEnv + ENVCHUNK));

	    if (!new_environ) {
		errno = ENOMEM;
		return -1;
	    }

	    environment = new_environ;

	    for (int j = sizeOfEnv + 1; j < sizeOfEnv + ENVCHUNK; j++) {
		environment[j] = NULL;
	    }
	    sizeOfEnv += ENVCHUNK;
	}
    }

    environment[i] = strdup(string);

    if (!environment[i]) {
	errno = ENOMEM;
	return -1;
    }

    return 0;
}

char* getPSIEnv(const char* name)
{
    /* search for the name in string */
    if (!name) return NULL;
    size_t len = strlen(name);

    int i;
    for (i = 0; i < sizeOfEnv; i++) {
	if (environment[i] && !strncmp(environment[i], name, len)
	    && environment[i][len] == '=') {
	    /* the environment names are the same, including the length */
	    break;
	}
    }

    if (i < sizeOfEnv) return &(environment[i])[len + 1];

    return NULL;
}

int packPSIEnv(char *buffer, size_t size)
{
    size_t msglen = 0;

    if (! buffer) return -1; /* We need a buffer */
    if (! sizeOfEnv) return 0; /* No environment to pack */

    for (int i = 0; i < sizeOfEnv; i++) {
	if (environment[i]) {
	    if ((msglen + strlen(environment[i])) < size) {
		strcpy(&buffer[msglen], environment[i]);
		msglen += strlen(environment[i]) + 1;
	    } else {
		return -1;  /* buffer too small */
	    }
	}
    }

    if (msglen >= size) return -1;  /* buf too small */

    buffer[++msglen] = 0;
    return msglen;
}

int numPSIEnv(void)
{
    int count = 0;
    for (int i = 0; i < sizeOfEnv; i++) if (environment[i]) count++;

    return count;
}

char ** dumpPSIEnv(void)
{
    int count = numPSIEnv();

    char **env_copy = malloc((count + 1) * sizeof(char*));

    int j = 0;
    for (int i = 0; i < sizeOfEnv && j <= count; i++) {
	if (environment[i]) {
	    env_copy[j] = strdup(environment[i]);
	    if (!env_copy[j]) {
		for (int k = 0; k < j; k++) free(env_copy[k]);
		free(env_copy);

		return NULL;
	    }
	    j++;
	}
    }

    if (j > count) {
	/* Cleanup */
	for (int i = 0; i < count + 1; i++) free(env_copy[i]);
	free(env_copy);

	return NULL;
    }

    env_copy[j] = NULL;
    return env_copy;
}
