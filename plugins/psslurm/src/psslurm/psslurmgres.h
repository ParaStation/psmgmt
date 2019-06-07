/*
 * ParaStation
 *
 * Copyright (C) 2014-2019 ParTec Cluster Competence Center GmbH, Munich
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

/**
 * @brief Get Gres configuration count
 *
 * @return Returns the number of Gres configurations
 */
int countGresConf();

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseGresConf() in order to visit
 * each gres configuration currently registered.
 *
 * The parameters are as follows: @a gres points to the gres config to
 * visit. @a info points to the additional information passed to @ref
 * traverseGresConf() in order to be forwarded to each gres config.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref traverseGresConf() will return to its calling
 * function.
 */
typedef bool GresConfVisitor_t(Gres_Conf_t *gres , void *info);

/**
 * @brief Traverse all gres configurations
 *
 * Traverse all gres configurations by calling @a visitor for each of the
 * gres configurations. In addition to a pointer to the current gres config
 * @a info is passed as additional information to @a visitor.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param visitor Visitor function to be called for each gres config
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the gres configurations
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal
 * false is returned.
 */
bool traverseGresConf(GresConfVisitor_t visitor, void *info);

#endif /* __PS_SLURM_GRES */
