/*
 * ParaStation
 *
 * Copyright (C) 2007-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "kvscommon.h"

const char *PSKVScmdToString(PSKVS_cmd_t cmd)
{
    switch(cmd) {
	case PUT:
	    return "PUT";
	case DAISY_SUCC_READY:
	    return "DAISY_SUCC_READY";
	case DAISY_BARRIER_IN:
	    return "DAISY_BARRIER_IN";
	case DAISY_BARRIER_OUT:
	    return "DAISY_BARRIER_OUT";
	case UPDATE_CACHE:
	    return "UPDATE_CACHE";
	case UPDATE_CACHE_FINISH:
	    return "UPDATE_CACHE_FINISH";
	case INIT:
	    return "INIT";
	case SPAWN:
	    return "SPAWN";
	case SPAWN_RESULT:
	    return "SPAWN_RESULT";
	case JOIN:
	    return "JOIN";
	case LEAVE:
	    return "LEAVE";
	case NOT_AVAILABLE:
	    return "NOT_AVAILABLE";
    }
    return NULL;
}

char *getpmivm(char *name, char *vbuffer)
{
    const char delimiters[] =" \n";
    char *cmd, *toksave, *res, *bufcpy, *ret = NULL;
    int nlen;

    if	(!name || !vbuffer) return NULL;

    if (!(bufcpy = strdup(vbuffer))) {
	fprintf(stderr, "%s: out of memory\n", __func__);
	exit(0);
    }
    cmd = strtok_r(bufcpy,delimiters,&toksave);
    nlen = strlen(name);

    while (cmd != NULL) {
    	if (!strncmp(name,cmd,nlen) && cmd[nlen] == '=') {
	    if (!(res = (cmd + nlen + 1))) {
		fprintf(stderr, "%s: invalid value\n", __func__);
	    } else {
		ret = strdup(res);
	    }
	    break;
	}
	cmd = strtok_r(NULL, delimiters, &toksave);
    }

    free(bufcpy);
    return ret;
}

int getpmiv(char *name, char *vbuffer, char *pmivalue, size_t vallen)
{
    const char delimiters[] =" \n";
    char *cmd, *toksave, *res, *bufcpy;
    int nlen, ret=0;

    if	(!name || !vbuffer || !pmivalue  || !vallen) {
	return 0;
    }

    if (!(bufcpy = strdup(vbuffer))) {
	fprintf(stderr, "%s: out of memory\n", __func__);
	exit(0);
    }
    cmd = strtok_r(bufcpy,delimiters,&toksave);
    nlen = strlen(name);
    pmivalue[0]='\0';

    while (cmd != NULL) {
    	if (!strncmp(name,cmd,nlen) && cmd[nlen] == '=') {
	    if (!(res = (cmd + nlen + 1))) {
		fprintf(stderr, "%s: invalid value\n", __func__);
	    } else if (vallen < strlen(res)) {
		fprintf(stderr, "%s: buffer to small\n", __func__);
	    } else {
		strncpy(pmivalue,res,vallen);
		ret = 1;
	    }
	    break;
	}
	cmd = strtok_r( NULL, delimiters, &toksave);
    }
    free(bufcpy);
    return ret;
}

int getKVSCmd(char **ptr)
{
    uint8_t cmd;

    cmd = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);

    return cmd;
}

void setKVSCmd(char **ptr, size_t *len, PSKVS_cmd_t cmd)
{
    *(uint8_t *) *ptr = cmd;
    *ptr += sizeof(uint8_t);
    *len += sizeof(uint8_t);
}

void addKVSInt32(char **ptr, size_t *len, int32_t *num)
{
    *(uint32_t *) *ptr = *num;
    *ptr += sizeof(uint32_t);
    *len += sizeof(uint32_t);
}

int32_t getKVSInt32(char **ptr)
{
    uint32_t num;

    num = *(uint32_t *) *ptr;
    *ptr += sizeof(uint32_t);

    return num;
}

int addKVSString(char **ptr, size_t *bufSize, char *string)
{
    size_t len;

    len = strlen(string);

    /* string length */
    *(int16_t *) *ptr = len;
    *ptr += sizeof(int16_t);
    *bufSize += sizeof(int16_t);

    /* add string itself */
    if (len > 0) {
	memcpy(*ptr, string, len);
	*ptr += len;
	*bufSize += len;
    }
    return len;
}

int getKVSString(char **ptr, char *buf, size_t bufSize)
{
    size_t len;

    /* string length */
    len = *(int16_t *) *ptr;
    *ptr += sizeof(int16_t);

    /* buffer to small */
    if (len +1 > bufSize) {
	return -1;
    }

    /* extract the string */
    if (len > 0) {
	memcpy(buf, *ptr, len);
	buf[len] = '\0';
	*ptr += len;
    } else {
	buf[0] = '\0';
    }

    return len;
}
