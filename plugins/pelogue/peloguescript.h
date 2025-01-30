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
 * Verify the existence of the directory @a dDir and correct
 * permissions of it and all pelogue script snippets contained.
 *
 * @a dDir must not be writable by other users than root.
 *
 * pelogue script snippets are assumed to be not hidden and their
 * names end with an ".sh". They must not be writable by other users
 * than root. Their owner must have the right to read and execute it.
 *
 * @param dDir .d directory to be checked
 *
 * @return Returns true iff all checks were succesfull
 */
bool checkDDir(char *dDir);

#endif  /* __PELOGUE_SCRIPT */
