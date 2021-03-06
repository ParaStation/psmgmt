/*
 * ParaStation
 *
 * Copyright (C) 2014-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_SLURM_AUTH
#define __PS_SLURM_AUTH

#include <stdbool.h>

#include "list.h"
#include "psslurmjob.h"
#include "psserial.h"
#include "psslurmcomm.h"
#include "psslurmbcast.h"

/**
 * @brief Generate a Slurm authentication
 *
 * Generate and return a Slurm authentication token. Currently only
 * psmunge is supported as authentication method. The caller is
 * responsible to free the allocated memory using freeSlurmAuth().
 *
 * @return Returns the authentication token or NULL on error.
 */
Slurm_Auth_t *getSlurmAuth(void);

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
 * GRes credential from the provided message pointer and add it
 * to the list of credentials @a gresList.
 *
 * @param gresList List of GRes credential structures the included
 * GRes credential will be appended to
 *
 * @param sMsg The message to unpack
 *
 * @param verify If true verify the data using psmunge
 *
 * @return Returns the extracted job credential or NULL on error
 */
JobCred_t *extractJobCred(list_t *gresList, Slurm_Msg_t *sMsg, bool verify);

/**
 * @brief Free a job credential
 *
 * @param Pointer to the job credential
 */
void freeJobCred(JobCred_t *cred);

/**
 * @brief Extract and verify a BCast credential
 *
 * @param sMsg The message to unpack
 *
 * @param bcast The BCast structure holding the result
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool extractBCastCred(Slurm_Msg_t *sMsg, BCast_t *bcast);

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
