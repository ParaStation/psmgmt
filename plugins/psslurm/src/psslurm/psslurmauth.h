/*
 * ParaStation
 *
 * Copyright (C) 2014-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_AUTH
#define __PS_SLURM_AUTH

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/** structure holding a Slurm authentication */
typedef struct {
    char *cred;		/**< authentication credential */
    uint32_t pluginID;	/**< plugin used for authentication */
} Slurm_Auth_t;

// leave after Slurm_Auth_t definition to break include cycle
#include "psslurmmsg.h"  // IWYU pragma: keep

/**
 * @brief Generate a Slurm authentication
 *
 * Generate and return a Slurm authentication token. Currently only
 * psmunge is supported as authentication method. The caller is
 * responsible to free the allocated memory using freeSlurmAuth().
 *
 * @param uid The user ID allowed to decode the credential
 *
 * @return Returns the authentication token or NULL on error
 */
Slurm_Auth_t *getSlurmAuth(uid_t uid);

/**
 * @brief Free a Slurm authentication structure
 *
 * @param auth The authentication structure to free
 */
void freeSlurmAuth(Slurm_Auth_t *auth);

/**
 * @brief Duplicate a Slurm authentication
 *
 * @param auth The authentication structure to duplicate
 *
 * @return Returns the duplicated authentication structure or
 * NULL on error.
 */
Slurm_Auth_t *dupSlurmAuth(Slurm_Auth_t *auth);

/**
 * @brief Extract and verify Slurm authentication
 *
 * Extract Slurm authentication from a message pointer and
 * verify the included credential. The extracted user ID and group ID
 * are set in the Slurm message header. Currently only psmunge
 * is supported as authentication method.
 *
 * @param sMsg The Slurm message to unpack
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool extractSlurmAuth(Slurm_Msg_t *sMsg);

/**
 * @brief Test if the user ID is authorized
 *
 * @param userID The user ID to verify
 *
 * @param validID Additional valid user ID
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool verifyUserId(uid_t userID, uid_t validID);

#endif /*  __PS_SLURM_AUTH */
