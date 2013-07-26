/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PMI__KVS
#define __PS_PMI__KVS

/* file handle for memory debug output */
extern FILE *memoryDebug;

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

/**
 * @brief Set a key-value pair.
 *
 * @param key The key to set.
 *
 * @param value The value to set.
 *
 * @return Returns a pointer a message describing the success.
 */
char *set(char *key, char *value);

/**
 * @brief Remove a key-value pair.
 *
 * @param key The key as index to unset.
 *
 * @param value The value to unset.
 *
 * @return Returns a pointer a message describing the success.
 */
char *unset(char *key);

#endif
