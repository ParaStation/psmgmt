/*
 * ParaStation
 *
 * Copyright (C) 2007-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __KVS_H
#define __KVS_H

#include <stdbool.h>

/**
 * @brief Creates a new kvs with the specifyed name.
 *
 * @param name Name of the kvs to create.
 *
 * In case of running out of memory this function will exit() execution.
 *
 * @return Return true on success, or false in case of error
 */
bool kvs_create(char *name);

/**
 * @brief Destroys a kvs with the specifyed name.
 *
 * @param name The name of the kvs to destroy.
 *
 * @return Return true on success, or false in case of error
 */
bool kvs_destroy(char *name);

/**
 * @brief Save a key-value pair to the kvs
 *
 * @param kvsname Name of the kvs
 *
 * @param name Name of the value to save
 *
 * @param value Value to save in the kvs
 *
 * @return Returns true on success, or false if an error occurred
 */
bool kvs_set(char *kvsname, char *name, char *value);

/**
 * @brief Read a value from a kvs
 *
 * @param kvsname Name of the kvs
 *
 * @param name Name of the value to read
 *
 * @return Returns the requested kvs value or NULL if an error occurred
 */
char *kvs_get(char *kvsname, char *name);

/**
 * @brief Read a value by index from kvs
 *
 * @param kvsname Name of the kvs
 *
 * @param index Index of the kvs value
 *
 * @return Returns the value in format name=value or NULL if an error occurred
 */
char * kvs_getbyidx(char *kvsname, int index);

/**
 * @brief Count the values in a kvs
 *
 * @param kvsname Name of the kvs
 *
 * @return Returns the number of values or -1 if an error occurred
 */
int kvs_count_values(char *kvsname);

/**
 * @brief Count the number of kvs created
 *
 * @return The number of kvs created
 */
int kvs_count(void);

#endif  /* __KVS_H */
