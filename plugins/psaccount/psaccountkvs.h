/*
 * ParaStation
 *
 * Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_KVS
#define __PS_ACCOUNT_KVS

/**
 * @brief Show a key value pair.
 *
 * @param key The key of the kv-pair to query. If NULL than all kv-pairs should
 * be returned.
 *
 * @return Returns a dynamically allocated string holding the
 * requested kv-pairs.
 */
char *show(char *key);

/**
 * @brief Display a help message for the psmom kvs.
 *
 * @return Returns the dynamically allocated help message.
 */
char *help(void);

#endif
