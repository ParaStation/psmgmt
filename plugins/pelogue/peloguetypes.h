/*
 * ParaStation
 *
 * Copyright (C) 2015-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE__TYPES
#define __PELOGUE__TYPES

#include <stdbool.h>
#include <time.h>

#include "list.h"
#include "psnodes.h"
#include "psenv.h"

#include "pluginconfig.h"
#include "pluginforwarder.h"

/** Types of pelogues currently handled */
typedef enum {
    PELOGUE_PROLOGUE = 1,  /**< prologue */
    PELOGUE_EPILOGUE,      /**< epilogue */
} PElogueType_t;

/** All information available on a running pelogue */
typedef struct {
    list_t next;        /**< used to put into list */
    char *plugin;       /**< name of the registering plugin */
    char *jobid;        /**< batch system's job ID */
    PElogueType_t type; /**< Type of pelogue to run */
    PSnodes_ID_t mainPElogue; /**< Node initiating the pelogue */
    char *scriptDir;    /**< directory containing all the scripts */
    char *tmpDir;       /**< Name of the pelogue's temp directory if any */
    char *rootHome;     /**< root's HOME; don't free() this!*/
    char *hostName;     /**< local hostname; don't free() this! */
    int32_t timeout;    /**< pelogue's timeout told to the forwarder */
    int32_t rounds;     /**< number of rounds for the pelogue (root / user) */
    env_t env;          /**< environment provided to the pelogue */
    uid_t uid;          /**< user ID under which a pelogue is executed */
    gid_t gid;          /**< group ID under which a pelogue is executed */
    time_t startTime;   /**< Job's pelogue starttime identifies answers */
    Forwarder_Data_t *fwData; /**< description of the forwarder to use */
    char **argv;        /**< argument vector of the pelogue actually started */
    int32_t signalFlag; /**< flag if any signal was sent to the pelogue */
    int32_t exit;       /**< pelogue's exit value */
    bool fwStdOE;	/**< flag to forward stdout/stderr of pelogue script */
} PElogueChild_t;

/** Various states a pelogue might be in */
typedef enum {
    PELOGUE_PENDING = 1,  /**< pelogue is started, no result yet */
    PELOGUE_DONE,         /**< pelogue finished succesfully */
    PELOGUE_FAILED,       /**< pelogue finished but failed */
    PELOGUE_TIMEDOUT,     /**< pelogue did not finish in time */
    PELOGUE_NODEDOWN	  /**< node died while running pelogue */
} PElogueState_t;

/** Collection of pelogue results associated to a job */
typedef struct {
    PSnodes_ID_t id;         /**< node this pelogues were running on */
    PElogueState_t prologue; /**< result of the corresponding prologue */
    PElogueState_t epilogue; /**< result of the corresponding epilogue */
} PElogueResList_t;

/** Various message types used in between pelogue plugins */
typedef enum {
    PSP_PROLOGUE_START,	    /**< prologue script start */
    PSP_PROLOGUE_FINISH,    /**< result from prologue */
    PSP_EPILOGUE_START,	    /**< epilogue script start */
    PSP_EPILOGUE_FINISH,    /**< result from epilogue script */
    PSP_PELOGUE_SIGNAL,	    /**< send a signal to a PElogue script */
    PSP_PELOGUE_REQ,	    /**< remote pelogue request */
    PSP_PELOGUE_RESP,	    /**< remote pelogue response */
    PSP_PLUGIN_CONFIG_ADD,  /**< add plugin configuration */
    PSP_PLUGIN_CONFIG_DEL,  /**< delete plugin configuration */
    PSP_PELOGUE_DROP,	    /**< pelogue response got dropped */
} PSP_PELOGUE_t;

typedef void(PElogueResourceCb_t)(char *plugin, char *jobid, uint16_t res);

/** Information needed to request additional resources  */
typedef struct {
    char *plugin;	    /**< name of the registering plugin */
    char *jobid;	    /**< batch system's job ID */
    env_t *env;		    /**< environment provided to the pelogue */
    uid_t uid;		    /**< user ID of the job owner */
    gid_t gid;		    /**< group ID of the job owner */
    PStask_ID_t src;	    /**< task ID of RPC source -- used by psgw */
    PElogueResourceCb_t *cb;/**< callback to return the result */
} PElogueResource_t;

typedef enum {
    PELOGUE_OE_STDOUT = 7,  /**< message content was stdout data */
    PELOGUE_OE_STDERR,      /**< message content was stderr data */
} PElogue_OEtype_t;

/** Argument of hook PSIDHOOK_PELOGUE_OE for pelogues stdout/stderr */
typedef struct {
    PElogueChild_t *child;  /**< holding information about a running pelogue */
    PElogue_OEtype_t type;  /**< type of message */
    char *msg;		    /**< the message to handle */
} PElogue_OEdata_t;

typedef struct {
    char *jobid;            /**< batch system's job ID */
    PElogueResList_t *res;  /**< list of results */
    int exit;		    /**< accumulated exit status */
} PElogue_Global_Res_t;

/**
 * @brief Job callback
 *
 * Callback invoked every time a pelogue for a job is finished. In
 * order to help the registering plugin to identify the finalized
 * pelogue @a jobid presents the ID used while registering the job via
 * @ref psPelogueAddJob(). @a exit presents the exit status of the
 * pelogues, i.e. it will be different from 0 if at least one pelogue
 * failed. The flag @a timeout indicates if the collection of pelogues
 * ran into a timeout. Finally @a res presents the results of
 * individual pelogues on the corresponding nodes.
 *
 * @param jobid Job ID helping the registering plugin to identify the
 * finalized pelogue.
 *
 * @param exit Exit status of the pelogues
 *
 * @param timeout Flag indication the pelogue ran into the timeout
 *
 * @param res Array of individual pelogue results on the nodes
 *
 * @return No return value
 */
typedef void(PElogueJobCb_t)(char *jobid, int exit, bool timeout,
			     PElogueResList_t *res, void *info);

/**
 * @brief Add configuration
 *
 * Add the configuration @a config to pelogue plugin's repository. The
 * configuration is tagged with @a name for future reference in
 * e.g. @ref psPelogueAddJob(), @ref psPelogueStartPE(), or @ref
 * psPelogueDelPluginConfig(). By convention a plugin shall use its
 * own name for tagging a configuration.
 *
 * While adding the configuration a check for the existence of a
 * parameter DIR_SCRIPTS is made. Furthermore the existence of the
 * referred directory and the scripts 'prologue', 'prologue.parallel',
 * 'epilogue', and 'epilogue.parallel' therein is enforced. Otherwise
 * the operation will not succeed.
 *
 * Up to MAX_SUPPORTED_PLUGINS configuration might be added.
 *
 * @param name Name tag of the configuration to register
 *
 * @param config The configuration to add to the repository
 *
 * @return On success true is returned or false in case of error
 */
typedef bool(psPelogueAddPluginConfig_t)(char *name, Config_t config);

/**
 * @brief Remove configuration
 *
 * Remove the configuration identified by the tag @a name from pelogue
 * plugin's repository.
 *
 * @param name Tag marking the configuration to be removed
 *
 * @return Return true if the configuration was found and removed or
 * false otherwise
 */
typedef bool(psPelogueDelPluginConfig_t)(char *name);

/**
 * @brief Add job
 *
 * Add a job identified by the job ID @a jobid that is associated to
 * the plugin @a plugin. @a plugin will be used to identify a
 * corresponding configuration during handling. The job might use the
 * user ID @a uid and the group ID @a gid for execution of part of the
 * initiated pelogues. pelogues will be executed on a total of @a
 * numNodes nodes given by the array of node id @a nodes. Upon
 * finalization of a pelogue execution the callback @a cb will be
 * called.
 *
 * @param plugin Name of the plugin the job is associated to
 *
 * @param jobid ID of the job to create
 *
 * @param uid User ID part of the pelogues might use
 *
 * @param gid Group ID part of the pelogues might use
 *
 * @param numNodes Number of nodes pelogue are executed on. Size of @ref nodes.
 *
 * @param nodes Array of node ID pelogues are executed on
 *
 * @param cb Callback called on finalization of a pelogue run
 *
 * @param info Pointer to additional information passed to callback @a cb
 *
 * @return Return true on success or false if the job could not be
 * registered.
 */
typedef bool(psPelogueAddJob_t)(const char *plugin, const char *jobid,
				uid_t uid, gid_t gid, int numNode,
				PSnodes_ID_t *nodes, PElogueJobCb_t *cb,
				void *info, bool fwStdOE);

/**
 * @brief Start job's pelogues
 *
 * Tell all nodes associated to the job identified by its @a jobid and
 * the associated @a plugin to start a pelogue of type @a type. The
 * environment @a env will be used on the target node in order to run
 * the pelogue. The pelogue will be started @a rounds times in order
 * to enable for different types of pelogues (e.g. prologue and
 * prologue.user in PBS type of RMS). To actually start the specific
 * pelogue for a given round PSIDHOOK_PELOGUE_PREPARE shall be used.
 *
 * In order to trigger the start, according messages will be sent to
 * the pelogue plugins on all nodes associated to the job.
 *
 * @param plugin Name of the plugin the job is associated to
 *
 * @param jobid Job ID of the job to be handled
 *
 * @param type Type of pelogue to start
 *
 * @param rounds Number of times the pelogue shall be started
 *
 * @param env Environment to use on the target node for the pelogue
 *
 * @return Return true on success or false on error
*/
typedef bool(psPelogueStartPE_t)(const char *plugin, const char *jobid,
				 PElogueType_t type, int rounds, env_t env);

/**
 * @brief Signal job's pelogues
 *
 * Send the signal @a sig to all pelogues associated to the job
 * identified by its @a jobid and the associated @a plugin. @a reason
 * is mentioned within the corresponding log messages.
 *
 * In order to deliver the signal messages will be sent to the pelogue
 * plugins of all involved nodes.
 *
 * @param plugin Name of the plugin the job is associated to
 *
 * @param jobid Job ID of the job to be handled
 *
 * @param sig Signal to send to the job's pelogues
 *
 * @param reason Reason to be mentioned in the logs
 *
 * @return Return true on success or false on error
*/
typedef bool(psPelogueSignalPE_t)(const char *plugin, const char *jobid,
				  int sig, char *reason);

/**
 * @brief Delete job
 *
 * Delete the job associated to the plugin @a plugin and identified by
 * the job ID @a jobid.
 *
 * @param plugin Name of the plugin the job is associated to
 *
 * @param jobid Job ID identifying the job
 *
 * @return No return value
 */
typedef void(psPelogueDeleteJob_t)(const char *plugin, const char *jobid);


#endif  /* __PELOGUE__TYPES */
