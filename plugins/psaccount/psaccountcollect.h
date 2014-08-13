/*
 * ParaStation
 *
 * Copyright (C) 2010-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_COLLECT
#define __PS_ACCOUNT_COLLECT

#include "psaccountclient.h"

/**
 * @brief Update Accounting Data for a client.
 *
 * Reads data from /proc/pid/stat and updates the
 * accounting data structure accData. Used for generating
 * accounting files and to replace the resmom of torque.
 * For accounting the memory usage the rss and vsize or monitored.
 *
 * VSIZE (Virtual memory SIZE) - The amount of memory the process is
 * currently using. This includes the amount in RAM and the amount in
 * swap.
 *
 * RSS (Resident Set Size) - The portion of a process that exists in
 * physical memory (RAM). The rest of the program exists in swap. If
 * the computer has not used swap, this number will be equal to VSIZE.
 *
 * @param client The client to update the accounting data.
 *
 * @return No return value.
 */
void updateAccountData(Client_t *client);

#endif
