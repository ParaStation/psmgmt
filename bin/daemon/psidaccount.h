/*
 * ParaStation
 *
 * Copyright (C) 2006-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file Helper functions for accounting actions
 */
#ifndef __PSIDACCOUNT_H
#define __PSIDACCOUNT_H

#include <stdbool.h>

#include "pstask.h"
#include "psnodes.h"

/**
 * @brief Initialize accounting stuff
 *
 * Initialize the accounting framework. This registers the necessary
 * message handlers.
 *
 * @return Return true on successful initialization or false on failure
 */
bool PSIDacct_init(void);

/**
 * @brief Register accounter
 *
 * Register a new accounter task with task ID @a acctr.
 *
 * @param acctr Task ID of the new accounter to register.
 *
 * @return No return value
 */
void PSID_addAcct(PStask_ID_t acctr);

/**
 * @brief Unregister accounter
 *
 * Unregister the accounter task with task ID @a acctr.
 *
 * @param acctr Task ID of the new accounter to register.
 *
 * @return No return value
 */
void PSID_remAcct(PStask_ID_t acctr);

/**
 * @brief Cleanup node's accounters
 *
 * Cleanup all accounters located on node @a node from the list of
 * accounters. Typically this function is called whenever a node is
 * detected to gone down.
 *
 * @param node The node ID used to cleanup accounters.
 *
 * @return No return value
 */
void PSID_cleanAcctFromNode(PSnodes_ID_t node);

/**
 * @brief Send list of accounters
 *
 * Send an option list of all known accounters to @a dest. Depending
 * on the value of @a all, either all accounters known are sent within
 * @ref PSP_OP_ACCT options or only local accounters, i.e. accounters
 * running on the local node, are sent using the @ref PSP_OP_ADD_ACCT
 * option type.
 *
 * @param dest ID of the destination task to send to.
 *
 * @param all Flag determining option type and class of accounters to
 * report.
 *
 * @return No return value
 */
void send_acct_OPTIONS(PStask_ID_t dest, int all);

/**
 * @brief Get number of accounters
 *
 * Return the number of accounters currently registered to the set of
 * ParaStation daemons.
 *
 * @return Returns number of registered accounters
 */
int PSID_getNumAcct(void);

#endif  /* __PSIDACCOUNT_H */
