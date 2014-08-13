/*
 * ParaStation
 *
 * Copyright (C) 2012 - 2014 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "pluginmalloc.h"
#include "pluginlog.h"
#include "psidcomm.h"

#include "plugincomm.h"

#  define UINT64_SWAP_LE_BE(val)      ((uint64_t) (                           \
        (((uint64_t) (val) &                                                  \
          (uint64_t) (0x00000000000000ffU)) << 56) |                          \
        (((uint64_t) (val) &                                                  \
          (uint64_t) (0x000000000000ff00U)) << 40) |                          \
        (((uint64_t) (val) &                                                  \
          (uint64_t) (0x0000000000ff0000U)) << 24) |                          \
        (((uint64_t) (val) &                                                  \
          (uint64_t) (0x00000000ff000000U)) <<  8) |                          \
        (((uint64_t) (val)                  >>  8) &                          \
          (uint64_t) (0x00000000ff000000U))        |                          \
        (((uint64_t) (val)                  >> 24) &                          \
          (uint64_t) (0x0000000000ff0000U))        |                          \
        (((uint64_t) (val)                  >> 40) &                          \
          (uint64_t) (0x000000000000ff00U))        |                          \
        (((uint64_t) (val)                  >> 56) &                          \
          (uint64_t) (0x00000000000000ffU)) ))

#if SLURM_BIGENDIAN
# define HTON_int64(x)    ((int64_t)  (x))
# define NTOH_int64(x)    ((int64_t)  (x))
# define HTON_uint64(x)   ((uint64_t) (x))
# define NTOH_uint64(x)   ((uint64_t) (x))
#else
# define HTON_int64(x)    ((int64_t) UINT64_SWAP_LE_BE (x))
# define NTOH_int64(x)    ((int64_t) UINT64_SWAP_LE_BE (x))
# define HTON_uint64(x)   UINT64_SWAP_LE_BE (x)
# define NTOH_uint64(x)   UINT64_SWAP_LE_BE (x)
#endif

#define FLOAT_CONVERT 1000000

static int debug = 0;

static int byteOrder = 1;

static int typeInfo = 0;

int setByteOrder(int val)
{
    int old = byteOrder;
    byteOrder = val;
    return old;
}

int setTypeInfo(int val)
{
    int old = typeInfo;
    typeInfo = val;
    return old;
}

void addDataType(char **ptr, PS_DataType_t type)
{
    *(uint8_t *) *ptr = type;
    *ptr += sizeof(uint8_t);
}

int verifyTypeInfo(char **ptr, PS_DataType_t expectedType, const char *caller)
{
    uint8_t type;

    if (!typeInfo) return 1;

    type = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);

    if (type != expectedType) {
	if (debug) {
	    pluginlog("%s(%s): error got type '%i' should be '%i'\n",
			__func__, caller, type, expectedType);
	}
	return 0;
    }
    return 1;
}

int __doWrite(int fd, void *buffer, size_t towrite, const char *func,
			int pedantic)
{
    ssize_t ret;

    while (1) {
	if ((ret = write(fd, buffer, towrite)) == -1) {
	    if (errno == EINTR || errno == EAGAIN) continue;

	    pluginlog("%s (%s): write to fd '%i' failed (%i): %s\n", __func__,
		    func, fd, errno, strerror(errno));
	    return -1;
	} if (pedantic && (size_t) ret != towrite) {
	    pluginlog("%s (%s): not all data written to fd '%i' towrite "
		    "'%zu' written '%zu'\n", __func__, func, fd, towrite, ret);
	}
	return ret;
    }
    return ret;
}

#define MAX_RETRY 10
int __doRead(int fd, void *buffer, size_t toread, const char *func,
		    int pedantic)
{
    size_t ret = 0;
    ssize_t size = 0, left;
    char *ptr;
    int flags, retry = 0;

    if (pedantic) {
	flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    ptr = buffer;
    left = toread;
    while (1) {
	if ((size = read(fd, ptr, left)) < 0) {
	    if (errno == EINTR || errno == EAGAIN) continue;

	    pluginlog("%s (%s): read from fd '%i' failed (%i): %s\n", __func__,
		    func, fd, errno, strerror(errno));
	    return -1;
	}
	if (!pedantic) return size;

	ptr += size;
	ret += size;
	left -= size;
	if (ret == toread) break;
	if (!ret) return ret;
	if (retry++ >MAX_RETRY) return -1;
    }

    return ret;
}

/**
 * @brief Grow the data buffer if needed.
 *
 * @param len The additional size needed.
 *
 * @param data Pointer to the actual data buffer.
 *
 * @return No return value.
 */
static void growBuffer(size_t len, PS_DataBuffer_t *data)
{
    if (data->buf == NULL) {
	data->buf = umalloc(BufTypedMsgSize);
	data->bufSize = BufTypedMsgSize;
	data->bufUsed = 0;
    }

    while (data->bufUsed + len > data->bufSize) {
	data->buf = urealloc(data->buf, data->bufSize + BufTypedMsgSize);
	data->bufSize += BufTypedMsgSize;
    }
}

int __addStringArrayToMsg(char **array, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller)
{
    uint32_t i;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    if (!( __addUint32ToMsg(len, data, caller))) return 0;

    for (i=0; i<len; i++) {
	if (!(__addStringToMsg(array[i], data, caller))) return 0;
    }

    return 1;
}

int __addStringToMsg(const char *string, PS_DataBuffer_t *data,
			const char *caller)
{
    size_t len;
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    len = (!string) ? 0 : strlen(string) +1;

    growBuffer(sizeof(uint8_t) + sizeof(uint32_t) + len, data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	addDataType(&ptr, PSDATA_STRING);
	data->bufUsed +=sizeof(uint8_t);
    }

    /* string length */
    *(uint32_t *) ptr = byteOrder ? htonl(len) : len;
    ptr += sizeof(uint32_t);
    data->bufUsed += sizeof(uint32_t);

    /* add string itself */
    if (len > 0) {
	memcpy(ptr, string, len);
	data->bufUsed += len;
    }

    return 1;
}

int __addUint8ToMsg(const uint8_t val, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(uint8_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	addDataType(&ptr, PSDATA_UINT8);
	data->bufUsed += sizeof(uint8_t);
    }

    /* add data */
    *(uint8_t *) ptr = val;
    data->bufUsed += sizeof(uint8_t);

    return 1;
}

int __addUint16ToMsg(const uint16_t val, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(uint16_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	addDataType(&ptr, PSDATA_UINT16);
	data->bufUsed += sizeof(uint8_t);
    }

    /* add result */
    *(uint16_t *) ptr = byteOrder ? htons(val) : val;
    data->bufUsed += sizeof(uint16_t);

    return 1;
}

int __addUint32ToMsg(const uint32_t val, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(uint32_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	addDataType(&ptr, PSDATA_UINT32);
	data->bufUsed += sizeof(uint8_t);
    }

    /* add result */
    *(uint32_t *) ptr = byteOrder ? htonl(val) : val;
    data->bufUsed += sizeof(uint32_t);

    return 1;
}

int __addUint64ToMsg(const uint64_t val, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(uint64_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	addDataType(&ptr, PSDATA_UINT64);
	data->bufUsed += sizeof(uint8_t);
    }

    /* add result */
    *(uint64_t *) ptr = byteOrder ? HTON_uint64(val) : val;
    data->bufUsed += sizeof(uint64_t);

    return 1;
}

int __addDoubleToMsg(double val, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    union {
	double d;
	uint64_t u;
    } uval;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    uval.d = (val * FLOAT_CONVERT);

    growBuffer(sizeof(uint8_t) + sizeof(uint64_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	addDataType(&ptr, PSDATA_DOUBLE);
	data->bufUsed += sizeof(uint8_t);
    }

    /* add result */
    *(uint64_t *) ptr = byteOrder ? HTON_uint64(uval.u) : uval.u;
    data->bufUsed += sizeof(uint64_t);

    return 1;
}

int __addInt16ToMsg(const int16_t val, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(int16_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	addDataType(&ptr, PSDATA_INT16);
	data->bufUsed += sizeof(uint8_t);
    }

    /* add result */
    *(int16_t *) ptr = val;
    data->bufUsed += sizeof(int16_t);

    return 1;
}

int __addInt32ToMsg(const int32_t val, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(int32_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	*(uint8_t *)ptr = PSDATA_INT32;
	ptr += sizeof(uint8_t);
	data->bufUsed += sizeof(uint8_t);
    }

    /* add result */
    *(int32_t *) ptr = val;
    data->bufUsed += sizeof(int32_t);

    return 1;
}

int __addUint16ArrayToMsg(const uint16_t *val, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller)
{
    uint32_t i;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    if (!( __addUint32ToMsg(len, data, caller))) return 0;

    for (i=0; i<len; i++) {
	__addUint16ToMsg(val[i], data, caller);
    }

    return 1;
}

int __addUint32ArrayToMsg(const uint32_t *val, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller)
{
    uint32_t i;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    if (!( __addUint32ToMsg(len, data, caller))) return 0;

    for (i=0; i<len; i++) {
	__addUint32ToMsg(val[i], data, caller);
    }

    return 1;
}

int __addInt16ArrayToMsg(const int16_t *val, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller)
{
    uint32_t i;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    if (!( __addUint32ToMsg(len, data, caller))) return 0;

    for (i=0; i<len; i++) {
	__addInt16ToMsg(val[i], data, caller);
    }

    return 1;
}

int __addInt32ArrayToMsg(const int32_t *val, const uint32_t len,
			    PS_DataBuffer_t *data, const char *caller)
{
    uint32_t i;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    if (!( __addUint32ToMsg(len, data, caller))) return 0;

    for (i=0; i<len; i++) {
	__addInt32ToMsg(val[i], data, caller);
    }

    return 1;
}

int __addTimeToMsg(const time_t *time, PS_DataBuffer_t *data,
		    const char *caller)
{
    char *ptr;
    time_t tmp;

    if (!time) {
	pluginlog("%s: invalid time from '%s'\n", __func__, caller);
	return 0;
    }

    if (!data) {
	pluginlog("%s: invalid data buffer from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(uint64_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	*(uint8_t *)ptr = PSDATA_TIME;
	ptr += sizeof(uint8_t);
	data->bufUsed += sizeof(uint8_t);
    }

    tmp = byteOrder ? HTON_int64(*time) : *time;
    *(int64_t *) ptr = tmp;
    data->bufUsed += sizeof(int64_t);

    return 1;
}

int __addMemToMsg(void *mem, uint32_t memLen, PS_DataBuffer_t *data,
		    const char *caller)
{
    void *ptr;

    if (!data) {
	pluginlog("%s: invalid data buffer from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(uint32_t) + memLen, data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	*(uint8_t *)ptr = PSDATA_MEM;
	ptr += sizeof(uint8_t);
	data->bufUsed += sizeof(uint8_t);
    }

    memcpy(ptr, mem, memLen);
    data->bufUsed += memLen;

    return 1;
}

int __addPidToMsg(const pid_t pid, PS_DataBuffer_t *data, const char *caller)
{
    char *ptr;

    if (!data) {
	pluginlog("%s: invalid data from '%s'\n", __func__, caller);
	return 0;
    }

    growBuffer(sizeof(uint8_t) + sizeof(pid_t), data);
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	*(uint8_t *)ptr = PSDATA_PID;
	ptr += sizeof(uint8_t);
	data->bufUsed += sizeof(uint8_t);
    }

    *(pid_t *) ptr = pid;
    data->bufUsed += sizeof(pid_t);

    return 1;
}

int __addStringToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
    const char *string, const char *caller)
{
    size_t len;

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    len = (!string) ? 0 : strlen(string) +1;

    /* add data type */
    if (typeInfo) {
	*(uint8_t *) *ptr = PSDATA_STRING;
	*ptr += sizeof(uint8_t);
	msg->header.len += sizeof(uint8_t);
    }

    /* string length */
    *(uint32_t *) *ptr = byteOrder ? htonl(len) : len;
    *ptr += sizeof(uint32_t);
    msg->header.len += sizeof(uint32_t);

    if (msg->header.len + len> BufTypedMsgSize) {
	pluginlog("%s: message buffer to small from '%s'!\n", __func__, caller);
	return 0;
    }

    /* add string itself */
    if (len > 0) {
	memcpy(*ptr, string, len);
	*ptr += len;
	msg->header.len += len;
    }

    /*
    pluginlog("adding buffer '%s' len '%zu'\n", string, len);
    */
    return 1;
}

int __addTimeToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
				time_t *time, const char *caller)
{
    time_t tmp;

    if (!time) {
	pluginlog("%s: invalid time from '%s'\n", __func__, caller);
	return 0;
    }

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    if ((msg->header.len + sizeof(int64_t)) > BufTypedMsgSize) {
	pluginlog("%s: message buffer to small from '%s'\n", __func__, caller);
	return 0;
    }

    /* add data type */
    if (typeInfo) {
	*(uint8_t *) *ptr = PSDATA_TIME;
	*ptr += sizeof(uint8_t);
	msg->header.len += sizeof(uint8_t);
    }

    tmp = byteOrder ? HTON_int64(*time) : *time;
    *(int64_t *) *ptr = tmp;
    *ptr += sizeof(int64_t);
    msg->header.len += sizeof(int64_t);

    return 1;
}
int __addInt32ToMsgBuf(DDTypedBufferMsg_t *msg, char **ptr,
			int32_t val, const char *caller)
{
    if (!msg) {
	pluginlog("%s: invalid msg from '%s'\n", __func__, caller);
	return 0;
    }

    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	return 0;
    }

    /* add data type */
    if (typeInfo) {
	addDataType(ptr, PSDATA_INT32);
	msg->header.len += sizeof(uint8_t);
    }

    /* add result */
    *(int32_t *) *ptr = val;
    *ptr += sizeof(int32_t);
    msg->header.len += sizeof(int32_t);

    return 1;
}

static int checkGetParam(char **ptr, void *val, const char *func,
			    const char *caller, const int line)
{
    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s'\n", func, caller);
	return 0;
    }

    if (!val) {
	pluginlog("%s: invalid value from '%s'\n", func, caller);
	return 0;
    }

    return 1;
}

int __getUint8(char **ptr, uint8_t *val, const char *caller,
			    const int line)
{
    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_UINT8, caller))) return 0;

    *val = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);
    return 1;
}

int __getUint16(char **ptr, uint16_t *val, const char *caller,
			    const int line)
{
    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_UINT16, caller))) return 0;

    *val = *(uint16_t *) *ptr;
    *val = byteOrder ? ntohs(*val) : *val;
    *ptr += sizeof(uint16_t);
    return 1;
}

int __getUint32(char **ptr, uint32_t *val, const char *caller,
			    const int line)
{
    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_UINT32, caller))) return 0;

    *val = *(uint32_t *) *ptr;
    *val = byteOrder ? ntohl(*val) : *val;
    *ptr += sizeof(uint32_t);
    return 1;
}

int __getUint64(char **ptr, uint64_t *val, const char *caller,
			    const int line)
{
    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_UINT64, caller))) return 0;

    *val = *(uint64_t *) *ptr;
    *val = byteOrder ? NTOH_uint64(*val) : *val;
    *ptr += sizeof(uint64_t);
    return 1;
}

int __getDouble(char **ptr, double *val, const char *caller,
			    const int line)
{
    union {
	double d;
	uint64_t u;
    } uval;

    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_DOUBLE, caller))) return 0;

    uval.u = *(uint64_t *) *ptr;
    *ptr += sizeof(uint64_t);

    uval.u = byteOrder ? NTOH_uint64(uval.u) : uval.u;
    *val = uval.d / FLOAT_CONVERT;
    return 1;
}

int __getInt8(char **ptr, int8_t *val, const char *caller,
			    const int line)
{
    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_INT8, caller))) return 0;

    *val = *(int8_t *) *ptr;
    *ptr += sizeof(int8_t);
    return 1;
}

int __getInt16(char **ptr, int16_t *val, const char *caller,
			    const int line)
{
    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_INT16, caller))) return 0;

    *val = *(int16_t *) *ptr;
    *ptr += sizeof(int16_t);
    return 1;
}

int __getInt32(char **ptr, int32_t *val, const char *caller,
			    const int line)
{
    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_INT32, caller))) return 0;

    *val = *(int32_t *) *ptr;
    *ptr += sizeof(int32_t);
    return 1;
}

int __getInt64(char **ptr, int64_t *val, const char *caller,
			    const int line)
{
    if (!(checkGetParam(ptr, val, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_INT64, caller))) return 0;

    *val = *(int64_t *) *ptr;
    *ptr += sizeof(int64_t);
    return 1;
}

int __getUint16Array(char **ptr, uint16_t **val, uint32_t *len,
			const char *caller, const int line)
{
    uint32_t i;

    if (!( __getUint32(ptr, len, caller, line))) return 0;

    if (*len <= 0) return 1;
    *val = umalloc(sizeof(uint16_t) * *len);

    for (i=0; i<*len; i++) {
	__getUint16(ptr, &(*val)[i], caller, line);
    }

    return 1;
}

int __getUint32Array(char **ptr, uint32_t **val, uint32_t *len,
			const char *caller, const int line)
{
    uint32_t i;

    if (!( __getUint32(ptr, len, caller, line))) return 0;

    if (*len <= 0) return 1;
    *val = umalloc(sizeof(uint32_t) * *len);

    for (i=0; i<*len; i++) {
	__getUint32(ptr, &(*val)[i], caller, line);
    }

    return 1;
}

int __getInt16Array(char **ptr, int16_t **val, uint32_t *len,
			const char *caller, const int line)
{
    uint32_t i;

    if (!( __getUint32(ptr, len, caller, line))) return 0;

    if (*len <= 0) return 1;
    *val = umalloc(sizeof(int16_t) * *len);

    for (i=0; i<*len; i++) {
	__getInt16(ptr, &(*val)[i], caller, line);
    }

    return 1;
}

int __getInt32Array(char **ptr, int32_t **val, uint32_t *len,
			const char *caller, const int line)
{
    uint32_t i;

    if (!( __getUint32(ptr, len, caller, line))) return 0;

    if (*len <= 0) return 1;
    *val = umalloc(sizeof(int32_t) * *len);

    for (i=0; i<*len; i++) {
	__getInt32(ptr, &(*val)[i], caller, line);
    }

    return 1;
}

int __getTime(char **ptr, time_t *time, const char *caller, const int line)
{
    if (!(checkGetParam(ptr, time, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_TIME, caller))) return 0;

    *time = *(int64_t *) *ptr;
    *time = byteOrder ? NTOH_int64(*time) : *time;
    *ptr += sizeof(int64_t);

    return 1;
}

int __getPid(char **ptr, pid_t *pid, const char *caller, const int line)
{
    if (!(checkGetParam(ptr, pid, __func__, caller, line))) return 0;
    if (!(verifyTypeInfo(ptr, PSDATA_PID, caller))) return 0;

    *pid = *(pid_t *) *ptr;
    *ptr += sizeof(pid_t);

    return 1;
}

char *__getStringM(char **ptr, const char *caller, const int line)
{
    char *data;
    size_t len;

    if (!*ptr) {
	if (debug) {
	    pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	}
	return NULL;
    }

    if (!(verifyTypeInfo(ptr, PSDATA_STRING, caller))) return NULL;

    /* string length */
    len = *(uint32_t *) *ptr;
    len = byteOrder ? ntohl(len) : len;
    *ptr += sizeof(uint32_t);

    data = umalloc(len);

    /* extract the string */
    if (len > 0) {
	memcpy(data, *ptr, len);
	data[len-1] = '\0';
	*ptr += len;
    } else {
	data[0] = '\0';
    }

    return data;
}

int __getStringArrayM(char **ptr, char ***array, uint32_t *len,
			const char *caller, const int line)
{
    uint32_t i = 0;

    *array = NULL;
    if (!( __getUint32(ptr, len, caller, line))) return 0;

    if (!*len) return 1;

    *array = umalloc(sizeof(char *) * (*len + 1));

    for (i=0; i<*len; i++) {
	(*array)[i] = __getStringM(ptr, caller, line);
    }

    (*array)[*len] = NULL;

    return 1;
}

char *__getString(char **ptr, char *buf, size_t buflen,
		    const char *caller, const int line)
{
    size_t len;

    if (!buf) {
	pluginlog("%s: invalid buffer from '%s'\n", __func__, caller);
	return NULL;
    }
    buf[0] = '\0';

    if (!*ptr) {
	if (debug) {
	    pluginlog("%s: invalid ptr from '%s'\n", __func__, caller);
	}
	return NULL;
    }

    if (!(verifyTypeInfo(ptr, PSDATA_STRING, caller))) return NULL;

    /* string length */
    len = *(uint32_t *) *ptr;
    len = byteOrder ? ntohl(len) : len;
    *ptr += sizeof(uint32_t);

    //pluginlog("reading buffer len '%i'\n", len);

    /* buffer to small */
    if (len > buflen) {
	pluginlog("%s: buffer (%zu) to small for message (%zu) from '%s'\n",
		__func__, buflen, len, caller);
	return NULL;
    }

    /* extract the string */
    if (len > 0) {
	memcpy(buf, *ptr, len);
	buf[len] = '\0';
	*ptr += len;
    } else {
	buf[0] = '\0';
    }

    return buf;
}
