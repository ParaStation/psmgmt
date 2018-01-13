/*
 * ParaStation
 *
 * Copyright (C) 2012-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h>

#include "pluginmalloc.h"
#include "pluginlog.h"

#include "plugincomm.h"

#define UINT64_SWAP_LE_BE(val) ((uint64_t) (				\
    (((uint64_t) (val) & (uint64_t) (0x00000000000000ffU)) << 56)	\
    | (((uint64_t) (val) & (uint64_t) (0x000000000000ff00U)) << 40)	\
    | (((uint64_t) (val) & (uint64_t) (0x0000000000ff0000U)) << 24)	\
    | (((uint64_t) (val) & (uint64_t) (0x00000000ff000000U)) <<  8)	\
    | (((uint64_t) (val) >>  8) & (uint64_t) (0x00000000ff000000U))	\
    | (((uint64_t) (val) >> 24) & (uint64_t) (0x0000000000ff0000U))	\
    | (((uint64_t) (val) >> 40) & (uint64_t) (0x000000000000ff00U))	\
    | (((uint64_t) (val) >> 56) & (uint64_t) (0x00000000000000ffU)) ))

#if SLURM_BIGENDIAN
# define HTON64(x)   ((uint64_t) (x))
# define NTOH64(x)   ((uint64_t) (x))
#else
# define HTON64(x)        UINT64_SWAP_LE_BE (x)
# define NTOH64(x)        UINT64_SWAP_LE_BE (x)
#endif

#define FLOAT_CONVERT 1000000

/** Flag output of debug information */
static bool debug = false;

/** Flag byte-order conversation */
static bool byteOrder = true;

/** Flag insertion of type information into actual messages */
static bool typeInfo = false;

bool setByteOrder(bool flag)
{
    bool old = byteOrder;
    byteOrder = flag;
    return old;
}

bool setTypeInfo(bool flag)
{
    bool old = typeInfo;
    typeInfo = flag;
    return old;
}

void setFDblock(int fd, bool block)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (block) {
	fcntl(fd, F_SETFL, flags & (~O_NONBLOCK));
    } else {
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

/**
 * @brief Grow the data buffer if needed.
 *
 * Grow the data buffer @a data such that @a len additional bytes fit
 * into the buffer. Buffers will grow by multiples of @ref
 * BufTypedMsgSize and never grow beyond @ref UINT32_MAX.
 *
 * @a caller and @a line are passed to the corresponding allocation
 * functions @ref __umalloc() and __urealloc() in order to create
 * debug messages if required.
 *
 * @param len Number of additional bytes needed
 *
 * @param data Pointer to the data buffer to grow
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Return true on success and false on error
 */
static bool growDataBuffer(size_t len, PS_DataBuffer_t *data,
			   const char *caller, const int line)
{
    if (!data->buf) data->bufSize = data->bufUsed = 0;

    size_t newLen = (!len) ? (data->bufSize ? data->bufSize: BufTypedMsgSize) :
	((data->bufUsed + len - 1) / BufTypedMsgSize + 1) * BufTypedMsgSize;
    if (newLen > UINT32_MAX) return false;

    data->buf = __urealloc(data->buf, newLen, caller, line);
    data->bufSize = newLen;

    return true;
}

void freeDataBuffer(PS_DataBuffer_t *data)
{
    if (!data) return;

    if (data->buf) ufree(data->buf);
    data->buf = NULL;
    data->bufUsed = data->bufSize = 0;
}

PS_DataBuffer_t * dupDataBuffer(PS_DataBuffer_t *data)
{
    PS_DataBuffer_t *dup = malloc(sizeof(*dup));

    dup->buf = umalloc(data->bufSize);
    memcpy(dup->buf, data->buf, data->bufSize);
    dup->bufSize = data->bufSize;
    dup->bufUsed = data->bufUsed;

    return dup;
}

/** Maximum number of retries within @ref __doWrite() and __doRead() */
#define MAX_RETRY 20

int __doWriteEx(int fd, void *buffer, size_t toWrite, size_t *written,
		const char *func, bool pedantic, bool infinite)
{
    static time_t lastLog = 0;
    char *ptr = buffer;
    int retries = 0;

    *written = 0;

    while ((*written < toWrite) && (infinite || retries++ <= MAX_RETRY)) {
	ssize_t ret = write(fd, ptr + *written, toWrite - *written);
	if (ret == -1) {
	    int eno = errno;
	    if (eno == EINTR || eno == EAGAIN) continue;

	    time_t now = time(NULL);
	    if (lastLog == now) return -1;

	    pluginwarn(eno, "%s (%s): write(%i) failed", __func__, func, fd);
	    lastLog = now;
	    return -1;
	} else if (!ret) {
	    return ret;
	}
	if (!pedantic) return ret;

	*written += ret;
    }

    if (*written < toWrite) return -1;

    return *written;
}

int __doWrite(int fd, void *buffer, size_t toWrite, const char *func,
	      bool pedantic, bool infinite)
{
    size_t written;

    return __doWriteEx(fd, buffer, toWrite, &written, func, pedantic, infinite);
}

int __doReadExt(int fd, void *buffer, size_t toRead, size_t *numRead,
		const char *func, bool pedantic)
{
    static time_t lastLog = 0;
    char *ptr = buffer;
    int retries = 0;

    *numRead = 0;
    setFDblock(fd, !pedantic);

    while ((*numRead < toRead) && (retries++ <= MAX_RETRY)) {
	ssize_t num = read(fd, ptr + *numRead, toRead - *numRead);
	if (num < 0) {
	    int eno = errno;
	    if (eno == EINTR || eno == EAGAIN) continue;

	    time_t now = time(NULL);
	    if (lastLog == now) return -1;

	    pluginwarn(eno, "%s (%s): read(%i) failed", __func__, func, fd);
	    lastLog = now;
	    return -1;
	} else if (!num) {
	    return num;
	}
	if (!pedantic) return num;

	*numRead += num;
    }

    if (*numRead < toRead) return -1;

    return *numRead;
}

int __doRead(int fd, void *buffer, size_t toRead, const char *func,
	     bool pedantic)
{
    size_t read;

    return __doReadExt(fd, buffer, toRead, &read, func, pedantic);
}

/******************** fetching data  ********************/

/**
 * @brief Verify type info
 *
 * Verify the type information of the next data available at @a
 * ptr. If the type is identical to the one provided in @a
 * expectedType true is returned. At the same time @a ptr is modified
 * such that it points behind the checked type information.
 *
 * If the global @ref typeInfo flag is false, no action is taken und
 * true is returned.
 *
 * @param ptr Pointer to the next data available. Modified in the
 * course of fetching the actual type information.
 *
 * @param expectedType Type that is expected.
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return If the type of the available data is identical to @a
 * expectedType true is returned. Otherwise false is returned. If
 * type-checking is switched of, i.e. @ref typeInfo is false, true is
 * returned.
 */
static bool verifyTypeInfo(char **ptr, PS_DataType_t expectedType,
			   const char *caller, const int line)
{
    uint8_t type;

    if (!typeInfo) return true;

    type = *(uint8_t *) *ptr;
    *ptr += sizeof(uint8_t);

    if (type != expectedType) {
	if (debug) {
	    pluginlog("%s(%s@%d): error got type %i should be %i\n",
		      __func__, caller, line, type, expectedType);
	}
	return false;
    }
    return true;
}

bool getFromBuf(char **ptr, void *val, PS_DataType_t type,
		size_t size, const char *caller, const int line)
{
    if (!*ptr) {
	pluginlog("%s: invalid ptr from '%s' at %d\n", __func__, caller, line);
	return false;
    }

    if (!val) {
	pluginlog("%s: invalid val from '%s' at %d\n", __func__, caller, line);
	return false;
    }

    if (!verifyTypeInfo(ptr, type, caller, line)) return false;

    memcpy(val, *ptr, size);
    if (byteOrder) {
	switch (size) {
	case 1:
	    break;
	case 2:
	    *(uint16_t*)val = ntohs(*(uint16_t*)val);
	    break;
	case 4:
	    *(uint32_t*)val = ntohl(*(uint32_t*)val);
	    break;
	case 8:
	    *(uint64_t*)val = NTOH64(*(uint64_t*)val);
	    break;
	default:
	    pluginlog("%s(%s@%d): unknown conversion for size %zd\n",
		      __func__, caller, line, size);
	}
    }
    *ptr += size;

    return true;
}

bool getArrayFromBuf(char **ptr, void **val, uint32_t *len,
		     PS_DataType_t type, size_t size,
		     const char *caller, const int line)
{
    uint32_t i;

    if (!getFromBuf(ptr, len, PSDATA_UINT32, sizeof(*len), caller, line))
	return false;

    if (*len <= 0) return true;
    *val = __umalloc(size * *len, caller, line);

    for (i = 0; i < *len; i++) {
	getFromBuf(ptr, *val + i*size, type, size, caller, line);
    }

    return true;
}

void *getMemFromBuf(char **ptr, char *data, size_t dataSize, size_t *len,
		    PS_DataType_t type, const char *caller, const int line)
{
    uint32_t l;

    if (dataSize && !data) {
	pluginlog("%s: invalid buffer from '%s' at %d\n", __func__,
		  caller, line);
	return NULL;
    }

    if (!*ptr) {
	if (debug) pluginlog("%s: invalid ptr from '%s' at %d\n", __func__,
			     caller, line);
	return NULL;
    }

    if (!verifyTypeInfo(ptr, type, caller, line)) return NULL;

    /* data length */
    l = *(uint32_t *) *ptr;
    *ptr += sizeof(uint32_t);

    if (byteOrder) l = ntohl(l);
    if (len) *len = l;

    if (data) {
	if (l >= dataSize) {
	    /* buffer to small */
	    pluginlog("%s: buffer (%zu) to small for message (%u) from '%s'"
		      " at %d\n", __func__, dataSize, l, caller, line);
	    return NULL;
	}
    } else {
	data = __umalloc(l, caller, line);
    }

    /* extract data */
    if (l > 0) {
	memcpy(data, *ptr, l);
	if (type == PSDATA_STRING) data[l-1] = '\0';
	*ptr += l;
    } else if (type == PSDATA_STRING) {
	data[0] = '\0';
    }

    return data;
}

bool __getStringArrayM(char **ptr, char ***array, uint32_t *len,
			const char *caller, const int line)
{
    uint32_t i;

    *array = NULL;
    if (!getFromBuf(ptr, len, PSDATA_UINT32, sizeof(*len), caller, line))
	return false;

    if (!*len) return true;

    *array = __umalloc(sizeof(char *) * (*len + 1), caller, line);

    for (i = 0; i < *len; i++) {
	(*array)[i] = getMemFromBuf(ptr, NULL, 0, NULL, PSDATA_STRING,
				    caller, line);
    }

    (*array)[*len] = NULL;

    return true;
}

/******************** adding data  ********************/

static void addDataType(void **ptr, PS_DataType_t type)
{
    *(uint8_t *) *ptr = type;
    *ptr += sizeof(uint8_t);
}

bool addToBuf(const void *val, const uint32_t size, PS_DataBuffer_t *data,
	      PS_DataType_t type, const char *caller, const int line)
{
    void *ptr;
    bool hasLen = (type == PSDATA_STRING || type == PSDATA_DATA);
    size_t growth = (typeInfo ? sizeof(uint8_t) : 0)
	+ (hasLen ? sizeof(uint32_t) : 0) + size;

    if (!data) {
	pluginlog("%s: invalid data from '%s' at %d\n", __func__, caller, line);
	return false;
    }

    if (!val && (!hasLen || size)) {
	pluginlog("%s: invalid val from '%s' at %d\n", __func__, caller, line);
	return false;
    }

    if (!growDataBuffer(growth, data, caller, line)) {
	pluginlog("%s: growing buffer failed from '%s' at %d\n", __func__,
		  caller, line);
	return false;
    }
    ptr = data->buf + data->bufUsed;

    /* add data type */
    if (typeInfo) {
	addDataType(&ptr, type);
	data->bufUsed += sizeof(uint8_t);
    }

    /* add data length if required */
    if (hasLen) {
	*(uint32_t *) ptr = byteOrder ? htonl(size) : size;
	ptr += sizeof(uint32_t);
	data->bufUsed += sizeof(uint32_t);
    }

    /* add data */
    if (size) memcpy(ptr, val, size);
    if (byteOrder && !hasLen && type != PSDATA_MEM) {
	switch (size) {
	case 1:
	    break;
	case 2:
	    *(uint16_t*)ptr = htons(*(uint16_t*)ptr);
	    break;
	case 4:
	    *(uint32_t*)ptr = htonl(*(uint32_t*)ptr);
	    break;
	case 8:
	    *(uint64_t*)ptr = HTON64(*(uint64_t*)ptr);
	    break;
	default:
	    pluginlog("%s(%s@%d): unknown conversion for size %d\n",
		      __func__, caller, line, size);
	}
    }
    data->bufUsed += size;

    return true;
}

bool addArrayToBuf(const void *val, const uint32_t num, PS_DataBuffer_t *data,
		   PS_DataType_t type, size_t size,
		   const char *caller, const int line)
{
    uint32_t i;
    const char *valPtr = val;

    if (!data) {
	pluginlog("%s: invalid data from '%s' at %d\n", __func__, caller, line);
	return false;
    }
    if (!val) {
	pluginlog("%s: invalid val from '%s' at %d\n", __func__, caller, line);
	return false;
    }

    if (!addToBuf(&num, sizeof(num), data, PSDATA_UINT32, caller, line))
	return false;

    for (i = 0; i < num; i++) {
	addToBuf(valPtr + i*size, size, data, type, caller, line);
    }

    return true;
}


/******************** adding to messages  *********************/

bool addToMsgBuf(DDTypedBufferMsg_t *msg, void *val, uint32_t size,
		 PS_DataType_t type, const char *caller)
{
    if (!msg) {
	pluginlog("%s: invalid msg ptr from %s\n", __func__, caller);
	return false;
    }

    /* add data type */
    if (typeInfo) {
	uint8_t t = type;
	PSP_putTypedMsgBuf(msg, caller, "type", &t, sizeof(t));
    }

    /* add data length if required */
    if (type == PSDATA_STRING || type == PSDATA_DATA) {
	uint32_t len = (byteOrder ? htonl(size) : size);
	PSP_putTypedMsgBuf(msg, caller, "len", &len, sizeof(len));
    }

    if (msg->header.len + size > BufTypedMsgSize) {
	pluginlog("%s: message buffer to small from %s\n", __func__, caller);
	return false;
    }

    switch (type) {
    case PSDATA_UINT8:
    case PSDATA_STRING:
    case PSDATA_DATA:
	break;
    case PSDATA_TIME:
	if (byteOrder) *(uint64_t *)val = HTON64(*(uint64_t *)val);
	break;
    case PSDATA_INT32:
	if (byteOrder) *(int32_t *)val = htonl(*(int32_t *)val);
	break;
    case PSDATA_UINT16:
	if (byteOrder) *(uint16_t *)val = htons(*(uint16_t *)val);
	break;
    case PSDATA_UINT32:
	if (byteOrder) *(uint32_t *)val = htonl(*(uint32_t *)val);
	break;
    default:
	pluginlog("%s: unsupported type %d from %s\n", __func__, type, caller);
    }

    return PSP_putTypedMsgBuf(msg, caller, "val", val, size);
}
