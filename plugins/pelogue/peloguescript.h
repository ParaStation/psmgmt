/*
 * ParaStation
 *
 * Copyright (C) 2013-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PELOGUE_SCRIPT
#define __PELOGUE_SCRIPT

#include <stdbool.h>

/**
 * @brief Verify .d directory used for pelogue
 *
 * Verify the existence of the directory and correct permissions of it
 * and all pelogue scripts contained.
 *
 * @param dDir .d directory to be checked
 *
 * @param root Flag to check only for root's permissions
 *
 * @return Returns true iff all checks were succesfull
 */
bool checkDDir(char *dDir, bool root);

int checkPELogueFileStats(char *filename, bool root);

#endif  /* __PELOGUE_SCRIPT */
