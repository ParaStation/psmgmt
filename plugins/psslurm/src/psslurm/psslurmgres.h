/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_GRES
#define __PS_SLURM_GRES

#include <stdint.h>

#include "list.h"
#include "psserial.h"
#include "slurmcommon.h"

#define GRES_PLUGIN_GPU	7696487
#define GRES_PLUGIN_MIC	6515053

typedef struct {
    list_t next;                /**< used to put into some gres-conf-lists */
    char *name;
    char *cpus;
    char *file;
    char *type;
    uint64_t count;
    uint32_t id;
} Gres_Conf_t;

typedef struct {
    list_t next;                /**< used to put into some gres-cred-lists */
    uint32_t id;
    uint64_t countAlloc;
    uint64_t *countStepAlloc;
    char *typeModel;
    uint32_t nodeCount;
    char **bitAlloc;
    char **bitStepAlloc;
    char *nodeInUse;
    int job;
} Gres_Cred_t;

/**
 * @doctodo
 */
Gres_Conf_t *addGresConf(char *name, char *count, char *file, char *cpus);

/**
 * @doctodo
 */
void addGresData(PS_SendDB_t *msg, int version);

/**
 * @doctodo
 */
void clearGresConf(void);

/**
 * @doctodo
 */
Gres_Cred_t * getGresCred(void);

/**
 * @doctodo
 */
Gres_Cred_t * findGresCred(list_t *gresList, uint32_t id, int job);

/**
 * @doctodo
 */
void releaseGresCred(Gres_Cred_t *gres);

/**
 * @doctodo
 */
void freeGresCred(list_t *gresList);

#endif /* __PS_SLURM_GRES */
