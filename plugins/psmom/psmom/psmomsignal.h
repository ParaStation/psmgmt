/*
 * ParaStation
 *
 * Copyright (C) 2010-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSMOM_SIGNAL
#define __PSMOM_SIGNAL

#include <stdbool.h>

#include "psmomjob.h"

/**
 * @brief Convert a signal string into a signal number.
 *
 * @param sig Signal string to convert
 *
 * @return Return the signal as integer or 0 on error
 */
int string2Signal(char *sig);

/**
 * @brief Convert a signal number to its string representation.
 *
 * @param sig Signal number to convert
 *
 * @return Return the signal as string or NULL on error
 */
char *signal2String(int sig);

/**
 * @brief Send a signal to a job using various ways.
 *
 * @param job A pointer to the job structure to send the signal to.
 *
 * @param signal The signal to send.
 *
 * @return Returns true on success and false on error.
 */
bool signalJob(Job_t *job, int signal, char *reason);

#endif /* __PSMOM_SIGNAL */
