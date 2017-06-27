/*
 * ParaStation
 *
 * Copyright (C) 2016-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_SLURM_PACK
#define __PS_SLURM_PACK

#include <netinet/in.h>
#include <stdbool.h>

#include "plugincomm.h"
#include "psslurmauth.h"
#include "psslurmio.h"

/**
 * @brief Pack a Slurm authentication
 *
 * Pack a Slurm authentication and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param auth The authentication structure to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmAuth(PS_DataBuffer_t *data, Slurm_Auth_t *auth,
		    const char *caller, const int line);

#define packSlurmAuth(data, auth) \
    __packSlurmAuth(data, auth, __func__, __LINE__)

/**
 * @brief Unpack a Slurm authentication
 *
 * Unpack a Slurm authentication from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param ptr The message to unpack the data from
 *
 * @param auth The authentication structure holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a ptr might be not updated.
 */
bool __unpackSlurmAuth(char **ptr, Slurm_Auth_t **authPtr, const char *caller,
			const int line);

#define unpackSlurmAuth(ptr, authPtr) \
    __unpackSlurmAuth(ptr, authPtr, __func__, __LINE__)

/**
 * @brief Unpack a job credential
 *
 * Unpack a job credential including the embedded gres
 * credential. The memory is allocated using umalloc().
 * The caller is responsible to free the memory using ufree().
 *
 * @param ptr The message to unpack the data from
 *
 * @param cred The job credential holding the result
 *
 * @param gres The gres credential holding the result
 *
 * @param credEnd Pointer updated to the end of the credential
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a ptr might be not updated.
 */
bool __unpackJobCred(char **ptr, JobCred_t **credPtr, Gres_Cred_t **gresPtr,
		    char **credEnd, const char *caller, const int line);

#define unpackJobCred(ptr, credPtr, gresPtr, credEnd) \
    __unpackJobCred(ptr, credPtr, gresPtr, credEnd, __func__, __LINE__)

/**
 * @brief Unpack a BCast credential
 *
 * Unpack a BCast credential from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param ptr The message to unpack the data from
 *
 * @param bcast The bcast credential holding the result
 *
 * @param credEnd Pointer updated to the end of the credential
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a ptr might be not updated.
 */
bool __unpackBCastCred(char **ptr, BCast_t *bcast, char **credEnd,
			const char *caller, const int line);

#define unpackBCastCred(ptr, bcast, credEnd) \
    __unpackBCastCred(ptr, bcast, credEnd, __func__, __LINE__)

/**
 * @brief Pack a Slurm message header
 *
 * Pack a Slurm message header and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param head The Slurm message header to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmHeader(PS_DataBuffer_t *data, Slurm_Msg_Header_t *head,
		    const char *caller, const int line);

#define packSlurmHeader(data, head) \
    __packSlurmHeader(data, head, __func__, __LINE__)

/**
 * @brief Pack a Slurm I/O message
 *
 * Pack a Slurm I/O message and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param ioMsg The Slurm I/O head to pack
 *
 * @param body The message body to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmIOMsg(PS_DataBuffer_t *data, Slurm_IO_Header_t *ioh, char *body,
			const char *caller, const int line);

#define packSlurmIOMsg(data, ioMsg, body) \
    __packSlurmIOMsg(data, ioMsg, body, __func__, __LINE__)

/**
 * @brief Unpack a Slurm I/O message header
 *
 * Unpack a Slurm I/O message header from the provided message pointer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param ptr The message to unpack the data from
 *
 * @param iohPtr The Slurm I/O message header holding the result
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a ptr might be not updated.
 */
bool __unpackSlurmIOHeader(char **ptr, Slurm_IO_Header_t **iohPtr,
			    const char *caller, const int line);

#define unpackSlurmIOHeader(ptr, iohPtr) \
    __unpackSlurmIOHeader(ptr, iohPtr, __func__, __LINE__)

/**
 * @brief Pack a Slurm message
 *
 * Pack a Slurm message and add it to the provided data
 * buffer.
 *
 * @param data Data buffer to save data to
 *
 * @param head The Slurm head to pack
 *
 * @param body The message body to pack
 *
 * @param auth The Slurm authentication to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If writing was not successful, @a data might be not updated.
 */
bool __packSlurmMsg(PS_DataBuffer_t *data, Slurm_Msg_Header_t *head,
		    PS_DataBuffer_t *body, Slurm_Auth_t *auth,
		    const char *caller, const int line);

#define packSlurmMsg(data, head, body, auth) \
    __packSlurmMsg(data, head, body, auth, __func__, __LINE__)

#endif
