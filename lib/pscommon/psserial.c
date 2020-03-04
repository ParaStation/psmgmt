/*
 * ParaStation
 *
 * Copyright (C) 2012-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h>
#include <dlfcn.h>

#include "pscommon.h"
#include "psitems.h"

#include "psidhook.h"
#include "psidutil.h"

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

#define DEFAULT_BUFFER_SIZE 128 * 1024

typedef enum {
    FRAGMENT_PART = 0x01,   /* one fragment, more to follow */
    FRAGMENT_END  = 0x02,   /* the end of a fragmented message */
} FragType_t;

/** Flag byte-order conversation */
static bool byteOrder = true;

/** Flag insertion of type information into actual messages */
static bool typeInfo = false;

/** send buffer for non fragmented messages; this also flags initialization */
static char *sendBuf = NULL;

/** the send buffer size */
static uint32_t sendBufLen = 0;

/** destination nodes for fragmented messages */
static PStask_ID_t *destNodes = NULL;

/** message to collect and send fragments */
static DDTypedBufferMsg_t fragMsg;

/** count number of active users that have called @ref initSerial() */
static int activeUsers = 0;

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
    free(b->buf);
    b->buf = NULL;
    b->bufSize = 0;
    b->bufUsed = 0;
    b->nextFrag = 0;
}

typedef struct {
    list_t next;          /**< used to put into reservation-lists */
    PStask_ID_t tid;      /**< task ID of the message sender */
    PS_DataBuffer_t dBuf; /**< data buffer to collect message fragments in */
} recvBuf_t;

/** data structure to handle a pool of receive buffers (of type recvBuf_t) */
static PSitems_t recvBuffers = { .initialized = false };

/** List of active receive buffers */
static LIST_HEAD(activeRecvBufs);

/**
 * @brief Get receive buffer from pool
 *
 * Get a receive buffer from the pool of idle receive buffers.
 *
 * @return On success, a pointer to the new receive buffer is
 * returned. Or NULL if an error occurred.
 */
static recvBuf_t *getRecvBuf(void)
{
    recvBuf_t *r = PSitems_getItem(&recvBuffers);

    if (!r) {
	PSC_log(-1, "%s: no receive buffers left\n", __func__);
	return NULL;
    }

    resetBuf(&r->dBuf);

    return r;
}

/**
 * @brief Put receive buffer to pool
 *
 * Put the receive buffer @a recvBuf into the pool of idle receive
 * buffers. For this @a recvBuf will be removed from the list it
 * belongs to -- if any.
 *
 * @return On success, a pointer to the new receive buffer is
 * returned. Or NULL if an error occurred.
 */
static void putRecvBuf(recvBuf_t *recvBuf)
{
    recvBuf->tid = PSITEM_IDLE;
    resetBuf(&recvBuf->dBuf);
    if (!list_empty(&recvBuf->next)) list_del(&recvBuf->next);
    PSitems_putItem(&recvBuffers, recvBuf);
}

/**
 * @brief Find receive buffer
 *
 * Find an active receive buffer holding messages from sender @a tid
 * in the list @ref activeRecvBufs. If a corresponding receive buffer
 * was found a pointer to the corresponding data buffer is returned.
 *
 * @param tid Sender's task ID of the active receive buffer to search for
 *
 * @return If a receive buffer was foud a pointer to its data buffer
 * is returned. Or NULL in case of error, i.e. no buffer was found.
 */
static PS_DataBuffer_t *findRecvBuf(PStask_ID_t tid)
{
    list_t *r;
    list_for_each(r, &activeRecvBufs) {
	recvBuf_t *recvBuf = list_entry(r, recvBuf_t, next);

	if (recvBuf->tid == tid) return &recvBuf->dBuf;
    }

    return NULL;
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
	PSC_log(-1, "%s: invalid node id %i\n", __func__, id);
	return 1;
    }

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &activeRecvBufs) {
	recvBuf_t *recvBuf = list_entry(r, recvBuf_t, next);

	if (PSC_getID(recvBuf->tid) == id) {
	    PSC_log(PSC_LOG_COMM, "%s: free recvBuffer for %s\n", __func__,
		    PSC_printTID(recvBuf->tid));
	    putRecvBuf(recvBuf);
	}
    }

    return 1;
}

/**
 * @brief Callback to clear memory
 *
 * This callback is called by the (child of the) hosting daemon to
 * aggresively clear memory after a new process was forked right
 * before handling other tasks, e.g. becoming a forwarder.
 *
 * @param dummy Ignored parameter
 *
 * @return Always return 0
 */
static int clearMem(void *dummy)
{
    free(sendBuf);
    sendBuf = NULL;
    sendBufLen = 0;
    free(destNodes);
    destNodes = NULL;

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &activeRecvBufs) {
	recvBuf_t *recvBuf = list_entry(r, recvBuf_t, next);

	putRecvBuf(recvBuf);
    }
    PSitems_clearMem(&recvBuffers);

    activeUsers = 0;
    sendPSMsg = NULL;

    return 0;
}

/** Function to remove registered hooks later on */
static int (*hookDel)(PSIDhook_t, PSIDhook_func_t) = NULL;

/**
 * @brief Register hook for down nodes if possible
 *
 * Try to register @ref nodeDownHandler() to the PSIDHOOK_NODE_DOWN
 * hook and @ref clearMem() to the PSIDHOOK_CLEARMEM hook of psid. For
 * this try to determine if we are running within psid by searching
 * for the symbols of @ref PSIDhook_add() and @ref PSIDhook_del(). If
 * both symbols are found they are used to register the hook functions
 * immediately. Furthermore, @a hookDel is set in order to be
 * prepared to remove the hook functions from the hooks within @ref
 * finalizeSerial().
 *
 * @return No return value
 */
static void initSerialHooks(void)
{
    /* Determine if PSIDhook_add is available */
    void *mainHandle = dlopen(NULL, 0);

    int (*hookAdd)(PSIDhook_t, PSIDhook_func_t) = dlsym(mainHandle,
							"PSIDhook_add");
    hookDel = dlsym(mainHandle, "PSIDhook_del");

    if (hookAdd && hookDel) {
	if (!hookAdd(PSIDHOOK_NODE_DOWN, nodeDownHandler)) {
	    PSC_log(-1, "%s: cannot register PSIDHOOK_NODE_DOWN\n", __func__);
	}
	if (!hookAdd(PSIDHOOK_CLEARMEM, clearMem)) {
	    PSC_log(-1, "%s: cannot register PSIDHOOK_CLEARMEM\n", __func__);
	}
    }
}

static bool relocRecvBuf(void *item)
{
    recvBuf_t *orig = (recvBuf_t *)item, *repl = getRecvBuf();

    if (!repl) return false;

    repl->tid = orig->tid;
    repl->dBuf.buf = orig->dBuf.buf;
    repl->dBuf.bufSize = orig->dBuf.bufSize;
    repl->dBuf.bufUsed = orig->dBuf.bufUsed;
    repl->dBuf.nextFrag = orig->dBuf.nextFrag;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

static void recvBuf_gc(void)
{
    PSitems_gc(&recvBuffers, relocRecvBuf);
}

/** Function to remove registered hooks later on */
static int (*relLoopAct)(PSID_loopAction_t) = NULL;

/**
 * @brief Register loop action if possible
 *
 * Try to register the garbage collector @ref recvBuf_gc() to the
 * hosting daemons central loop action. For this try to determine if
 * we are running within psid by searching for the symbols of @ref
 * PSID_registerLoopAct() and @ref PSID_unregisterLoopAct(). If both
 * symbols are found they are used to register @ref recvBuf_gc()
 * immediately. Furthermore, @a relLoopAct is set in order to be
 * prepared to remove it from the central loop within @ref
 * finalizeSerial().
 *
 * @return No return value
 */
static void initLoopAction(void)
{
    /* Determine if PSID_registerLoopAct is available */
    void *mainHandle = dlopen(NULL, 0);

    int (*regLoopAct)(PSID_loopAction_t) = dlsym(mainHandle,
						 "PSID_registerLoopAct");
    relLoopAct = dlsym(mainHandle, "PSID_unregisterLoopAct");

    if (regLoopAct && relLoopAct) {
	if (regLoopAct(recvBuf_gc) < 0) {
	    PSC_warn(-1, errno, "%s: register loop action", __func__);
	    return;
	}
    }
}

bool initSerialBuf(size_t bufSize)
{
    if (sendBuf) return true;

    if (!PSC_logInitialized()) PSC_initLog(stderr);

    /* allocate send buffers */
    sendBufLen = bufSize ? bufSize : DEFAULT_BUFFER_SIZE;
    sendBuf = malloc(sendBufLen);

    if (!sendBuf) {
	PSC_log(-1, "%s: cannot allocate buffer\n", __func__);

	return false;
    }

    return true;
}

bool initSerial(size_t bufSize, Send_Msg_Func_t *func)
{
    PSnodes_ID_t numNodes = PSC_getNrOfNodes();

    if (activeUsers++) return true;

    if (!PSC_logInitialized()) PSC_initLog(stderr);

    if (numNodes == -1) {
	PSC_log(-1, "%s: unable to get number of nodes\n", __func__);
	return false;
    }

    if (!initSerialBuf(bufSize)) return false;

    /* allocated space for destination nodes */
    free(destNodes);
    destNodes = malloc(sizeof(*destNodes) * numNodes);

    if (!sendBuf || !destNodes) {
	PSC_log(-1, "%s: cannot allocate all buffers\n", __func__);
	free(sendBuf);
	sendBuf = NULL;
	free(destNodes);
	destNodes = NULL;

	return false;
    }

    sendPSMsg = func;
    if (!hookDel) initSerialHooks();
    if (!relLoopAct) initLoopAction();

    /* Initialize receive buffer handling */
    PSitems_init(&recvBuffers, sizeof(recvBuf_t), "recvBuffers");

    return true;
}

void finalizeSerial(void)
{
    if (--activeUsers) return;

    clearMem(NULL);

    if (hookDel) {
	hookDel(PSIDHOOK_NODE_DOWN, nodeDownHandler);
	hookDel(PSIDHOOK_CLEARMEM, clearMem);
    }
    if (relLoopAct) relLoopAct(recvBuf_gc);
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
	PSC_log(-1, "%s: max destinations (%d) reached\n", __func__, numNodes);
	return false;
    }

    if (!PSC_validNode(PSC_getID(id))) {
	PSC_log(-1, "%s: nodeID %i out of range\n", __func__, PSC_getID(id));
	return false;
    }

    destNodes[buffer->numDest++] = id;

    return true;
}

bool setFragDestUniq(PS_SendDB_t *buffer, PStask_ID_t id)
{
    int i;

    for (i = 0; i < buffer->numDest; i++) {
	if (destNodes[i] == id) return false;
    }

    return setFragDest(buffer, id);
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
	PSC_log(-1, "%s(%s@%d): empty nodelist\n", __func__, caller, line);
	return false;
    }

    if (!sendPSMsg) {
	PSC_log(-1, "%s(%s@%d): no send function\n", __func__, caller, line);
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
	PSC_log(PSC_LOG_COMM, "%s: send fragment %d to %s len %u\n", __func__,
		buf->fragNum, PSC_printTID(destNodes[i]), fragMsg.header.len);

	int res = sendPSMsg(&fragMsg);

	if (res == -1 && errno != EWOULDBLOCK) {
	    ret = false;
	    PSC_warn(-1, errno, "%s(%s@%d): send(fragment %u)", __func__,
		     caller, line, buf->fragNum);
	}
    }

    /* send any local messages now */
    if (localTask != -1) {
	fragMsg.header.dest = localTask;
	PSC_log(PSC_LOG_COMM, "%s: send fragment %d to %s len %u\n", __func__,
		buf->fragNum, PSC_printTID(localTask), fragMsg.header.len);

	int res = sendPSMsg(&fragMsg);

	if (res == -1 && errno != EWOULDBLOCK) {
	    ret = false;
	    PSC_warn(-1, errno, "%s(%s@%d): send (fragment %u)", __func__,
		     caller, line, buf->fragNum);
	}
    }
    return ret;
}

bool __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		   const char *caller, const int line)
{
    PS_DataBuffer_t *recvBuf, myRecvBuf;
    size_t used = 0;
    uint8_t fType;
    uint16_t fNum;

    if (!msg) {
	PSC_log(-1, "%s(%s@%d): invalid msg\n", __func__, caller, line);
	return false;
    }

    if (!func) {
	PSC_log(-1, "%s(%s@%d): no callback\n", __func__, caller, line);
	return false;
    }

    PSP_getTypedMsgBuf(msg, &used, __func__, "fragType", &fType, sizeof(fType));
    PSP_getTypedMsgBuf(msg, &used, __func__, "fragNum", &fNum, sizeof(fNum));

    if (fType != FRAGMENT_PART && fType != FRAGMENT_END) {
	PSC_log(-1, "%s: invalid fragment type %u from %s\n", __func__,
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
	if (!sendBuf) {
	    PSC_log(-1, "%s(%s@%d): call initSerial()\n", __func__,
		    caller, line);
	    return false;
	}

	recvBuf = findRecvBuf(msg->header.sender);
	uint16_t expectedFrag = recvBuf ? recvBuf->nextFrag : 0;

	if (fNum != expectedFrag) {
	    PSC_log(-1, "%s: unexpected fragment %u/%u from %s\n", __func__,
		    fNum, expectedFrag, PSC_printTID(msg->header.sender));
	    if (fNum) return false;
	}

	if (!recvBuf) {
	    recvBuf_t *r = getRecvBuf();
	    if (!r) {
		PSC_log(-1, "%s(%s@%d): no buffer\n", __func__, caller, line);
		return false;
	    }
	    r->tid = msg->header.sender;
	    recvBuf = &r->dBuf;
	    list_add_tail(&r->next, &activeRecvBufs);
	}

	if (fNum == 0) {
	    if (recvBuf->buf) PSC_log(-1, "%s: found buffer for%s\n", __func__,
				      PSC_printTID(msg->header.sender));

	    resetBuf(recvBuf);
	    recvBuf->bufSize = DEFAULT_BUFFER_SIZE;
	    recvBuf->buf = malloc(recvBuf->bufSize);
	    if (!recvBuf->buf) {
		PSC_log(-1, "%s(%s@%d): no memory\n", __func__, caller, line);
		putRecvBuf(list_entry(recvBuf, recvBuf_t, dBuf));
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
		putRecvBuf(list_entry(recvBuf, recvBuf_t, dBuf));
		return false;
	    }
	    recvBuf->buf = tmp;
	}
	char *ptr = recvBuf->buf + recvBuf->bufUsed;
	PSP_getTypedMsgBuf(msg, &used, __func__, "payload", ptr, toCopy);
	recvBuf->bufUsed += toCopy;
	recvBuf->nextFrag++;
    }

    PSC_log(PSC_LOG_COMM, "%s: recv fragment %d from %s\n", __func__, fNum,
	    PSC_printTID(msg->header.sender));

    /* last message fragment ? */
    if (fType == FRAGMENT_END) {
	/* invoke callback */

	PSC_log(PSC_LOG_COMM, "%s: msg of %u bytes from %s complete\n",
		__func__, recvBuf->bufUsed, PSC_printTID(msg->header.sender));

	msg->buf[0] = '\0';
	func(msg, recvBuf);

	/* cleanup data if necessary */
	if (fNum) putRecvBuf(list_entry(recvBuf, recvBuf_t, dBuf));
    }

    return true;
}

int __sendFragMsg(PS_SendDB_t *buffer, const char *caller, const int line)
{
    if (!sendBuf) {
	PSC_log(-1, "%s(%s@%d): call initSerial()\n", __func__, caller, line);
	return -1;
    }

    if (!buffer->bufUsed) {
	PSC_log(-1, "%s(%s@%d): no data to send\n", __func__, caller, line);
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
	PSC_log(-1, "%s(%s@%d): allocation of %zd failed\n", __func__,
		caller, line, newLen);
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
	PSC_log(-1, "%s: duplication failed\n", __func__);
	return NULL;
    }

    dup->buf = umalloc(data->bufSize);
    if (!dup->buf) {
	PSC_log(-1, "%s: buffer duplication failed\n", __func__);
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
	PSC_log(-1, "%s(%s@%d): invalid buffer\n", __func__, caller, line);
	return false;
    }

    if (!growDataBuffer(len, buffer, caller, line)) {
	PSC_log(-1, "%s(%s@%d): cannot grow buffer\n", __func__, caller, line);
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

	    PSC_warn(-1, eno, "%s(%s): write(%d)", __func__, func, fd);
	    lastLog = now;
	    errno = eno;
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

	    PSC_warn(-1, eno, "%s(%s): read(%d)", __func__, func, fd);
	    lastLog = now;
	    errno = eno;
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
	PSC_log(-1, "%s(%s@%d): error got type %d should be %d\n", __func__,
		caller, line, type, expectedType);
	return false;
    }
    return true;
}

bool getFromBuf(char **ptr, void *val, PS_DataType_t type,
		size_t size, const char *caller, const int line)
{
    if (!*ptr) {
	PSC_log(-1, "%s(%s@%d): invalid ptr\n", __func__, caller, line);
	return false;
    }

    if (!val) {
	PSC_log(-1, "%s(%s@%d): invalid val\n", __func__, caller, line);
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
	    PSC_log(-1, "%s(%s@%d): unknown conversion for size %zd\n",
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

    if (!*len) return true;
    *val = umalloc(size * *len);
    if (!*val) {
	PSC_log(-1, "%s(%s@%d): allocation of %zd failed\n", __func__,
		caller, line, size * *len);
	return false;
    }

    for (i = 0; i < *len; i++) {
	getFromBuf(ptr, (char *)*val + i*size, type, size, caller, line);
    }

    return true;
}

void *getMemFromBuf(char **ptr, char *data, size_t dataSize, size_t *len,
		    PS_DataType_t type, const char *caller, const int line)
{
    uint32_t l;

    if (dataSize && !data) {
	PSC_log(-1, "%s(%s@%d): invalid buffer\n", __func__, caller, line);
	return NULL;
    }

    if (!*ptr) {
	PSC_log(-1, "%s(%s@%d): invalid ptr\n", __func__, caller, line);
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
	    PSC_log(-1, "%s(%s@%d): buffer (%zu) to small for message (%u)\n",
		    __func__, caller, line, dataSize, l);
	    return NULL;
	}
    } else {
	data = umalloc(l);
	if (!data) {
	    PSC_log(-1, "%s(%s@%d): allocation of %u failed\n", __func__,
		    caller, line, l);
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
	PSC_log(-1, "%s(%s@%d): allocation of %zd failed\n", __func__,
		caller, line, sizeof(char *) * (*len + 1));
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
			   (char *)data + (dataLen - dataLeft), tocopy);
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
		PSC_log(-1, "%s(%s@%d): allocation of %zd failed\n", __func__,
			caller, line, s);
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
	PSC_log(-1, "%s(%s@%d): invalid data\n", __func__, caller, line);
	return false;
    }

    if (!val && (!hasLen || size)) {
	PSC_log(-1, "%s(%s@%d): invalid val\n", __func__, caller, line);
	return false;
    }

    if (!sendBuf) {
	PSC_log(-1, "%s(%s@%d): call initSerial()\n", __func__, caller, line);
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
	    PSC_log(-1, "%s(%s@%d): unknown conversion for size %d\n",
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
	PSC_log(-1, "%s(%s@%d): invalid data\n", __func__, caller, line);
	return false;
    }
    if (!val) {
	PSC_log(-1, "%s(%s@%d): invalid val\n", __func__, caller, line);
	return false;
    }

    if (!addToBuf(&num, sizeof(num), data, PSDATA_UINT32, caller, line))
	return false;

    for (i = 0; i < num; i++) {
	addToBuf(valPtr + i*size, size, data, type, caller, line);
    }

    return true;
}
