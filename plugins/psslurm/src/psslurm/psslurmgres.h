/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_SLURM_GRES
#define __PS_SLURM_GRES

#include "list.h"
#include "plugincomm.h"

#define GRES_PLUGIN_GPU	7696487
#define GRES_PLUGIN_MIC	6515053

typedef struct {
    char *name;
    char *cpus;
    char *file;
#ifdef SLURM_PROTOCOL_1605
    char *type;
    uint64_t count;
#else
    uint32_t count;
#endif
    uint32_t id;
    struct list_head list;  /* the gres list header */
} Gres_Conf_t;

typedef struct {
    uint32_t id;
#ifdef SLURM_PROTOCOL_1605
    uint64_t countAlloc;
    uint64_t *countStepAlloc;
    char *typeModel;
#else
    uint32_t countAlloc;
    uint32_t *countStepAlloc;
#endif
    uint32_t nodeCount;
    char **bitAlloc;
    char **bitStepAlloc;
    char *nodeInUse;
    int job;
    struct list_head list;  /* the gres list header */
} Gres_Cred_t;

Gres_Conf_t GresConfList;

void initGresConf(void);
void clearGresConf(void);
Gres_Conf_t *addGresConf(char *name, char *count, char *file, char *cpus);
void addGresData(PS_DataBuffer_t *msg, int version);
int getGresJobCred(Gres_Cred_t *gresList, char **ptr, uint32_t jobid,
		    uint32_t stepid, uid_t uid);
void clearGresCred(Gres_Cred_t *gresList);
Gres_Cred_t * findGresCred(Gres_Cred_t *gresList, uint32_t id, int job);

#endif
