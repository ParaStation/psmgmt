/*
 * ParaStation
 *
 * Copyright (C) 2010-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSMOM_SIGNAL
#define __PSMOM_SIGNAL

/**
 * @brief Convert a signal string into a signal number.
 *
 * @param signal The signal string to convert.
 *
 * @return Returns the signal as integer or 0 on error.
 */
int string2Signal(char *signal);

/**
 * @brief Convert a signal number to its string representation.
 *
 * @param signal The signal number to convert.
 *
 * @return Returns the signal as string or NULL on error.
 */
char *signal2String(int signal);

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
