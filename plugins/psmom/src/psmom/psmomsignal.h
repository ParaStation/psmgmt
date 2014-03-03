/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_MOM_SIGNAL
#define __PS_MOM_SIGNAL

struct sigTable {
    char *sigName;
    char *sigStrNum;
    int sigNum;
};

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
 * @brief Send a signal to one or more jobs.
 *
 * The signal must be send to the corresponding forwarder of
 * the job, which will then send the signal to the appropriate processes.
 *
 * @param job The job to send the signal to or
 * NULL to send the signal to all jobs.
 *
 * @param signal The signal to send.
 *
 * @param reason The reason why the signal should be sent.
 *
 * @return Returns 1 on error and 0 on success.
 */
int sendSignaltoJob(Job_t *job, int signal, char *reason);

#endif
