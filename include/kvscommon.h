/*
 * ParaStation
 *
 * Copyright (C) 2007-2019 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __KVSCOMMON_H
#define __KVSCOMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* some pmi definitions */
#define PMI_SUCCESS 0
#define PMI_ERROR -1

/** maximal size of a pmi command */
#define PMIU_MAXLINE 1024

/** maximal size of a kvs name */
#define PMI_KVSNAME_MAX 128

/** maximal size of a kvs key */
#define PMI_KEYLEN_MAX  32

/** maximal size of a kvs value */
//#define PMI_VALLEN_MAX 1024
#define PMI_VALLEN_MAX (PMIU_MAXLINE - PMI_KVSNAME_MAX - PMI_KEYLEN_MAX - 48)

/** maximal number of arguments for pmi spawn */
#define PMI_SPAWN_MAX_ARGUMENTS	256

typedef enum {
    PUT = 10,		    /** put a kv-pair into global kvs */
    DAISY_SUCC_READY,	    /** the successor is ready to receive messages
				via daisy-chain */
    DAISY_BARRIER_IN,	    /** barrier-in over daisy-chain */
    DAISY_BARRIER_OUT,	    /** barrier-out over daisy-chain */
    UPDATE_CACHE,	    /** distribute the global kvs to all clients */
    UPDATE_CACHE_FINISH,    /** the end of the global kvs cache distribution */
    JOIN,		    /** forwarder joined to the global kvs */
    INIT,		    /** mpi client joined to the global kvs */
    LEAVE,		    /** a mpi client finished and leaves the
				global kvs */
    NOT_AVAILABLE,	    /** global kvs is not available */
    CHILD_SPAWN_RES,	    /** spawned child tells parent its alive */
} PSKVS_cmd_t;

const char *PSKVScmdToString(PSKVS_cmd_t cmd);

/**
 * @brief Set the delimiters for parsing a pmi buffer.
 *
 * @param newDelim The new delimiters to use. Use NULL to reset
 * to the default.
 *
 * @return No return value.
 */
void setPMIDelim(const char *newDelim);

/**
 * @brief Extract a single value from a pmi message.
 *
 * @param name Name (key) of the value to extract.
 *
 * @param vbuffer Buffer with the msg to extract from.
 *
 * @param pmivalue The buffer which receives the extracted value.
 *
 * @param vallen The size of the buffer which receives the extracted
 * value.
 *
 * @return On Success true is returned, or false on error.
 */
bool getpmiv(char *name, char *vbuffer, char *pmivalue, size_t vallen);

/**
 * @brief Extract a single value from a pmi message
 *
 * @param name Name (key) of the value to extract.
 *
 * @param vbuffer Buffer with the msg to extract from.
 *
 * @return Returns the requested value or NULL on error. The value is
 * allocated using malloc() and must be freed after use.
 */
char *getpmivm(char *name, char *vbuffer);

/**
 * @brief Extract a kvs command from a message buffer.
 *
 * @param ptr Pointer to a buffer which holds the message.
 *
 * @return Retuns the extracted command.
 */
int getKVSCmd(char **ptr);

/**
 * @brief Add a kvs cmd to a message buffer.
 *
 * Note: The pointer to the buffer and buffer size will be shifted
 * accordingly.
 *
 * @param ptr Pointer to the message buffer to add the msg to.
 *
 * @param len Pointer to the length of the message buffer.
 *
 * @param type The command to add.
 */
void setKVSCmd(char **ptr, size_t *len, PSKVS_cmd_t cmd);

/**
 * @brief Add a kvs string to a message buffer.
 *
 * Note: The pointer to the buffer and buffer size will be shifted
 * accordingly.
 *
 * @param ptr Pointer to the message buffer to add the msg to.
 *
 * @param bufSize Pointer to the length of the message buffer.
 *
 * @param string The string to add.
 *
 * @return Returns the length of the string added.
 */
int addKVSString(char **ptr, size_t *bufSize, char *string);

/**
 * @brief Extract a kvs string from a message buffer.
 *
 * @param ptr Pointer to a buffer which holds the message.
 *
 * @param buf Buffer which will receive the extracted string.
 *
 * @param bufSize The size of the buffer.
 *
 * @return Retuns the length of the extract string or -1 on error.
 */
int getKVSString(char **ptr, char *buf, size_t bufSize);

/**
 * @brief Extract a int32 from a message buffer.
 *
 * @param ptr Pointer to a buffer which holds the message.
 *
 * @return Retuns the requested value.
 */
int32_t getKVSInt32(char **ptr);

/**
 * @brief Add a int32 to a message buffer.
 *
 * Note: The pointer to the buffer and buffer size will be shifted
 * accordingly.
 *
 * @param ptr Pointer to the message buffer to add the msg to.
 *
 * @param len Pointer to the length of the message buffer.
 *
 * @param num The int32 to add.
 *
 * @return No return value.
 */
void addKVSInt32(char **ptr, size_t *len, int32_t *num);

#endif  /* __KVSCOMMON_H */
