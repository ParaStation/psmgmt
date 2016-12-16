/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_SCRIPT
#define __PELOGUE_SCRIPT

#include <stdbool.h>

/**
 * @brief Verify permissions of pelogue script
 *
 * Verify the correct permissions of the pelogue script contained in
 * @a filename. If @a root is true, this function checks for root's
 * permission to read and execute this file and that no other users
 * are allowed to modify it. Otherwise it checks for allowance to read
 * and execute this file for root and other users.
 *
 * @param filename Pelogue file to verify
 *
 * @param root Flag to check only for root's permissions
 *
 * @return Returns 1 on success, i.e. if the permission are set as
 * stated above. If the file is non-existing or stat() fails, -1 is
 * returned. If the file does not match the checked permissions, -2 is
 * returned.
 */
int checkPELogueFileStats(char *filename, bool root);

#endif  /* __PELOGUE_SCRIPT */
