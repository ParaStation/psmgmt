/*
 * ParaStation
 *
 * Copyright (C) 2012-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_MOM_ACCOUNT
#define __PS_MOM_ACCOUNT

/**
 * @brief Fetch and process accounting data from the psaccount plugin.
 *
 * @param job The job to fetch the data for.
 *
 * @return No return value.
 */
void fetchAccInfo(Job_t *job);

/**
 * @brief Update job information for a single job or all Jobs.
 *
 * @param job The job to update the information for or NULL to update the
 * information for all jobs.
 *
 * @return No return value.
 */
void updateJobInfo(Job_t *job);

/**
 * @brief Add cputime from the wait() system call.
 *
 * @param job The job to set the cputime for.
 *
 * @param cputime The cputime to add.
 *
 * @return No return value.
 */
void addJobWaitCpuTime(Job_t *job, uint64_t cputime);

#endif
