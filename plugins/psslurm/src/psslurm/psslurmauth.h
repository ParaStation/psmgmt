/*
 * ParaStation
 *
 * Copyright (C) 2014-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
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
 * @brief Initialize the authentication facility
 *
 * @return Returns true on success otherwise false is returned
 */
bool Auth_init(void);

/**
 * @brief Finalize the authentication facility
 */
void Auth_finalize(void);

/**
 * @brief Test if UID is denied to start jobs
 *
 * @apram uid The UID to test
 *
 * @param Returns true if the UID is denied to start jobs; otherwise
 * false is returned
 */
bool Auth_isDeniedUID(uid_t uid);

/**
 * @brief Generate a Slurm authentication
 *
 * Generate and return a Slurm authentication token. Currently only
 * psmunge is supported as authentication method. The caller is
 * responsible to free the allocated memory using freeSlurmAuth().
 *
 * The user ID of the @ref head will be used to set the allowed
 * user which can decode the credential. Additionally the message body
 * is used to calculate a message hash which is secured using munge.
 *
 * @param head Slurm message header
 *
 * @param body Slurm message body
 *
 * @param bodyLen Length of Slurm message body
 *
 * @return Returns the authentication token or NULL on error
 */
Slurm_Auth_t *getSlurmAuth(Slurm_Msg_Header_t *head, char *body,
			   uint32_t bodyLen);

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

/**
 * @brief Convert list of users to an array of UIDs
 *
 * Convert the comma separated list of usernames @a users to an array
 * of UIDs. This array is created via @ref malloc() in according size
 * and upon successful return given back via @a UIDs. The number of
 * elements will be reported via @a numUIDs.
 *
 * If any username cannot be converted into an UID the overall process
 * will fail and no result is provided.
 *
 * @param users List of usernames to convert
 *
 * @param UIDs Array of UIDs created to hold the results
 *
 * @param numUIDs Number of entries in @a UIDs
 *
 * @return On success true is returned or false in case of an error
 */
bool arrayFromUserList(const char *users, uid_t **UIDs, uint16_t *numUIDs);

#endif /*  __PS_SLURM_AUTH */
