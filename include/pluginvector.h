/*
 * ParaStation
 *
 * Copyright (C) 2018 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/*
 * This small library is a collection of functions for easy handling of dynamic
 * vectors.
 */
#ifndef __PLUGIN_LIB_VECTOR
#define __PLUGIN_LIB_VECTOR

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t chunksize;  /**< size of chunks in byte */
    uint16_t typesize;   /**< size of the type == size of each entry */
    size_t allocated;    /**< current space allocated in byte */
    size_t len;          /**< number of entries */
    void *data;          /**< actual vector */
} vector_t;

/**
 * @brief Initialize a vector
 *
 * @param vector     pointer to an uninitialized vector object
 * @param initcount  number of entries to initially allocate memory for
 * @param chunkcount number of entries building one allocation chunk
 * @param typesize   size of one entry
 * @param func       calling function
 * @param line       calling line
 */
void __vectorInit(vector_t *vector, size_t initcount, uint16_t chunkcount,
	uint16_t typesize, const char *func, const int line);

/**
 * @brief Initialize a vector
 *
 * @param v   pointer to an uninitialized vector object
 * @param i   number of entries to initially allocate memory for
 * @param c   number of entries building one allocation chunk
 * @param t   type of the entries
 */
#define vectorInit(v, i, c, t) \
    __vectorInit(v, i, c, sizeof(t), __func__, __LINE__)

/**
 * @brief Add chunk of entries to the end of the vector
 *
 * @param vector     pointer to an initialized vector object
 * @param new        pointer to the new entries to add (will be copied)
 * @param count      number of entries to add
 * @param func       calling function
 * @param line       calling line
 */
void __vectorAddCount(vector_t *vector, void *new, size_t count,
	const char *func, const int line);

/**
 * @brief Add chunk of entries to the end of the vector
 *
 * @param v     pointer to an initialized vector object
 * @param n     pointer to the new entries to add (will be copied)
 * @param c     number of entries to add
 */
#define vectorAddCount(v, n, c) __vectorAddCount(v, n, c, __func__, __LINE__)

/**
 * @brief Add entry to the end of the vector
 *
 * @param v     pointer to an initialized vector object
 * @param n     pointer to the new entry to add (will be copied)
 */
#define vectorAdd(v, n) __vectorAddCount(v, n, 1, __func__, __LINE__)

/**
 * @brief Get entry at @a index in the vector
 *
 * @param vector     pointer to an initialized vector object
 * @param index      index to get the entry of
 * @param func       calling function
 * @param line       calling line
 *
 * @return Returns a pointer to the entry in the vector (do not free)
 */
void * __vectorGet(vector_t *vector, size_t index, const char *func,
	const int line);

/**
 * @brief Get entry at @a index in the vector
 *
 * @param v     pointer to an initialized vector object of matching length
 * @param i     index to get the entry of
 * @param t     type of the entries (for casting return value)
 *
 * @return Returns a pointer to the entry in the vector (do not free)
 */
#define vectorGet(v, i, t) ((t *)__vectorGet(v, i, __func__, __LINE__))

/**
 * @brief Checks if the vector contains the given entry
 *
 * @param vector     pointer to an initialized vector object
 * @param entry      pointer to entry type object to compare entries with
 * @param func       calling function
 * @param line       calling line
 *
 * @return Returns true if a matching entry is found, false else
 */
bool __vectorContains(vector_t *vector, void *entry, const char *func,
	const int line);

/**
 * @brief Checks if the vector contains the given entry
 *
 * @param v     pointer to an initialized vector object
 * @param e     pointer to entry type object to compare entries with
 *
 * @return Returns true if a matching entry is found, false else
 */
#define vectorContains(v, e) __vectorContains(v, e, __func__, __LINE__)

/**
 * @brief Sorts the vector using qsort()
 *
 * @param vector     pointer to an initialized vector object
 * @param compar     compare function (see qsort(3) manpage)
 * @param func       calling function
 * @param line       calling line
 */
void __vectorSort(vector_t *vector, int (*compar)(const void *, const void *),
	const char *func, const int line);

/**
 * @brief Sorts the vector using qsort()
 *
 * @param v     pointer to an initialized vector object
 * @param c     compare function (see qsort(3) manpage)
 */
#define vectorSort(v, c) __vectorSort(v, c, __func__, __LINE__)

/**
 * @brief Destroy vector
 *
 * @param vector     pointer to an uninitialized vector object
 * @param func       calling function
 * @param line       calling line
 */
void __vectorDestroy(vector_t *vector, const char *func, const int line);

/**
 * @brief Destroy vector
 *
 * @param v     pointer to an initialized vector object
 */
#define vectorDestroy(v) __vectorDestroy(v, __func__, __LINE__)

int __compare_char (const void *a, const void *b);
int __compare_int (const void *a, const void *b);
int __compare_uint32 (const void *a, const void *b);

#define charvInit(v, c) vectorInit(v, c, c, char)
#define charvAdd(v, n) vectorAdd(v, (void *)n)
#define charvAddCount(v, n, c) vectorAddCount(v, (void *)n, c)
#define charvGet(v, i) vectorGet(v, i, char)
#define charvContains(v, e) vectorContains(v, e)
#define charSort(v) vectorSort(v, __compare_char)
#define charvDestroy(v) vectorDestroy(v)

#define intvInit(v, c) vectorInit(v, c, c, int)
#define intvAdd(v, n) vectorAdd(v, (void *)n)
#define intvAddCount(v, n, c) vectorAddCount(v, (void *)n, c)
#define intvGet(v, i) vectorGet(v, i, int)
#define intvContains(v, e) vectorContains(v, e)
#define intvSort(v) vectorSort(v, __compare_int)
#define intvDestroy(v) vectorDestroy(v)

#define uint32vInit(v, c) vectorInit(v, c, c, uint32_t)
#define uint32vAdd(v, n) vectorAdd(v, (void *)n)
#define uint32vAddCount(v, n, c) vectorAddCount(v, (void *)n, c)
#define uint32vGet(v, i) vectorGet(v, i, uint32_t)
#define uint32vContains(v, e) vectorContains(v, e)
#define uint32vSort(v) vectorSort(v, __compare_uint32)
#define uint32vDestroy(v) vectorDestroy(v)

#endif  /* __PLUGIN_LIB_VECTOR */
