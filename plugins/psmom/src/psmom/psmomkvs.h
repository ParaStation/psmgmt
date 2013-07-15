/*
 * ParaStation
 *
 * Copyright (C) 2011-2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_MOM_KVS
#define __PS_MOM_KVS

/* count started batch jobs */
extern uint32_t stat_batchJobs;

/* count started interactive jobs */
extern uint32_t stat_interJobs;

/* count successful batch jobs */
extern uint32_t stat_successBatchJobs;

/* count successful interactive jobs */
extern uint32_t stat_successInterJobs;

/* count failed batch jobs */
extern uint32_t stat_failedBatchJobs;

/* count failed interactive jobs */
extern uint32_t stat_failedInterJobs;

/* count remote jobs */
extern uint32_t stat_remoteJobs;

/* count ssh logins */
extern uint32_t stat_SSHLogins;

/* count successful local prologue executions */
extern uint32_t stat_lPrologue;

/* count successful remote prologue executions */
extern uint32_t stat_rPrologue;

/* count failed local prologue executions */
extern uint32_t stat_failedlPrologue;

/* count failed remote prologue executions */
extern uint32_t stat_failedrPrologue;

/* sum up the number of nodes */
extern uint64_t stat_numNodes;

/* sum up number of processes */
extern uint64_t stat_numProcs;

/* record psmom start time */
extern time_t stat_startTime;

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
