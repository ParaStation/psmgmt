/*
 * ParaStation
 *
 * Copyright (C) 2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSMUNGE_TYPES
#define __PSMUNGE_TYPES

#include <sys/types.h>
#include <stdbool.h>

/**
 * @brief Create credential
 *
 * Creates a credential contained in a NUL-terminated base64 string. A
 * pointer to the resulting credential is returned via @a cred; on
 * error, it is set to NULL. The caller is responsible to free() the
 * memory referenced by @a cred.
 *
 * @param cred Pointer to the created credential
 *
 * @return Returns 1 on success, or 0 in case of error
 */
typedef int(psMungeEncode_t)(char **cred);

/**
 * @brief Decode credential
 *
 * Validate the NUL-terminated credential @a cred. If @a uid or @a
 * gid is not NULL, they will be set to the UID/GID of the process
 * that created the credential.
 *
 * @param cred Credential to validate
 *
 * @param uid User ID of the process creating the credential
 *
 * @param gid Group ID of the process creating the credential
 *
 * @return Returns 1 on success, or 0 in case of error
 */
typedef int(psMungeDecode_t)(const char *cred, uid_t *uid, gid_t *gid);

/**
 * @brief Decode credential with payload
 *
 * Validate the NUL-terminated credential @a cred. If @a buf and @a
 * len are not NULL, memory will be allocated for the encapsulated
 * payload, @a buf will be set to point to this data, and @a len will
 * be set to its length. An additional NUL character will be appended
 * to this payload data but not included in its length. If no payload
 * exists, @a buf will be set to NULL and @a len will be set to 0. For
 * certain errors (i.e., EMUNGE_CRED_EXPIRED, EMUNGE_CRED_REWOUND,
 * EMUNGE_CRED_REPLAYED), payload memory will still be allocated if
 * necessary. The caller is responsible for freeing the memory
 * referenced by @a buf. If @a uid or @a gid is not NULL, they will be
 * set to the UID/GID of the process that created the credential.
 *
 * @param cred Credential to validate
 *
 * @param buf Pointer to credential's payload if any
 *
 * @param len Length of credential's payload if any
 *
 * @param uid User ID of the process creating the credential
 *
 * @param gid Group ID of the process creating the credential
 *
 * @return Returns 1 on success, or 0 in case of error
 */
typedef int(psMungeDecodeBuf_t)(const char *cred, void **buf, int *len,
				uid_t *uid, gid_t *gid);

/**
 * @brief Measure calls to libmunge
 *
 * Log execution times of libmunge encode/decoce calls to syslog
 * if @a active is set to true.
 *
 * @param active Flag to enable or disable timing information
 */
typedef void(psMungeMeasure_t)(bool active);

#endif  /* __PSMUNGE_TYPES */
