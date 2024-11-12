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
 * @file Basic enumerations for partition creation and reservation.
 */
#ifndef __PSPARTITION_H
#define __PSPARTITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "list_t.h"

#include "pscpu.h"
#include "psnodes.h"
#include "psprotocol.h"
#include "pssenddb_t.h"
#include "pstaskid.h"

/**
 * Stores information of the partition's resource slots. This is
 * mainly used on the master and for transport of slots.
 */
typedef struct {
    PSnodes_ID_t node;        /**< Node the slot belongs to */
    PSCPU_set_t CPUset;       /**< Set of CPUs the slot occupies */
} PSpart_slot_t;

/**
 * Stores information on the actual use of HW-threads within a partition
 */
typedef struct {
    PSnodes_ID_t node;        /**< Node the slot belongs to */
    int16_t id;               /**< The logical number of this HW-thread */
    int16_t timesUsed;        /**< The number of SW-threads assigned */
} PSpart_HWThread_t;

/** Various sort modes for partition creation. */
typedef enum {
    PART_SORT_NONE,           /**< No sorting at all. */
    PART_SORT_PROC,           /**< Sort by number of ParaStation processes. */
    PART_SORT_LOAD_1,         /**< Sort by 1 minute load average. */
    PART_SORT_LOAD_5,         /**< Sort by 5 minute load average. */
    PART_SORT_LOAD_15,        /**< Sort by 15 minute load average. */
    PART_SORT_PROCLOAD,       /**< Sort by load_1 + proc. */
    PART_SORT_DEFAULT,        /**< Use the daemon's default mode. */
    PART_SORT_UNKNOWN = 0x0ff /**< Dummy for unknown sort mode. */
} PSpart_sort_t;

/** Options possibly bound to a partition. */
typedef enum {
    PART_OPT_NODEFIRST = 0x0001, /**< Place consecutive processes on different
				    nodes, if possible. Usually consecutive
				    processes are placed on the same node. */
    PART_OPT_EXCLUSIVE = 0x0002, /**< Only get exclusive nodes. I.e. no further
				    processes are allowed on that node. */
    PART_OPT_OVERBOOK  = 0x0004, /**< Allow more than one process per
				    node. This induces @ref PART_OPT_EXCLUSIVE
				    implicitly. */
    PART_OPT_WAIT      = 0x0008, /**< If not enough nodes are available, wait
				    for them (batch mode). */
    PART_OPT_EXACT     = 0x0010, /**< Node-list comes from a batch-system */
    PART_OPT_RESPORTS  = 0x0020, /**< Obsolete */
    PART_OPT_DEFAULT   = 0x0040, /**< Use the job's default options */
    PART_OPT_DYNAMIC   = 0x0080, /**< Include dynamic resources, too. This
				    will require interaction with an external
				    resource manager. */
    PART_OPT_FULL_LIST = 0x0100, /**< Create partition from the whole list of
				    nodes/hosts (or hostfile) independent of
				    actual requirement. */
    PART_OPT_DUMMY     = 0x0200, /**< Handle partition as a placeholder: no
				    resources will be allocated, just fetch a
				    range of ranks and a reservation ID */
} PSpart_option_t;

/** Options possible for PSP_INFO_QUEUE_PARTITION requests */
typedef enum {
    PART_LIST_PEND  = 0x0001,  /**< Send pending requests */
    PART_LIST_SUSP  = 0x0002,  /**< Send suspended jobs */
    PART_LIST_RUN   = 0x0004,  /**< Send running jobs */
    PART_LIST_NODES = 0x0008,  /**< Also send attached node-lists */
} PSpart_list_t;

/**
 * Structure describing a actual request to create a partition. This
 * is also used to store active partitions at the master daemon.
 *
 * Members marked with C are (un)packed by
 * PSpart_addToMsg()/PSpart_decodeReq()
 */
typedef struct {
    list_t next;                   /**< used to put into some request-lists. */
    PStask_ID_t tid;               /**< TaskID of the requesting process */
    /*C*/ uint32_t size;           /**< Requested size of the partition */
    /*C*/ uint32_t hwType;         /**< Hardware type of the requested nodes */
    /*C*/ uid_t uid;               /**< UID of the requesting process */
    /*C*/ gid_t gid;               /**< GID of the requesting process */
    /*C*/ PSpart_sort_t sort;      /**< Sort mode for sorting candidates */
    /*C*/ PSpart_option_t options; /**< Options steering partition creation */
    /*C*/ uint32_t priority;       /**< Priority of the parallel task */
    /*C*/ uint32_t num;            /**< Number of elements in @ref nodes */
    /*C*/ uint16_t tpp;            /**< Threads per process requested */
    /*C*/ time_t start;            /**< starttime in PSP_INFO_QUEUE_PARTITION */
    unsigned int sizeGot;          /**< Number of slots currently received */
    unsigned int sizeExpected;     /**< Number of slots expected */
    PSnodes_ID_t *nodes;           /**< List of partition candidates */
    PSpart_slot_t *slots;          /**< Partition (list of slots) associated */
    bool deleted;                  /**< Flag to mark request for deletion */
    bool suspended;                /**< Corresponding task is suspended */
    bool freed;                    /**< Resources are freed temporarily */
} PSpart_request_t;

/**
 * @brief Create a new partition request structure
 *
 * A new partition request structure is created and initialized via
 * @ref PSpart_initReq(). It may be removed with @ref PSpart_delReq().
 * The memory required in order to store the request is allocated via
 * malloc().
 *
 * @return On success, a pointer to the newly created partition request
 * structure is returned; or NULL otherwise
 *
 * @see PSpart_initReq(), PSpart_delReq()
 */
PSpart_request_t *PSpart_newReq(void);

/**
 * @brief Initialize a partition request structure
 *
 * Initialize the partition request structure @a request, i.e. set all
 * member to default values.
 *
 * @param request Pointer to the partition request structure to be
 * initialized
 *
 * @return No return value.
 */
void PSpart_initReq(PSpart_request_t *request);

/**
 * @brief Reinitialize a partition request structure
 *
 * Reinitialize the partition request structure @a request that was
 * previously used. All allocated memory is removed, all pointers are
 * reset to NULL, etc.
 *
 * @param request Pointer to the partition request structure to be
 * reinitialized
 *
 * @return No return value
 */
void PSpart_reinitReq(PSpart_request_t *request);

/**
 * @brief Delete a partition request structure
 *
 * Delete the partition request structure @a request created via @ref
 * PSpart_newReq(). First the partition request is cleaned up by @ref
 * PSpart_reinitReq(), i.e. all allocated memory is removed. Afterward
 * the partition request itself is removed.
 *
 * @param request Pointer to the partition request structure to be deleted
 *
 * @return On success, true is returned, or false otherwise
 */
bool PSpart_delReq(PSpart_request_t *request);

/**
 * @brief Clear list of partition request structures
 *
 * Remove all partition request from the list @a queue and delete the
 * dequeued partition request structures.
 *
 * @param queue List of partition requests to clean up
 *
 * @return No return value
 */
void PSpart_clrQueue(list_t *queue);

/**
 * @brief Encode a partition request structure.
 *
 * Encode the partition request structure @a request and store it into
 * the payload-buffer @ref buf of the message @a msg of type @ref
 * DDBufferMsg_t. The data are placed with an offset defined by the
 * @ref len member of @a msg's header. Upon success, i.e. if the data
 * fitted into the remainder of @a msg's buffer, the @ref len entry of
 * @a msg's header is updated and @a true is returned. Otherwise, an
 * error-message is put out and @a false is returned. In the latter
 * case the len member of @a msg is not updated.
 *
 * @param msg Message to be modified
 *
 * @param request The partition request structure to encode
 *
 * @return On success @a true is returned. Or @a false if the buffer
 * is too small in order to encode the whole partition request.
 *
 * @see PSpart_decodeReq()
 */
bool PSpart_encodeReq(DDBufferMsg_t *msg, PSpart_request_t* request);

/**
 * @brief Decode a partition request structure.
 *
 * Decode a partition request structure encoded by @ref
 * PSpart_encodeReq() and stored within @a buffer and write it to the
 * partition request structure @a request is pointing to.
 *
 * @param buffer The buffer the encoded partition request structure is
 * stored to.
 *
 * @param request The partition request structure to write to.
 *
 * @return The number of chars within @a buffer used in order to
 * decode the partition request structure.
 */
size_t PSpart_decodeReqOld(char *buffer, PSpart_request_t *request);

/**
 * @brief Send partition request structure
 *
 * Send partition request structure @a req via the serialization layer
 * utilizing the data buffer @a msg. Only the core members of @a req
 * will be sent. Further information like the nodelist has to be
 * handled explicitly outside this function.
 *
 * @a msg has to be setup before in order to provide the message type,
 * the destination address, etc.
 *
 * @param req Partition request structure to be sent
 *
 * @param msg Data buffer used for sending
 *
 * @return On success true is returned; or false in case of error
 */
bool PSpart_addToMsg(PSpart_request_t *req, PS_SendDB_t *msg);

/**
 * @brief Decode partition request structure
 *
 * Decode a partition request structure sent via @ref PStpart_addToMsg()
 * and received as a data blob of size @a len provided in @a data. The
 * decoded data is written to the partition request structure @a req
 * is pointing to.
 *
 * Before adding any data to @a req the structure will be cleaned
 * up. Thus, any information contained will get lost.
 *
 * @param data Data blob holding the encoded partition request structure
 *
 * @param len Size of @a data
 *
 * @param req Partition request structure to write to
 *
 * @return On success return true is returned or false on error
 */
bool PSpart_decodeReq(void *data, size_t len, PSpart_request_t *req);

/**
 * @brief Print a partition request in a string.
 *
 * Print the description of the partition request @a request into the
 * character array @a txt. At most @a size characters will be written
 * into the character array @a txt.
 *
 * @param txt Character array to print the partition request into.
 *
 * @param size Size of the character array @a txt.
 *
 * @param request Pointer to the partition request to print.
 *
 * @param protoVersion Protocol version used for encoding
 *
 * @return No return value.
 */
void PSpart_snprintf(char *txt, size_t size, PSpart_request_t *request);

#endif  /* __PSPARTITION_H */
