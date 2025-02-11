/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psserial.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "pscommon.h"
#include "psitems.h"

#include "psidhook.h"
#include "psidutil.h"

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

/** Flag byte-order conversation */
static bool byteOrder = true;

/** Flag insertion of type information into actual messages */
static bool typeInfo = false;

/** send buffer for non fragmented messages; this also flags initialization */
static char *sendBuf = NULL;

/** the send buffer size */
static uint32_t sendBufLen = 0;

/** destination task IDs for fragmented messages */
static PStask_ID_t *destTIDs = NULL;

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

PS_DataBuffer_t PSdbNew(char *buffer, size_t bufSize)
{
    PS_DataBuffer_t data = calloc(1, sizeof(*data));
    data->buf = data->unpackPtr = buffer;
    data->size = data->used = bufSize;

    return data;
}

void PSdbDelete(PS_DataBuffer_t data)
{
    free(data);
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
static void resetBuf(PS_DataBuffer_t b)
{
    free(b->buf);
    memset(b, 0, sizeof(*b));
}

typedef struct {
    list_t next;          /**< used to put into reservation-lists */
    PStask_ID_t tid;      /**< task ID of the message sender */
    struct PS_DataBuffer dBuf; /**< data buffer to collect message fragments in */
} recvBuf_t;

/** data structure to handle a pool of receive buffers (of type recvBuf_t) */
static PSitems_t recvBuffers = NULL;

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
    recvBuf_t *r = PSitems_getItem(recvBuffers);

    if (!r) {
	PSC_flog("no receive buffers left\n");
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
    PSitems_putItem(recvBuffers, recvBuf);
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
static PS_DataBuffer_t findRecvBuf(PStask_ID_t tid)
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
	PSC_flog("invalid node id %i\n", id);
	return 1;
    }

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &activeRecvBufs) {
	recvBuf_t *recvBuf = list_entry(r, recvBuf_t, next);

	if (PSC_getID(recvBuf->tid) == id) {
	    PSC_fdbg(PSC_LOG_COMM, "free recvBuffer for %s\n",
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
    free(destTIDs);
    destTIDs = NULL;

    list_t *r, *tmp;
    list_for_each_safe(r, tmp, &activeRecvBufs) {
	recvBuf_t *recvBuf = list_entry(r, recvBuf_t, next);

	putRecvBuf(recvBuf);
    }
    PSitems_clearMem(recvBuffers);
    recvBuffers = NULL;

    activeUsers = 0;
    sendPSMsg = NULL;

    return 0;
}

/** Dynamic linkers handle to the local main program */
static void *mainHandle = NULL;

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
    if (!mainHandle) mainHandle = dlopen(NULL, 0);
    int (*hookAdd)(PSIDhook_t, PSIDhook_func_t) = dlsym(mainHandle,
							"PSIDhook_add");
    hookDel = dlsym(mainHandle, "PSIDhook_del");

    if (hookAdd && hookDel) {
	if (!hookAdd(PSIDHOOK_NODE_DOWN, nodeDownHandler)) {
	    PSC_flog("cannot register PSIDHOOK_NODE_DOWN\n");
	}
	if (!hookAdd(PSIDHOOK_CLEARMEM, clearMem)) {
	    PSC_flog("cannot register PSIDHOOK_CLEARMEM\n");
	}
    }
}

static bool relocRecvBuf(void *item)
{
    recvBuf_t *orig = (recvBuf_t *)item, *repl = getRecvBuf();

    if (!repl) return false;

    repl->tid = orig->tid;
    repl->dBuf.buf = orig->dBuf.buf;
    repl->dBuf.size = orig->dBuf.size;
    repl->dBuf.used = orig->dBuf.used;
    repl->dBuf.nextFrag = orig->dBuf.nextFrag;

    /* tweak the list */
    __list_add(&repl->next, orig->next.prev, orig->next.next);

    return true;
}

static void recvBuf_gc(void)
{
    PSitems_gc(recvBuffers, relocRecvBuf);
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
    if (!mainHandle) mainHandle = dlopen(NULL, 0);
    int (*regLoopAct)(PSID_loopAction_t) = dlsym(mainHandle,
						 "PSID_registerLoopAct");
    relLoopAct = dlsym(mainHandle, "PSID_unregisterLoopAct");

    if (regLoopAct && relLoopAct) {
	if (regLoopAct(recvBuf_gc) < 0) {
	    PSC_fwarn(errno, "register loop action");
	    return;
	}
    }
}

/**
 * The protocol version resolver. This might be either @ref
 * PSIDnodes_getProtoV() (if we are inside psid) or @ref
 * PSI_protoVersion() (if libpsi is accessible)
 */
static int (*getProtoV)(PSnodes_ID_t) = NULL;

/**
 * @brief Initialize determination of protocol version
 *
 * Initialize the protocol version resolver. This will setup @ref
 * getProtoV() appropriately.
 *
 * @return No return value
 */
static void initProtoResolver(void)
{
    /* Determine if PSIDnodes_getProtoV is available, i.e. we live in psid */
    if (!mainHandle) mainHandle = dlopen(NULL, 0);

    getProtoV = dlsym(mainHandle, "PSIDnodes_getProtoV");
    if (getProtoV) return;

    /* Maybe we live inside a client program and libpsi is available */
    getProtoV = dlsym(mainHandle, "PSI_protocolVersion");
    if (getProtoV) return;

    PSC_exit(EACCES, "%s: cannot initialize", __func__);
}

bool initSerialBuf(size_t bufSize)
{
    if (sendBuf) return true;

    if (!PSC_logInitialized()) PSC_initLog(stderr);

    /* allocate send buffers */
    sendBufLen = bufSize ? bufSize : DEFAULT_BUFFER_SIZE;
    sendBuf = malloc(sendBufLen);

    if (!sendBuf) {
	PSC_flog("cannot allocate buffer\n");

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
	PSC_flog("unable to get number of nodes\n");
	return false;
    }

    if (!initSerialBuf(bufSize)) return false;

    /* allocated space for destination task IDs */
    free(destTIDs);
    destTIDs = malloc(sizeof(*destTIDs) * numNodes);

    if (!sendBuf || !destTIDs) {
	PSC_flog("cannot allocate all buffers\n");
	free(sendBuf);
	sendBuf = NULL;
	free(destTIDs);
	destTIDs = NULL;

	return false;
    }

    sendPSMsg = func;
    if (!hookDel) initSerialHooks();
    if (!relLoopAct) initLoopAction();
    if (!getProtoV) initProtoResolver();

    /* Initialize receive buffer handling */
    recvBuffers = PSitems_new(sizeof(recvBuf_t), "recvBuffers");
    if (!recvBuffers) {
	PSC_flog("cannot get recvBuffer items\n");
	free(sendBuf);
	sendBuf = NULL;
	free(destTIDs);
	destTIDs = NULL;
	return false;
    }

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

void initFragBufferExtra(PS_SendDB_t *buffer, int16_t headType, int32_t msgType,
			 void *extra, uint8_t extraSize)
{
    buffer->useFrag = true;
    buffer->bufUsed = 0;
    buffer->headType = headType;
    buffer->msgType = msgType;
    buffer->fragNum = 0;
    buffer->numDest = 0;
    buffer->extra = extra;
    buffer->extraSize = extra ? extraSize : 0;
}

bool _setFragDest(PS_SendDB_t *buffer, PStask_ID_t tid, const char *func,
		  const int line)
{
    PSnodes_ID_t numNodes = PSC_getNrOfNodes();

    if (buffer->numDest >= numNodes) {
	PSC_flog("max destinations (%d) reached at %s:%d\n", numNodes, func, line);
	return false;
    }

    if (!PSC_validNode(PSC_getID(tid))) {
	PSC_flog("nodeID %i out of range at %s:%d\n", PSC_getID(tid), func, line);
	return false;
    }

    destTIDs[buffer->numDest++] = tid;

    return true;
}

bool setFragDestUniq(PS_SendDB_t *buffer, PStask_ID_t tid)
{
    for (int i = 0; i < buffer->numDest; i++) {
	if (destTIDs[i] == tid) return false;
    }

    return setFragDest(buffer, tid);
}

char *serialStrErr(serial_Err_Types_t err)
{
    static char buf[128];

    switch(err) {
    case E_PSSERIAL_SUCCESS:
	return "operation successful";
    case E_PSSERIAL_INSUF:
	return "insufficient data in buffer";
    case E_PSSERIAL_PARAM:
	return "invalid parameter";
    case E_PSSERIAL_TYPE:
	return "data type mismatch";
    case E_PSSERIAL_BUFSIZE:
	return "provided buffer to small";
    case E_PSSERIAL_MEM:
	return "out of memory";
    case E_PSSERIAL_CONV:
	return "unknown conversion size";
    default:
	snprintf(buf, sizeof(buf), "unknown error code %i", err);
	return buf;
    }
}

/**
 * @brief Send a message fragment
 *
 * Send a message fragment to all nodes set in @a destTIDs.
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
    if (!buf->numDest) {
	PSC_flog("empty nodelist at %s@%d\n", caller, line);
	return false;
    }

    if (!sendPSMsg) {
	PSC_flog("no send function at %s@%d\n", caller, line);
	return false;
    }

    bool ret = true;
    PStask_ID_t localDest = -1;
    for (PSnodes_ID_t i = 0; i < buf->numDest; i++) {
	if (PSC_getID(destTIDs[i]) == PSC_getMyID()) {
	    /* local messages might overwrite the shared send message buffer,
	     * therefore it needs to be the last message send */
	    localDest = destTIDs[i];
	    continue;
	}

	fragMsg.header.dest = destTIDs[i];
	PSC_fdbg(PSC_LOG_COMM, "send fragment %d to %s len %u\n",
		 buf->fragNum, PSC_printTID(destTIDs[i]), fragMsg.header.len);

	int res = sendPSMsg(&fragMsg);
	if (res == -1 && errno != EWOULDBLOCK) {
	    ret = false;
	    PSC_fwarn(errno, "at %s@%d: send(fragment %u, dest %s)",
		      caller, line, buf->fragNum, PSC_printTID(destTIDs[i]));
	}
    }

    /* send any local messages now */
    if (localDest != -1) {
	fragMsg.header.dest = localDest;
	PSC_fdbg(PSC_LOG_COMM, "send fragment %d to %s len %u\n",
		 buf->fragNum, PSC_printTID(localDest), fragMsg.header.len);

	int res = sendPSMsg(&fragMsg);

	if (res == -1 && errno != EWOULDBLOCK) {
	    ret = false;
	    PSC_fwarn(errno, "at %s@%d: send(fragment %u, local dest %s)",
		      caller, line, buf->fragNum, PSC_printTID(localDest));
	}
    }
    return ret;
}

bool fetchFragHeader(DDTypedBufferMsg_t *msg, size_t *used, uint8_t *fragType,
		     uint16_t *fragNum, void **extra, size_t *extraSize)
{
    if (!used) return false;

    uint8_t fType = 0;
    if (!PSP_getTypedMsgBuf(msg, used, "fragType", &fType, sizeof(fType))) {
	return false;
    }
    if (fragType) *fragType = fType;

    uint16_t fNum = -1;
    if (!PSP_getTypedMsgBuf(msg, used, "fragNum", &fNum, sizeof(fNum))) {
	return false;
    }
    if (fragNum) *fragNum = fNum;

    if (extraSize) *extraSize = 0;
    if (extra) *extra = NULL;

    /* handle extra data */
    uint8_t eS;
    PSP_getTypedMsgBuf(msg, used, "extraSize", &eS, sizeof(eS));
    if (extraSize) *extraSize = eS;
    if (extra && eS) *extra = msg->buf + *used;
    *used += eS;

    return true;
}

bool __recvFragMsg(DDTypedBufferMsg_t *msg, SerialRecvCB_t *cb,
		   SerialRecvInfoCB_t *infoCB, void *info, bool verbose,
		   const char *caller, const int line)
{
    if (!msg) {
	PSC_flog("invalid msg at %s@%d\n", caller, line);
	return false;
    }

    if (!cb && !infoCB) {
	PSC_flog("no callback at %s@%d\n", caller, line);
	return false;
    }

    size_t used = 0;
    uint8_t fragType = 0;
    uint16_t fragNum = -1;
    if (!fetchFragHeader(msg, &used, &fragType, &fragNum, NULL, NULL)) {
	PSC_flog("unable to fetch fragment header from %s at %s@%d\n",
		 PSC_printTID(msg->header.sender), caller, line);
	return false;
    }

    if (fragType != FRAGMENT_PART && fragType != FRAGMENT_END) {
	PSC_flog("invalid fragment type %u from %s at %s@%d\n", fragType,
		 PSC_printTID(msg->header.sender), caller, line);
	return false;
    }

    struct PS_DataBuffer myRecvBuf;
    PS_DataBuffer_t recvBuf;
    if (fragType == FRAGMENT_END && fragNum == 0) {
	/* Shortcut: message consists of just one fragment */
	uint32_t s = msg->header.len - DDTypedBufMsgOffset - used;

	myRecvBuf.buf = msg->buf + used;
	myRecvBuf.size = myRecvBuf.used = s;

	recvBuf = &myRecvBuf;
    } else {
	if (!sendBuf) {
	    PSC_flog("call initSerial() before %s@%d\n",  caller, line);
	    return false;
	}

	recvBuf = findRecvBuf(msg->header.sender);
	uint16_t expectedFrag = recvBuf ? recvBuf->nextFrag : 0;
	if (fragNum != expectedFrag) {
	    if (verbose) PSC_flog("unexpected fragment %u/%u from %s at %s@%d\n",
				  fragNum, expectedFrag,
				  PSC_printTID(msg->header.sender), caller, line);
	    if (fragNum) return false;
	}

	if (!recvBuf) {
	    recvBuf_t *r = getRecvBuf();
	    if (!r) {
		PSC_flog("no buffer at %s@%d\n", caller, line);
		return false;
	    }
	    r->tid = msg->header.sender;
	    recvBuf = &r->dBuf;
	    list_add_tail(&r->next, &activeRecvBufs);
	}

	if (fragNum == 0) {
	    if (recvBuf->buf) PSC_flog("found buffer for %s\n",
				       PSC_printTID(msg->header.sender));

	    resetBuf(recvBuf);
	    recvBuf->size = DEFAULT_BUFFER_SIZE;
	    recvBuf->buf = malloc(recvBuf->size);
	    if (!recvBuf->buf) {
		PSC_flog("no memory at %s@%d\n", caller, line);
		putRecvBuf(list_entry(recvBuf, recvBuf_t, dBuf));
		return false;
	    }
	}

	/* copy payload */
	size_t toCopy = msg->header.len - DDTypedBufMsgOffset - used;
	if (recvBuf->used + toCopy > recvBuf->size) {
	    /* grow buffer, if necessary */
	    recvBuf->size *= 2;
	    char * tmp = realloc(recvBuf->buf, recvBuf->size);
	    if (!tmp) {
		PSC_flog("realloc(%p, %zd) failed at %s@%d\n",
			 recvBuf->buf, recvBuf->size, caller, line);
		putRecvBuf(list_entry(recvBuf, recvBuf_t, dBuf));
		return false;
	    }
	    recvBuf->buf = tmp;
	}
	char *ptr = recvBuf->buf + recvBuf->used;
	PSP_getTypedMsgBuf(msg, &used, "payload", ptr, toCopy);
	recvBuf->used += toCopy;
	recvBuf->nextFrag++;
	if (!recvBuf->nextFrag) recvBuf->nextFrag = 1; // avoid second 0
    }

    PSC_fdbg(PSC_LOG_COMM, "recv fragment %d from %s\n", fragNum,
	     PSC_printTID(msg->header.sender));

    /* last message fragment ? */
    if (fragType == FRAGMENT_END) {
	/* invoke callback */

	PSC_fdbg(PSC_LOG_COMM, "msg of %zu bytes from %s (%d fragments)\n",
		 recvBuf->used, PSC_printTID(msg->header.sender), fragNum);

	recvBuf->unpackPtr = recvBuf->buf;
	recvBuf->unpackErr = 0;
	if (cb) {
	    cb(msg, recvBuf);
	} else if (infoCB) {
	    infoCB(msg, recvBuf, info);
	}

	/* cleanup data if necessary */
	if (fragNum) putRecvBuf(list_entry(recvBuf, recvBuf_t, dBuf));
    }

    return true;
}

/**
 * @brief Setup @ref fragMsg
 *
 * Setup @ref fragMsg, i.e. the message utilized to collect and send
 * fragments, based on information extracted from the send data-buffer
 * @a buf.
 *
 * @param buf Send data-buffer defining the content to be set up
 *
 * @return No return value
 */
static void setupFragMsg(PS_SendDB_t *buf)
{
    fragMsg.header.type = buf->headType;
    fragMsg.header.len = 0;
    fragMsg.header.sender = PSC_getMyTID();
    fragMsg.type = buf->msgType;
    const uint8_t type = FRAGMENT_PART;
    PSP_putTypedMsgBuf(&fragMsg, "fragType", &type, sizeof(type));
    PSP_putTypedMsgBuf(&fragMsg, "fragNum", &buf->fragNum, sizeof(buf->fragNum));
    PSP_putTypedMsgBuf(&fragMsg, "extraSize", &buf->extraSize,
		       sizeof(buf->extraSize));
    if (buf->extraSize) {
	PSP_putTypedMsgBuf(&fragMsg, "extra", buf->extra, buf->extraSize);
    }
}

int __sendFragMsg(PS_SendDB_t *buffer, const char *caller, const int line)
{
    if (!sendBuf) {
	PSC_flog("call initSerial() before %s@%d\n", caller, line);
	return -1;
    }

    if (!buffer->bufUsed) setupFragMsg(buffer);

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

/**
 * @brief Grow the data buffer if needed
 *
 * Grow the data buffer @a data such that @a len additional bytes fit
 * into the buffer. Buffers will grow by multiples of @ref
 * BufTypedMsgSize and never grow beyond @ref UINT32_MAX.
 *
 * @param len Number of additional bytes needed
 *
 * @param data Pointer to the data buffer to grow
 *
 * @return Return true on success and false on error
 */
static bool growDataBuffer(size_t len, PS_DataBuffer_t data)
{
    if (!data->buf) resetBuf(data);

    size_t newLen;
    if (len) {
	newLen = ((data->used + len - 1) / BufTypedMsgSize + 1)*BufTypedMsgSize;
    } else {
	newLen = data->size ? data->size: BufTypedMsgSize;
    }
    if (newLen > UINT32_MAX) return false;

    char *tmp = realloc(data->buf, newLen);
    if (!tmp) {
	PSC_fwarn(errno, "realloc(%p, %zd)", data->buf, newLen);
	return false;
    }
    data->buf = tmp;
    data->size = newLen;

    return true;
}

void PSdbClear(PS_DataBuffer_t data)
{
    if (!data) return;
    resetBuf(data);
}

void PSdbRewind(PS_DataBuffer_t data)
{
    if (!data) return;

    data->unpackPtr = data->buf;
    data->unpackErr = 0;
}

serial_Err_Types_t PSdbGetErrState(PS_DataBuffer_t data)
{
    if (!data) return E_PSSERIAL_PARAM;
    return data->unpackErr;
}

PS_DataBuffer_t PSdbDup(PS_DataBuffer_t data)
{
    PS_DataBuffer_t dup = PSdbNew(NULL, 0);
    if (!dup) {
	data->unpackErr = E_PSSERIAL_MEM;
	PSC_flog("%s\n", serialStrErr(data->unpackErr));
	return NULL;
    }

    if (!data->size) return dup;

    dup->buf = umalloc(data->size);
    if (!dup->buf) {
	data->unpackErr = E_PSSERIAL_MEM;
	PSC_flog("buf: %s\n", serialStrErr(data->unpackErr));
	PSdbDelete(dup);
	return NULL;
    }

    memcpy(dup->buf, data->buf, data->size);
    dup->size = data->size;
    dup->used = data->used;
    /* start clean reading from top of buffer for duplicate */
    dup->unpackErr = 0;
    dup->unpackPtr = dup->buf;

    return dup;
}

bool __memToDataBuffer(void *mem, size_t len, PS_DataBuffer_t buffer,
		       const char *caller, const int line)
{
    if (!buffer) {
	PSC_flog("invalid buffer at %s@%d\n", caller, line);
	return false;
    }

    if (!growDataBuffer(len, buffer)) {
	PSC_flog("cannot grow buffer at %s@%d\n", caller, line);
	return false;
    }

    memcpy(buffer->buf + buffer->used, mem, len);
    buffer->used += len;

    return true;
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
static bool verifyTypeInfo(PS_DataBuffer_t data, PS_DataType_t expectedType,
			   const char *caller, const int line)
{
    if (!typeInfo) return true;

    uint8_t type = *(uint8_t *) data->unpackPtr;
    data->unpackPtr += sizeof(uint8_t);

    if (type != expectedType) {
	data->unpackErr = E_PSSERIAL_TYPE;
	PSC_flog("type %d vs %d at %s@%d: %s\n", type, expectedType,
		 caller, line, serialStrErr(data->unpackErr));
	return false;
    }
    return true;
}

/**
 * @brief Verify data buffer
 *
 * Make sanity checks to ensure unpacking from the given data buffer is save.
 * This includes checking for previous unpack errors as well as sufficient
 * data is left to read.
 *
 * @param data The data buffer to verify
 *
 * @param size Amount of data to read from @a data
 *
 * @param addTypeInfo Flag to take optional type info into account
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns true if it is safe to continue unpacking data from the
 * buffer or false otherwise
 */
static inline bool verifyDataBuf(PS_DataBuffer_t data, size_t size,
				 bool addTypeInfo, const char *caller,
				 const int line)
{
    if (data->unpackErr) {
	PSC_fdbg(PSC_LOG_VERB, "previous unpack error at %s@%d: %s\n",
		 caller, line, serialStrErr(data->unpackErr));
	return false;
    }

    size_t toread = (addTypeInfo && typeInfo) ? sizeof(uint8_t) + size : size;
    size_t avail = data->used - (data->unpackPtr - data->buf);
    if (toread > avail) {
	data->unpackErr = E_PSSERIAL_INSUF;
	PSC_flog("%zu < %zu at %s@%d: %s\n", avail, toread, caller, line,
		 serialStrErr(data->unpackErr));
	return false;
    }
    return true;
}

bool getFromBuf(PS_DataBuffer_t data, void *val, PS_DataType_t type,
		size_t size, const char *caller, const int line)
{
    if (!data || !data->unpackPtr) {
	PSC_flog("at %s@%d: %s\n", caller, line, serialStrErr(E_PSSERIAL_PARAM));
	if (data) data->unpackErr = E_PSSERIAL_PARAM;
	return false;
    }
    if (!val) {
	data->unpackErr = E_PSSERIAL_PARAM;
	PSC_flog("val at %s@%d: %s\n", caller, line,
		 serialStrErr(data->unpackErr));
	return false;
    }

    if (!verifyDataBuf(data, size, true, caller, line)) return false;
    if (!verifyTypeInfo(data, type, caller, line)) return false;

    memcpy(val, data->unpackPtr, size);
    if (byteOrder && type != PSDATA_MEM) {
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
	    data->unpackErr = E_PSSERIAL_CONV;
	    PSC_flog("size %zd at %s@%d: %s\n", size, caller, line,
		     serialStrErr(data->unpackErr));
	    return false;
	}
    }
    data->unpackPtr += size;

    return true;
}

bool getArrayFromBuf(PS_DataBuffer_t data, void **val, uint32_t *len,
		     PS_DataType_t type, size_t size,
		     const char *caller, const int line)
{
    uint32_t myLen;
    if (!len) len = &myLen;
    if (!getFromBuf(data, len, PSDATA_UINT32, sizeof(*len), caller, line))
	return false;

    if (!*len) return true;
    *val = umalloc(size * *len);
    if (!*val) {
	data->unpackErr = E_PSSERIAL_MEM;
	PSC_flog("size %zd at %s@%d: %s\n", size * *len, caller, line,
		 serialStrErr(data->unpackErr));
	return false;
    }

    for (uint32_t i = 0; i < *len; i++) {
	if (!getFromBuf(data, (char *)*val + i*size, type, size, caller, line)) {
	    free(*val);
	    *val = NULL;
	    return false;
	}
    }

    return true;
}

void *getMemFromBuf(PS_DataBuffer_t data, char *dest, size_t destSize,
		    size_t *len, PS_DataType_t type, const char *caller,
		    const int line)
{
    if (!data || !data->unpackPtr) {
	PSC_flog("data at %s@%d: %s\n", caller, line,
		 serialStrErr(E_PSSERIAL_PARAM));
	if (data) data->unpackErr = E_PSSERIAL_PARAM;
	return NULL;
    }
    if (destSize && !dest) {
	data->unpackErr = E_PSSERIAL_PARAM;
	PSC_flog("buffer at %s@%d: %s\n", caller, line,
		serialStrErr(data->unpackErr));
	return NULL;
    }

    if (!verifyDataBuf(data, sizeof(uint32_t), true, caller, line)) return NULL;
    if (!verifyTypeInfo(data, type, caller, line)) return NULL;

    /* data length */
    uint32_t l = *(uint32_t *) data->unpackPtr;
    data->unpackPtr += sizeof(uint32_t);

    if (byteOrder) l = ntohl(l);
    if (len) *len = l;

    if (!verifyDataBuf(data, l, false, caller, line)) return NULL;

    if (dest) {
	if (l >= destSize) {
	    /* buffer too small */
	    data->unpackErr = E_PSSERIAL_BUFSIZE;
	    PSC_flog("size %zu vs %u at %s@%d: %s\n", destSize, l, caller, line,
		     serialStrErr(data->unpackErr));
	    return NULL;
	}
    } else {
	dest = umalloc(l);
	if (!dest) {
	    data->unpackErr = E_PSSERIAL_MEM;
	    PSC_flog("size %u at %s@%d: %s\n", l, caller, line,
		     serialStrErr(data->unpackErr));
	    return NULL;
	}
    }

    /* extract data */
    if (l > 0) {
	memcpy(dest, data->unpackPtr, l);
	if (type == PSDATA_STRING) dest[l-1] = '\0';
	data->unpackPtr += l;
    } else if (type == PSDATA_STRING) {
	dest[0] = '\0';
    }

    return dest;
}

bool __getStringArrayM(PS_DataBuffer_t data, char ***array, uint32_t *len,
			const char *caller, const int line)
{
    if (!array) {
	PSC_flog("array at %s@%d: %s\n", caller, line,
		 serialStrErr(E_PSSERIAL_PARAM));
	if (data) data->unpackErr = E_PSSERIAL_PARAM;
	return false;
    }
    *array = NULL;

    uint32_t myLen;
    if (!len) len = &myLen;
    if (!getFromBuf(data, len, PSDATA_UINT32, sizeof(*len), caller, line))
	return false;

    if (!*len) return true;

    *array = umalloc(sizeof(char *) * (*len + 1));
    if (!*array) {
	data->unpackErr = E_PSSERIAL_MEM;
	PSC_flog("size %zd at %s@%d: %s\n", sizeof(char *) * (*len + 1),
		 caller, line, serialStrErr(data->unpackErr));
	return false;
    }

    for (uint32_t i = 0; i < *len; i++) {
	(*array)[i] = getMemFromBuf(data, NULL, 0, NULL, PSDATA_STRING,
				    caller, line);
	if (data->unpackErr) {
	    for (uint32_t j = 0; j < i; j++) free((*array)[j]);
	    free(*array);
	    *array = NULL;
	    return false;
	}
    }

    (*array)[*len] = NULL;

    return true;
}

/******************** adding data  ********************/

static void addFragmentedData(PS_SendDB_t *buffer, const void *data,
			      const size_t dataLen, const char *caller,
			      const int line)
{
    if (!buffer->bufUsed) setupFragMsg(buffer);

    size_t dataLeft = dataLen;
    while (dataLeft > 0) {
	size_t off = fragMsg.header.len - DDTypedBufMsgOffset;
	size_t chunkLeft = BufTypedMsgSize - off;

	/* fill message buffer */
	size_t tocopy = (dataLeft > chunkLeft) ? chunkLeft : dataLeft;
	PSP_putTypedMsgBuf(&fragMsg, "payload",
			   (char *)data + (dataLen - dataLeft), tocopy);
	dataLeft -= tocopy;
	chunkLeft -= tocopy;
	buffer->bufUsed += tocopy;

	if (!chunkLeft) {
	    /* message is filled, send fragment to all nodes */
	    sendFragment(buffer, caller, line);

	    /* prepare for next fragment */
	    buffer->fragNum++;
	    if (!buffer->fragNum) buffer->fragNum = 1; // avoid second 0

	    setupFragMsg(buffer);
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
		PSC_flog("allocation of %zd failed at %s@%d\n", s, caller, line);
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

bool addToBuf(const void *val, const uint32_t size, PS_SendDB_t *buffer,
	      PS_DataType_t type, const char *caller, const int line)
{
    bool hasLen = (type == PSDATA_STRING || type == PSDATA_DATA);

    if (!buffer) {
	PSC_flog("invalid buffer at %s@%d\n", caller, line);
	return false;
    }

    if (!val && (!hasLen || size)) {
	PSC_flog("invalid val at %s@%d\n", caller, line);
	return false;
    }

    if (!sendBuf) {
	PSC_flog("call initSerial() before %s@%d\n", caller, line);
	return false;
    }

    /* add data type */
    if (typeInfo) {
	uint8_t dType = type;
	if (!addData(buffer, &dType, sizeof(dType), caller, line)) return false;
    }

    /* add data length if required */
    if (hasLen) {
	uint32_t len = byteOrder ? htonl(size) : size;
	if (!addData(buffer, &len, sizeof(len), caller, line)) return false;
    }

    /* add data */
    if (byteOrder && !hasLen && type != PSDATA_MEM) {
	switch (size) {
	case 1:
	    if (!addData(buffer, val, size, caller, line)) return false;
	    break;
	case 2:
	{
	    uint16_t u16 = htons(*(uint16_t*)val);
	    if (!addData(buffer, &u16, sizeof(u16), caller, line)) return false;
	    break;
	}
	case 4:
	{
	    uint32_t u32 = htonl(*(uint32_t*)val);
	    if (!addData(buffer, &u32, sizeof(u32), caller, line)) return false;
	    break;
	}
	case 8:
	{
	    uint64_t u64 = HTON64(*(uint64_t*)val);
	    if (!addData(buffer, &u64, sizeof(u64), caller, line)) return false;
	    break;
	}
	default:
	    if (!addData(buffer, val, size, caller, line)) return false;
	    PSC_flog("unknown conversion for size %d at %s@%d\n",
		     size, caller, line);
	}
    } else {
	if (!addData(buffer, val, size, caller, line)) return false;
    }

    return true;
}

bool addArrayToBuf(const void *val, const uint32_t num, PS_SendDB_t *buffer,
		   PS_DataType_t type, size_t size,
		   const char *caller, const int line)
{
    if (!buffer) {
	PSC_flog("invalid buffer at %s@%d\n", caller, line);
	return false;
    }
    if (!val) {
	PSC_flog("invalid val at %s@%d\n", caller, line);
	return false;
    }

    if (!addToBuf(&num, sizeof(num), buffer, PSDATA_UINT32, caller, line))
	return false;

    const char *valPtr = val;
    for (uint32_t i = 0; i < num; i++) {
	addToBuf(valPtr + i*size, size, buffer, type, caller, line);
    }

    return true;
}

bool __addStringArrayToBuf(char **array, PS_SendDB_t *buffer,
			   const char *caller, const int line)
{
    if (!buffer) {
	PSC_flog("invalid buffer at %s@%d\n", caller, line);
	return false;
    }
    char *empty = NULL;
    if (!array) array = &empty;

    uint32_t num = 0;
    while (array[num]) num++;
    if (!addToBuf(&num, sizeof(num), buffer, PSDATA_UINT32, caller, line))
	return false;

    for (uint32_t i = 0; i < num; i++) {
	if (!addToBuf(array[i], PSP_strLen(array[i]), buffer, PSDATA_STRING,
		      caller, line)) return false;
    }

    return true;
}
