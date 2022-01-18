/*
 * ParaStation
 *
 * Copyright (C) 2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
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
#include <stdint.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "pscpu.h"
#include "psnodes.h"
#include "pstask.h"

/**
 * @brief Initialize the PSIDnodes module
 *
 * Initialize the PSIDnodes module. It will be prepared to handle @a
 * numNodes nodes.
 *
 * This is mainly a wrapper of @ref PSIDnodes_grow().
 *
 * @param numNodes Number of nodes the PSIDnodes module is capable to
 * handle after successful return of this function
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_init(PSnodes_ID_t numNodes);

/**
 * @brief Grow PSIDnodes module
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
 * handle after successful return of this function
 *
 * @return On success, 0 is returned or -1 if an error occurred
 */
int PSIDnodes_grow(PSnodes_ID_t numNodes);

/**
 * @brief Get the number of nodes
 *
 * Get the actual number of nodes the PSIDnodes module is currently
 * capable to handle after the last call of PSIDnodes_init() or
 * PSIDnodes_grow().
 *
 * @return The actual number of nodes
 */
PSnodes_ID_t PSIDnodes_getNum(void);

/**
 * @brief Get the maximum ID used
 *
 * Get the maximum ID of the nodes currently registered within the
 * PSIDnodes module. This only reflects nodes registered via @ref
 * PSIDnodes_register().
 *
 * Typically this number is smaller than the one reported by @ref
 * PSIDnodes_getNum().
 *
 * @return The maximum ID of registered nodes
 */
PSnodes_ID_t PSIDnodes_getMaxID(void);

/**
 * @brief Register a new node
 *
 * Register a new node with ParaStation ID @a id. This node will
 * reside on the host with IP address @a IPaddr. The IP address has to
 * be given in network byteorder.
 *
 * @param id ParaStation ID of the new node
 *
 * @param addr IP address of the new node
 *
 * @return On success, true is returned; or false if an error occurred
 */
bool PSIDnodes_register(PSnodes_ID_t id, in_addr_t addr);


/**
 * @brief Get node's ParaStation ID
 *
 * Get the ParaStation ID of the node with IP address @a IPaddr. The
 * IP address has to be given in network byteorder.
 *
 * @param addr IP address of the node to lookup
 *
 * @return If the node was found, the ParaStation ID is returned; or
 * -1 if an error occurred
 */
PSnodes_ID_t PSIDnodes_lookupHost(in_addr_t addr);

/**
 * @brief Get node's IP address
 *
 * Get the IP address of the node with ParaStation ID @a id. The IP
 * address will be given in network byteorder.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the IP address is returned; or
 * INADDR_ANY if an error occurred
 */
in_addr_t PSIDnodes_getAddr(PSnodes_ID_t id);

/**
 * @brief Declare a node to be up
 *
 * Declare the node with ParaStation ID @a id to be up.
 *
 * @param id ParaStation ID of the node to bring up
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_bringUp(PSnodes_ID_t id);

/**
 * @brief Declare a node to be down
 *
 * Declare the node with ParaStation ID @a id to be shutdown.
 *
 * @param id ParaStation ID of the node to bring down
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_bringDown(PSnodes_ID_t id);

/**
 * @brief Test if a node is up
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
 * @brief Set node's protocol version
 *
 * Set the protocol version the node with ParaStation ID @a id is
 * capable to understand to @a version.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param version Protocol version this node supports
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setProtoV(PSnodes_ID_t id, int version);

/**
 * @brief Get node's protocol version
 *
 * Get the protocol version the node with ParaStation ID @a id talks.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the protocol version is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_getProtoV(PSnodes_ID_t id);

/**
 * @brief Set node's daemon-protocol version
 *
 * Set the daemon-protocol version the node with ParaStation ID @a id
 * talks to @a version.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param version daemon-protocol version this node supports
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setDmnProtoV(PSnodes_ID_t id, int version);

/**
 * @brief Get node's daemon-protocol version
 *
 * Get the daemon-protocol version the node with ParaStation ID @a id
 * talks.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the daemon-protocol version is
 * returned; or -1 if an error occurred
 */
int PSIDnodes_getDmnProtoV(PSnodes_ID_t id);

/**
 * @brief Set node's hardware type
 *
 * Set the hardware type of the node with ParaStation ID @a id to @a hwType.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param hwType The hardware type to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setHWType(PSnodes_ID_t id, int hwType);

/**
 * @brief Get node's hardware type
 *
 * Get the hardware type of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the hardware type is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_getHWType(PSnodes_ID_t id);

/**
 * @brief Set node's runjobs flag
 *
 * Set the runjobs flag of the node with ParaStation ID @a id to @a
 * runjobs.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param runjobs The runjobs flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setRunJobs(PSnodes_ID_t id, int runjobs);

/**
 * @brief Get node's runjobs flag
 *
 * Get the runjobs flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the jobs flag is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_runJobs(PSnodes_ID_t id);

/**
 * @brief Set node's starter flag
 *
 * Set the starter flag of the node with ParaStation ID @a id to @a starter.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param starter The starter flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setIsStarter(PSnodes_ID_t id, int starter);

/**
 * @brief Get node's starter flag
 *
 * Get the starter flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the starter flag is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_isStarter(PSnodes_ID_t id);

/**
 * @brief Set node's extra IP address
 *
 * Set the extra IP address of the node with ParaStation ID @a id to @a addr.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param addr The extra IP address to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setExtraIP(PSnodes_ID_t id, in_addr_t addr);

/**
 * @brief Get node's extra IP address
 *
 * Get the extra IP address of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the extra IP address is returned; or
 * INADDR_ANY if an error occurred or the extra IP address was not set
 */
in_addr_t PSIDnodes_getExtraIP(PSnodes_ID_t id);

/**
 * @brief Set node's number of physical cores
 *
 * Set the number of physical processor cores of the node with
 * ParaStation ID @a id to @a numCores.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param numCores Number of physical processor cores to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setNumCores(PSnodes_ID_t id, short numCores);

/**
 * @brief Get node's number of physical cores
 *
 * Get the number of physical processor cores of the node with
 * ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the number of physical processor
 * cores is returned; or -1 if an error occurred
 */
short PSIDnodes_getNumCores(PSnodes_ID_t id);

/**
 * @brief Set node's number of hardware threads
 *
 * Set the number of hardware threads of the node with ParaStation ID
 * @a id to @a numThrds.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param numThrds Number of hardware threads to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setNumThrds(PSnodes_ID_t id, short numThrds);

/**
 * @brief Get node's number of hardware threads
 *
 * Get the number of virtual CPUs of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the number of hardware threads is
 * returned; or -1 if an error occurred
 */
short PSIDnodes_getNumThrds(PSnodes_ID_t id);

/**
 * @brief Set node's hardware status
 *
 * Set the hardware status of the node with ParaStation ID @a id to @a
 * hwStatus.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param hwStatus Hardware status to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setHWStatus(PSnodes_ID_t id, int hwStatus);

/**
 * @brief Get node's hardware status
 *
 * Get the hardware status of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the hardware status is returned; or
 * -1 if an error occurred
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
 * @brief Set node's user/group ID
 *
 * Set the user/group ID of type @a what on the node with ParaStation
 * ID @a id to @a guid.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param what Flag to address the type of ID to set
 *
 * @param guid The user/group ID to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid);

/**
 * @brief Add user/group ID to a node
 *
 * Add the user/group ID of type @a what with value @a guid to the node
 * with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param what Flag to address the type of ID to add
 *
 * @param guid The user/group ID to be added to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_addGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid);

/**
 * @brief Remove user/group ID from a node
 *
 * Remove the user/group ID of type @a what with value @a guid from the
 * node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param what Flag to address the type of ID to remove
 *
 * @param guid The user/group ID to be removed from this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_remGUID(PSnodes_ID_t id,
		      PSIDnodes_gu_t what, PSIDnodes_guid_t guid);

/**
 * @brief Test node's user/group ID
 *
 * Test if the user/group ID of type @a what with value @a guid is valid
 * on the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to be tested
 *
 * @param what Flag to address the type of ID to be tested
 *
 * @param guid The user/group ID to be tested on this node
 *
 * @return If the ID to be tested is valid on this node, 1 is
 * returned; otherwise 0 is given back
 */
int PSIDnodes_testGUID(PSnodes_ID_t id,
		       PSIDnodes_gu_t what, PSIDnodes_guid_t guid);

/**
 * @brief Send list of user/group IDs
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
 * @param dest Task ID of the destination task to send to
 *
 * @param what Type of GUID info to send
 *
 * @return No return value
 */
void send_GUID_OPTIONS(PStask_ID_t dest, PSIDnodes_gu_t what);

/**
 * @brief Set node's maximum number of processes
 *
 * Set the maximum number of processes of the node with ParaStation ID
 * @a id to @a procs.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param procs Maximum number of processes to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setProcs(PSnodes_ID_t id, int procs);

/**
 * @brief Get node's maximum number of processes
 *
 * Get the maximum number of processes of the node with ParaStation ID
 * @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the maximum number of processes is
 * returned; or -1 if an error occurred
 */
int PSIDnodes_getProcs(PSnodes_ID_t id);

/**
 * @brief Set node's overbook flag
 *
 * Set the overbook flag of the node with ParaStation ID @a id to @a
 * overbook.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param overbook The overbook flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setOverbook(PSnodes_ID_t id, PSnodes_overbook_t overbook);

/**
 * @brief Get node's overbook flag
 *
 * Get the overbook flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the overbook flag is returned; or
 * -1 if an error occurred
 */
PSnodes_overbook_t PSIDnodes_overbook(PSnodes_ID_t id);

/**
 * @brief Set node's exclusive flag
 *
 * Set the exclusive flag of the node with ParaStation ID @a id to @a
 * exclusive.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param exclusive The exclusive flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setExclusive(PSnodes_ID_t id, int exclusive);

/**
 * @brief Get node's exclusive flag
 *
 * Get the exclusive flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the exclusive flag is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_exclusive(PSnodes_ID_t id);

/**
 * @brief Set node's process-pinning flag
 *
 * Set the process-pinning flag of the node with ParaStation ID @a id to @a
 * pinProcs.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param pinProcs The process-pinning flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setPinProcs(PSnodes_ID_t id, int pinProcs);

/**
 * @brief Get node's process-pinning flag
 *
 * Get the process-pinning flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the process-pinning flag is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_pinProcs(PSnodes_ID_t id);

/**
 * @brief Set node's memory-binding flag
 *
 * Set the memory-binding flag of the node with ParaStation ID @a id to @a
 * bindMem.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param bindMem The memory-binding flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setBindMem(PSnodes_ID_t id, int bindMem);

/**
 * @brief Get node's memory-binding flag
 *
 * Get the memory-binding flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the memory-binding flag is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_bindMem(PSnodes_ID_t id);

/**
 * @brief Set node's GPU-binding flag
 *
 * Set the GPU-binding flag of the node with ParaStation ID @a id to @a
 * bindGPUs.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param bindGPUs The GPU-binding flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setBindGPUs(PSnodes_ID_t id, int bindGPUs);

/**
 * @brief Get node's GPU-binding flag
 *
 * Get the GPU-binding flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the GPU-binding flag is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_bindGPUs(PSnodes_ID_t id);

/**
 * @brief Set node's NIC-binding flag
 *
 * Set the NIC-binding flag of the node with ParaStation ID @a id to @a
 * bindNICs.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param bindGPUs The NIC-binding flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setBindNICs(PSnodes_ID_t id, int bindNICs);

/**
 * @brief Get node's NIC-binding flag
 *
 * Get the NIC-binding flag of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the NIC-binding flag is returned; or
 * -1 if an error occurred
 */
int PSIDnodes_bindNICs(PSnodes_ID_t id);


/**
 * @brief Clear node's CPU-map
 *
 * Clear the CPU-map of the node with ParaStation ID @a id. The
 * CPU-map is used to map virtual CPU slots given by the scheduler to
 * physical hardware threads on the local node.
 *
 * @param id ParaStation ID of the node to change
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_clearCPUMap(PSnodes_ID_t id);

/**
 * @brief Append CPU to node's CPU-map
 *
 * Append the CPU with number @a cpu to the CPU-map of the node with
 * ParaStation ID @a id. The CPU-map is used to map virtual CPU slots
 * given by the scheduler to physical hardware threads on the local node.
 *
 * @param id ParaStation ID of the node to change
 *
 * @param cpu The number of the CPU to append to the CPU-map
 *
 * @return On success, 0 is returned ; or -1 if an error occurred
 */
int PSIDnodes_appendCPUMap(PSnodes_ID_t id, short cpu);

/**
 * @brief Map CPU-slot to physical hardware thread
 *
 * Map the CPU-slot @a cpu to a physical hardware thread according CPU-map
 * of the node with ParaStation ID @a id. The CPU-map is put together by
 * calling @ref PSIDnodes_appendCPUMap() subsequently.
 *
 * @param id ParaStation ID of the CPU-map to use
 *
 * @param cpu Number of the CPU-slot to map on a physical hardware
 * thread
 *
 * @return On success, the number of the hardware thread the CPU-slot
 * is mapped to will be returned; or -1 if an error occurred
 */
short PSIDnodes_mapCPU(PSnodes_ID_t id, short cpu);

/**
 * @brief Map physical hardware thread to CPU-slot
 *
 * Map the physical hardware thread @a hwthread to a CPU-slot according CPU-map
 * of the node with ParaStation ID @a id. The CPU-map is put together by
 * calling @ref PSIDnodes_appendCPUMap() subsequently.
 *
 * @param id ParaStation ID of the CPU-map to use
 *
 * @param hwthread Number of the physical hardware thread to map on a
 * CPU-slot
 *
 * @return On success, the number of the CPU-slot the hardware thread
 * is mapped to will be returned; or -1 if an error occurred
 */
short PSIDnodes_unmapCPU(PSnodes_ID_t id, short hwthread);

/**
 * @brief Send CPU-map
 *
 * Send the CPU-map of the local daemon to @a dest within one or more
 * option messages of type PSP_OP_CPUMAP.
 *
 * @param dest Task ID of the destination task to send to
 *
 * @return No return value
 */
void send_CPUMap_OPTIONS(PStask_ID_t dest);

/**
 * @brief Set node's allowUserMap flag
 *
 * Set the allowUserMap flag of the node with ParaStation ID @a id to
 * @a allowMap. If this flag is different from 0, users are allowed to
 * influence the local mapping of their processes by providing the
 * environment __PSI_CPUMAP.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param allowMap The allowUserMap flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setAllowUserMap(PSnodes_ID_t id, int allowMap);

/**
 * @brief Get node's allowUserMap flag
 *
 * Get the allowUserMap flag of the node with ParaStation ID @a id. If
 * this flag is different from 0, users are allowed to influence the
 * local mapping of their processes by providing the environment
 * __PSI_CPUMAP.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the allowUserMap flag is returned;
 * or -1 if an error occurred
 */
int PSIDnodes_allowUserMap(PSnodes_ID_t id);

/**
 * @brief Set node's kill delay
 *
 * Set the kill delay of node @a id to @a delay. This determines the
 * number of seconds between a relative signal and the follow-up SIGKILL.
 *
 * @param id ParaStation ID of the node to change
 *
 * @param delay Kill delay in seconds to be set
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setKillDelay(PSnodes_ID_t id, int delay);

/**
 * @brief Get node's kill delay
 *
 * Get the kill delay of node @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the kill delay is returned; or -1
 * if an error occurred
 */
int PSIDnodes_killDelay(PSnodes_ID_t id);

/**
 * @brief Set node's supplementary groups flag
 *
 * Set the supplementary groups flag of the node with ParaStation ID
 * @a id to @a supplGrps.
 *
 * The supplementary groups flags marks if the node will set all the
 * user's supplementary groups while spawning a new process.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param supplGrps The supplementary groups flag to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setSupplGrps(PSnodes_ID_t id, int supplGrps);

/**
 * @brief Get node's supplementary groups flag
 *
 * Get the supplementary groups flag of the node with ParaStation ID
 * @a id.
 *
 * The supplementary groups flags marks if the node will set all the
 * user's supplementary groups while spawning a new process.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the supplementary groups flag is
 * returned; or -1 if an error occurred
 */
int PSIDnodes_supplGrps(PSnodes_ID_t id);

/**
 * @brief Set a node's maximum number of stat() tries
 *
 * Set the maximum number of tries to stat() an executable to spawn of
 * the node with ParaStation ID @a id to @a tries.
 *
 * Whenever a process is spawned, before actually starting the
 * executable via execv() stat() is applied in order to find out if
 * the executable is accessible. This gives the maximum number of
 * retries before failing this operation.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param tries Numer of tries to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setMaxStatTry(PSnodes_ID_t id, int tries);

/**
 * @brief Get node's maximum number of stat() tries
 *
 * Get the maximum number of tries to stat() an executable to spawn of
 * the node with ParaStation ID @a id.
 *
 * Whenever a process is spawned, before actually starting the
 * executable via execv(), stat() is applied in order to find out if
 * the executable is accessible. This gives the maximum number of
 * retries before failing this operation.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the maximum number of tries is
 * returned; or -1 if an error occurred
 */
int PSIDnodes_maxStatTry(PSnodes_ID_t id);

/**
 * @brief Set node's number of NUMA domains
 *
 * Set the number of NUMA domains of the node with ParaStation ID @a
 * id to @a num.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param num Number of NUMA domains to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setNumNUMADoms(PSnodes_ID_t id, short num);

/**
 * @brief Get node's number of NUMA domains
 *
 * Get number of NUMA domains of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the number of NUMA domains is
 * returned; or -1 if an error occurred
 */
short PSIDnodes_numNUMADoms(PSnodes_ID_t id);

/**
 * @brief Set node's matrix of distances between NUMA domains
 *
 * Set the matrix of distances between NUMA domains of the node with
 * ParaStation ID @a id to @a distances.
 *
 * The distance matrix is represented by an one-dimensional array of
 * of uint32_t elements.  For a node with <numNUMA> NUMA domains
 * (<numNUMA> might be determined via @ref PSIDnodes_numNUMADoms())
 * the array is of size <numNUMA>*<numNUMA>. The distance from the
 * i-th to the j-th domain is stored in element i*<numNUMA>+j.
 *
 * @a distances is expected to be allocated with @ref malloc() and
 * friends and will be freed within @ref PSIDnodes_clearMem(). If a
 * distance matrix was set before, further calls to this function will
 * @ref free() the old matrix before setting the new one. If @a
 * distances is NULL, the old matrix will be freed, too.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param distances Matrix of latency distances between NUMA domains
 * on the node to be modified
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setDistances(PSnodes_ID_t id, uint32_t *distances);

/**
 * @brief Get node's matrix of distances between NUMA domains
 *
 * Get the matrix of distances between NUMA domains of the node with
 * ParaStation ID @a id.
 *
 * The distance matrix is represented by an one-dimensional array of
 * of uint32_t elements. For a node with <numNUMA> NUMA domains
 * (<numNUMA> might be determined via @ref PSIDnodes_numNUMADoms())
 * the array is of size <numNUMA>*<numNUMA>. The distance from the
 * i-th to the j-th domain is stored in element i*<numNUMA>+j.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, a distance matrix is returned; or
 * NULL if an error occurred or no matrix was set.
 */
uint32_t * PSIDnodes_distances(PSnodes_ID_t id);

/**
 * @brief Get node's distance between NUMA domains
 *
 * Provide the distance between the NUMA domains @a from and @a to on
 * the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @param from Source NUMA domain of the distance to lookup
 *
 * @param to Destination NUMA domain of the distance to lookup
 *
 * @return Return the distances between NUMA domains or 0 in case of
 * error; the latter hints to the fact that @a id is invalid, @a from
 * or @a to is out of bound or no distance matrix is stored for the
 * node
 */
uint32_t PSIDnodes_distance(PSnodes_ID_t id, uint16_t from, uint16_t to);

/**
 * @brief Set node's hardware thread distribution over NUMA domains
 *
 * Set the distribution of hardware threads over NUMA domains of the
 * node with ParaStation ID @a id to @a CPUset.
 *
 * The distribution of hardware threads is represented by an array of
 * bit-sets -- one set per NUMA domain (@see PSIDnodes_numNUMADoms()
 * for the size of the array)). Each bit-set represents the hardware
 * threads directly attached to this NUMA domain. The bit-sets are
 * expected to have valid entries according to the number of hardware
 * threads on this node (@see PSIDnodes_numThrds()). Further entries
 * might be ignored.
 *
 * Hardware threads are numbered according the hwloc
 * conventions. I.e. psmgmt's mapping is not applied at all.
 *
 * @a CPUset is expected to be allocated with @ref malloc() and
 * friends and will be freed within @ref PSIDnodes_clearMem(). If a
 * distribution was set before, further calls to this function will
 * @ref free() the old distribution before setting the new one. If @a
 * CPUset is NULL, the old distribution will be freed, too.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param CPUset Bit-sets representing hardware threads attached to a
 * NUMA domain on the node to be modified
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setCPUSets(PSnodes_ID_t id, PSCPU_set_t *CPUset);

/**
 * @brief Get node's hardware thread distribution over NUMA domains
 *
 * Get the distribution of hardware threads over NUMA domains of the
 * node with ParaStation ID @a id.
 *
 * The distribution of hardware threads is represented by an array of
 * bit-sets -- one set per NUMA domain (@see PSIDnodes_numNUMADoms()
 * for the size of the array)). Each bit-set represents the hardware
 * threads directly attached to this NUMA domain. The bit-sets are
 * expected to have valid entries according to the number of hardware
 * threads on this node (@see PSIDnodes_numThrds()). Further entries
 * might be ignored.
 *
 * Hardware threads are numbered according the hwloc
 * conventions. I.e. psmgmt's mapping is not applied at all.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, an array of bit-sets is returned;
 * or NULL if an error occurred or no distribution was set.
 */
PSCPU_set_t * PSIDnodes_CPUSets(PSnodes_ID_t id);

/**
 * @brief Set node's number of GPUs
 *
 * Set the number of GPUs of the node with ParaStation ID @a id to @a
 * num.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param num Number of GPUs to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setNumGPUs(PSnodes_ID_t id, short num);

/**
 * @brief Get node's number of GPUs
 *
 * Get number of GPUs of the node with ParaStation ID @a id.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the number of GPUs is returned; or
 * -1 if an error occurred
 */
short PSIDnodes_numGPUs(PSnodes_ID_t id);

/**
 * @brief Set node's GPU distribution over NUMA domains
 *
 * Set the distribution of GPUs over NUMA domains of the node with
 * ParaStation ID @a id to @a GPUset.
 *
 * The distribution of GPUs is represented by an array of bit-sets --
 * one set per NUMA domain (@see PSIDnodes_numNUMADoms() for the size
 * of the array)). Each bit-set represents the GPUs directly attached
 * to this NUMA domain. The bit-sets are expected to have valid
 * entries according to the number of GPUs on this node (@see
 * PSIDnodes_numGPUs()). Further entries might be ignored.
 *
 * GPUs are numbered accoring to PCI device order as expected by CUDA.
 *
 * @a GPUset is expected to be allocated with @ref malloc() and
 * friends and will be freed within @ref PSIDnodes_clearMem(). If a
 * distribution was set before, further calls to this function will
 * @ref free() the old distribution before setting the new one. If @a
 * GPUset is NULL, the old distribution will be freed, too.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param GPUset Bit-sets representing GPUs attached to a NUMA domain
 * on the node to be modified
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setGPUSets(PSnodes_ID_t id, PSCPU_set_t *GPUset);

/**
 * @brief Get node's GPUs distribution over NUMA domains
 *
 * Get the distribution of GPUs over NUMA domains of the node with
 * ParaStation ID @a id.
 *
 * The distribution of GPUs is represented by an array of bit-sets --
 * one set per NUMA domain (@see PSIDnodes_numNUMADoms() for the size
 * of the array)). Each bit-set represents the GPUs directly attached
 * to this NUMA domain. The bit-sets are expected to have valid
 * entries according to the number of GPUs on this node (@see
 * PSIDnodes_numGPUs()). Further entries might be ignored.
 *
 * GPUs are numbered accoring to PCI device order as expected by CUDA.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, an array of bit-sets is returned;
 * or NULL if an error occurred or no distribution was set.
 */
PSCPU_set_t * PSIDnodes_GPUSets(PSnodes_ID_t id);

/**
 * @brief Set node's number of NICs
 *
 * Set the number of NICs of the node with ParaStation ID @a id to @a
 * num. This number shall reflect the amount of HPC NICs (like HCAs,
 * HFIs, etc.)  of this node.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param num Number of NICs to be set to this node
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setNumNICs(PSnodes_ID_t id, short num);

/**
 * @brief Get node's number of NICs
 *
 * Get number of NICs of the node with ParaStation ID @a id. This
 * number shall reflect the amount of HPC NICs (like HCAs, HFIs, etc.)
 * of this node.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, the number of NICs is returned; or
 * -1 if an error occurred
 */
short PSIDnodes_numNICs(PSnodes_ID_t id);

/**
 * @brief Set node's NIC distribution over NUMA domains
 *
 * Set the distribution of NICs over NUMA domains of the node with
 * ParaStation ID @a id to @a NICset.
 *
 * The distribution of NICs is represented by an array of bit-sets --
 * one set per NUMA domain (@see PSIDnodes_numNUMADoms() for the size
 * of the array)). Each bit-set represents the NICs directly attached
 * to this NUMA domain. The bit-sets are expected to have valid
 * entries according to the number of NICs on this node (@see
 * PSIDnodes_numNICs()). Further entries might be ignored.
 *
 * NICs are numbered according to hwloc order.
 *
 * @a NICset is expected to be allocated with @ref malloc() and
 * friends and will be freed within @ref PSIDnodes_clearMem(). If a
 * distribution was set before, further calls to this function will
 * @ref free() the old distribution before setting the new one. If @a
 * NICset is NULL, the old distribution will be freed, too.
 *
 * @param id ParaStation ID of the node to be modified
 *
 * @param NICset Bit-sets representing NICs attached to a NUMA domain
 * on the node to be modified
 *
 * @return On success, 0 is returned; or -1 if an error occurred
 */
int PSIDnodes_setNICSets(PSnodes_ID_t id, PSCPU_set_t *NICset);

/**
 * @brief Get node's NICs distribution over NUMA domains
 *
 * Get the distribution of NICs over NUMA domains of the node with
 * ParaStation ID @a id.
 *
 * The distribution of NICs is represented by an array of bit-sets --
 * one set per NUMA domain (@see PSIDnodes_numNUMADoms() for the size
 * of the array)). Each bit-set represents the NICs directly attached
 * to this NUMA domain. The bit-sets are expected to have valid
 * entries according to the number of NICs on this node (@see
 * PSIDnodes_numNICs()). Further entries might be ignored.
 *
 * NICs are numbered according to hwloc order.
 *
 * @param id ParaStation ID of the node to look up
 *
 * @return If the node was found, an array of bit-sets is returned;
 * or NULL if an error occurred or no distribution was set
 */
PSCPU_set_t * PSIDnodes_NICSets(PSnodes_ID_t id);


/**
 * @brief Memory cleanup
 *
 * Cleanup all memory currently used by the module. It will very
 * aggressively free all allocated memory most likely destroying
 * all the module's functionality.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value
 */
void PSIDnodes_clearMem(void);

#endif  /* __PSIDNODES_H */
