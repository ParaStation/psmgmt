/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PS_PELOGUE_ACCOUNT_FUNC
#define __PS_PELOGUE_ACCOUNT_FUNC

/**
 * @brief Send a signal to a session.
 *
 * @param session The session ID to send the signal to.
 *
 * @param sig The signal to send.
 *
 * @return Returns the number of children which the signal
 * was sent to.
 */
int (*psAccountsendSignal2Session)(pid_t, int);


#endif
