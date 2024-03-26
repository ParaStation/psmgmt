/*
 * ParaStation
 *
 * Copyright (C) 2010-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_CONV
#define __PS_MOM_CONV

#include <stddef.h>

#include "list.h"

#include "psmomcomm.h"
#include "psmomlist.h"

typedef struct {
    char *name;
    int ignore;
} Data_Filter_t;

/** the standard job data filter */
extern const Data_Filter_t DataFilterJob[];

/**
 * @brief Read a PBS encoded signed integer.
 *
 * @param com The communication handle to use.
 *
 * @param digit The pointer to a signed integer where the digit will be saved.
 *
 * @return Returns 0 on success or -1 on error.
 */
int ReadDigitI(ComHandle_t *com, signed int *digit);

/**
 * @brief Read a PBS encoded unsigned integer.
 *
 * @param com The communication handle to use.
 *
 * @param digit The pointer to a unsigned integer where the digit will be saved.
 *
 * @return Returns 0 on success or -1 on error.
 */
int ReadDigitUI(ComHandle_t *com, unsigned int *digit);

/**
 * @brief Read a PBS encoded signed long integer.
 *
 * @param com The communication handle to use.
 *
 * @param digit The pointer to a signed long where the digit will be saved.
 *
 * @return Returns 0 on success or -1 on error.
 */
int ReadDigitL(ComHandle_t *com, signed long *digit);

/**
 * @brief Read a PBS encoded unsigned long integer.
 *
 * @param com The communication handle to use.
 *
 * @param digit The pointer to a unsigned long where the digit will be saved.
 *
 * @return Returns 0 on success or -1 on error.
 */
int ReadDigitUL(ComHandle_t *com, unsigned long *digit);

/**
 * @brief Read a PBS encoded string.
 *
 * @param com The communication handle to use.
 *
 * @param buffer Pointer to a buffer to write the string to.
 *
 * @param len The size of the buffer.
 *
 * @return Returns the number of bytes read or -1 on error.
 */
#define ReadString(com, buffer, len) __ReadString(com, buffer, len, __func__)
int __ReadString(ComHandle_t *com, char *buffer, size_t len, const char *caller);

/**
 * @brief Read a PBS encoded string.
 *
 * This function will use malloc to allocate the space for the string it will
 * read. The returned string must be deallocated using ufree().
 *
 * @param com The communication handle to use.
 *
 * @param len The length of the string to read.
 *
 * @return Returns a pointer to the read string or NULL on error.
 */
#define ReadStringEx(com, len)  __ReadStringEx(com, len, __func__)
char *__ReadStringEx(ComHandle_t *com, size_t *len, const char *func);

/**
 * @brief Read a PBS data structure.
 *
 * Read a PBS data structure holding various job information. The
 * filter can include/exclude defined entries and will generate a
 * warning if unknown entries or read.
 *
 * @param com The communication handle to use.
 *
 * @param len The number of data entries in the structure to read.
 *
 * @param list The list to save the read entries in.
 *
 * @param filter A pointer to a data filter.
 *
 * @return Returns 0 on success and -1 on error.
 */
int ReadDataStruct(ComHandle_t *com, size_t len, list_t *list,
    const Data_Filter_t *filter);

/**
 * @brief Write a PBS encoded digit to the communication buffer.
 *
 * @param com The communication handle to use.
 *
 * @param digit The digit to write.
 *
 * @return Returns the number of bytes written or -1 on error.
 */
int WriteDigit(ComHandle_t *com, long digit);

/**
 * @brief Write a PBS encoded string to the communication buffer.
 *
 * @param com The communication handle to use.
 *
 * @param data The string to write.
 *
 * @return Returns the number of bytes written or -1 on error.
 */
int WriteString(ComHandle_t *com, char *data);

/**
 * @brief Write a PBS data structure.
 *
 * @param com The communication handle to use.
 *
 * @param data A pointer to a data structure to write.
 *
 * @return Returns 1 on success and -1 on error.
 */
int WriteDataStruct(ComHandle_t *com, Data_Entry_t *data);

#endif  /* __PS_MOM_CONV */
