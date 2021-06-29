/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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

/** ID of the GRES GPU plugin */
#define GRES_PLUGIN_GPU 7696487

/** ID of the GRES MIC plugin */
#define GRES_PLUGIN_MIC 6515053

typedef enum {
    GRES_CRED_STEP,             /**< GRES step credential */
    GRES_CRED_JOB,              /**< GRES job credential */
} GRes_Cred_type_t;

/** Structure holding a GRes configuration */
typedef struct {
    list_t next;                /**< used to put into some gres-conf-lists */
    char *name;                 /**< name of the GRES resource (e.g. gpu) */
    char *cpus;                 /**< obsolete, replaced by cores */
    char *file;                 /**< filename of the device (e.g. /dev/gpu0) */
    char *type;                 /**< GRES type */
    char *cores;                /**< cores to bind to GRES */
    char *flags;                /**< flags specified for the GRES */
    uint64_t count;             /**< number of GRES resources */
    uint32_t id;                /**< GRES plugin ID */
} Gres_Conf_t;

/** Structure holding a GRes credential */
typedef struct {
    list_t next;                /**< used to put into some gres-cred-lists */
    uint32_t id;                /**< GRES plugin ID */
    uint64_t countAlloc;        /**< GRES per node */
    uint64_t *countStepAlloc;
    char *typeModel;
    uint32_t nodeCount;
    char **bitAlloc;            /**< GRES job bit-string allocation */
    char **bitStepAlloc;        /**< GRES step bit-string allocation */
    char *nodeInUse;            /**< GRES use per node */
    int credType;               /**< credential type (job or step) */
    uint16_t cpusPerGRes;       /**< CPUs per GRES */
    uint16_t flags;             /**< GRES flags */
    uint64_t gresPerJob;        /**< GRES count per job */
    uint64_t gresPerStep;       /**< GRES count per step */
    uint64_t gresPerNode;       /**< GRES count per node */
    uint64_t gresPerSocket;     /**< GRES count per socket */
    uint64_t gresPerTask;       /**< GRES count per task */
    uint64_t memPerGRes;        /**< memory per GRES */
    uint64_t totalGres;         /**< total GRES count */
    uint16_t numTasksPerGres;   /**< number of tasks per GRES */
} Gres_Cred_t;

/** Structure holding a GRes job allocation used in prologue/epilogue */
typedef struct {
    list_t next;                /**< used to put into some gres-alloc-lists */
    uint32_t pluginID;          /**< plugin identifier */
    uint32_t nodeCount;         /**< node count */
    uint64_t *nodeAlloc;        /**< node allocation */
    char **bitAlloc;            /**< bit-string allocation */
} Gres_Job_Alloc_t;

/**
 * @brief Save a GRES configuration
 *
 * @param gres The GRES configuration to save
 *
 * @param count The number of GRES resources as string
 *
 * @return Returns the saved GRES configuration on success or
 * NULL otherwise
 */
Gres_Conf_t *saveGresConf(Gres_Conf_t *gres, char *count);

/**
 * @brief Free all saved GRES configurations
 */
void clearGresConf(void);

/**
 * @brief Allocate and initialize a new GRES credential
 *
 * @return Returns the created GRES credential
 */
Gres_Cred_t *getGresCred(void);

/**
 * @brief Find a GRES credential
 *
 * @param list The GRES list to search
 *
 * @param id The GRES plugin ID or NO_VAL for any ID
 *
 * @param credType The GRES credential type
 * (currently GRES_CRED_STEP|GRES_CRED_JOB)
 *
 * @return Returns the found GRES credential or NULL otherwise
 */
Gres_Cred_t *findGresCred(list_t *gresList, uint32_t id, int credType);

/**
 * @brief Free a GRES credential
 *
 * @param gres The GRES credential to free
 */
void releaseGresCred(Gres_Cred_t *gres);

/**
 * @brief Free GRES credential of a list
 *
 * @param gresList The GRES credential list to free
 */
void freeGresCred(list_t *gresList);

/**
 * @brief Get GRES configuration count
 *
 * @return Returns the number of GRES configurations
 */
int countGresConf(void);

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

/**
 * @brief Free GRES job allocation list
 *
 * @param gresList The GRES job allocation list to free
 */
void freeGresJobAlloc(list_t *gresList);

#endif /* __PS_SLURM_GRES */
