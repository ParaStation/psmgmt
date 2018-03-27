/*
 * ParaStation
 *
 * Copyright (C) 2012-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h>
#include <dlfcn.h>

#include "psidhook.h"

#include "pluginlog.h"

#include "psserial.h"

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

#define DEFAULT_BUFFER_SIZE 256 * 1024

typedef enum {
    FRAGMENT_PART = 0x01,   /* one fragment, more to follow */
    FRAGMENT_END  = 0x02,   /* the end of a fragmented message */
} FragType_t;

/** Flag output of debug information */
static bool debug = false;

/** Flag byte-order conversation */
static bool byteOrder = true;

/** Flag insertion of type information into actual messages */
static bool typeInfo = false;

/** send buffer for non fragmented messages */
static char *sendBuf = NULL;

/** the send buffer size */
static uint32_t sendBufLen = 0;

/** destination nodes for fragmented messages */
static PStask_ID_t *destNodes = NULL;

/** message to collect and send fragments */
static DDTypedBufferMsg_t fragMsg;

/**
 * Temporary receive buffers used to collect fragments of a
 * message. One per node.
 */
static PS_DataBuffer_t *recvBuffers = NULL;

/**
 * Custom function used by @ref __sendFragMsg() to actually send
 * fragments. Set via @ref initSerial().
 */
static Send_Msg_Func_t *sendPSMsg = NULL;

/** Minimum size of any allocation done by psserial */
#define MIN_MALLOC_SIZE 64

/** Wrapper around malloc enforcing @ref MIN_MALLOC_SIZE */
static inline void *umalloc(size_t size)
{
    return malloc(size < MIN_MALLOC_SIZE ? MIN_MALLOC_SIZE : size);
}

/**
 * @brief Reset data buffer
 *
 * Reset the data buffer @a b is pointing to. This includes to free
 * all memory.
 *
 * @param b Pointer to data buffer to reset
 *
 * @return No return value
 */
static void resetBuf(PS_DataBuffer_t *b)
{
    if (b->buf) free(b->buf);
    b->buf = NULL;
    b->bufSize = 0;
    b->bufUsed = 0;
    b->nextFrag = 0;
}

/**
 * @brief Callback on downed node
 *
 * This callback is called by a hosting daemon if it detects that a
 * remote node went down. It aims to cleanup all now useless data
 * structures, i.e. all incomplete messages to be received from the
 * node that went down.
 *
 * Keep in mind that this mechanism can only work within psid!
 *
 * @param nodeID ParaStation ID of the node that went down
 *
 * @return Always return 1 to make the calling hook-handler happy
 */
static int nodeDownHandler(void *nodeID)
{
    if (!nodeID) return 1;

    PSnodes_ID_t id = *(PSnodes_ID_t *)nodeID;

    if (!PSC_validNode(id)) {
	pluginlog("%s: invalid node id %i\n", __func__, id);
	return 1;
    }

    pluginlog("%s: freeing recv buffer for node '%i'\n", __func__, id);
    resetBuf(&recvBuffers[id]);

    return 1;
}

/** Function to remove registered hooks later on */
static int (*hookDelFunc)(PSIDhook_t, PSIDhook_func_t) = NULL;

/**
 * @brief Register hook for down nodes if possible
 *
 * Try to register @ref nodeDownHandler() to the PSIDHOOK_NODE_DOWN
 * hook of psid. For this try to determine if we are running within
 * psid by searching for the symbols of @ref PSIDhook_add() and @ref
 * PSIDhook_del(). If both symbols are found they are used to register
 * @ref nodeDownHandler() immediately. Furthermore, @a hookDelFunc is
 * set in order to be prepared to remove nodeDownHandler() from the
 * hook within @ref finalizeSerial().
 *
 * @return No return value
 */
static void initNodeDownHook(void)
{
    /* Determine if PSIDhook_add is available */
    void *mainHandle = dlopen(NULL, 0);

    int (*hookAdd)(PSIDhook_t, PSIDhook_func_t) = dlsym(mainHandle,
							"PSIDhook_add");
    hookDelFunc = dlsym(mainHandle, "PSIDhook_del");

    if (hookAdd && hookDelFunc) {
	if (!hookAdd(PSIDHOOK_NODE_DOWN, nodeDownHandler)) {
	    pluginlog("%s: cannot register PSIDHOOK_NODE_DOWN\n", __func__);
	    return;
	}
    }
}

bool initSerial(size_t bufSize, Send_Msg_Func_t *func)
{
    PSnodes_ID_t numNodes = PSC_getNrOfNodes();

    if (sendBuf) return false;

    if (numNodes == -1) {
	pluginlog("%s: unable to get number of nodes\n", __func__);
	return false;
    }

    /* allocate send buffers */
    sendBufLen = bufSize ? bufSize : DEFAULT_BUFFER_SIZE;
    sendBuf = malloc(sendBufLen);

    /* allocated space for destination nodes */
    destNodes = malloc(sizeof(*destNodes) * numNodes);

    sendPSMsg = func;
    initNodeDownHook();

    /* allocate receive buffers */
    recvBuffers = malloc(sizeof(*recvBuffers) * numNodes);

    if (!sendBuf || !destNodes || !recvBuffers) {
	pluginlog("%s: cannot allocate all buffers\n", __func__);
	if (sendBuf) free(sendBuf);
	sendBuf = NULL;
	if (destNodes) free(destNodes);
	destNodes = NULL;
	if (recvBuffers) free(recvBuffers);
	recvBuffers = NULL;

	return false;
    }

    memset(recvBuffers, 0, sizeof(*recvBuffers) * numNodes);

    return true;
}

void finalizeSerial(void)
{
    if (sendBuf) free(sendBuf);
    sendBuf = NULL;
    sendBufLen = 0;
    if (destNodes) free(destNodes);
    destNodes = NULL;

    if (recvBuffers) {
	PSnodes_ID_t i, numNodes = PSC_getNrOfNodes();

	for (i=0; i<numNodes; i++) {
	    if (recvBuffers[i].buf) free(recvBuffers[i].buf);
	}
	free(recvBuffers);
	recvBuffers = NULL;
    }

    if (hookDelFunc) hookDelFunc(PSIDHOOK_NODE_DOWN, nodeDownHandler);
}

void initFragBuffer(PS_SendDB_t *buffer, int32_t headType, int32_t msgType)
{
    buffer->useFrag = true;
    buffer->bufUsed = 0;
    buffer->headType = headType;
    buffer->msgType = msgType;
    buffer->fragNum = 0;
    buffer->numDest = 0;
}

bool setFragDest(PS_SendDB_t *buffer, PStask_ID_t id)
{
    PSnodes_ID_t numNodes = PSC_getNrOfNodes();

    if (buffer->numDest >= numNodes) {
	pluginlog("%s: maximum number of %i destinations reached\n",
		__func__, numNodes);
	return false;
    }

    if (!PSC_validNode(PSC_getID(id))) {
	pluginlog("%s: node ID %i is out of range\n", __func__, PSC_getID(id));
	return false;
    }

    destNodes[buffer->numDest++] = id;

    return true;
}

/**
 * @brief Send a message fragment
 *
 * Send a message fragment to all nodes set in @a destNodes.
 *
 * @param buf Buffer to send
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return If the fragment was sent successfully to all destinations,
 * true is returned. Or false in case of at least one failed
 * destination or no send at all.
 */
static bool sendFragment(PS_SendDB_t *buf, const char *caller, const int line)
{
    bool ret = true;
    PSnodes_ID_t i;
    PStask_ID_t localTask = -1;

    if (!buf->numDest) {
	pluginlog("%s: empty nodelist from caller %s at line %i\n", __func__,
		  caller, line);
	return false;
    }

    if (!sendPSMsg) {
	pluginlog("%s: no send function defined from caller %s at line %i\n",
		  __func__, caller, line);
	return false;
    }

    for (i = 0; i < buf->numDest; i++) {
	if (PSC_getID(destNodes[i]) == PSC_getMyID()) {
	    /* local messages might overwrite the shared send message buffer,
	     * therefore it needs to be the last message send */
	    localTask = destNodes[i];
	    continue;
	}
	fragMsg.header.dest = destNodes[i];
	pluginlog("%s: sending msg fragment %i to %s len %u\n", __func__,
		  buf->fragNum, PSC_printTID(destNodes[i]), fragMsg.header.len);

	int res = sendPSMsg(&fragMsg);

	if (res == -1 && errno != EWOULDBLOCK) {
	    ret = false;
	    PSC_warn(-1, errno, "%s(%s:%i): send failed, fragment %u",
		     __func__, caller, line, buf->fragNum);
	}
    }

    /* send any local messages now */
    if (localTask != -1) {
	fragMsg.header.dest = localTask;
	pluginlog("%s: sending msg fragment %i to %s len %u\n", __func__,
		  buf->fragNum, PSC_printTID(localTask), fragMsg.header.len);

	int res = sendPSMsg(&fragMsg);

	if (res == -1 && errno != EWOULDBLOCK) {
	    ret = false;
	    PSC_warn(-1, errno, "%s(%s:%i): send failed, fragment %u",
		     __func__, caller, line, buf->fragNum);
	}
    }
    return ret;
}

bool __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		   const char *caller, const int line)
{
    PSnodes_ID_t srcNode = PSC_getID(msg->header.sender);
    PS_DataBuffer_t *recvBuf, myRecvBuf;
    size_t used = 0;
    uint8_t fType;
    uint16_t fNum;

    if (!msg) {
	pluginlog("%s(%s:%i): invalid msg\n", __func__, caller, line);
	return false;
    }

    if (!func) {
	pluginlog("%s(%s:%i): invalid callback function\n", __func__,
		  caller, line);
	return false;
    }

    if (!PSC_validNode(srcNode)) {
	pluginlog("%s: invalid sender node '%i'\n", __func__, srcNode);
	return false;
    }

    PSP_getTypedMsgBuf(msg, &used, __func__, "fragType", &fType, sizeof(fType));
    PSP_getTypedMsgBuf(msg, &used, __func__, "fragNum", &fNum, sizeof(fNum));

    if (fType != FRAGMENT_PART && fType != FRAGMENT_END) {
	pluginlog("%s: invalid fragment type %u from %s\n", __func__,
		  fType, PSC_printTID(msg->header.sender));
	return false;
    }

    if (fType == FRAGMENT_END && fNum == 0) {
	/* Shortcut: message consists of just one fragment */
	uint32_t s = msg->header.len - offsetof(DDTypedBufferMsg_t, buf) - used;

	myRecvBuf.buf = msg->buf + used;
	myRecvBuf.bufSize = myRecvBuf.bufUsed = s;

	recvBuf = &myRecvBuf;
    } else {
	if (!recvBuffers) {
	    pluginlog("%s:(%s:%i): call initFragComm() first\n", __func__,
		      caller, line);
	    return false;
	}

	recvBuf = &recvBuffers[srcNode];

	if (fNum != recvBuf->nextFrag) {
	    pluginlog("%s: unexpected fragment %u/%u from %s\n", __func__,
		      fNum, recvBuf->nextFrag,
		      PSC_printTID(msg->header.sender));
	    if (fNum) return false;
	}

	if (fNum == 0) {
	    if (recvBuf->buf) pluginlog("%s: found buffer for%s\n", __func__,
					PSC_printTID(msg->header.sender));

	    resetBuf(recvBuf);
	    recvBuf->bufSize = DEFAULT_BUFFER_SIZE;
	    recvBuf->buf = malloc(recvBuf->bufSize);
	    if (!recvBuf->buf) {
		pluginlog("%s(%s:%i): no memory\n", __func__, caller, line);
		recvBuf->bufSize = 0;
		return false;
	    }
	}

	/* copy payload */
	size_t toCopy = msg->header.len-offsetof(DDTypedBufferMsg_t, buf)-used;
	if (recvBuf->bufUsed + toCopy > recvBuf->bufSize) {
	    /* grow buffer, if necessary */
	    recvBuf->bufSize *= 2;
	    char * tmp = realloc(recvBuf->buf, recvBuf->bufSize);
	    if (!tmp) {
		resetBuf(recvBuf);
		return false;
	    }
	    recvBuf->buf = tmp;
	}
	char *ptr = recvBuf->buf + recvBuf->bufUsed;
	PSP_getTypedMsgBuf(msg, &used, __func__, "payload", ptr, toCopy);
	recvBuf->bufUsed += toCopy;
	recvBuf->nextFrag++;
    }

    pluginlog("%s: recv fragment %i from %s\n", __func__, fNum,
	      PSC_printTID(msg->header.sender));

    /* last message fragment ? */
    if (fType == FRAGMENT_END) {
	/* invoke callback */

	pluginlog("%s: msg complete with %u bytes from %s\n", __func__,
		  recvBuf->bufUsed, PSC_printTID(msg->header.sender));

	msg->buf[0] = '\0';
	func(msg, recvBuf);

	/* cleanup data if necessary */
	if (!fNum) resetBuf(recvBuf);
    }

    return true;
}

int __sendFragMsg(PS_SendDB_t *buffer, const char *caller, const int line)
{
    if (!sendBuf) {
	pluginlog("%s: %s at line %u, please call initSerial() before usage\n",
		  __func__, caller, line);
	return -1;
    }

    if (!buffer->bufUsed) {
	pluginlog("%s: no data to send for caller %s line %i\n",
		  __func__, caller, line);
	return -1;
    }

    /* last fragmented message */
    *(uint8_t *) fragMsg.buf = FRAGMENT_END;

    if (!sendFragment(buffer, caller, line)) return -1;

    return buffer->bufUsed;
}

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
 * @a caller and @a line are in order to create debug messages if
 * required.
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
    size_t newLen;

    if (!data->buf) resetBuf(data);

    if (len) {
	newLen = ((data->bufUsed+len-1) / BufTypedMsgSize + 1)*BufTypedMsgSize;
    } else {
	newLen = data->bufSize ? data->bufSize: BufTypedMsgSize;
    }
    if (newLen > UINT32_MAX) return false;

    char *tmp = realloc(data->buf, newLen);
    if (!tmp) {
	pluginlog("%s(%s@%d): allocation of %zd failed\n",
		  __func__, caller, line, newLen);
	return false;
    }
    data->buf = tmp;
    data->bufSize = newLen;

    return true;
}

void freeDataBuffer(PS_DataBuffer_t *data)
{
    if (!data) return;

    resetBuf(data);
}

PS_DataBuffer_t *dupDataBuffer(PS_DataBuffer_t *data)
{
    PS_DataBuffer_t *dup = umalloc(sizeof(*dup));

    if (!dup) {
	pluginlog("%s: duplication failed\n", __func__);
	return NULL;
    }

    dup->buf = umalloc(data->bufSize);
    if (!dup->buf) {
	pluginlog("%s: buffer duplication failed\n", __func__);
	free(dup);
	return NULL;
    }

    memcpy(dup->buf, data->buf, data->bufSize);
    dup->bufSize = data->bufSize;
    dup->bufUsed = data->bufUsed;

    return dup;
}

bool __memToDataBuffer(void *mem, size_t len, PS_DataBuffer_t *buffer,
		       const char *caller, const int line)
{
    if (!buffer) {
	pluginlog("%s: invalid buffer from '%s' at %d\n", __func__,
		  caller, line);
	return false;
    }

    if (!growDataBuffer(len, buffer, caller, line)) {
	pluginlog("%s: growing buffer failed from '%s' at %d\n", __func__,
		caller, line);
	return false;
    }

    memcpy(buffer->buf + buffer->bufUsed, mem, len);
    buffer->bufUsed += len;

    return true;
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

	    PSC_warn(-1, eno, "%s (%s): write(%i) failed", __func__, func, fd);
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

	    PSC_warn(-1, eno, "%s (%s): read(%i) failed", __func__, func, fd);
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
    *val = umalloc(size * *len);
    if (!*val) {
	pluginlog("%s(%s@%d): allocation of %zd failed\n",
		  __func__, caller, line, size * *len);
	return false;
    }

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
	pluginlog("%s(%s@%d): invalid buffer\n", __func__, caller, line);
	return NULL;
    }

    if (!*ptr) {
	if (debug) pluginlog("%s(%s@%d): invalid pointer\n",
			     __func__, caller, line);
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
	    pluginlog("%s(%s@%d): buffer (%zu) to small for message (%u)\n",
		      __func__, caller, line, dataSize, l);
	    return NULL;
	}
    } else {
	data = umalloc(l);
	if (!data) {
	    pluginlog("%s(%s@%d): allocation of %u failed\n",
		      __func__, caller, line, l);
	    return NULL;
	}
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

    *array = umalloc(sizeof(char *) * (*len + 1));
    if (!*array) {
	pluginlog("%s(%s@%d): allocation of %zd failed\n",
		  __func__, caller, line, sizeof(char *) * (*len + 1));
	return false;
    }

    for (i = 0; i < *len; i++) {
	(*array)[i] = getMemFromBuf(ptr, NULL, 0, NULL, PSDATA_STRING,
				    caller, line);
    }

    (*array)[*len] = NULL;

    return true;
}

/******************** adding data  ********************/

static void addFragmentedData(PS_SendDB_t *buffer, const void *data,
			      const size_t dataLen, const char *caller,
			      const int line)
{
    const uint8_t type = FRAGMENT_PART;
    size_t dataLeft = dataLen;

    if (!buffer->bufUsed) {
	/* init first message */
	buffer->fragNum = 0;

	fragMsg.header.len = offsetof(DDTypedBufferMsg_t, buf);
	fragMsg.header.sender = PSC_getMyTID();
	fragMsg.header.type = buffer->headType;
	fragMsg.type = buffer->msgType;
	PSP_putTypedMsgBuf(&fragMsg, __func__, "fragType", &type, sizeof(type));
	PSP_putTypedMsgBuf(&fragMsg, __func__, "fragNum",
			   &buffer->fragNum, sizeof(buffer->fragNum));
    }

    while (dataLeft>0) {
	size_t off = fragMsg.header.len - offsetof(DDTypedBufferMsg_t, buf);
	size_t chunkLeft = BufTypedMsgSize - off;

	/* fill message buffer */
	size_t tocopy = (dataLeft > chunkLeft) ? chunkLeft : dataLeft;
	PSP_putTypedMsgBuf(&fragMsg, __func__, "payload",
			   data + (dataLen - dataLeft), tocopy);
	dataLeft -= tocopy;
	chunkLeft -= tocopy;
	buffer->bufUsed += tocopy;

	if (!chunkLeft) {
	    /* message is filled, send fragment to all nodes */
	    sendFragment(buffer, caller, line);

	    /* prepare for next fragment */
	    fragMsg.header.len = offsetof(DDTypedBufferMsg_t, buf);
	    buffer->fragNum++;
	    PSP_putTypedMsgBuf(&fragMsg, __func__, "fragType", &type,
			       sizeof(type));
	    PSP_putTypedMsgBuf(&fragMsg, __func__, "fragNum",
			       &buffer->fragNum, sizeof(buffer->fragNum));
	}
    }
}

/**
 * @brief Add data to a send buffer.
 *
 * @param buffer The send buffer to use
 *
 * @param data The data to add
 *
 * @param dataLen The size of the data to add
 */
static bool addData(PS_SendDB_t *buffer, const void *data, const size_t dataLen,
		    const char *caller, const int line)
{
    if (buffer->useFrag) {
	/* fragmented message */
	addFragmentedData(buffer, data, dataLen, caller, line);
    } else {
	if (!buffer->buf) buffer->bufUsed = 0;
	/* grow send buffer if needed */
	if (buffer->bufUsed + dataLen > sendBufLen) {
	    size_t s = sendBufLen ? sendBufLen * 2 : DEFAULT_BUFFER_SIZE;
	    char *tmp = realloc(sendBuf, s);
	    if (!tmp) {
		pluginlog("%s(%s@%d): allocation of %zd failed\n",
			  __func__, caller, line, s);
		return false;
	    }
	    sendBufLen = s;
	    sendBuf = tmp;
	}

	memcpy(sendBuf + buffer->bufUsed, data, dataLen);
	buffer->bufUsed += dataLen;
	buffer->buf = sendBuf;
    }
    return true;
}

bool addToBuf(const void *val, const uint32_t size, PS_SendDB_t *data,
	      PS_DataType_t type, const char *caller, const int line)
{
    bool hasLen = (type == PSDATA_STRING || type == PSDATA_DATA);

    if (!data) {
	pluginlog("%s(%s@%d): invalid data\n", __func__, caller, line);
	return false;
    }

    if (!val && (!hasLen || size)) {
	pluginlog("%s(%s@%d): invalid val\n", __func__, caller, line);
	return false;
    }

    if (!sendBuf) {
	pluginlog("%s(%s@%d): call initSerial()\n", __func__, caller, line);
	return false;
    }

    /* add data type */
    if (typeInfo) {
	uint8_t dType = type;
	if (!addData(data, &dType, sizeof(dType), caller, line)) return false;
    }

    /* add data length if required */
    if (hasLen) {
	uint32_t len = byteOrder ? htonl(size) : size;
	if (!addData(data, &len, sizeof(len), caller, line)) return false;
    }

    /* add data */
    if (byteOrder && !hasLen && type != PSDATA_MEM) {
	switch (size) {
	case 1:
	    if (!addData(data, val, size, caller, line)) return false;
	    break;
	case 2:
	{
	    uint16_t u16 = htons(*(uint16_t*)val);
	    if (!addData(data, &u16, sizeof(u16), caller, line)) return false;
	    break;
	}
	case 4:
	{
	    uint32_t u32 = htonl(*(uint32_t*)val);
	    if (!addData(data, &u32, sizeof(u32), caller, line)) return false;
	    break;
	}
	case 8:
	{
	    uint64_t u64 = HTON64(*(uint64_t*)val);
	    if (!addData(data, &u64, sizeof(u64), caller, line)) return false;
	    break;
	}
	default:
	    if (!addData(data, val, size, caller, line)) return false;
	    pluginlog("%s(%s@%d): unknown conversion for size %d\n",
		      __func__, caller, line, size);
	}
    } else {
	if (!addData(data, val, size, caller, line)) return false;
    }

    return true;
}

bool addArrayToBuf(const void *val, const uint32_t num, PS_SendDB_t *data,
		   PS_DataType_t type, size_t size,
		   const char *caller, const int line)
{
    uint32_t i;
    const char *valPtr = val;

    if (!data) {
	pluginlog("%s(%s@%d): invalid data\n", __func__, caller, line);
	return false;
    }
    if (!val) {
	pluginlog("%s(%s@%d): invalid val\n", __func__, caller, line);
	return false;
    }

    if (!addToBuf(&num, sizeof(num), data, PSDATA_UINT32, caller, line))
	return false;

    for (i = 0; i < num; i++) {
	addToBuf(valPtr + i*size, size, data, type, caller, line);
    }

    return true;
}
