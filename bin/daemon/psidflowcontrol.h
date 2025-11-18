/*
 * ParaStation
 *
 * Copyright (C) 2014-2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Flow-control handling for the ParaStation protocol
 */
#ifndef __PSIDFLOWCONTROL_H
#define __PSIDFLOWCONTROL_H

#include <stdbool.h>

#include "list.h"
#include "psprotocol.h"
#include "pstask.h"

/** The actual size of the flow-control hashes */
#define FLWCNTRL_HASH_SIZE 32

/** Hash to store IDs of tasks that got a SENDSTOP message */
typedef list_t PSIDFlwCntrl_hash_t[FLWCNTRL_HASH_SIZE];

/**
 * @brief Initialize flow-control functionality
 *
 * Initialize the flow-control module. This has to be called before any
 * function within this module is used.
 *
 * @return Return true on successful initialization or false on failure
 */
bool PSIDFlwCntrl_init(void);

/**
 * @brief Check for applicability of flow-control
 *
 * Check if the flow-control can be applied to message @a msg. The
 * results of this check depends on both, the sending task-ID and the
 * message type.
 *
 * @param msg The message to investigate
 *
 * @return If flow-control is applicable to @a msg, true is returned; or
 * false in any other case
 */
bool PSIDFlwCntrl_applicable(DDMsg_t *msg);

/**
 * @brief Initialize a stopTID-hash
 *
 * Initialize the stopTID-hash @a hash. The function @ref
 * PSIDFlwCntrl_addStop() might be used to add to add task-IDs of such
 * tasks to which a SENDSTOP message was sent. Thus, once the
 * congestion dissolved the corresponding hash might be used to send
 * the corresponding SENDCONT messages via @ref
 * PSIDFlwCntrl_sendContMsgs().
 *
 * @param hash The hash to be initialized
 *
 * @return No return value.
 *
 * @see PSIDFlwCntrl_addStop(), PSIDFlwCntrl_sendContMsgs()
 */
void PSIDFlwCntrl_initHash(PSIDFlwCntrl_hash_t hash);

/**
 * @brief Empty a stopTID-hash
 *
 * Empty the stopTID-hash @a hash to be used for the next round. The
 * function @ref PSIDFlwCntrl_addStop() might be used to add to add
 * task-IDs of such tasks to which a SENDSTOP message was sent. Thus,
 * once the congestion dissolved the corresponding hash might be used
 * to send the corresponding SENDCONT messages via @ref
 * PSIDFlwCntrl_sendContMsgs().
 *
 * Basically this function just empties the hash putting all the
 * internally used stopTID structures back into the pool.
 *
 * @param hash The hash to be initialized
 *
 * @return No return value.
 *
 * @see PSIDFlwCntrl_addStop(), PSIDFlwCntrl_sendContMsgs()
 */
void PSIDFlwCntrl_emptyHash(PSIDFlwCntrl_hash_t hash);

/**
 * @brief Add a task-ID to a stopTID-hash
 *
 * Add the task-ID @a key to the stopTID-hash @a hash. The function
 * ensures that each task-ID is only stored once within the hash,
 * i.e. that each occurrence of a task-ID within the hash is
 * unique. The stopTID-hash has to be initialized via
 * PSIDFlwCntrl_initHash() beforehand. If the task-ID stored within @a
 * hash are used as destinations of SENDSTOP messages, it might be
 * used in order to send SENDCONT messages via
 * PSIDFlwCntrl_sendContMsgs().
 *
 * @param hash The hash to store the task-ID to.
 *
 * @param key The task-ID to store to the hash.
 *
 * @return On success, this function returns 0 or 1 depending on
 * having key stored before (0) or right now (1). Or -1 if an error
 * occurred.
 *
 * @see PSIDFlwCntrl_initHash(), PSIDFlwCntrl_sendContMsgs()
 */
int PSIDFlwCntrl_addStop(PSIDFlwCntrl_hash_t table, PStask_ID_t key);

/**
 * @brief Send SENDCONT messages according to the hash
 *
 * Send SENDCONT messages according to the hash @a stops. It is
 * assumed that the hash was initialized by @ref
 * PSIDFlwCntrl_initHash() and then filled via @ref
 * PSIDFlwCntrl_addStop(). All SENDCONT messages are sent using the
 * sending task-ID @a sender. Along sending messages the hash @a stops
 * is destructed, thus, upon return it will be empty and ready for
 * re-use.
 *
 * @param stops The hash containing all destination task-IDs.
 *
 * @param sender Task-ID to be used as the sender of the SENDCONT messages.
 *
 * @return Return the number of SENDCONT messages actually sent.
 *
 * @see PSIDFlwCntrl_initHash(), PSIDFlwCntrl_addStop()
 */
int PSIDFlwCntrl_sendContMsgs(PSIDFlwCntrl_hash_t stops, PStask_ID_t sender);

/**
 * @brief Print statistics
 *
 * Print statistics concerning the usage of internal stopTID structures.
 *
 * @return No return value.
 */
void PSIDFlwCntrl_printStat(void);

/**
 * @brief Memory cleanup
 *
 * Cleanup all dynamic memory currently used by the module. It will
 * very aggressively free() all allocated memory most likely
 * destroying the existing flow-control representation.
 *
 * The purpose of this function is to cleanup before a fork()ed
 * process is handling other tasks, e.g. becoming a forwarder.
 *
 * @return No return value.
 */
void PSIDFlwCntrl_clearMem(void);


#endif /* __PSIDFLOWCONTROL_H */
