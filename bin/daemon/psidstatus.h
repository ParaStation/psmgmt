/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Helper functions for master-node detection and status actions.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDSTATUS_H
#define __PSIDSTATUS_H

#include <stdint.h>

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
    short total;         /**< The total number of jobs */
    short normal;        /**< Number of "normal" jobs (i.e. without
			    admin, logger etc.) */
} PSID_Jobs_t;

/** Structure used for returning info on load. */
typedef struct {
    double load[3];      /**< The actual load parameters */
} PSID_Load_t;

typedef struct {
    uint64_t total;
    uint64_t free;
} PSID_Mem_t;

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
 * If MCast is used for status control, @ref incJobsMCast is
 * called. Otherwise an extra status ping is is triggered.
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
 * If MCast is used for status control, @ref incJobsMCast is
 * called. Otherwise an extra status ping is is triggered.
 *
 * @param total Flag if total count of jobs has to be increased.
 *
 * @param normal Flag if count of normal jobs has to be increased.
 *
 * @return No return value
 */
void decJobs(int total, int normal);

/**
 * @brief Give job info hint.
 *
 * Give a hint to the status handling system that the number of normal
 * jobs on node @a node will be decreased by one soon, i.e. most
 * likely when the next PSP_DD_LOAD message will be received from this
 * node.
 *
 * @param node The node on which the number of normal jobs will be
 * decreased by one soon.
 *
 * @return No return value
 */
void decJobsHint(PSnodes_ID_t node);

/** @brief Get node status info.
 *
 * Get information on the status of node @a node. The actual
 * information returned contains number concerning the number of jobs
 * and the load on the requested node.
 *
 * This is mainly an abstraction layer in order to hide the actual
 * mechanism used for information distribution within
 * ParaStation. This might on the one hand be the MCast facility, on
 * the other hand a autonomous system based on RDP.
 *
 * @param node The node information is requested from.
 *
 * @return On success, i.e. if the requested information is available,
 * this info is returned. Otherwise, some dummy information with a
 * negative jobs numbers is returned. The latter case might occur, if
 * MCast is not used and info on some remote node is requested on a
 * node which is not the master node.
 */
PSID_NodeStatus_t getStatusInfo(PSnodes_ID_t node);

/** @brief Get node memory info.
 *
 * Get information on the memory status of node @a node. The actual
 * information returned contains the number of total available bytes
 * and number of free bytes.
 *
 * This is mainly an abstraction layer in order to hide the actual
 * mechanism used for information distribution within
 * ParaStation. This might on the one hand be the MCast facility, on
 * the other hand a autonomous system based on RDP.
 *
 * For clusters using the MCast facility memory information is
 * typically unavailable.
 *
 * @param node The node information is requested from.
 *
 * @return On success, i.e. if the requested information is available,
 * this info is returned. Otherwise, some dummy information with both
 * numbers equal to -1 is returned. The latter case might occur, if
 * MCast is not used and info on some remote node is requested on a
 * node which is not the master node or if MCast is used on any node.
 */
PSID_Mem_t getMemoryInfo(PSnodes_ID_t node);

/**
 * @brief Declare master node.
 *
 * Declare the node @a newMaster to be the master of the cluster until
 * another node is declared to handle this task.
 *
 * The actual tasks of the master depend on the method used for status
 * control. If the MCast facility is used, the master only has to
 * handle partition requests. Otherwise it also controls, if all nodes
 * are still alive and if daemons on further nodes were started.
 *
 * If the local node is declare to be master, @ref alloceMasterSpace
 * is called in order to provide the space needed to handle all
 * necessary tasks.
 *
 * @param newMaster ParaStation ID of the new master to declare.
 *
 * @return No return value.
 */
void declareMaster(PSnodes_ID_t newMaster);

/**
 * @brief Test if master is already known.
 *
 * Test if the master node is already known. In case of not using the
 * MCast facility for status control this might happen during a short
 * startup phase of the cluster.
 *
 * @return If the master is still unknown, 0 is returned. After
 * determining the master is finished, 1 is returned.
 */
int knowMaster(void);

/**
 * @brief Get master's ParaStation ID.
 *
 * Get the unique ParaStation ID of the node actually serving as the
 * master node. In case of usage of the MCast facility, the master's
 * only task is the handling of partition requests. Otherwise the
 * master also controll, if all nodes are still alive and if daemons
 * on further nodes were started.
 *
 * In order to be able to rely on the result of this call, also the
 * result of @ref knowMaster() has to be taken into account. I.e. only
 * if that function returns a value different from 0, the return value
 * of this function is valid.
 *
 * @return The ParaStation ID of the actual master is returned. Use to
 * result of @ref knowMaster in order to decide if the ID is valid.
 *
 * @see knowMaster()
 */
PSnodes_ID_t getMasterID(void);

/**
 * @brief Release status timer.
 *
 * Release the timer used within the status control mechanism. This
 * should only be done, when status control is no longer needed,
 * i.e. just befor exit.
 *
 * @return No return value.
 */
void releaseStatusTimer(void);

/** @brief Declare a node dead.
 *
 * Declare the node with ParaStation ID @a id to be dead. Therefore
 * various internal and external (e.g. within PSnodes or MCast)
 * indicators are set appropriately. Furthermore local processes
 * connected with foreign ones on the according node will be signaled.
 *
 * If @a sendDeadnode is different from 0 and the actual node acts as
 * master, a PSP_DD_DEAD_NODE message is sent to all node known to be
 * up in order to inform them about the dead node.
 *
 * @param id The ParaStation ID of the node declared to be dead.
 *
 * @param sendDeadnode Flag triggering PSP_DD_DEAD_NODE messages.
 *
 * @return No return value.
 */
void declareNodeDead(PSnodes_ID_t id, int sendDeadnode);

/** @brief Declare a node alive.
 *
 * Declare the node with ParaStation ID @a id to be alive. Therefore
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
 * A PSP_DD_DAEMONCONNECT message is sent whenever a daemon detects a
 * node it is not connected to. Receiving this message provides
 * information on the setup and status of the sending node.
 *
 * This message is answered by a PSP_DD_DAEMONESTABLISHED message
 * providing the corresponding information to the connecting node.
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
 * Receiving this answer on a PSP_DD_DAEMONCONNECT message sent to the
 * sending node provides the local daemon with the information on the
 * setup and status of this other node.
 *
 * With the receive of this message the setup of the daemon-daemon
 * connection is finished and the other node is marked to be up now.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_DAEMONESTABLISHED(DDBufferMsg_t *msg);

/**
 * @brief Broadcast a PSP_DD_DAEMONSHUTDOWN message.
 *
 * Broadcast a PSP_DD_DAEMONSHUTDOWN message to all active nodes. This
 * message indicates that the sender will go down soon and no longer
 * handles any communication. Thus this function should only be called
 * immediately befor exit.
 *
 * @return On success, the number of nodes the PSP_DD_DAEMONSHUTDOWN
 * message is sent to is returned, i.e. the value returned by the @ref
 * broadcastMsg() call. If an error occured, -1 is returned and errno
 * is set appropriately.
 */
int send_DAEMONSHUTDOWN(void);

/**
 * @brief Handle a PSP_DD_DAEMONSHUTDOWN message.
 *
 * Handle the message @a msg of type PSP_DD_DAEMONSHUTDOWN.
 *
 * This kind of messages tells the receiver that the sending node will
 * go down soon and no longer accepts messages for receive. Thus this
 * node should be marked to be down now.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_DAEMONSHUTDOWN(DDMsg_t *msg);

/**
 * @brief Handle a PSP_DD_LOAD message.
 *
 * Handle the message @a msg of type PSP_DD_LOAD.
 *
 * PSP_DD_LOAD messages are send by each node to the current master
 * process. Thus upon receive of this kind of message by a node not
 * acting as the master, a PSP_DD_MASTER_IS message will be initiated
 * in order to inform the sending node about the actual master
 * process.
 *
 * The master process will handle this message by storing the
 * information contained to the local status arrays.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_LOAD(DDBufferMsg_t *msg);

/**
 * @brief Send a PSP_DD_MASTER_IS message.
 *
 * Send a PSP_DD_MASTER_IS message to node @a dest. This gives a hint
 * to the receiving node which node is the correct master.
 *
 * @param dest ParaStation ID of the node to send the message to.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_MASTER_IS message is returned. If an error occured, -1 is
 * returned and errno is set appropriately.
 */
int send_MASTERIS(PSnodes_ID_t dest);

/**
 * @brief Handle a PSP_DD_MASTER_IS message.
 *
 * Handle the message @a msg of type PSP_DD_MASTER_IS.
 *
 * The sending node give a hint on the correct master. If the local
 * information differs from the information provided, one of two
 * measures will be taken:
 *
 * - If the master provided has a node number smaller than the current
 * master, it will be tried to contact this new master via @ref
 * send_DAEMONCONNECT().
 *
 * - Otherwise a PSP_DD_MASTER_IS message is sent to the sender in
 * order to inform on the actual master node.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_MASTERIS(DDBufferMsg_t *msg);

/**
 * @brief Handle a PSP_DD_ACTIVE_NODES message.
 *
 * Handle the message @a msg of type PSP_DD_ACTIVE_NODES.
 *
 * Whenever a daemon node connects to a new node, one or more
 * PSP_DD_ACTIVE_NODES messages are sent the this node in order to
 * inform about the active nodes currently known to the master. The
 * receiving node will try to contact each of this nodes provided in
 * order to setup a working connection.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_ACTIVENODES(DDBufferMsg_t *msg);

/**
 * @brief Handle a PSP_DD_DEAD_NODE message.
 *
 * Handle the message @a msg of type PSP_DD_DEAD_NODE.
 *
 * Whenever a daemon node detects a node to be down all other nodes
 * will be informed about this fact via a PSP_DD_DEAD_NODE
 * message. Each node receiving this kind of message will try contact
 * the according node and usually mark this node as dead via a
 * callback from daemon's the RDP facility.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_DEADNODE(DDBufferMsg_t *msg);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDSTATUS_H */
