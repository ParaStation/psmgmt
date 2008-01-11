/*
 *               ParaStation
 *
 * Copyright (C) 2007-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * kvscommon.c: ParaStation key value space common functions
 *
 * $Id$ 
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "kvscommon.h"


/** 
 * @brief Extract a single value from a pmi msg.
 *
 * @param name Name of the value to extract.
 *
 * @param vbuffer Buffer with the msg to extract from.
 *
 * @param pmivalue The buffer which receives the extracted
 * value.
 *
 * @param vallen The size of the buffer which receives the 
 * extracted value.
 *
 * @return On Success 0 is returned, 1 on error. 
 */
int getpmiv(char *name, char *vbuffer, char *pmivalue, size_t vallen)
{
    char *cmd, *toksave, *res, *bufcpy;
    int nlen, ret=1;

    if	(!name || !vbuffer || !pmivalue  || !vallen) {
	return 0;
    }
    
    const char delimiters[] =" \n";
    if (!(bufcpy = strdup(vbuffer))) {
	fprintf(stderr, "%s: out of memory\n", __func__);
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
		ret = 0;
	    }
	    break;
	}
	cmd = strtok_r( NULL, delimiters, &toksave);
    } 
    free(bufcpy);
    return ret;
}
