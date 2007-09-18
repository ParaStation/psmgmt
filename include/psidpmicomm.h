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
 * pspmi.h: ParaStation pspmi commen defines
 *
 * $Id$ 
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */


#ifndef __PSIDKVSCOMM
#define __PSIDKVSCOMM

#define PMI_SUCCESS 0
#define PMI_ERROR -1

/** max size of a pmi command */ 
#define PMIU_MAXLINE 1024

/** max size of the kvs name */
#define KVSNAME_MAX 256 

/** max size of a key */
#define KEYLEN_MAX  32 

/** max size of a value */
#define VALLEN_MAX 1024 

/** max number of kvs */
#define MAX_KVS	32


/** 
 * @brief Extract a single value from a pmi msg.
 *
 * @param name Name of the value to extract.
 *
 * @param vbuffer Buffer with the msg to extract from.
 *
 * @param pmivalue The buffer which receives the extracted value.
 *
 * @param vallen The size of the buffer which receives the extracted
 * value.
 *
 * @return On Success 1 is returned, 0 on error. 
 */
int getpmiv(char *name, char *vbuffer, char *pmivalue, int vallen);

#endif
