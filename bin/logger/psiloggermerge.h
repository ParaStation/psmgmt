/*
 *               ParaStation
 *
 * Copyright (C) 2007-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * ParaStation functions for output merging in logger
 *
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PSILOGGERMERGE
#define __PSILOGGERMERGE

#include "pslog.h"

/**
 * @brief Print all collected output which is already merged, or a
 * timeout is reached.
 *
 * @param flush If set to 1 any output is flushed without waiting for
 * a timeout.
 *
 * @return No return value.
 */
void displayCachedOutput(int flush);

/**
 * @brief Cache the received output msg.
 *
 * @param msg The received msg which holds the buffer to cache.
 *
 * @param outfd The file descriptor to write the msgs to.
 *
 * @return No return value.
 */
void cacheOutput(PSLog_Msg_t *msg, int outfd);

/**
 * @brief Init the merge structures/functions of the logger.
 *
 * Initialize the merge structures and functions of the logger. After
 * calling this function, the merger is prepared to handle clients
 * with a maximum rank of @a maxRank.
 *
 * This function must be called bevor any other merge function.
 *
 * @param maxRank The maximum rank to handle.
 *
 * @return No return value.
 */
void outputMergeInit(void);

#endif
