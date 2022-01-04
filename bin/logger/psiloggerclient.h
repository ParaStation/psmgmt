/*
 * ParaStation
 *
 * Copyright (C) 2009-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Facility to manage clients and input forwarding of the
 * log-daemon
 */
#ifndef __PSILOGGERCLIENT_H
#define __PSILOGGERCLIENT_H

#include <stdbool.h>
#include <stddef.h>

#include "psprotocol.h"
#include "pstask.h"
#include "pslog.h"

/**
 * @brief Initialize client management
 *
 * Initialized the client management. Afterwards to client management
 * expects rank between a minimum of @a minClientRank and a maximum of
 * @a maxClientRank. Subsequent calls to @ref registerClient() might
 * extend this range dynamically.
 *
 * Since reducing the minimum rank induces major reorganization of the
 * internal data structures, it is recommended to set at least the
 * minimum value to the one used throughout the complete run-time of
 * the facility.
 *
 * @param minClientRank The minimum rank for clients expected.
 *
 * @param maxClientRank The maximum rank for clients expected.
 *
 * @return No return value.
 */
void initClients(int minClientRank, int maxClientRank);

/**
 * @brief Register new client
 *
 * Register the client working on rank @a rank with task-ID @a tid. If
 * rank is larger than the current maximum or smaller than the current
 * minimum, internal structure are correspondingly extended.
 *
 * @param rank Rank of the client to register
 *
 * @param tid Task ID of the client to register
 *
 * @param group Task group of the client to register
 *
 * @return If the client was registered successfully, true is
 * returned; false is returned if some problem occurred, e.g. a
 * client with the same rank was already registered.
 */
bool registerClient(int rank, PStask_ID_t tid, PStask_group_t group);

/**
 * @brief Unregister client
 *
 * Unregister the client working on rank @a rank. If rank is out of
 * range, i.e. smaller than the minimum rank or larger than the
 * maximum rank, the function will exit() the calling program.
 *
 * @param rank Rank of the client to unregister.
 *
 * @return No return value
 */
void deregisterClient(int rank);

/**
 * @brief Get number of clients
 *
 * Determine the number of client processes currently registered
 * within the facility.
 *
 * Before calling @ref initClients(), -1 is returned.
 *
 * @return The current number of clients is returned.
 */
int getNoClients(void);

/**
 * @brief Get minimum rank
 *
 * Request the minimum rank currently handled by the logger.
 *
 * If @ref initClient() was not called before, the minimum rank is
 * larger than the result of @ref getMaxRank(). After calling @ref
 * initClients(), this is never the case.
 *
 * @return Returns the minimum rank.
 */
int getMinRank(void);

/**
 * @brief Get maximum rank
 *
 * Request the maximum rank currently handled by the logger.
 *
 * If @ref initClient() was not called before, the maximum rank is
 * smaller than the result of @ref getMinRank(). After calling @ref
 * initClients(), this is never the case.
 *
 * @return Returns the maximum rank.
 */
int getMaxRank(void);

/**
 * @brief Get next free service rank
 *
 * Request the next free (and unique) service rank.
 *
 * The next free service rank is commonly equal to the minimum rank.
 * However, since the service ranks should be kept unique also in the case
 * of multiple (external) inquiries for new ranks, an incremental offset
 * ensures that subsequent calls will not return the same rank value.
 *
 * @return Returns the next service rank to be assigned.
 */
int getNextServiceRank(void);

/**
 * @brief Get client's rank
 *
 * Determine the rank of the client with task-ID @a tid.
 *
 * @param tid Task-ID of the client of interest.
 *
 * @return The client's rank. If the client is unknown, the result is
 * @ref getMaxRank() + 1
 */
int getClientRank(PStask_ID_t tid);

/**
 * @brief Get client's task-ID
 *
 * Determine the task-ID of the client with rank @a rank.
 *
 * @param rank Rank of the client of interest.
 *
 * @return The client's task-ID. If @a rank is out of range or the
 * corresponding client has not yet connected, -1 is returned.
 */
PStask_ID_t getClientTID(int rank);

/**
 * @brief Test client's activity
 *
 * Test if client with rank @a rank is currently marked as a active
 * receiver of input.
 *
 * @param rank Rank of the client to test
 *
 * @return If the client is marked to receive input, true is
 * returned. Otherwise false is given back.
 */
bool clientIsActive(int rank);

/**
 * @brief Test all active clients
 *
 * Test, if all clients marked to receive input are currently
 * registered to the facility.
 *
 * @return If all clients are registered, true is returned. Otherwise
 * false is given back.
 */
bool allActiveThere(void);

/**
 * @brief Test, if client is gone
 *
 * Test, if the client with rank @a rank is gone. A client is marked
 * as gone, if it was registered in the past but in the meantime
 * unregistered.
 *
 * @param rank Rank of the client to be tested.
 *
 * @return If the client is marked as gone, a value different from
 * true is returned. Otherwise false is returned.
 */
bool clientIsGone(int rank);

/**
 * @brief Handle STOP message
 *
 * Handle the STOP messages @a msg. This will mark the corresponding
 * client as stopped, stop any input-forwarding, etc.
 *
 * @param msg The STOP message to handle.
 *
 * @return No return value.
 */
void handleSTOPMsg(PSLog_Msg_t *msg);

/**
 * @brief Handle CONT message
 *
 * Handle the CONT messages @a msg. This will clear the stop-flag of
 * the corresponding client, continue input-forwarding, etc.
 *
 * @param msg The CONT message to handle.
 *
 * @return No return value.
 */
void handleCONTMsg(PSLog_Msg_t *msg);

/**
 * @brief Handle SENDSTOP message
 *
 * Handle the SENDSTOP messages @a msg. This will generically stop any
 * input-forwarding, etc.
 *
 * @param msg The SENDSTOP message to handle (currently ignored)
 *
 * @return No return value
 */
void handleSENDSTOP(DDBufferMsg_t *msg);

/**
 * @brief Handle SENDCONT message
 *
 * Handle the SENDCONT messages @a msg. This will generically continue any
 * input-forwarding, etc.
 *
 * @param msg The SENDCONT message to handle (currently ignored)
 *
 * @return No return value
 */
void handleSENDCONT(DDBufferMsg_t *msg);

/**
 * @brief Create list of destinations
 *
 * Create the list of clients that are destinations of input from the
 * describing string @a input. This will setup all internal structures
 * necessary to handle input-forwarding.
 *
 * @a input is a comma-separated list of ranges or the literal string
 * "all". Each range is of the form "start[-end]".
 *
 * @param input String describing the job's input-destinations.
 *
 * @return No return value
 */
void setupDestList(char *input);

/**
 * @brief Create description string for input destinations
 *
 * Print a string describing the current input destinations used by
 * calls to forwardInputStr(). The maximum size of the returned string
 * is silently limited to @a maxLen or 1024 whatever is smaller.
 *
 * The description string returned is a static character array. Thus,
 * repeated calls to this function will invalidate results returned
 * before.
 *
 * @param maxLen Maximum length of the string returned.
 *
 * @return The Strings created.
 */
char *getDestStr(size_t maxLen);

/**
 * @brief Forward input
 *
 * Send the input within @a buf to all input destinations defined by a
 * prior call to @ref setupDestList(). @a buf is expected to contain
 * @a len characters.
 *
 * @param buf Current input to forward.
 *
 * @param len Number of characters within @a buf to forward.
 *
 * @return On success, the number of characters forwarded, i.e. @a
 * len, is returned. Otherwise the -1 is returned and errno is set
 * appropriately.
 */
int forwardInputStr(char *buf, size_t len);

#endif  /* __PSILOGGERCLIENT_H */
