/*
 *               ParaStation
 *
 * Copyright (C) 2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * psiloggermerge.h: ParaStation functions for output merging
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
void cacheOutput(PSLog_Msg_t msg, int outfd);

/**
 * @brief Init the merge structures/functions of the logger. This
 * function must be called bevor any other merge function.
 *
 * @return No return value.
 */
void outputMergeInit(void);

/**
 * @brief If more clients are connected than maxClients,
 * the caches have to be reallocated so all new clients can be
 * handled.
 *
 * @param newSize The new size of the next max client.
 *
 * @return No return value.
 */
void reallocClientOutBuf(int newSize);

#endif
