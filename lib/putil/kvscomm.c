/*
 *               ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * kvscomm.c: ParaStation key value space common functions
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

#include "psidpmicomm.h"


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
 * @return On Success 1 is returned, 0 on error. 
 */
int getpmiv(char *name, char *vbuffer, char *pmivalue, int vallen)
{
    char *cmd, *toksave, *res, *bcopy;
    int nlen;

    if	(!name || !vbuffer) {
	return 0;
    }
    
    const char delimiters[] =" \n";
    bcopy = strdup(vbuffer);
    cmd = strtok_r(bcopy,delimiters,&toksave);
    nlen = strlen(name);

    while (cmd != NULL) {
	if (!strncmp(name,cmd,nlen) && cmd[nlen] == '=') {
	   res = (cmd + nlen + 1); 
	   strncpy(pmivalue,res,vallen);
	   free(bcopy);
	   return 0;
	}
	cmd = strtok_r( NULL, delimiters, &toksave);
    } 
    free(bcopy);
    return 1;
}
