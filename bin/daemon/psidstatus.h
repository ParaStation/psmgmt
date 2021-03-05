/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Helper functions for master-node detection and status actions.
 */
#ifndef __PSIDSTATUS_H
#define __PSIDSTATUS_H

#include <stdbool.h>
#include <stdint.h>

#include "psprotocol.h"
#include "psnodes.h"

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
 * @brief Initialize status stuff
 *
 * Initialize the master-node detection and status framework. This
 * registers the necessary message handlers.
 *
 * @return No return value.
 */
void initStatus(void);

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

/**
 * @brief Get node status info.
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

/**
 * @brief Get node memory info.
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
 * If the local node is declare to be master, @ref allocMasterSpace
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
 * master also control, if all nodes are still alive and if daemons
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
 * i.e. just before exit.
 *
 * @return No return value.
 */
void releaseStatusTimer(void);

/**
 * @brief Get status-timeout.
 *
 * Get the status-timeout in milli-seconds.
 *
 * Every time the status timeout is elapsed each node sends a
 * keep-alive ping into the fabric. On the master node additionally
 * the arrival of all ping messages is tested.
 *
 * @return The current status-timeout in milli-seconds is returned.
 */
int getStatusTimeout(void);

/**
 * @brief Set status-timeout.
 *
 * Set the status-timeout in milli-seconds to @a timeout.
 *
 * Every time the status timeout is elapsed each node sends a
 * keep-alive ping into the fabric. On the master node additionally
 * the arrival of all ping messages is tested.
 *
 * @param timeout The new timeout to be set in milli-seconds.
 *
 * @return No return value.
 */
void setStatusTimeout(int timeout);

/**
 * @brief Get dead-limit
 *
 * Get the daemon's dead limit.
 *
 * After this number of consecutively missing RDP-pings from a node
 * the master declares this node to be dead.
 *
 * @return The actual dead-limit of the local node. This might be
 * different from the effective value which is only stored on the
 * current master node.
 */
int getDeadLimit(void);

/**
 * @brief Set dead-limit
 *
 * Set the daemon's dead limit to @a limit.
 *
 * After this number of consecutively missing RDP-pings from a node
 * the master declares this node to be dead.
 *
 * @param limit The new limit to be set. The effective limit might be
 * untouched since only the value on the current master node takes any
 * effect.
 *
 * @return No return value.
 */
void setDeadLimit(int limit);

/**
 * @brief Get maximum status-broadcasts
 *
 * Get the maximum number of status-broadcasts per round. This is used
 * to limit the number of status-broadcasts per status-iteration. Too
 * many broadcast might lead to running out of message-buffers within
 * RDP on huge clusters.
 *
 * If more than this number of broadcasts are triggered during one
 * status-iteration, all future broadcasts will be ignored. The
 * corresponding counter is reset upon start of the next
 * status-iteration.
 *
 * The length of the status-iteration is steered via the @ref
 * setStatusTimeout() function.
 *
 * @return The number of allowed status-broadcast per status-iteration
 * is returned.
 *
 * @see setMaxStatBCast(), getStatusTimeout(), setStatusTimeout()
 */
int getMaxStatBCast(void);

/**
 * @brief Set maximum status-broadcasts
 *
 * Set the maximum number of status-broadcasts per round to @a
 * limit. This is used to limit the number of status-broadcasts per
 * status-iteration. Too many broadcast might lead to running out of
 * message-buffers within RDP on huge clusters.
 *
 * If more than @a limit broadcasts are triggered during one
 * status-iteration, all future broadcasts will be ignored. The
 * corresponding counter is reset upon start of the next
 * status-iteration.
 *
 * A value of 0 will completely suppress sending of
 * status-broadcasts. In this case information on dead nodes will be
 * propagated by sending ACTIVENODES messages upon receive of too many
 * wrong LOAD messages, only.
 *
 * The length of the status-iteration is steered via the @ref
 * setStatusTimeout() function.
 *
 * @param limit The new limit on the status-broadcasts per
 * status-iteration.
 *
 * @return No return value.
 *
 * @see getMaxStatBCast(), getStatusTimeout(), setStatusTimeout()
 */
void setMaxStatBCast(int limit);

/**
 * @brief Declare a node dead.
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
 * If @a silent is true, no message concerning the lost connection is
 * created within the logs -- unless PSID_LOG_STATUS is part of the
 * debug-mask.
 *
 * @param id The ParaStation ID of the node declared to be dead.
 *
 * @param sendDeadnode Flag triggering PSP_DD_DEAD_NODE messages.
 *
 * @param silent Flag triggering log-suppression of lost connection
 *
 * @return If successful, true is returned. Or false if @id is out of
 * range.
 */
bool declareNodeDead(PSnodes_ID_t id, int sendDeadnode, bool silent);

/**
 * @brief Declare a node alive.
 *
 * Declare the node with ParaStation ID @a id to be alive. Therefore
 * various internal and external (e.g. within PSnodes or MCast)
 * indicators are set appropriately. Within this process the node is
 * registered to have @a numThrds hardware threads realized by @a
 * numCores physical processor cores. These numbers differ on some
 * platforms due to CPUs implementing multi threading (SMT). An
 * example for the latter case is Intels Hyper-Threading-Technology
 * (HTT). Furthermore, the node is registered to understand protocol
 * version @a proto and daemon protocol version @a dmnProto.
 *
 * @param id The ParaStation ID of the node declared to be alive
 *
 * @param numCores Number of physical processor cores the registered
 * node is claimed to have
 *
 * @param numThrds Number of hardware threads the registered node is
 * claimed to have
 *
 * @param proto Protocol version the registered node supports
 *
 * @param dmnProto Daemon protocol version the registered node supports
 *
 * @return If successful, true is returned. Or false if @id is out of
 * range.
 */
bool declareNodeAlive(PSnodes_ID_t id, int numCores, int numThrds, int proto,
		      int dmnProto);

/**
 * @brief Send a PSP_DD_DAEMONCONNECT message.
 *
 * Send a initial PSP_DD_DAEMONCONNECT message to node @a id.
 *
 * @param id ParaStation ID of the node to send the message to.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_DAEMONCONNECT message is returned. If an error occurred, -1
 * is returned and errno is set appropriately.
 */
int send_DAEMONCONNECT(PSnodes_ID_t id);

/**
 * @brief Broadcast a PSP_DD_DAEMONSHUTDOWN message.
 *
 * Broadcast a PSP_DD_DAEMONSHUTDOWN message to all active nodes. This
 * message indicates that the sender will go down soon and no longer
 * handles any communication. Thus this function should only be called
 * immediately before exit.
 *
 * @return On success, the number of nodes the PSP_DD_DAEMONSHUTDOWN
 * message is sent to is returned, i.e. the value returned by the @ref
 * broadcastMsg() call. If an error occurred, -1 is returned and errno
 * is set appropriately.
 */
int send_DAEMONSHUTDOWN(void);

/**
 * @brief Send a PSP_DD_MASTER_IS message.
 *
 * Send a PSP_DD_MASTER_IS message to node @a dest. This gives a hint
 * to the receiving node which node is the correct master.
 *
 * @param dest ParaStation ID of the node to send the message to.
 *
 * @return On success, the number of bytes sent within the
 * PSP_DD_MASTER_IS message is returned. If an error occurred, -1 is
 * returned and errno is set appropriately.
 */
int send_MASTERIS(PSnodes_ID_t dest);

#endif  /* __PSIDSTATUS_H */
