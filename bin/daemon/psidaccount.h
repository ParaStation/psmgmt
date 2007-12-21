/*
 *               ParaStation
 *
 * Copyright (C) 2006 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Helper functions for accounting actions.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDACCOUNT_H
#define __PSIDACCOUNT_H

#include "psprotocol.h"
#include "pstask.h"
#include "psnodes.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Register accounter.
 *
 * Register a new accounter task with task ID @a acctr.
 *
 * @param acctr Task ID of the new accounter to register.
 *
 * @return No return value.
 */
void PSID_addAcct(PStask_ID_t acctr);

/**
 * @brief De-register accounter.
 *
 * De-register the accounter task with task ID @a acctr.
 *
 * @param acctr Task ID of the new accounter to register.
 *
 * @return No return value.
 */
void PSID_remAcct(PStask_ID_t acctr);

/**
 * @brief Cleanup node's accounters.
 *
 * Cleanup all accounters located on node @a node from the list of
 * accounters. Typically this function is called whenever a node is
 * detected to gone down.
 *
 * @param node The node ID used to cleanup accounters.
 *
 * @return No return value.
 */
void PSID_cleanAcctFromNode(PSnodes_ID_t node);

/**
 * @brief Send list of accounters.
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
 * @return No return value.
 */
void send_acct_OPTIONS(PStask_ID_t dest, int all);

/**
 * @brief Handle PSP_CD_ACCOUNT message.
 *
 * Handle the message @a msg of type PSP_CD_ACCOUNT. If the message is
 * destined to the local daemon it will be forwarded to all registered
 * accounter tasks. I.e. the message will be multiplexed if more than
 * one accounter is registered through the corresponding @ref
 * PSID_addAcct() calls.
 *
 * @param msg Pointer to the message to handle.
 *
 * @return No return value.
 */
void msg_ACCOUNT(DDBufferMsg_t *msg);

/**
 * @brief Get number of accounters. 
 *
 * Return the number of accounters currently registered to the set of
 * ParaStation daemons.
 *
 * @return Returns number of registered accounters.
 */
int PSID_getNumAcct(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDACCOUNT_H */
