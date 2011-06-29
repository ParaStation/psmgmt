/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __PS_ACCOUNT_HELPER
#define __PS_ACCOUNT_HELPER

#include <stdint.h>

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
void *umalloc(size_t size, const char *func);

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
void *urealloc(void *old ,size_t size, const char *func);

#endif
