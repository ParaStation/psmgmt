/*
 *               ParaStation
 * psidstatus.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidstatus.h,v 1.1 2003/12/19 15:12:08 eicker Exp $
 *
 */
/**
 * @file
 * Helper functions for master-node detection and status actions.
 *
 * $Id: psidstatus.h,v 1.1 2003/12/19 15:12:08 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDSTATUS_H
#define __PSIDSTATUS_H

#include "psprotocol.h"
#include "psnodes.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif


/** Structure used for returning info on number of jobs. */
typedef struct {
    short total;              /**< The total number of jobs */
    short normal;             /**< Number of "normal" jobs (i.e. without
				 admin, logger etc.) */
} PSID_Jobs_t;

/** Structure used for returning info on load. */
typedef struct {
    double load[3];           /**< The actual load parameters */
} PSID_Load_t;

/** Structure used for returning info on node status. */
typedef struct {
    PSID_Load_t load;    /**< The load info of the node @see PSID_Load_t */
    PSID_Jobs_t jobs;    /**< The job info of the node @see PSID_Jobs_t */
} PSID_NodeStatus_t;

/**
 * @brief Set job info.
 *
 * Set the information on running jobs. If @a total is different from
 * 0, the absolute number of jobs is increased by one. The same holds
 * for @a normal and the number of normal jobs, i.e. jobs that are
 * intended for computation and not for administrational tasks.
 *
 * If MCast is used for status update, @ref incJobsMCast is
 * called. Otherwise an extra RDPping() @todo is is triggered.
 *
 * @param total Flag if total count of jobs has to be increased.
 *
 * @param normal Flag if count of normal jobs has to be increased.
 *
 * @return No return value
 */
void incJobs(int total, int normal);

/**
 * @brief Set job info.
 *
 * Set the information on running jobs. If @a total is different from
 * 0, the absolute number of jobs is decreased by one. The same holds
 * for @a normal and the number of normal jobs, i.e. jobs that are
 * intended for computation and not for administrational tasks.
 *
 * If MCast is used for status update, @ref incJobsMCast is
 * called. Otherwise an extra RDPping() @todo is is triggered.
 *
 * @param total Flag if total count of jobs has to be increased.
 *
 * @param normal Flag if count of normal jobs has to be increased.
 *
 * @return No return value
 */
void decJobs(int total, int normal);

/** @brief Get node status info.
 *
 * Get information on the status of node @a node. The actual
 * information returned contains number concerning the number of jobs
 * and the load on the reqeusted node.
 *
 * This is mainly an abstraction layer in order to hide the actual
 * mechanism used for information distribution within
 * ParaStation. This might on the one hand be the MCast facility, on
 * the other hand a autonomous system based on RDP.
 *
 * @param node The node information is reqeusted from.
 *
 * @return On success, i.e. if the requested information is available,
 * this info is returned. Otherwise, some dummy information withi
 * negativ jobs numbers is returned. The latter case might occur, if
 * MCast is not used and info on some remote node is requested on a
 * node which is not the master node.
 */
PSID_NodeStatus_t getStatus(PSnodes_ID_t node);

/** @todo */
void setMasterID(PSnodes_ID_t id);

/** @todo */
PSnodes_ID_t getMasterID(void);

/** @todo */
int amMasterNode(void);

/** @brief Declare a node dead.
 *
 * Declare the node with ParaStation ID @a id to be dead. Therefor
 * various internal and externel (e.g. within PSnodes or MCast)
 * indicators are set appropriately. Furthermore local processes
 * connected with foreign ones on the according node will be signaled.
 *
 * @param id The ParaStation ID of the node declared to be dead.
 *
 * @return No return value.
 */
void declareNodeDead(PSnodes_ID_t id);

/** @brief Declare a node alive.
 *
 * Declare the node with ParaStation ID @a id to be alive. Therefor
 * various internal and externel (e.g. within PSnodes or MCast)
 * indicators are set appropriately. Within this process the node is
 * registered to have @a virtCPUs logical CPUs realized by @a physCPUs
 * physical ones. These numbers differ on some platforms because of
 * multi core CPUs or CPUs implementing multi threading. An example
 * for the latter case is Intels Hyper-Threading-Technology (HTT).
 *
 * Besides these main tasks furthermore the validity and coverage of
 * the installed license is tested.
 *
 * @param id The ParaStation ID of the node declared to be alive.
 *
 * @param physCPUs The number of physical CPUs the registered node is
 * claimed to have.
 *
 * @param virtCPUs The number of virtual CPUs the registered node is
 * claimed to have.
 *
 * @return No return value.
 */
void declareNodeAlive(PSnodes_ID_t id, int physCPUs, int virtCPUs);

/**
 * @brief Send a PSP_DD_DAEMONCONNECT message.
 *
 * Send a initial PSP_DD_DAEMONCONNECT message to node @a id.
 *
 * @param id ParaStation ID of the node to send the message to.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_DAEMONCONNECT message is returned. If an error occured, -1
 * is returned and errno is set appropriately.
 */
int send_DAEMONCONNECT(PSnodes_ID_t id);

/**
 * @brief Handle a PSP_DD_DAEMONCONNECT message.
 *
 * Handle the message @a msg of type PSP_DD_DAEMONCONNECT.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_DAEMONCONNECT(DDBufferMsg_t *msg);

/**
 * @brief Handle a PSP_DD_DAEMONESTABLISHED message.
 *
 * Handle the message @a msg of type PSP_DD_DAEMONESTABLISHED.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_DAEMONESTABLISHED(DDBufferMsg_t *msg);

/**
 * @brief Handle a PSP_DD_LOAD message.
 *
 * Handle the message @a msg of type PSP_DD_LOAD.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_LOAD(DDBufferMsg_t *msg);

/**
 * @brief Handle a PSP_DD_MASTER_IS message.
 *
 * Handle the message @a msg of type PSP_DD_MASTER_IS.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_MASTER_IS(DDBufferMsg_t *msg);

/**
 * @brief Handle a PSP_DD_DEAD_NODE message.
 *
 * Handle the message @a msg of type PSP_DD_DEAD_NODE.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_DEAD_NODE(DDBufferMsg_t *msg);

/**
 * @brief Handle a PSP_DD_ACTIVE_NODES message.
 *
 * Handle the message @a msg of type PSP_DD_ACTIVE_NODES.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_ACTIVE_NODES(DDBufferMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDSTATUS_H */
