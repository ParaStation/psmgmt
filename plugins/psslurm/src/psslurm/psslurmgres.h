/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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
    uint32_t count;
    uint32_t id;
    struct list_head list;  /* the gres list header */
} Gres_Conf_t;

typedef struct {
    uint32_t id;
    uint32_t countAlloc;
    uint32_t nodeCount;
    char **bitAlloc;
    char **bitStepAlloc;
    uint32_t *countStepAlloc;
    char *nodeInUse;
    int job;
    struct list_head list;  /* the gres list header */
} Gres_Cred_t;

Gres_Conf_t GresConfList;

void initGresConf();
void clearGresConf();
Gres_Conf_t *addGresConf(char *name, char *count, char *file, char *cpus);
void addGresData(PS_DataBuffer_t *msg, int version);
int getGresJobCred(Gres_Cred_t *gresList, char **ptr, uint32_t jobid,
		    uint32_t stepid, uid_t uid);
void clearGresCred(Gres_Cred_t *gresList);
Gres_Cred_t * findGresCred(Gres_Cred_t *gresList, uint32_t id, int job);

#endif
