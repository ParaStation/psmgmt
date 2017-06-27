/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_SLURM_AUTH
#define __PS_SLURM_AUTH

#include <stdbool.h>

#include "psslurmjob.h"
#include "plugincomm.h"
#include "psslurmcomm.h"

/**
 * @brief Generate a Slurm authentication
 *
 * Generate and return a Slurm authentication token. Currently only
 * psmunge is supported as authentication method. The caller is
 * responsible to free the allocated memory using freeSlurmAuth().
 */
Slurm_Auth_t *getSlurmAuth(void);

/**
 * @brief Free a Slurm authentication structure
 *
 * @param auth The authentication structure to free
 */
void freeSlurmAuth(Slurm_Auth_t *auth);

Slurm_Auth_t *dupSlurmAuth(Slurm_Auth_t *auth);

/**
 * @brief Extract and verify Slurm authentication
 *
 * Extract Slurm authentication from a message pointer and
 * verify the included credential. The extracted userid and groupid
 * are set in the Slurm message header. Currently only psmunge
 * is supported as authentication method.
 *
 * @param ptr The message to unpack the data from
 *
 * @param msgHead Slurm message header to save data to
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool extractSlurmAuth(char **ptr, Slurm_Msg_Header_t *msgHead);

/**
 * @brief Verify step information
 *
 * Perform various tests to verify the step information is
 * valid.
 *
 * @param step Pointer to the step
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool verifyStepData(Step_t *step);

/**
 * @brief Verify job information
 *
 * Perform various tests to verify the job information is
 * valid.
 *
 * @param job Pointer to the job
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool verifyJobData(Job_t *job);

/**
 * @brief Extract and verify a job credential
 *
 * Extract and verify a job credential including the embedded
 * gres credential from the provided message pointer.
 *
 * @param gres Pointer to a gres credential structure
 *
 * @param ptr The message to unpack the data from
 *
 * @param verify If true verify the data using psmunge
 *
 * @return Returns the extracted job credential or NULL on error
 */
JobCred_t *extractJobCred(Gres_Cred_t **gres, char **ptr, bool verify);

/**
 * @brief Free a job credential
 *
 * @param Pointer to the job credential
 */
void freeJobCred(JobCred_t *cred);

/**
 * @brief Extract and verify a BCast credential
 *
 * @param ptr The message to unpack the data from
 *
 * @param bcast The bcast structure holding the result
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool extractBCastCred(char **ptr, BCast_t *bcast);

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

#endif
