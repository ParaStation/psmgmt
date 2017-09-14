/*
 * ParaStation
 *
 * Copyright (C) 2007-2017 ParTec Cluster Competence Center GmbH, Munich
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
 * @brief Saves a value in the kvs.
 *
 * @param kvsname The name of the kvs.
 *
 * @param name The name of the value to save.
 *
 * @param value The value to save in the kvs.
 *
 * @return Returns 0 on success, and 1 if an error occured.
 */
int kvs_put(char *kvsname, char *name, char *value);

/**
 * @brief Saves a value in the kvs.
 *
 * @param kvsname The name of the kvs.
 *
 * @param name The name of the value to save.
 *
 * @param value The value to save in the kvs.
 *
 * @param index The integer which will receive the index of the put value
 * or -1 on * error.
 *
 * @return Returns 0 on success, and 1 if an error occured.
 */
int kvs_putIdx(char *kvsname, char *name, char *value, int *index);

/**
 * @brief Read a value from a kvs.
 *
 * @param kvsname The name of the kvs.
 *
 * @param name The name of the value to read.
 *
 * @return Returns the requested kvs value or 0 if an error occured.
 */
char *kvs_get(char *kvsname, char *name);

/**
 * @brief Read a value from a kvs.
 *
 * @param kvsname The name of the kvs.
 *
 * @param name The name of the value to read.
 *
 * @param index The integer will receive the index in the kvs environment.
 *
 * @return Returns the requested kvs value or 0 if an error occured.
 */
char *kvs_getIdx(char *kvsname, char *name, int *index);

/**
 * @brief Read a value by index from kvs.
 *
 * @param kvsname The name of the kvs.
 *
 * @param index The index of the kvs value.
 *
 * @return Returns the value in format name=value or 0 if an error
 * occured.
 */
char * kvs_getbyidx(char *kvsname, int index);

/**
 * @brief Count the values in a kvs.
 *
 * @param The name of the kvs.
 *
 * @return Returns the number of values or -1 if an error occured.
 */
int kvs_count_values(char *kvsname);

/**
 * @brief Count the number of kvs created.
 *
 * @return The number of kvs created.
 */
int kvs_count(void);

#endif  /* __KVS_H */
