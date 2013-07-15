/*
 *               ParaStation
 *
 * Copyright (C) 2010 - 2012 ParTec Cluster Competence Center GmbH, Munich
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
 * @brief Save a string into a buffer and let it dynamically grow if needed.
 *
 * @param strSave The string to write to the buffer.
 *
 * @param buffer The buffer to write the string to.
 *
 * @param bufSize The current size of the buffer.
 *
 * @return Returns a pointer to the buffer.
 */
char *str2Buf(char *strSave, char *buffer, size_t *bufSize);

/**
 * @brief Convert a time string to seconds.
 */
unsigned long stringTimeToSec(char *wtime);

int strToInt(char *string);
unsigned long sizeToBytes(char *string);
int removeDir(char *directory, int root);

#endif
