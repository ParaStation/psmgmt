/*
 *               ParaStation
 *
 * Copyright (C) 2007-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * ParaStation key value space -- client part
 *
 * $Id$ 
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 */

#ifndef __KVS_H
#define __KVS_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** 
 * @brief Init the kvs structure. This must be called 
 * bevor any other kvs call.
 *
 * @return No return value.
 */
void kvs_init(void);

/** 
 * @brief Creates a new kvs with the specifyed name.
 *
 * @param name Name of the kvs to create.
 *
 * @return Returns 0 on success, and 1 if an error occured.
 */
int kvs_create(char *name);

/** 
 * @brief Destroys a kvs with the specifyed name.
 *
 * @param name The name of the kvs to destroy.
 *
 * @return Returns 0 on success, and 1 if an error occured.
 */
int kvs_destroy(char *name);

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

/** 
 * @brief Read the name of a kvs by index. 
 *
 * @param index The index of the kvs.
 *
 * @return Returns the name of a kvs by index or 0 on error.
 */
char *kvs_getKvsNameByIndex(int index);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __KVS_H */
