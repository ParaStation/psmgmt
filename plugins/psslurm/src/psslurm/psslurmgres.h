/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_GRES
#define __PS_SLURM_GRES

#include <stdbool.h>
#include <stdint.h>

#include "list.h"

/** ID of the GRes GPU plugin */
#define GRES_PLUGIN_GPU 7696487

/** ID of the GRes MIC plugin */
#define GRES_PLUGIN_MIC 6515053

typedef enum {
    GRES_CRED_STEP,             /**< GRes step credential */
    GRES_CRED_JOB,              /**< GRes job credential */
} GRes_Cred_type_t;

/** Structure holding a GRes device */
typedef struct {
    list_t next;                /**< used to put into some GRes device list */
    char *path;			/**< path to device (e.g. /dev/gpu0) */
    unsigned int major;		/**< major device ID */
    unsigned int minor;		/**< minor device ID */
    uint32_t slurmIdx;		/**< Slurm bitmap index of device */
    bool isBlock;		/**< true if device is a block device */
} GRes_Dev_t;

/** Structure holding a GRes configuration */
typedef struct {
    list_t next;                /**< used to put into some GRes config list */
    char *name;                 /**< name of the GRes resource (e.g. gpu) */
    char *cpus;                 /**< obsolete, replaced by cores */
    char *file;                 /**< compressed filename of the device
				     (e.g. /dev/gpu[01-03]) */
    char *type;                 /**< GRes type */
    char *cores;                /**< cores to bind to GRes */
    char *strFlags;             /**< flags specified for the GRes from config */
    char *links;                /**< comma separated list of numbers identifying
				     the number of connections between
				     devices */
    uint32_t flags;             /**< parsed binary flags */
    uint64_t count;             /**< number of GRes resources */
    uint32_t hash;              /**< GRes hash (plugin ID) */
    uint32_t nextDevID;		/**< first device ID in this configuration */
    list_t devices;		/**< list of GRes devices */
} Gres_Conf_t;

/** Structure holding a GRes credential */
typedef struct {
    list_t next;                /**< used to put into some GRes lists */
    uint32_t hash;              /**< GRes hash (plugin ID) */
    uint64_t countAlloc;        /**< GRes per node */
    uint64_t *countStepAlloc;
    char *typeModel;
    uint32_t nodeCount;
    char **bitAlloc;            /**< GRes job bit-string allocation */
    uint64_t **perBitAlloc;	/**< per bit allocation for shared GRes */
    char **bitStepAlloc;        /**< GRes step bit-string allocation */
    uint64_t **stepPerBitAlloc;	/**< step per bit allocation for shared GRes */
    char *nodeInUse;            /**< GRes use per node */
    GRes_Cred_type_t credType;  /**< credential type (job or step) */
    uint16_t cpusPerGRes;       /**< CPUs per GRes */
    uint16_t flags;             /**< GRes flags */
    uint64_t gresPerJob;        /**< GRes count per job */
    uint64_t gresPerStep;       /**< GRes count per step */
    uint64_t gresPerNode;       /**< GRes count per node */
    uint64_t gresPerSocket;     /**< GRes count per socket */
    uint64_t gresPerTask;       /**< GRes count per task */
    uint64_t memPerGRes;        /**< memory per GRes */
    uint64_t totalGres;         /**< total GRes count */
    uint16_t numTasksPerGres;   /**< number of tasks per GRes */
    char *typeName;		/**< type name (since 24.05, unused) */
    uint32_t typeID;		/**< type identifier (since 24.05, unused) */
} Gres_Cred_t;

/** Structure holding a GRes job allocation used in prologue/epilogue */
typedef struct {
    list_t next;                /**< used to put into some GRes allocation list */
    uint32_t pluginID;          /**< plugin identifier */
    uint32_t nodeCount;         /**< node count */
    uint64_t *nodeAlloc;        /**< node allocation */
    char **bitAlloc;            /**< bit-string allocation */
} Gres_Job_Alloc_t;

/**
 * @brief Save a GRes configuration
 *
 * @param gres The GRes configuration to save
 *
 * @param count The number of GRes resources as string
 *
 * @return Returns the saved GRes configuration on success or
 * NULL otherwise
 */
Gres_Conf_t *saveGresConf(Gres_Conf_t *gres, char *count);

/**
 * @brief Get name from GRes hash
 *
 * @attention This function returns the name of the first matching
 * configuration. In the rare case to hash collisions, this might
 * belong to a different configuration.
 *
 * @param hash The GRes hash (plugin id) identifying the configuration
 *
 * @return Returns the requested name on success otherwise "unknown"
 * is returned
 */
const char *GRes_getNamebyHash(uint32_t hash);

/**
 * @brief Free all saved GRes configurations
 */
void clearGresConf(void);

/**
 * @brief Allocate and initialize a new GRes credential
 *
 * @return Returns the created GRes credential
 */
Gres_Cred_t *getGresCred(void);

/**
 * @brief Find a GRes credential
 *
 * @param list The GRes list to search
 *
 * @param id The GRes plugin ID or NO_VAL for any ID
 *
 * @param credType The GRes credential type
 * (currently GRES_CRED_STEP|GRES_CRED_JOB)
 *
 * @return Returns the found GRes credential or NULL otherwise
 */
Gres_Cred_t *findGresCred(list_t *gresList, uint32_t id,
			  GRes_Cred_type_t credType);

/**
 * @brief Free a GRes credential
 *
 * @param gres The GRes credential to free
 */
void releaseGresCred(Gres_Cred_t *gres);

/**
 * @brief Free GRes credential of a list
 *
 * @param gresList The GRes credential list to free
 */
void freeGresCred(list_t *gresList);

/**
 * @brief Get GRes configuration count
 *
 * @return Returns the number of GRes configurations
 */
int countGresConf(void);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseGresConf() in order to visit
 * each gres configuration currently registered.
 *
 * The parameters are as follows: @a gres points to the GRes configuration to
 * visit. @a info points to the additional information passed to @ref
 * traverseGresConf() in order to be forwarded to each GRes config.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref traverseGresConf() will return to its calling
 * function.
 */
typedef bool GresConfVisitor_t(Gres_Conf_t *gres , void *info);

/**
 * @brief Traverse all gres configurations
 *
 * Traverse all GRes configurations by calling @a visitor for each of
 * the GRes configurations. In addition to a pointer to the current
 * GRes configuration, @a info is passed as additional information to
 * @a visitor.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param visitor Visitor function to be called for each GRes configuration
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the GRes configurations
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal
 * false is returned.
 */
bool traverseGresConf(GresConfVisitor_t visitor, void *info);

/**
 * @brief Free GRes job allocation list
 *
 * @param gresList The GRes job allocation list to free
 */
void freeGresJobAlloc(list_t *gresList);

/**
 * @brief Count all devices identified by their hash (plugin ID)
 *
 * @param pluginID The ID of the plugin for the devices to count
 *
 * @return Returns the number of devices found
 */
uint32_t GRes_countDevices(uint32_t hash);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref traverseGResDevs() in order to visit
 * each GRes device associated to a given GRes plugin ID.
 *
 * The parameters are as follows: @a dev points to the GRes device to
 * visit. @a id contains the GRes plugin ID of the devices to
 * consider. @a info points to the additional information passed to
 * @ref traverseGResDevs() in order to be forwarded to each GRes
 * device.
 *
 * If the visitor function returns true, the traversal will stop and
 * @ref traverseGResDevs() will return to its calling function.
 */
typedef bool GResDevVisitor_t(GRes_Dev_t *dev, uint32_t id, void *info);

/**
 * @brief Traverse GRes devices
 *
 * Traverse all GRes devices associated to the GRes plugin ID @a id by
 * calling @a visitor for each of the GRes devices. As parameters @a
 * visitor will get a pointer to the current GRes device, the plugin
 * ID and the pointer to additional information @a info.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * The visitor is not allowed to modify @ref GresConfList.
 *
 * @param hash GRes hash (plugin ID) traversed devices must be associated to
 *
 * @param visitor Visitor function to be called for each GRes device
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the GRes devices
 *
 * @return If the visitor returns true, traversal will be stopped
 * immediately and true is returned; if no visitor returned true
 * during the traversal, false is returned
 */
bool traverseGResDevs(uint32_t hash, GResDevVisitor_t visitor, void *info);

/**
 * @brief Convert GRes credential type to string
 *
 * For certain type the returned pointer leads to a static character array
 * that contains the type. Subsequent calls to @ref GRes_strType() will
 * change the content of this array. Therefore the result might be unexpected
 * if more than one call is made.
 *
 * @param type The numeric type to convert
 *
 * @return Returns the given GRes type as string
 */
const char *GRes_strType(GRes_Cred_type_t type);

/**
 * @brief Calculate hash (plugin ID) for given GRes name
 *
 * Basic hash which might not be collision free.
 *
 * @param name GRes name to calculate hash for
 *
 * @return Returns the requested hash or 0 on error
 */
uint32_t GRes_getHash(char *name);

#endif /* __PS_SLURM_GRES */
