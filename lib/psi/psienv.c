/*
 *               ParaStation3
 * psienv.c
 *
 * ParaStation Environment
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psienv.c,v 1.5 2002/04/26 12:39:22 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psienv.c,v 1.5 2002/04/26 12:39:22 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "psienv.h"

static char** environment  = NULL;
static int sizeOfEnv = 0;


void clearPSIEnv(void)
{
    int i;

    for (i=0; i<sizeOfEnv; i++) {
	if (environment[i]) {
	    free(environment[i]);
	}
    }

    if (environment) free(environment);

    environment = NULL;
    sizeOfEnv = 0;
}

int setPSIEnv(const char *name, const char *value, int overwrite)
{
    int ret;
    char *envstr;

    if (getPSIEnv(name)) {
	if (!overwrite) {
	    return 0;
	} else {
	    unsetPSIEnv(name);
	}
    }

    envstr = (char *) malloc(strlen(name)+strlen(value)+2);
    if (!envstr) {
	return -1;
    }
    sprintf(envstr, "%s=%s", name, value);

    ret = putPSIEnv(envstr);

    free(envstr);

    return ret;
}

void unsetPSIEnv(const char *name)
{
    int i, len;

    len = strlen(name);

    for (i=0; i<sizeOfEnv; i++) {
	if ((environment[i])
	    && !strncmp(environment[i], name, len)
	    && environment[i][len]=='=') {
	    /* the environment strings are the same, including the "=" */
	    break;
	}
    }

    if (i<sizeOfEnv) {
	/* the name is found => delete it */
	free(environment[i]);
	environment[i] = NULL;
    }
}

int putPSIEnv(const char *string)
{
    char* beg;
    int len;
    int i;

    /* 
     * search for the name in string 
     */
    beg = strchr(string,'=');
    if (beg==NULL) {
	return -1;
    }

    len = ((long)beg) - ((long)string);

    for (i=0; i<sizeOfEnv; i++) {
	if ((environment[i]) && (strncmp(environment[i],string,len+1)==0)) {
	    /* the environment strings are the same, including the "=" */
	    break;
	}
    }

    if (i<sizeOfEnv) {
	/* the name is found => replace it */
	free(environment[i]);
    } else {
	/* Look for a free place */
	for (i=0; i<sizeOfEnv && environment[i]; i++);
	if (i==sizeOfEnv) {
	    /* no free place found => extend the environment */
	    int j;
	    char** new_environ;

	    new_environ = realloc(environment, sizeof(char*)*(sizeOfEnv+5));

	    if (! new_environ) {
		errno = ENOMEM;
		return -1;
	    }

	    environment = new_environ;

	    for (j=sizeOfEnv+1; j<sizeOfEnv+5; j++) {
		environment[j]= NULL;
	    }
	    sizeOfEnv += 5;

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
    int len;
    int i;

    /* 
     * search for the name in string 
     */
    if (!name) return NULL;
    len = strlen(name);

    for (i=0; i<sizeOfEnv; i++) {
	if ((environment[i]) && (strncmp(environment[i],name,len)==0)) {
	    break;
	}
    }

    if (i<sizeOfEnv) {
	return &(environment[i])[len+1];
    } else {
	return NULL;
    }
}

int packPSIEnv(char *buffer, size_t size)
{
    int i;
    size_t msglen = 0;

    if (! buffer) return -1; /* We need a buffer */
    if (! sizeOfEnv) return 0; /* No environment to pack */

    for (i=0; i<sizeOfEnv; i++) {
	if (environment[i]) {
	    if ( (msglen + strlen(environment[i])) < size) {
		strcpy(&buffer[msglen],environment[i]);
		msglen += strlen(environment[i]) + 1;
	    } else {
		return -1;  /* buffer to small */
	    }
	}
    }

    if (msglen < size) {
	buffer[++msglen] = 0;
    } else {
	return -1;  /* buf to small */
    }

    return msglen;
}

int numPSIEnv(void)
{
    int i, count = 0;

    for (i=0; i<sizeOfEnv; i++) {
	if (environment[i]) {
	    count++;
	}
    }

    return count;
}

char ** dumpPSIEnv(void)
{
    int i, j=0, count;
    char **env_copy;

    count = numPSIEnv();

    env_copy = (char **) malloc((count+1)*sizeof(char*));

    for (i=0; i<sizeOfEnv; i++) {
	if (environment[i]) {
	    env_copy[j] = strdup(environment[i]);
	    if (!env_copy[j]) {
		return NULL;
	    }
	    j++;
	}
    }
    if (j>count) return NULL;

    env_copy[j] = NULL;

    return env_copy;
}
