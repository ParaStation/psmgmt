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
#ifndef __PSSLURM_PROTO
#define __PSSLURM_PROTO

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "list.h"
#include "pscommon.h"
#include "psenv.h"

#include "psaccounttypes.h"

#include "slurmmsg.h"
#include "psslurmcomm.h"
#include "psslurmjob.h"
#include "psslurmjobcred.h"
#include "psslurmmsg.h"
#include "psslurmstep.h"
#include "psslurmtypes.h"
#include "psslurmprototypes.h"

/** Structure holding a Slurm config file */
typedef struct {
    bool create;    /**< flag to create/delete the file */
    char *name;	    /**< file name */
    char *data;	    /**< file content */
} Config_File_t;

/** Structure holding all received configuration files */
typedef struct {
    Config_File_t *files;	    /**< holding config files (since 21.08) */
    uint32_t numFiles;		    /**< number of config files (since 21.08) */
    char *slurm_conf;
    char *acct_gather_conf;
    char *cgroup_conf;
    char *cgroup_allowed_dev_conf;
    char *ext_sensor_conf;
    char *gres_conf;
    char *knl_cray_conf;
    char *knl_generic_conf;
    char *plugstack_conf;
    char *topology_conf;
    char *xtra_conf;
    char *slurmd_spooldir;
} Config_Msg_t;

/** Holding Slurm configuration actions */
typedef enum {
    CONF_ACT_STARTUP = 0x20,	/**< receive configuration files at startup */
    CONF_ACT_RELOAD,		/**< reload configuration
				  (currently unsupported) */
    CONF_ACT_NONE		/**< no change in configuration */
} Config_Action_t;

/** Slurm protocol version */
extern uint32_t slurmProto;

/** Slurm protocol version string */
extern char *slurmProtoStr;

/** Slurm version string */
extern char *slurmVerStr;

/** Flag to measure Slurm RPC execution times */
extern bool measureRPC;

/** Node registration response data (TRes) */
extern Ext_Resp_Node_Reg_t *tresDBconfig;

/**
 * @brief Send a node registration message
 *
 * Send the current node configuration and status to the slurmctld.
 */
void sendNodeRegStatus(bool startup);

/**
 * @brief Process a new Slurm message
 *
 * Extract and verify a Slurm message header including the protocol
 * version and munge authentication. Additionally the Slurm tree
 * forwarding is handled. On success the message payload is passed
 * to the connection callback to actually handle the message. The
 * default handler is @ref handleSlurmdMsg().
 *
 * @param sMsg The message to process
 *
 * @param fw The Slurm tree forward header
 *
 * @param cb The callback to handle to processed message
 *
 * @param info Additional information passed to the callback
 */
void processSlurmMsg(Slurm_Msg_t *sMsg, Msg_Forward_t *fw, Connection_CB_t *cb,
		     void *info);

/**
 * @brief Send a Slurm rc message
 *
 * Send a Slurm rc (return code) message.
 *
 * @param sMsg The rc message to send
 *
 * @param rc The return code
 *
 * @return Returns -1 on error or a positive number indicating that
 * the message was either successfully send or stored.
 */
int __sendSlurmRC(Slurm_Msg_t *sMsg, uint32_t rc,
		    const char *func, const int line);

#define sendSlurmRC(sMsg, rc) __sendSlurmRC(sMsg, rc, __func__, __LINE__)

/**
 * @brief Send a Slurm reply message
 *
 * Send a Slurm reply message choosing the appropriate channel. First
 * the message type is set. Then the message will either be send back
 * to the root of the forwarding tree. Stored until all forwarding reply
 * messages are received. Or send to its target destination if forwarding
 * is disabled for the reply message.
 *
 * @param sMsg The reply message to send
 *
 * @param type The message type
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns -1 on error or a positive number indicating that
 * the message was either successfully send or stored.
 */
int __sendSlurmReply(Slurm_Msg_t *sMsg, slurm_msg_type_t type,
			const char *func, const int line);

#define sendSlurmReply(sMsg, type) \
		    __sendSlurmReply(sMsg, type, __func__, __LINE__)

/**
 * @brief Save the job-script to disk
 *
 * @param job The job of the job-script
 *
 * @param return Returns true on success and false on error.
 */
bool writeJobscript(Job_t *job);

/**
 * @brief Send a RESPONSE_LAUNCH_TASKS message to srun
 *
 * Send a RESPONSE_LAUNCH_TASKS message including the local
 * PIDs and matching global task IDs of the locally started processes
 * from the given step.
 *
 * @param step The step to send the message for
 */
void sendTaskPids(Step_t *step);

/* Magic value for a nodeID to send a launch response to all nodes */
#define ALL_NODES -1

/**
 * @brief Send a RESPONSE_LAUNCH_TASKS error message to srun
 *
 * Send a RESPONSE_LAUNCH_TASKS error message for all or a selected
 * node of a step. This is basically a wrapper for
 * @ref doSendLaunchTasksFailed().
 *
 * @param step The step to send the message for
 *
 * @param nodeID The step local nodeID to send the message for. Use ALL_NODES
 * to send the message for every node of the step.
 *
 * @param error Slurm error code
 */
void sendLaunchTasksFailed(Step_t *step, uint32_t nodeID, uint32_t error);

/**
 * @brief Send a task exit message
 *
 * Send one or more task exit message(s) to srun or sattach. If
 * ctlPort or ctlAddr is NULL a new control connection
 * to the srun process of the provided step will be
 * opened.
 *
 * @param step The step to send the message for
 *
 * @param ctlPort The control port of srun or sattach
 *
 * @param ctlAddr The control address of srun or sattach
 */
void sendTaskExit(Step_t *step, int *ctlPort, int *ctlAddr);

/**
 * @brief Send a step complete message
 *
 * Send a step complete message to the slurmctld. This message
 * will be generated for the complete node list of the step and
 * include the exit status and accounting information.
 *
 * @param step The step to send the message for
 *
 * @param exitStatus The exit status of the step
 */
void sendStepExit(Step_t *step, uint32_t exitStatus);

/**
 * @brief Send a batch job complete message
 *
 * Send a batch job complete message to the slurmctld.
 *
 * @param job The job to send the message for
 *
 * @param exit_status The exit status of the job
 */
void sendJobExit(Job_t *job, uint32_t exitStatus);

/**
 * @brief Send a epilogue complete message
 *
 * Send a epilogue complete message to the slurmctld.
 *
 * @param jobid The jobid to send the message for
 *
 * @param rc The return code of the epilogue
 */
void sendEpilogueComplete(uint32_t jobid, uint32_t rc);

/**
 * @brief Convert a PS node ID to a job local node ID
 *
 * @param psNodeID The ParaStation node ID to convert
 *
 * @param nodes ParaStation node-list of the job
 *
 * @param nrOfNodes Number of nodes in the node-list
 *
 * @return Returns the requested node ID or -1 on error
 */
int getSlurmNodeID(PSnodes_ID_t psNodeID, PSnodes_ID_t *nodes,
		    uint32_t nrOfNodes);

/**
 * @brief Convert a Slurm job global rank to a node local rank
 *
 * @param rank The job global rank to convert
 *
 * @param step The step for the rank to convert
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the converted node local rank or NO_VAL on error
 */
uint32_t __getLocalRankID(uint32_t rank, Step_t *step,
			  const char *caller, const int line);
#define getLocalRankID(rank, step)			\
    __getLocalRankID(rank, step, __func__, __LINE__)

/**
 * @brief Register message handler function
 *
 * Register the function @a handler to handle all messages of type @a
 * msgType. If @a handler is NULL, all messages of type @a msgType
 * will be silently ignored in the future.
 *
 * @param msgType The message-type to handle
 *
 * @param handler The function to call whenever a message of type @a
 * msgType has to be handled
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If this is the
 * first handler registered for this message-type, NULL is returned.
 */
slurmdHandlerFunc_t registerSlurmdMsg(int msgType, slurmdHandlerFunc_t handler);

/**
 * @brief Unregister message handler function
 *
 * Unregister the message-type @a msgType such that it will not be
 * handled in the future. This includes end of silent ignore of this
 * message-type.
 *
 * @param msgType The message-type not to handle any longer
 *
 * @return If a handler for this message-type was registered before,
 * the corresponding function pointer is returned. If no handler was
 * registered or the message-type was unknown before, NULL is
 * returned.
  */
slurmdHandlerFunc_t clearSlurmdMsg(int msgType);

/**
 * @brief Central protocol switch.
 *
 * Handle the message @a msg according to its message-type. The
 * handler associated to the message-type might be registered via @ref
 * registerSlurmdMsg() and unregistered via @ref clearSlurmdMsg().
 *
 * @param msg The message to handle
 *
 * @param info Additional info currently unused
 *
 * @return Always return 0
 */
int handleSlurmdMsg(Slurm_Msg_t *sMsg, void *info);

bool initSlurmdProto(void);

void clearSlurmdProto(void);

/**
 * @brief Request job information from slurmctld
 *
 * @param jobid The job ID to request information for
 *
 * @param cb Callback function to handle the result or NULL
 *
 * @return Returns the number of bytes written, -1 on error or -2 if
 * the message was stored and will be send out later
 */
int requestJobInfo(uint32_t jobid, Connection_CB_t *cb);

/**
 * @brief Get the local ID of current node
 *
 * @param nodes The node-list to extract the local ID from
 *
 * @param nrOfNodes The number of nodes in @a nodes
 *
 * @return Returns the requested local ID or -1 on error
 */
uint32_t getLocalID(PSnodes_ID_t *nodes, uint32_t nrOfNodes);

/**
 * @brief Drain node(s) in Slurm
 *
 * This will drain nodes in Slurm using the same messages
 * like scontrol.
 *
 * @param nodeList A list holding all nodes to drain
 *
 * @param reason The reason the nodes will be drained
 */
void sendDrainNode(const char *nodeList, const char *reason);

/**
 * @brief Send a Slurm configuration request
 *
 * Send a Slurm configuration request to the given slurmctld
 * server. In config-less mode the slurmctld will send all known
 * configuration files in the response message. The response is
 * handled in @ref handleSlurmConf().
 *
 * @param server The slurmctld server to send the request to
 *
 * @param action The action to be taken if the configuration was successful
 * fetched
 *
 * @return Returns true on success or false on error
 */
bool sendConfigReq(const char *server, const int action);

/**
 * @brief Send a prologue complete request to slurmctld
 *
 * If rc is SLURM_ERROR the node will be drained and
 * the job gets requeued if possible (steps started
 * without a job will be cancelled).
 *
 * @param jobid The jobid of the prologue completed
 *
 * @param rc The return code of the prologue script
 */
void sendPrologComplete(uint32_t jobid, uint32_t rc);

/**
 * @brief Send a job requeue request
 *
 * Tries to requeue a job. If requeuing fails the job
 * will get cancelled.
 *
 * @param jobid The ID of the job to requeue
 */
void sendJobRequeue(uint32_t jobid);

/**
 * @brief Execute a Slurm health-check
 *
 * @return Returns true on success otherwise false is returned
 */
bool runHealthCheck(void);

/**
 * @brief Stop a running Slurm health-check
 *
 * @param signal The signal to send
 *
 * @return Returns true if no script is running or false otherwise
 */
bool stopHealthCheck(int signal);

/**
 * @brief Get Slurm health-check runs
 *
 * @return Returns the number of times the health-check was executed
 */
uint64_t getSlurmHCRuns(void);

/**
 * @brief Calculate the real memory of the local node
 * in megabytes
 *
 * @return Returns the real memory of the local node or 1 on error.
 */
uint64_t getNodeMem(void);

#endif /* __PSSLURM_PROTO */
