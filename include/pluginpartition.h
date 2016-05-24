/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PLUGIN_LIB_PARTITION
#define __PLUGIN_LIB_PARTITION

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#include "pstask.h"
#include "pspartition.h"

/**
 * @brief Check for admin rights
 *
 * Check if either the user ID @a uid or the group ID @a gid grants
 * admin rights to a task.
 *
 * @param uid User ID to consider
 *
 * @param gid Group ID to consider
 *
 * @return If @a uid or @a gid allows for admin rights, true is
 * returned. Otherwise false is returned.
 */
bool isPSAdminUser(uid_t uid, gid_t gid);

/**
 * @brief Reject partition request
 *
 * Reject the partition request filed by @a dest. For this a
 * corresponding PSP_CD_PARTITIONRES message is sent. Since this
 * answer will contain the @ref errno it is assumed that @a errno is
 * set appropriately.
 *
 * @param dest Task ID to send the answering message to
 *
 * @return No return value
 */
void rejectPartitionRequest(PStask_ID_t dest);

/**
 * @brief Accept parition request
 *
 * Accept the partition request filed by @a dest. The partition will
 * consists out of @a numHWthreads HW-threads provided in the array @a
 * hwThreads. To grant the partition corresponding information is
 * added to the task structure @a task. The parition will be
 * registered to the master daemon and a corresponding
 * PSP_CD_PARTITIONRES message is sent to @a dest.
 *
 * @param hwThreads An array of HW-treads to form the partition
 *
 * @param numHWthreads Number of HW-threads contained in @a hwThreads
 *
 * @param dest Task ID to send the answering message to
 *
 * @param task Task structure used to store the new partition to
 *
 * @return No return value
 */
void grantPartitionRequest(PSpart_HWThread_t *hwThreads, uint32_t numHWthreads,
			   PStask_ID_t dest, PStask_t *task);

#endif  /* __PLUGIN_LIB_PARTITION */
