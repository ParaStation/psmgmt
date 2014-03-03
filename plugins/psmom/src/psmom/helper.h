/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_MOM_HELPER
#define __PS_MOM_HELPER

#include <stdint.h>

struct convTable {
    char *format;
    uint64_t mult;
};

/**
 * @brief Convert a time string to seconds.
 */
unsigned long stringTimeToSec(char *wtime);

/**
 * @brief Convert a size string to bytes.
 *
 * @param string The string to convert to.
 *
 * @return Returns the convert string or 0 on error.
 */
unsigned long sizeToBytes(char *string);

int strToInt(char *string);

int removeDir(char *directory, int root);

#endif
