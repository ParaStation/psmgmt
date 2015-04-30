/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2015 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PLUGIN_LIB_COMM
#define __PLUGIN_LIB_COMM

#include "pscommon.h"
#include "psprotocol.h"


typedef enum {
    PSDATA_STRING = 0x03,
    PSDATA_TIME,
    PSDATA_INT8,
    PSDATA_INT16,
    PSDATA_INT32,
    PSDATA_INT64,
    PSDATA_UINT8,
    PSDATA_UINT16,
    PSDATA_UINT32,
    PSDATA_UINT64,
    PSDATA_PID,
    PSDATA_MEM,
    PSDATA_DOUBLE,
    PSDATA_DATA,
} PS_DataType_t;

typedef struct {
    char *buf;
    uint32_t bufSize;
    uint32_t bufUsed;
} PS_DataBuffer_t;

typedef void PS_DataBuffer_func_t(DDTypedBufferMsg_t *msg,
				    PS_DataBuffer_t *data);

/**
 * @brief Write data to a file descriptor.
 *
 * Write data to a file descripter and retry on minor errors until all data was
 * written.
 *
 * @param fd The socket to write to.
 *
 * @param buffer The buffer holding the data to write.
 *
 * @param towrite The number of bytes to write.
 *
 * @return Returns the number of bytes written or -1 on error.
 */
#define doWrite(fd, buffer, towrite) __doWrite(fd, buffer, towrite, __func__, 0,  0)
#define doWriteP(fd, buffer, towrite) __doWrite(fd, buffer, towrite, __func__, 1, 0)
#define doWriteF(fd, buffer, towrite) __doWrite(fd, buffer, towrite, __func__, 1, 1)
int __doWrite(int fd, void *buffer, size_t towrite, const char *func,
		int pedantic, int fini);

/**
 * @brief Read data from a file descriptor.
 *
 * @param fd The socket to read the data from.
 *
 * @param buffer The buffer to write the data to.
 *
 * @param toread The number of bytes to read.
 *
 * @return Returns the number of bytes read or -1 on error.
 */
#define doRead(fd, buffer, toread) __doRead(fd, buffer, toread, __func__, 0)
#define doReadP(fd, buffer, toread) __doRead(fd, buffer, toread, __func__, 1)
int __doRead(int fd, void *buffer, size_t toread, const char *func,
		int pedantic);

#define doReadExt(fd, buffer, toread, ret) __doReadExt(fd, buffer, toread, ret, __func__, 0)
#define doReadExtP(fd, buffer, toread, ret) __doReadExt(fd, buffer, toread, ret, __func__, 1)
int __doReadExt(int fd, void *buffer, size_t toread, size_t *ret,
		    const char *func, int pedantic);

#define listenUnixSocket(sock) __listenUnixSocket(sock,  __func__)
int __listenUnixSocket(char *socketName, const char *func);

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

#define getInt8(ptr, val) __getInt8(ptr, val, __func__, __LINE__)
int __getInt8(char **ptr, int8_t *val, const char *caller, const int line);

#define getInt16(ptr, val) __getInt16(ptr, val, __func__, __LINE__)
int __getInt16(char **ptr, int16_t *val, const char *caller, const int line);

#define getInt32(ptr, val) __getInt32(ptr, val, __func__, __LINE__)
int __getInt32(char **ptr, int32_t *val, const char *caller, const int line);

#define getInt64(ptr, val) __getInt64(ptr, val, __func__, __LINE__)
int __getInt64(char **ptr, int64_t *val, const char *caller, const int line);

#define getUint8(ptr, val) __getUint8(ptr, val, __func__, __LINE__)
int __getUint8(char **ptr, uint8_t *val, const char *caller, const int line);

#define getUint16(ptr, val) __getUint16(ptr, val, __func__, __LINE__)
int __getUint16(char **ptr, uint16_t *val, const char *caller, const int line);

#define getUint32(ptr, val) __getUint32(ptr, val, __func__, __LINE__)
int __getUint32(char **ptr, uint32_t *val, const char *caller, const int line);

#define getUint64(ptr, val) __getUint64(ptr, val, __func__, __LINE__)
int __getUint64(char **ptr, uint64_t *val, const char *caller, const int line);

#define getUint16Array(ptr, val, len) \
	    __getUint16Array(ptr, val, len, __func__, __LINE__)
int __getUint16Array(char **ptr, uint16_t **val, uint32_t *len,
			const char *caller, const int line);

#define getUint32Array(ptr, val, len) \
	    __getUint32Array(ptr, val, len, __func__, __LINE__)
int __getUint32Array(char **ptr, uint32_t **val, uint32_t *len,
			const char *caller, const int line);

#define getInt16Array(ptr, val, len) \
	    __getInt16Array(ptr, val, len, __func__, __LINE__)
int __getInt16Array(char **ptr, int16_t **val, uint32_t *len,
			const char *caller, const int line);

#define getInt32Array(ptr, val, len) \
	    __getInt32Array(ptr, val, len, __func__, __LINE__)
int __getInt32Array(char **ptr, int32_t **val, uint32_t *len,
			const char *caller, const int line);

#define getDouble(ptr, val) __getDouble(ptr, val, __func__, __LINE__)
int __getDouble(char **ptr, double *val, const char *caller, const int line);

#define getTime(ptr, time) __getTime(ptr, time, __func__, __LINE__)
int __getTime(char **ptr, time_t *time, const char *caller, const int line);

#define getPid(ptr, pid) __getPid(ptr, pid, __func__, __LINE__)
int __getPid(char **ptr, pid_t *pid, const char *caller, const int line);

#define getStringM(ptr) __getStringM(ptr, __func__, __LINE__)
char *__getStringM(char **ptr, const char *caller, const int line);

#define getStringML(ptr, size) __getStringML(ptr, size, __func__, __LINE__)
char *__getStringML(char **ptr, size_t *len, const char *caller, const int line);

#define getString(ptr, buf, buflen) \
	    __getString(ptr, buf, buflen, __func__, __LINE__)
char *__getString(char **ptr, char *buf, size_t buflen,
		    const char *caller, const int line);

#define getStringArrayM(ptr, array, len) \
	__getStringArrayM(ptr, array, len, __func__, __LINE__)
int __getStringArrayM(char **ptr, char ***array, uint32_t *len,
			const char *caller, const int line);

#define getDataM(ptr, len) __getDataM(ptr, len, __func__, __LINE__)
void *__getDataM(void **ptr, uint32_t *len, const char *caller, const int line);

/**
 * @brief Add a string to a message.
 *
 * @param string The string to add.
 *
 * @param data The message buffer to save the string to.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define addStringToMsg(string, data) \
		__addStringToMsg(string, data, __func__, __LINE__)
int __addStringToMsg(const char *string, PS_DataBuffer_t *data,
			const char *caller, const int line);

#define addStringArraytoMsg(array, len, data) \
	__addStringArrayToMsg(array, len, data, __func__, __LINE__)
int __addStringArrayToMsg(char **array, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller,
			    const int line);

/**
 * @brief Add a int32 value to a message.
 *
 * @param val The int32 value to add.
 *
 * @param data The message buffer to save the int32 value to.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define addInt32ToMsg(val, data) __addInt32ToMsg(val, data, __func__, __LINE__)
int __addInt32ToMsg(const int32_t val, PS_DataBuffer_t *data,
		    const char *caller, const int line);

#define addUint32ToMsg(val, data) \
	__addUint32ToMsg(val, data, __func__, __LINE__)
int __addUint32ToMsg(const uint32_t val, PS_DataBuffer_t *data,
		    const char *caller, const int line);

#define addUint64ToMsg(val, data) \
	__addUint64ToMsg(val, data, __func__, __LINE__)
int __addUint64ToMsg(const uint64_t val, PS_DataBuffer_t *data,
		    const char *caller, const int line);

#define addDoubleToMsg(val, data) \
	__addDoubleToMsg(val, data, __func__, __LINE__)
int __addDoubleToMsg(double val, PS_DataBuffer_t *data,
		    const char *caller, const int line);

#define addInt16ToMsg(val, data) \
	__addInt16ToMsg(val, data, __func__, __LINE__)
int __addInt16ToMsg(const int16_t val, PS_DataBuffer_t *data,
		    const char *caller, const int line);

#define addUint16ToMsg(val, data) \
	__addUint16ToMsg(val, data, __func__, __LINE__)
int __addUint16ToMsg(const uint16_t val, PS_DataBuffer_t *data,
		    const char *caller, const int line);

#define addUint8ToMsg(val, data) __addUint8ToMsg(val, data, __func__, __LINE__)
int __addUint8ToMsg(const uint8_t val, PS_DataBuffer_t *data,
		    const char *caller, const int line);

#define addUint16ArrayToMsg(val, len, data) \
	__addUint16ArrayToMsg(val, len, data, __func__, __LINE__)
int __addUint16ArrayToMsg(const uint16_t *val, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller,
			    const int line);

#define addUint32ArrayToMsg(val, len, data) \
	__addUint32ArrayToMsg(val, len, data, __func__, __LINE__)
int __addUint32ArrayToMsg(const uint32_t *val, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller,
			    const int line);

#define addInt16ArrayToMsg(val, len, data) \
	__addInt16ArrayToMsg(val, len, data, __func__, __LINE__)
int __addInt16ArrayToMsg(const int16_t *val, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller,
			    const int line);

#define addInt32ArrayToMsg(val, len, data) \
	__addInt32ArrayToMsg(val, len, data, __func__, __LINE__)
int __addInt32ArrayToMsg(const int32_t *val, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller,
			    const int line);

/**
 * @brief Add a time structure to a message.
 *
 * @param time The time structure to add.
 *
 * @param data The message buffer to save the time structure to.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define addTimeToMsg(time, data) __addTimeToMsg(time, data, __func__, __LINE__)
int __addTimeToMsg(const time_t *time, PS_DataBuffer_t *data,
		    const char *caller, const int line);

/**
 * @brief Add a pid to a message.
 *
 * @param time The pid to add.
 *
 * @param data The message buffer to save the pid to.
 *
 * @return Returns 1 on success and 0 on error.
 */
#define addPidToMsg(pid, data) __addPidToMsg(pid, data, __func__, __LINE__)
int __addPidToMsg(const pid_t pid, PS_DataBuffer_t *data, const char *caller,
		    const int line);

#define addMemToMsg(mem, memLen, data) \
	__addMemToMsg(mem, memLen, data, __func__, __LINE__)
int __addMemToMsg(void *mem, uint32_t memLen, PS_DataBuffer_t *data,
		    const char *caller, const int line);

#define addDataToMsg(buf, bufLen, data) \
	__addDataToMsg(buf, bufLen, data, __func__, __LINE__)
int __addDataToMsg(const void *buf, uint32_t bufLen, PS_DataBuffer_t *data,
		    const char *caller, const int line);

int setByteOrder(int val);

int setTypeInfo(int val);

#endif
