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

/**
 * @brief Verify correct permissions of pelogue scripts.
 *
 * @param filename The filename to verfiy.
 *
 * @param root True if only root be able to execute the script.
 *
 * @return Returns 1 on success or an error code <0 on failure.
 */
int checkPELogueFileStats(char *filename, int root);

#endif
