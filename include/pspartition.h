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
 * Basic enumerations for partition creation and reservation.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSPARTITION_H
#define __PSPARTITION_H

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** Various sort modes for partition creation. */
typedef enum {
    PART_SORT_NONE,           /**< No sorting at all. */
    PART_SORT_PROC,           /**< Sort by number of ParaStation processes. */
    PART_SORT_LOAD_1,         /**< Sort by 1 minute load average. */
    PART_SORT_LOAD_5,         /**< Sort by 5 minute load average. */
    PART_SORT_LOAD_15,        /**< Sort by 15 minute load average. */
    PART_SORT_PROCLOAD,       /**< Sort by load_1 + proc. */
    PART_SORT_UNKNOWN = 0x0ff /**< Dummy for unknown sort mode. */
} PSpart_sort_t;

/** Options possibly bound to a partition. */
typedef enum {
    PART_OPT_NODEFIRST = 0x0001, /**< Place consecutive processes on different
				    nodes, if possible. Usually consecutive
				    processes are placed on the same node. */
    PART_OPT_EXCLUSIVE = 0x0002, /**< Only get exclusive nodes. I.e. no further
				    processes are allowed on that node. */
    PART_OPT_OVERBOOK = 0x0004,  /**< Allow more than one process per
				    node. This induces @ref PART_OPT_EXCLUSIVE
				    implicitely. */
    PART_OPT_WAIT = 0x0008,      /**< If not enough nodes are available, wait
				    for them (batch mode). */
} PSpart_option_t;

/**
 * Structure describing a actual request to create a partition
 */
/*
 * Members marked with C are (un)packed by
 * PSpart_encodeReq()/PSpart_decodeReq()
 */
typedef struct request{
    struct request *next;          /**< Pointer to the next request */
    PStask_ID_t tid;               /**< TaskID of the requesting process */
    uint32_t size;           /*C*/ /**< Requested size of the partition */
    uint32_t hwType;         /*C*/ /**< Hardware type of the requested nodes */
    uid_t uid;               /*C*/ /**< UID of the requesting process */
    gid_t gid;               /*C*/ /**< GID of the requesting process */
    PSpart_sort_t sort;      /*C*/ /**< Sort mode for sorting candidates */
    PSpart_option_t options; /*C*/ /**< Options steering partition creation */
    uint32_t priority;       /*C*/ /**< Priority of the parallel task */
    int32_t num;             /*C*/ /**< Number of nodes within request */
    int numGot;                    /**< Number of nodes currently received */
    PSnodes_ID_t *nodes;           /**< List of partition candidates */
    char deleted;                  /**< Flag to mark request for deletion */
    char suspended;                /**< Corresponding task is suspended */
    char freed;                    /**< Resources are freed temporarily */
} PSpart_request_t;

/**
 * @brief Create a new partition request structure.
 *
 * A new partition request structure is created and initialized via
 * @ref PSpart_initReq(). It may be removed with @ref PSpart_delReq().
 * The memory needed in order to store the request is allocated via
 * malloc().
 *
 * @return On success, a pointer to the newly created partition request
 * structure is returned, or NULL otherwise.
 *
 * @see PSpart_initReq(), PSpart_delReq()
 */
PSpart_request_t *PSpart_newReq(void);

/**
 * @brief Initialize a partition request structure.
 *
 * Initialize the partition request structure @a request, i.e. set all
 * member to default values.
 *
 * @param request Pointer to the partition request structure to be
 * initialized.
 *
 * @return No return value.
 */
void PSpart_initReq(PSpart_request_t *request);

/**
 * @brief Reinitialize a partition request structure.
 *
 * Reinitialize the partition request structure @a request that was
 * previously used. All allocated strings and signallists shall be
 * removed, all links are reset to NULL.
 *
 * @param request Pointer to the partition request structure to be
 * reinitialized.
 *
 * @return No return value.
 */
void PSpart_reinitReq(PSpart_request_t *request);

/**
 * @brief Delete a partition request structure.
 *
 * Delete the partition request structure @a request created via @ref
 * PSpart_newReq(). First the partition request is cleaned up by @ref
 * PSpart_reinitReq(), i.e. all allocated strings and signallists are
 * removed. Afterward the partition request itself is removed.
 *
 * @param request Pointer to the partition request structure to be deleted.
 *
 * @return On success, 1 is returned, or 0 otherwise.
 */
int PSpart_delReq(PSpart_request_t *request);

/**
 * @brief Encode a partition request structure.
 *
 * Encode the partition request structure @a request into the the
 * buffer @a buffer of size @a size. This enables the partition
 * request to be sent to a remote node where it can be decoded using
 * the @ref PSpart_decodeReq() function.
 *
 * @param buffer The buffer used to encode the partition request
 * structure.
 *
 * @param size The size of the buffer.
 *
 * @param request The partition request structure to encode.
 *
 * @return On success, the number of bytes written to the buffer are
 * returned. If the return value is larger than @a size, the buffer is
 * to small in order to encode the whole partition request.
 *
 * @see PSpart_decodeReq()
 */
size_t PSpart_encodeReq(char *buffer, size_t size, PSpart_request_t *request);

/**
 * @brief Decode a partition request structure.
 *
 * Decode a partition request structure encoded by @ref
 * PSpart_encodeReq() and stored within @a buffer and write it to the
 * partition request structure @a request is pointing to.
 *
 * @param buffer The buffer the encoded partition request strucure is
 * stored to.
 *
 * @param request The partition request structure to write to.
 *
 * @return The number of chars within @a buffer used in order to
 * decode the partition request structure.
 */
int PSpart_decodeReq(char *buffer, PSpart_request_t *request);

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
 * @param task Pointer to the partition request to print.
 *
 * @return No return value.
 */
void PSpart_snprintf(char *txt, size_t size, PSpart_request_t *request);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSPARTITION_H */
