/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * psiinfo: Functions for information retrieving from ParaStation
 * daemon
 */
#ifndef __PSIINFO_H
#define __PSIINFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "psnodes.h"
#include "pstask.h"
#include "psprotocol.h"

/**
 * @brief Retrieve integer information.
 *
 * Retrieve the integer information of type @a what from node @a node
 * and store it to @a val. Depending on the type of information
 * requested, @a param points to further parameter(s) needed in order
 * to answer the request.
 *
 * The possible values for @a what are:
 *
 * - PSP_INFO_NROFNODES request the total number of nodes within the
 * ParaStation cluster. No further parameters needed.
 *
 * - PSP_INFO_HWNUM request the total number of hardware types
 * configured within the ParaStation configuration file. No further
 * parameters needed.
 *
 * - PSP_INFO_HWINDEX request the index from a hardware's name. @a
 * param points to a '\\0' terminated character array containing the
 * name.
 *
 * - PSP_INFO_TASKSIZE request the total number of processes within
 * the actual task. No further parameters needed.
 *
 * - PSP_INFO_TASKRANK request the rank of the local process within
 * the parallel task. The result might be a positiv number or -1,
 * where the latter denotes the process to be the logger process. No
 * further parameters needed.
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param what The type of information to request as described above
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information
 *
 * @param val Pointer to the datum reserved for the result
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoInt(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		int32_t *val, bool verbose);

/**
 * @brief Retrieve 64-bit integer information.
 *
 * Retrieve the 64-bit integer information of type @a what from node
 * @a node and store it to @a val. Depending on the type of
 * information requested, @a param points to further parameter(s)
 * needed in order to answer the request.
 *
 * The possible values for @a what are:
 *
 * - PSP_INFO_STARTTIME request the start-time of the node. No further
 *   parameters needed.
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param what The type of information to request as described above
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information
 *
 * @param val Pointer to the datum reserved for the result
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoInt64(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		  int64_t *val, bool verbose);

/**
 * @brief Retrieve unsigned integer information.
 *
 * Retrieve the unsigned integer information of type @a what from node
 * @a node and store it to @a val. Depending on the type of
 * information requested, @a param points to further parameter(s)
 * needed in order to answer the request.
 *
 * The possible values for @a what are:
 *
 * - PSP_INFO_NODE request a nodes IP address from its ParaStation
 * ID. @a param points to a datum of type PSnodes_ID_t containing the
 * ParaStation ID. Although @a val points to a datum of type @c
 * int32_t, the value returned has to be interpreted as a @c uint32_t.
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param what The type of information to request as described above
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information
 *
 * @param val Pointer to the datum reserved for the result
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoUInt(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		 uint32_t *val, bool verbose);

/**
 * @brief Retrieve string information.
 *
 * Retrieve the string information of type @a what from node @a node
 * and store it to @a string, which is of size @a size. Depending on
 * the type of information requested, @a param points to further
 * parameter(s) needed in order to answer the request.
 *
 * The possible values for @a what are:
 *
 * -PSP_INFO_COUNTHEADER request the header line of the counter output
 * for a special hardware on the designated node. @a param points to
 * the hardware index of the *hardware wanted. The hardware index has
 * to be stored within a @c *int32_t datum.
 *
 * -PSP_INFO_COUNTSTATUS request the actual status line of the counter
 * output for a special hardware on the designated node. @a param
 * points to the hardware index of the *hardware wanted. The hardware
 * index has to be stored within a @c *int32_t datum.
 *
 * - PSP_INFO_HWNAME request the name of a special hardware as
 * configured within the ParaStation configuration file on the
 * designated node. @a param points to the hardware index of the
 * *hardware wanted. The hardware index has to be stored within a @c
 * *int32_t datum.
 *
 * - PSP_INFO_RDPSTATUS request the status of the RDP connection from
 * node @a node to the one designated within the extra parameter. Thus
 * @a param has to point to a datum of type @c PSnodes_ID_t holding
 * this connected node.
 *
 * - PSP_INFO_RDPCONNSTATUS request the info on RDP connection from
 * node @a node to the one designated within the extra parameter. Thus
 * @a param has to point to a datum of type @c PSnodes_ID_t holding
 * this connected node.
 *
 * - PSP_INFO_MCASTSTATUS request the status of the MCast connection
 * from node @a node to the one designated within the extra
 * parameter. Thus @a param has to point to a datum of type @c
 * PSnodes_ID_t holding this connected node.
 *
 * - PSP_INFO_INSTDIR request the ParaStation installation directory
 * as configured within the configuration file. No further parameters
 * needed.
 *
 * - PSP_INFO_CMDLINE requests the command line of a process running
 * on node @a node under the control of the ParaStation daemon. The
 * requested process is given within the extra parameter. Thus @a
 * param has to point to a datum of type @c pid_t holding the process
 * ID. The maximum size of the returned commandline is about 8000
 * byte.
 *
 * - PSP_INFO_STARTUPSCRIPT request the name of the script run during
 * startup of the daemon in order to test local features and stati
 * used to decide, if the local node is capable to take part in the
 * cluster-action.
 *
 * - PSP_INFO_NODEUPSCRIPT request the name of the script called by
 * the master daemon whenever a node becomes active in the concert of
 * daemons within a cluster.
 *
 * - PSP_INFO_NODEDOWNSCRIPT request the name of the script called by
 * the master daemon whenever a node disappeares from the concert of
 * daemons within a cluster.
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param what The type of information to request as described above
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information
 *
 * @param string Pointer to the character array reserved for the
 * result
 *
 * @param size The size of @a string
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoString(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		   char *string, size_t size, bool verbose);

/**
 * @brief Retrieve task ID.
 *
 * Retrieve the task ID information of type @a what from node @a node
 * and store it to @a tid. Depending on the type of information
 * requested, @a param points to further parameter(s) needed in order
 * to answer the request.
 *
 * The possible values for @a what are:
 *
 * - PSP_INFO_PARENTTID request the parent task ID of the actual task,
 * if any. If a parameter of is provided, not the parent task ID of
 * the actual task but of the parameter's parent is returned. It has
 * to be of type PStask_ID_t.
 *
 * - PSP_INFO_LOGGERTID request the logger task ID of the actual task,
 * if any. If a parameter of is provided, not the logger task ID of
 * the actual task but of the parameter's logger is returned. It has
 * to be of type PStask_ID_t.
 *
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param what The type of information to request as described above
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information
 *
 * @param tid Pointer to a datum reserved for the result
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoTaskID(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		   PStask_ID_t *tid, bool verbose);

/**
 * @brief Retrieve ParaStation ID.
 *
 * Retrieve the ParaStation ID information of type @a what from node @a node
 * and store it to @a tid. Depending on the type of information
 * requested, @a param points to further parameter(s) needed in order
 * to answer the request.
 *
 * The possible values for @a what are:
 *
 * - PSP_INFO_RANKID request the ParaStation ID of the node on which
 * the process with a destinct rank within the actual task will be
 * running on. @a param has to point to a @c int32_t datum holding
 * the requested rank.
 *
 * -PSP_INFO_HOST request the ParaStation ID corresponding to an IP
 * address within the cluster. @a param has to point to the IP address
 * of interest stored within a @c uint32_t datum.
 *
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param what The type of information to request as described above
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information
 *
 * @param nid Pointer to a datum reserved for the result
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoNodeID(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		   PSnodes_ID_t *nid, bool verbose);

/**
 * @brief Retrieve info list
 *
 * Retrieve a list of information of type @a what from node @a node
 * and store it to the buffer @a buf of size @a size. Depending on the
 * type of information requested, @a param points to further
 * parameter(s) needed in order to answer the request.
 *
 * The possible values for @a what are:
 *
 * - PSP_INFO_LIST_HOSTSTATUS requests the status of all nodes within
 * the ParaStation cluster. A node's status is provided as a @c char
 * having the value 0 if the node is down or 1 if it's up. No further
 * parameters needed.
 *
 * - PSP_INFO_LIST_VIRTCPUS requests the number of virtual CPUs of all
 * nodes within the ParaStation cluster. The number of virtual CPUs
 * includes multiple counts per CPU if Hyper-Threading-Technology on
 * Intel IA32 CPU is enabled. A node's number of virtual CPUs is
 * provided as a @c uint16_t. No further parameters needed.
 *
 * - PSP_INFO_LIST_PHYSCPUS requests the number of physical CPUs of
 * all nodes within the ParaStation cluster. The number of physical
 * CPUs excludes multiple counts per CPU even if
 * Hyper-Threading-Technology on Intel IA32 CPU is enabled. This is
 * mainly needed for license control. A node's number of physical CPUs
 * is provided as a @c uint16_t. No further parameters needed.
 *
 * - PSP_INFO_LIST_HWSTATUS requests the hardware status of all nodes
 * within the ParaStation cluster. A node's hardware status is
 * provided as a @c uint32_t, where the bit 1<<index represents the
 * hardware with a certain index to be present. No further parameters
 * needed.
 *
 * - PSP_INFO_LIST_LOAD requests the load of all nodes within the
 * ParaStation cluster. A node's load is provided as a @c float[3]
 * array containing the 1 minute, 5 minute and 15 minute averages. No
 * further parameters needed.
 *
 * - PSP_INFO_LIST_MEMORY requests the memory of all nodes within the
 * ParaStation cluster. A node's memory is provided as a @c
 * uint64_t[2] array containing the total and free memory. No further
 * parameters needed.
 *
 * - PSP_INFO_LIST_ALLJOBS requests the total number of jobs of all
 * nodes within the ParaStation cluster. A node's total number of jobs
 * is provided as a @c uint16_t. No further parameters needed.
 *
 * - PSP_INFO_LIST_NORMJOBS requests the number of normal jobs of all
 * nodes within the ParaStation cluster. Normal jobs are those with
 * task group TG_ANY, i.e. in contrast to the total number of jobs
 * this one excludes tasks representing admin, logger, forwared
 * etc. processes. A node's number of normal jobs is provided as a @c
 * uint16_t. No further parameters needed.
 *
 * - PSP_INFO_LIST_ALLOCJOBS requests the total number of allocated
 * jobs of all nodes within the ParaStation cluster. Allocated jobs
 * are job slots assigned to a task during a partition
 * request. I.e. the task is allowed to start this number of jobs on
 * the according node. A node's allocated number of jobs is provided
 * as a @c uint16_t. No further parameters needed.
 *
 * - PSP_INFO_LIST_EXCLUSIVE requests the exclusive flag of all nodes
 * within the ParaStation cluster. Nodes are marked to be exclusive if
 * the task running on it has requested exclusive access to this node
 * and this was granted by the resource manager on the master node. On
 * nodes marked as exclusive no other parallel applications are
 * allowed to start processes on. No further parameters needed.
 *
 * - PSP_INFO_LIST_PARTITION requests a list of all process slots
 * associated to the partition either of the requesting process or of
 * any other process given in @a param. The process slots are given
 * back as a list of entries of type PSnodes_ID_t. No further
 * parameters needed but param might point to a PStask_ID_t in order
 * to get info on some foreign tasks.
 *
 * - PSP_INFO_LIST_RESNODES requests a list of all nodes belonging to
 * a given reservation. @a param has to point to the corresponding
 * reservation ID with type PSrsrvtn_ID_t. The list of nodes is given
 * back as a list of entries of type PSnodes_ID_t.
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param what The type of information to request as described above
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information
 *
 * @param buf Buffer to store the requested list to
 *
 * @param size Actual size of the buffer @a buf
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, the number of bytes received and stored within
 * buf is returned. Otherwise -1 is returned and errno is set
 * appropriately.
 *
 * @see errno(3)
 */
int PSI_infoList(PSnodes_ID_t node, PSP_Info_t what, const void *param,
		 void *buf, size_t size, bool verbose);

/**
 * @brief Retrieve queue of information
 *
 * Trigger a queue of information of type @a what from node @a node.
 * The actual information items can be retrieved via subsequent calls
 * to @ref PSI_infoQueueNext(). As long as such answers are pending no
 * further calls to any other PSI_info-function is allowed.
 *
 * Depending on the type of information requested, @a param
 * points to further parameter(s) needed in order to answer the
 * request.
 *
 * The possible values for @a what are:
 *
 * - PSP_INFO_QUEUE_ALLTASK requests a queue of all tasks on the
 * designated node @a node. All the information about the single tasks
 * is returned within @ref PSP_taskInfo_t structures. No further
 * parameters needed.
 *
 * - PSP_INFO_QUEUE_NORMTASK requests a list of all normal tasks on
 * the designated node @a node. Normal tasks are those with task group
 * TG_ANY, i.e. in contrast to all tasks this one excludes tasks
 * representing admin, logger, forwared etc. processes. All the
 * information about the single tasks is returned within a @ref
 * PSP_taskInfo_t structures. No further parameters needed.
 *
 * - PSP_INFO_QUEUE_PARTITION requests a list of all partitions
 * (associated to actual jobs). The request will be automatically
 * forwarded to the current master daemon, the @a node parameter will
 * be ignored. As an answer for each partition a message containing
 * the following information is provided:
 *   - The task ID of the partition's root process.
 *   - A flag field of type @ref PSpart_list_t marking the partition as
 *     pending, running or suspended.
 *   - The encoded request to be decoded vi @ref PSpart_decodeReq().
 * This one is followed by one or more messages providing the actual
 * nodes building the partition if requested.
 * The parameter is of type @ref PSpart_list_t. Here only special
 * types of partitions (pending, running or suspended) might
 * requested. Furthermore it can be flaged of the list of scheduled
 * nodes should be provided.
 *
 * - PSP_INFO_QUEUE_PLUGINS requests a list of all plugins currently
 * loaded on the designated node @a node. All information about a
 * single plugin (i.e. name, version, and triggering plugins) is given
 * back in a character string. The size is limited by the underlying
 * communication protocol, i.e. the size of PSP_BufferMsg_t's
 * buffer. No further parameters needed.
 *
 * @param node The ParaStation ID of the node to ask.
 *
 * @param what The type of information to request as described above.
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information.
 *
 * @return On success, the 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoQueueReq(PSnodes_ID_t node, PSP_Info_t what, const void *param);

/**
 * @brief Retrieve (more) information from queue
 *
 * Retrieve (more) information of type @a what from the queue
 * triggered by @ref PSI_infoQueueReq() and store it to the buffer @a
 * buf of size @a size.
 *
 * Depending on the return value more information might be
 * available. It can be retrieved via subsequent calls to this
 * function. As long as such answers are pending no
 * further calls to any other PSI_info-function is allowed.
 *
 * The possible values for @a what are:
 *
 * - PSP_INFO_QUEUE_ALLTASK receives information concerning a single
 * task within a @ref PSP_taskInfo_t structure from node @a node.
 *
 * - PSP_INFO_QUEUE_NORMTASK receives information concerning a normal
 * task within a @ref PSP_taskInfo_t structure from node @a
 * node. Normal tasks are those with task group TG_ANY, i.e. in
 * contrast to all tasks this one excludes tasks representing admin,
 * logger, forwared etc. processes.
 *
 * - PSP_INFO_QUEUE_PARTITION requests a list of all partitions
 * (associated to actual jobs). The request will be automatically
 * forwarded to the current master daemon, the @a node parameter will
 * be ignored. As an answer for each partition a message containing
 * the following information is provided:
 *   - The task ID of the partition's root process.
 *   - A flag field of type @ref PSpart_list_t marking the partition as
 *     pending, running or suspended.
 *   - The encoded request to be decoded vi @ref PSpart_decodeReq().
 * This one is followed by one or more messages providing the actual
 * nodes building the partition if requested.
 *
 * - PSP_INFO_QUEUE_PLUGINS requests a list of all plugins currently
 * loaded on the designated node @a node. All information about a
 * single plugin (i.e. name, version, and triggering plugins) is given
 * back in a character string. The size is limited by the underlying
 * communication protocol, i.e. the size of PSP_BufferMsg_t's
 * buffer.
 *
 * @param what The type of information to request as described above
 *
 * @param buf Buffer to store the requested list to
 *
 * @param size Actual size of the buffer @a buf
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, the number of bytes received and stored within
 * @a buf is returned. If no more information is available, 0 is
 * returned. Otherwise -1 is returned and errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoQueueNext(PSP_Info_t what, void *buf, size_t size, bool verbose);

/**
 * @brief Request options
 *
 * Request @a num options from node @a node. The options to request
 * are stored within the array @a option, the corresponding values
 * will be returned within the @a value array. Up to @ref
 * DDOptionMsgMax options might be requested within one call to this
 * function.
 *
 * Upon return also the options within the array @a option will be
 * updated to the actual options received. Thus the type of options
 * returned by the daemon can be controlled. Furthermore unknown
 * options to the daemon (resulting in a PSP_OP_UNKNOWN returned) can
 * be controlled.
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param num The number of option to request
 *
 * @param option Array of size @a num holding the actual options to
 * request
 *
 * @param value Array of size @a num the requested options are stored to
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, the number of options received and stored
 * within value is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoOption(PSnodes_ID_t node, int num,
		   PSP_Option_t option[], PSP_Optval_t value[], bool verbose);

/**
 * @brief Retrieve list of options
 *
 * Trigger a list of options of type @a option from node @a node.
 * The actual information items can be retrieved via subsequent calls
 * to @ref PSI_infoOptionListNext(). As long as such answers are pending no
 * further calls to any other PSI_info-function is allowed.
 *
 * The possible values for @a option are:
 *
 * - PSP_OP_ACCT requests a series of options describing the
 * accounting tasks as known to node @a node. The corresponding option
 * values of the answers will contain task IDs.
 *
 * - PSP_OP_USER requests a series of options describing the
 * users allowed to run processes on node @a node. The corresponding option
 * values of the answers will contain UIDs.
 *
 * - PSP_OP_GROUP requests a series of options describing the
 * groups allowed to run processes on node @a node. The corresponding option
 * values of the answers will contain GIDs.
 *
 * - PSP_OP_ADMUSER requests a series of options describing the users
 * allowed to start administration processes from node @a node. The
 * corresponding option values of the answers will contain UIDs.
 *
 * - PSP_OP_ADMGROUP requests a series of options describing the groups
 * allowed to start administration processes from node @a node. The
 * corresponding option values of the answers will contain GIDs.
 *
 *
 * @param node The ParaStation ID of the node to ask
 *
 * @param option The type of information to request as described above
 *
 * @param param Pointer to further parameters needed in order to
 * retrieve the requested information
 *
 * @return On success, the 0 is returned. Otherwise -1 is returned and
 * errno is set appropriately.
 */
int PSI_infoOptionList(PSnodes_ID_t node, PSP_Option_t option);


/**
 * @brief Retrieve (more) information from option list
 *
 * Retrieve (more) information from the option list triggered by @ref
 * PSI_infoOptionList() and store at most @a num entries to the buffer
 * @a opts.
 *
 * Depending on the value within @a opts more information might be
 * available. It can be retrieved via subsequent calls to this
 * function. As long as such answers are pending no further calls to
 * any other PSI_info-function is allowed.
 *
 * The possible return values of the option-part of @a opts are:
 *
 * - PSP_OP_ACCT describes an accounting tasks as known to the
 * requested node. The corresponding value-part of @a opts contains a
 * task ID.
 *
 * - PSP_OP_USER describes an user allowed to run processes on the
 * requested node. The corresponding value-part of @a opts contains a
 * UID.
 *
 * - PSP_OP_GROUP describes a group allowed to run processes on the
 * requested node. The corresponding value-part of @a opts contains a
 * GID.
 *
 * - PSP_OP_ADMUSER describes an user allowed to start administation
 * processes from the requested node. The corresponding value-part of
 * @a opts contains a UID.
 *
 * - PSP_OP_ADMGROUP describes a group allowed to start administation
 * processes from the requested node. The corresponding value-part of
 * @a opts contains a GID.
 *
 * - PSP_OP_LISTEND flags the end of the option list. No further
 * option messages connected with the @ref PSI_infoOptionList() call
 * will be received.
 *
 *
 * @param opts Buffer to store the option list to
 *
 * @param num Actual number of entries within the buffer @a optsbuf
 *
 * @param verbose Flag to be more verbose in the case something went
 * wrong during information retrival
 *
 * @return On success, the number of options received and stored
 * within @a opts is returned. Otherwise -1 is returned and errno is
 * set appropriately.
 *
 * @see errno(3)
 */
int PSI_infoOptionListNext(DDOption_t opts[], int num, bool verbose);

/**
 * @brief Get hardware name.
 *
 * Create a string holding the name(s) of the hardware types set
 * within @a hwType as defined within the ParaStation configuration
 * file. @a hwType is provided as a bitset, where the bit 1<<index
 * represents the hardware with a certain index to be present.
 *
 * If @a hwType is 0, i.e. no hardware at all is present, 'none' is
 * returned.
 *
 * @param hwType The hardware type for which the actual name should be
 * requested.
 *
 * @return Upon success, i.e. if all hardware names could be resolved,
 * a pointer to a space separated static string of hardware names is
 * returned.  Successiv calls to this function might change this
 * string, i.e. don't count on the content of this string after
 * further calls to this function. If an error occurred within
 * information retrival, NULL is returned.
 */
char *PSI_printHWType(unsigned int hwType);

/**
 * @brief Retrieve the ParaStation ID for a given host.
 *
 * Resolve the ParaStation ID of a given node via its hostname @a
 * host. For this, the node's IP-address is resolved via the DNS and
 * matched to the corresponding ParaStation ID by asking the local
 * daemon.
 *
 * @param host The name of the host to retrieve the ParaStation ID for.
 *
 * @return If resolving was successful, the corresponding ID is
 * returned. Or -1, if an error occurred.
 */
PSnodes_ID_t PSI_resolveNodeID(const char *host);

#endif /* __PSIINFO_H */
