/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PLUGIN_COMM_HELPER
#define __PLUGIN_COMM_HELPER

#include "pscommon.h"
#include "psprotocol.h"

typedef enum {
    PSDATA_STRING = 0x03,
    PSDATA_TIME,
    PSDATA_INT16,
    PSDATA_INT32,
    PSDATA_INT64,
    PSDATA_UINT16,
    PSDATA_UINT32,
    PSDATA_UINT64,
    PSDATA_PID,
} PS_DataType_t;

typedef struct {
    char *buf;
    uint16_t bufSize;
    uint16_t bufUsed;
} PS_DataBuffer_t;

typedef void PS_DataBuffer_func_t(DDTypedBufferMsg_t *msg, char *data);

/**
 * @brief Add a string to a PS message buffer.
 *
 * @param msg The message itself.
 *
 * @param ptr A pointer to the current message buffer.
 *
 * @param string The string to add.
 *
 * @param func A pointer to the calling function.
 *
 * @return Returns a pointer to the next free space in the message buffer or
 * NULL on error.
 */
#define addStringToMsgBuf(msg, ptr, string) \
	    __addStringToMsgBuf(msg, ptr, string, __func__)
int __addStringToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
			const char *string, const char *caller);

/**
 * @brief Add a time type to a PS message buffer.
 *
 * @param msg The message itself.
 *
 * @param ptr A pointer to the current message buffer.
 *
 * @param time The time to add.
 *
 * @param func A pointer to the calling function.
 *
 * @return Returns a pointer to the next free space in the message buffer or
 * NULL on error.
 */
#define addTimeToMsgBuf(msg, ptr, time) \
		    __addTimeToMsgBuf(msg, ptr, time, __func__)
int __addTimeToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
				time_t *time, const char *caller);

/**
 * @brief Add a int32 type to a PS message buffer.
 *
 * @param msg The message itself.
 *
 * @param ptr A pointer to the current message buffer.
 *
 * @param time The integer to add.
 *
 * @param func A pointer to the calling function.
 *
 * @return Returns a pointer to the next free space in the message buffer or
 * NULL on error.
 */
#define addInt32ToMsgBuf(msg, ptr, val) \
	    __addInt32ToMsgBuf(msg, ptr, val, __func__)
int __addInt32ToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
				int32_t val, const char *caller);

/**
 * @brief Get a string from a message buffer.
 *
 * The string will be allocated using malloc() and has to be freed using free().
 *
 * @param ptr The pointer to the message buffer.
 *
 * @return Returns the read string or NULL on error.
 */
#define getStringFromMsgBufM(ptr) __getStringFromMsgBufM(ptr, __func__)
char *__getStringFromMsgBufM(char **ptr, const char *caller);

/**
 * @brief Get a string from a message buffer.
 *
 * @param ptr The pointer to the message buffer.
 *
 * @param buf The buffer to save the string.
 *
 * @param buflen The size of the buffer.
 *
 * @return Returns the read string or NULL on error.
 */
#define getStringFromMsgBuf(ptr, buf, buflen) \
	    __getStringFromMsgBuf(ptr, buf, buflen, __func__)
char *__getStringFromMsgBuf(char **ptr, char *buf, size_t buflen,
				const char *caller);

/**
 * @brief Get time from a message buffer.
 *
 * @param ptr The pointer to the message buffer.
 *
 * @param time Pointer to the time structure to save result in.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define getTimeFromMsgBuf(ptr, time) __getTimeFromMsgBuf(ptr, time, __func__)
int __getTimeFromMsgBuf(char **ptr, time_t *time, const char *caller);

/**
 * @brief Get a pid from a message buffer.
 *
 * @param ptr The pointer to the message buffer.
 *
 * @param time Pointer to the pid structure to save result in.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define getPidFromMsgBuf(ptr, pid) __getPidFromMsgBuf(ptr, pid, __func__)
int __getPidFromMsgBuf(char **ptr, pid_t *pid, const char *caller);

/**
 * @brief Get a int32 from a message buffer.
 *
 * @param ptr The pointer to the message buffer.
 *
 * @param time Pointer to the int32 value to save result in.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define getInt32FromMsgBuf(ptr, val) __getInt32FromMsgBuf(ptr, val, __func__)
int __getInt32FromMsgBuf(char **ptr, int32_t *val, const char *caller);

/**
 * @brief Add a string to a message.
 *
 * @param string The string to add.
 *
 * @param data The message buffer to save the string to.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define addStringToMsg(string, data) __addStringToMsg(string, data, __func__)
int __addStringToMsg(const char *string, PS_DataBuffer_t *data,
			const char *caller);

/**
 * @brief Add a int32 value to a message.
 *
 * @param val The int32 value to add.
 *
 * @param data The message buffer to save the int32 value to.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define addInt32ToMsg(val, data) __addInt32ToMsg(val, data, __func__)
int __addInt32ToMsg(const int32_t *val, PS_DataBuffer_t *data,
		    const char *caller);

/**
 * @brief Add a time structure to a message.
 *
 * @param time The time structure to add.
 *
 * @param data The message buffer to save the time structure to.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define addTimeToMsg(time, data) __addTimeToMsg(time, data, __func__)
int __addTimeToMsg(const time_t *time, PS_DataBuffer_t *data,
		    const char *caller);

/**
 * @brief Add a pid to a message.
 *
 * @param time The pid to add.
 *
 * @param data The message buffer to save the pid to.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define addPidToMsg(pid, data) __addPidToMsg(pid, data, __func__)
int __addPidToMsg(const pid_t *pid, PS_DataBuffer_t *data, const char *caller);

#endif
