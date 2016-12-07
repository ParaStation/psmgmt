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

#include <stdbool.h>
#include <sys/types.h>

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

#endif  /* __PLUGIN_LIB_PARTITION */
