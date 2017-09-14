/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Functions for handling the various information about the nodes
 * within the ParaStation daemon
 */
#ifndef __PSIDNODES_H
#define __PSIDNODES_H

#include <stdbool.h>
#include <netinet/in.h>

#include "psnodes.h"
#include "pstask.h"

/**
 * @brief Initialize the PSIDnodes module.
 *
 * Initialize the PSIDnodes module. It will be prepared to handle @a
 * numNodes nodes.
 *
 * This is mainly a wrapper of @ref PSIDnodes_grow().
 *
 * @param numNodes Number of nodes the PSIDnodes module is capable to
 * handle after successful return of this function.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_init(PSnodes_ID_t numNodes);

/**
 * @brief Grow PSIDnodes module.
 *
 * Re-initialize the PSIDnodes module and enable it to handle up to @a
 * numNodes nodes.
 *
 * Usually this function is called implicitly as soon as @ref
 * PSIDnodes_register() is called to register a new node which ID will
 * be out of the current range. @ref PSIDnodes_register() will grow
 * the number of nodes in chunks of 64 nodes.
 *
 * @param numNodes Number of nodes the PSIDnodes module is capable to
 * handle after successful return of this function.
 *
 * @return On success, 0 is returned or -1 if an error occurred.
 */
int PSIDnodes_grow(PSnodes_ID_t numNodes);

/**
 * @brief Get the number of nodes.
 *
 * Get the actual number of nodes the PSIDnodes module is currently
 * capable to handle after the last call of PSIDnodes_init() or
 * PSIDnodes_grow().
 *
 * @return The actual number of nodes.
 */
PSnodes_ID_t PSIDnodes_getNum(void);

/**
 * @brief Get the maximum ID used.
 *
 * Get the maximum ID of the nodes currently registered within the
 * PSIDnodes module. This only reflects nodes registered via @ref
 * PSIDnodes_register().
 *
 * Typically this number is smaller than the one reported by @ref
 * PSIDnodes_getNum().
 *
 * @return The maximum ID of registered nodes.
 */
PSnodes_ID_t PSIDnodes_getMaxID(void);

/**
 * @brief Register a new node.
 *
 * Register a new node with ParaStation ID @a id. This node will
 * reside on the host with IP address @a IPaddr. The IP address has to
 * be given in network byteorder.
 *
 * @param id ParaStation ID of the new node.
 *
 * @param addr IP address of the new node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_register(PSnodes_ID_t id, in_addr_t addr);


/**
 * @brief Get the ParaStation ID of a node.
 *
 * Get the ParaStation ID of the node with IP address @a IPaddr. The
 * IP address has to be given in network byteorder.
 *
 * @param addr IP address of the node to lookup.
 *
 * @return If the node was found, the ParaStation ID is returned. Or
 * -1 if an error occurred.
 */
PSnodes_ID_t PSIDnodes_lookupHost(in_addr_t addr);

/**
 * @brief Get the IP address of a node.
 *
 * Get the IP address of the node with ParaStation ID @a id. The IP
 * address will be given in network byteorder.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the IP address is returned. Or
 * INADDR_ANY if an error occurred.
 */
in_addr_t PSIDnodes_getAddr(PSnodes_ID_t id);

/**
 * @brief Declare a node to be up.
 *
 * Declare the node with ParaStation ID @a id to be up.
 *
 * @param id ParaStation ID of the node to bring up.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_bringUp(PSnodes_ID_t id);

/**
 * @brief Declare a node to be down.
 *
 * Declare the node with ParaStation ID @a id to be shutdown.
 *
 * @param id ParaStation ID of the node to bring down.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_bringDown(PSnodes_ID_t id);

/**
 * @brief Test if a node is up.
 *
 * Test if the node with ParaStation ID @a id is up.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node is up, true is returned; or false if @a id is
 * out of bound or the node is down
 */
bool PSIDnodes_isUp(PSnodes_ID_t id);


/**
 * @brief Set the protocol version of a node.
 *
 * Set the protocol version the node with ParaStation ID @a id is
 * capable to understand to @a version.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param hwType The protocol version of this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setProtoV(PSnodes_ID_t id, int version);

/**
 * @brief Get the protocol version of a node.
 *
 * Get the protocol version the node with ParaStation ID @a id talks.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the protocol version is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_getProtoV(PSnodes_ID_t id);

/**
 * @brief Set the daemon-protocol version of a node.
 *
 * Set the daemon-protocol version the node with ParaStation ID @a id
 * talks to @a version.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param hwType The daemon-protocol version of this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setDmnProtoV(PSnodes_ID_t id, int version);

/**
 * @brief Get the daemon-protocol version of a node.
 *
 * Get the daemon-protocol version the node with ParaStation ID @a id
 * talks.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the daemon-protocol version is
 * returned. Or -1 if an error occurred.
 */
int PSIDnodes_getDmnProtoV(PSnodes_ID_t id);

/**
 * @brief Set the hardware type of a node.
 *
 * Set the hardware type of the node with ParaStation ID @a id to @a hwType.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param hwType The hardware type to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setHWType(PSnodes_ID_t id, int hwType);

/**
 * @brief Get the hardware type of a node.
 *
 * Get the hardware type of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the hardware type is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_getHWType(PSnodes_ID_t id);

/**
 * @brief Set the jobs flag of a node.
 *
 * Set the jobs flag of the node with ParaStation ID @a id to @a runjobs.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param runjobs The runjobs flag to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setRunJobs(PSnodes_ID_t id, int runjobs);

/**
 * @brief Get the jobs flag of a node.
 *
 * Get the jobs flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the jobs flag is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_runJobs(PSnodes_ID_t id);

/**
 * @brief Set the starter flag of a node.
 *
 * Set the starter flag of the node with ParaStation ID @a id to @a starter.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param starter The starter flag to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setIsStarter(PSnodes_ID_t id, int starter);

/**
 * @brief Get the starter flag of a node.
 *
 * Get the starter flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the starter flag is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_isStarter(PSnodes_ID_t id);

/**
 * @brief Set the extra IP address of a node.
 *
 * Set the extra IP address of the node with ParaStation ID @a id to @a addr.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param addr The extra IP address to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setExtraIP(PSnodes_ID_t id, in_addr_t addr);

/**
 * @brief Get the extra IP address of a node.
 *
 * Get the extra IP address of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the extra IP address is returned. Or
 * INADDR_ANY if an error occurred or the extra IP address was not set.
 */
in_addr_t PSIDnodes_getExtraIP(PSnodes_ID_t id);

/**
 * @brief Set the number of physical CPUs of a node.
 *
 * Set the number of physical CPUs of the node with ParaStation ID @a
 * id to @a numCPU.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param numCPU The number of physical CPUs to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setPhysCPUs(PSnodes_ID_t id, short numCPU);

/**
 * @brief Get the number of physical CPUs of a node.
 *
 * Get the number of physical CPUs of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the number of physical CPUs is
 * returned. Or -1 if an error occurred.
 */
short PSIDnodes_getPhysCPUs(PSnodes_ID_t id);

/**
 * @brief Set the number of virtual CPUs of a node.
 *
 * Set the number of virtual CPUs of the node with ParaStation ID @a
 * id to @a numCPU.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param numCPU The number of virtual CPUs to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setVirtCPUs(PSnodes_ID_t id, short numCPU);

/**
 * @brief Get the number of virtual CPUs of a node.
 *
 * Get the number of virtual CPUs of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the number of virtual CPUs is
 * returned. Or -1 if an error occurred.
 */
short PSIDnodes_getVirtCPUs(PSnodes_ID_t id);

/**
 * @brief Set the hardware status of a node.
 *
 * Set the hardware status of the node with ParaStation ID @a id to @a
 * hwStatus.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param hwStatus The hardware status to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setHWStatus(PSnodes_ID_t id, int hwStatus);

/**
 * @brief Get the hardware status of a node.
 *
 * Get the hardware status of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the hardware status is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_getHWStatus(PSnodes_ID_t id);

/**
 * Container type to allow the use of PSID_nodes_*GUID() call for
 * both, uid_t and gid_t arguments
 */
typedef union {
    uid_t u;          /**< Use as user ID */
    gid_t g;          /**< Use as group ID */
} PSIDnodes_guid_t;

/** This indicates the type of argument and list to manipulate to the
 * PSID_nodes_*GUID() calls */
typedef enum {
    PSIDNODES_USER,     /**< Manipulate users allowed to use a node*/
    PSIDNODES_GROUP,    /**< Manipulate groups allowed to use a node*/
    PSIDNODES_ADMUSER,  /**< Manipulate node's administrative users */
    PSIDNODES_ADMGROUP, /**< Manipulate node's administrative groups */
} PSIDnodes_gu_t;

/**
 * @brief Set user/group ID to a node.
 *
 * Set the user/group ID of type @a what on the node with ParaStation
 * ID @a id to @a guid.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param what Flag to address the type of ID to set.
 *
 * @param guid The user/group ID to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid);

/**
 * @brief Add user/group ID to a node.
 *
 * Add the user/group ID of type @a what with value @a guid to the node
 * with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param what Flag to address the type of ID to add.
 *
 * @param guid The user/group ID to be added to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_addGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid);

/**
 * @brief Remove user/group ID from a node.
 *
 * Remove the user/group ID of type @a what with value @a guid from the
 * node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param what Flag to address the type of ID to remove.
 *
 * @param guid The user/group ID to be removed from this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_remGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid);

/**
 * @brief Test user/group ID on a node.
 *
 * Test if the user/group ID of type @a what with value @a guid is valid
 * on the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to be tested.
 *
 * @param what Flag to address the type of ID to be tested.
 *
 * @param guid The user/group ID to be tested on this node.
 *
 * @return If the ID to be tested is valid on this node, 1 is
 * returned. Otherwise 0 is given back.
 */
int PSIDnodes_testGUID(PSnodes_ID_t id,
		       PSIDnodes_gu_t what, PSIDnodes_guid_t guid);

/**
 * @brief Send list of user/group IDs.
 *
 * Send an option list of all user/group ID of type @a what to @a
 * dest. Depending on the destination @a dest, the type of GUIDs @a
 * what different types of options are used to actually send.
 *
 * If the destination task is another ParaStation daemon,
 * PSP_OP_SET_[GUID] and one or more PSP_OP_ADD_[GUID] options are
 * used to correctly set the corresponding GUIDs on the other
 * node. Otherwise PSP_OP_[GUID] options are used to send the
 * user-task the corresponding information.
 *
 * @param dest Task ID of the destination task to send to.
 *
 * @param what The type of GUID info to send.
 *
 * @return No return value.
 */
void send_GUID_OPTIONS(PStask_ID_t dest, PSIDnodes_gu_t what);

/**
 * @brief Set the maximum number of processes of a node.
 *
 * Set the maximum number of processes of the node with ParaStation ID
 * @a id to @a procs.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param procs The maximum number of processes to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setProcs(PSnodes_ID_t id, int procs);

/**
 * @brief Get the maximum number of processes of a node.
 *
 * Get the maximum number of processes of the node with ParaStation ID
 * @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the maximum number of processes is
 * returned. Or -1 if an error occurred.
 */
int PSIDnodes_getProcs(PSnodes_ID_t id);

/**
 * @brief Set the overbook flag of a node.
 *
 * Set the overbook flag of the node with ParaStation ID @a id to @a
 * overbook.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param overbook The overbook flag to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setOverbook(PSnodes_ID_t id, PSnodes_overbook_t overbook);

/**
 * @brief Get the overbook flag of a node.
 *
 * Get the overbook flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the overbook flag is returned. Or
 * -1 if an error occurred.
 */
PSnodes_overbook_t PSIDnodes_overbook(PSnodes_ID_t id);

/**
 * @brief Set the exclusive flag of a node.
 *
 * Set the exclusive flag of the node with ParaStation ID @a id to @a
 * exclusive.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param exclusive The exclusive flag to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setExclusive(PSnodes_ID_t id, int exclusive);

/**
 * @brief Get the exclusive flag of a node.
 *
 * Get the exclusive flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the exclusive flag is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_exclusive(PSnodes_ID_t id);

/**
 * @brief Set the process-pinning flag of a node.
 *
 * Set the process-pinning flag of the node with ParaStation ID @a id to @a
 * pinProcs.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param pinProcs The process-pinning flag to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setPinProcs(PSnodes_ID_t id, int pinProcs);

/**
 * @brief Get the process-pinning flag of a node.
 *
 * Get the process-pinning flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the process-pinning flag is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_pinProcs(PSnodes_ID_t id);

/**
 * @brief Set the memory-binding flag of a node.
 *
 * Set the memory-binding flag of the node with ParaStation ID @a id to @a
 * bindMem.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param bindMem The memory-binding flag to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setBindMem(PSnodes_ID_t id, int bindMem);

/**
 * @brief Get the memory-binding flag of a node.
 *
 * Get the memory-binding flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the memory-binding flag is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_bindMem(PSnodes_ID_t id);

/**
 * @brief Clear a node's CPU-map.
 *
 * Clear the CPU-map of the node with ParaStation ID @a id. The
 * CPU-map is used to map virtual CPU slots given by the scheduler to
 * physical cores on the local node.
 *
 * @param id ParaStation ID of the node to change.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_clearCPUMap(PSnodes_ID_t id);

/**
 * @brief Append CPU to a node's CPU-map.
 *
 * Append the CPU with number @a cpu to the CPU-map of the node with
 * ParaStation ID @a id. The CPU-map is used to map virtual CPU slots
 * given by the scheduler to physical cores on the local node.
 *
 * @param id ParaStation ID of the node to change.
 *
 * @param cpu The number of the CPU to append to the CPU-map.
 *
 * @return On success, 0 is returned . Or -1 if an error occurred.
 */
int PSIDnodes_appendCPUMap(PSnodes_ID_t id, short cpu);

/**
 * @brief Map CPU-slot to pysical core.
 *
 * Map the CPU-slot @a cpu to a physical core according CPU-map of the
 * node with ParaStation ID @a id. The CPU-map is put together by
 * calling @ref PSIDnodes_appendCPUMap() subsequently.
 *
 * @param id ParaStation ID of the CPU-map to use.
 *
 * @param cpu Number of the CPU-slot to map on a physical core.
 *
 * @return On success, the number of the core the CPU-slot is mapped
 * to will be returned. Or -1 if an error occurred.
 */
short PSIDnodes_mapCPU(PSnodes_ID_t id, short cpu);

/**
 * @brief Send CPU-map.
 *
 * Send the CPU-map of the local daemon to @a dest within one or more
 * option messages of type PSP_OP_CPUMAP.
 *
 * @param dest Task ID of the destination task to send to.
 *
 * @return No return value.
 */
void send_CPUMap_OPTIONS(PStask_ID_t dest);

/**
 * @brief Set the allowUserMap flag of a node.
 *
 * Set the allowUserMap flag of the node with ParaStation ID @a id to
 * @a allowMap. If this flag is different from 0, users are allowed to
 * influence the local mapping of their processes by providing the
 * environment __PSI_CPUMAP.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param allowMap The allowUserMap flag to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setAllowUserMap(PSnodes_ID_t id, int allowMap);

/**
 * @brief Get the allowUserMap flag of a node.
 *
 * Get the allowUserMap flag of the node with ParaStation ID @a id. If
 * this flag is different from 0, users are allowed to influence the
 * local mapping of their processes by providing the environment
 * __PSI_CPUMAP.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the allowUserMap flag is returned. Or
 * -1 if an error occurred.
 */
int PSIDnodes_allowUserMap(PSnodes_ID_t id);

/**
 * @brief Set node's accounter poll interval
 *
 * Set the accounter poll interval of node @a id to @a interval. If
 * set to 0, no polling at all will take place.
 *
 * @param id ParaStation ID of the node to change.
 *
 * @param interval The polling interval in seconds to be set.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setAcctPollI(PSnodes_ID_t id, int interval);

/**
 * @brief Get node's accounter poll interval
 *
 * Get the accounter poll interval of node @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the accounting poll interval is
 * returned. Or -1 if an error occurred.
 */
int PSIDnodes_acctPollI(PSnodes_ID_t id);

/**
 * @brief Set node's kill delay
 *
 * Set the kill delay of node @a id to @a delay. This determines the
 * number of seconds between a relative signal and the follow-up SIGKILL.
 *
 * @param id ParaStation ID of the node to change.
 *
 * @param delay The kill delay in seconds to be set.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setKillDelay(PSnodes_ID_t id, int delay);

/**
 * @brief Get node's kill delay
 *
 * Get the kill delay of node @a id.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the kill delay is returned. Or -1
 * if an error occurred.
 */
int PSIDnodes_killDelay(PSnodes_ID_t id);

/**
 * @brief Set the supplementary groups flag of a node.
 *
 * Set the supplementary groups flag of the node with ParaStation ID
 * @a id to @a supplGrps.
 *
 * The supplementary groups flags marks if the node will set all the
 * user's supplementary groups while spawning a new process.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param supplGrps The supplementary groups flag to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setSupplGrps(PSnodes_ID_t id, int supplGrps);

/**
 * @brief Get the supplementary groups flag of a node.
 *
 * Get the supplementary groups flag of the node with ParaStation ID
 * @a id.
 *
 * The supplementary groups flags marks if the node will set all the
 * user's supplementary groups while spawning a new process.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the supplementary groups flag is
 * returned. Or -1 if an error occurred.
 */
int PSIDnodes_supplGrps(PSnodes_ID_t id);

/**
 * @brief Set the maximum of stat() tries of a node.
 *
 * Set the maximum number of tries to stat() an executable to spawn of
 * the node with ParaStation ID @a id to @a tries.
 *
 * Whenever a process is spawned, before actually starting the
 * executable via execv() stat() is applied in order to find out if
 * the executable is accessible. This gives the maximum number of
 * retries before failing this operation.
 *
 * @param id ParaStation ID of the node to be modified.
 *
 * @param tries The number of tries to be set to this node.
 *
 * @return On success, 0 is returned. Or -1 if an error occurred.
 */
int PSIDnodes_setMaxStatTry(PSnodes_ID_t id, int tries);

/**
 * @brief Get the supplementary groups flag of a node.
 *
 * Get the maximum number of tries to stat() an executable to spawn of
 * the node with ParaStation ID @a id.
 *
 * Whenever a process is spawned, before actually starting the
 * executable via execv(), stat() is applied in order to find out if
 * the executable is accessible. This gives the maximum number of
 * retries before failing this operation.
 *
 * @param id ParaStation ID of the node to look up.
 *
 * @return If the node was found, the maximum number of tries is
 * returned. Or -1 if an error occurred.
 */
int PSIDnodes_maxStatTry(PSnodes_ID_t id);

/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by the module. It will very
 * aggressively free all allocated memory most likely destroying
 * all the module's funcitonality.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value.
 */
void PSIDnodes_clearMem(void);

#endif  /* __PSIDNODES_H */
