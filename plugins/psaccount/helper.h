/*
 * ParaStation
 *
 * Copyright (C) 2010-2012 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Authors:     Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_ACCOUNT_HELPER
#define __PS_ACCOUNT_HELPER

#include <stdint.h>

#define umalloc(size) __umalloc(size, __func__, __LINE__)
#define urealloc(old, size) __urealloc(old, size, __func__, __LINE__)


/**
 * @brief Malloc with error handling.
 *
 * Call malloc and handle errors.
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Function name of the calling function.
 *
 * @return Returned is a pointer to the allocated memory.
 */
void *__umalloc(size_t size, const char *func, const int line);

/**
 * @brief Realloc with error handling.
 *
 * Call realloc and handle errors.
 *
 * @param size Size in bytes to allocate.
 *
 * @param func Function name of the calling function.
 *
 * @return Returned is a pointer to the allocated memory.
 */
void *__urealloc(void *old ,size_t size, const char *func, const int line);

#endif
