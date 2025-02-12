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
/**
 * @file Serialization layer for messages potentially not fitting into
 * a single message of the ParaStation protocol (PSP). Data added to
 * such messages might either be stored in a PS_DataBuffer_t structure
 * ready to be send as a whole via a stream-connection or send via a
 * series of RDP messages as soon as a single message-buffer is
 * completely filled.
 *
 * The actual sending mode might be chosen by either setting
 * PS_SendDB_t's @ref useFrag flag to false (accumulate serialized
 * data in a buffer) or by initializing the PS_SendDB_t structure via
 * @ref initFragBuffer() or @ref initFragBufferExtra() and @ref
 * setFragDest() or @ref setFragDestUniq() (immediately send a series
 * of PSP messages).
 */
#ifndef __PSSERIAL_H
#define __PSSERIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "pssenddb_t.h"  // IWYU pragma: export
#include "psprotocol.h"
#include "pstaskid.h"

/** Data type information used to tag data in serialized messages */
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

/** Error types */
typedef enum {
    E_PSSERIAL_SUCCESS = 0, /**< operation successful */
    E_PSSERIAL_INSUF,	    /**< insufficient data */
    E_PSSERIAL_PARAM,	    /**< invalid parameter */
    E_PSSERIAL_TYPE,	    /**< data type mismatch */
    E_PSSERIAL_BUFSIZE,	    /**< provided buffer to small */
    E_PSSERIAL_MEM,	    /**< out of memory */
    E_PSSERIAL_CONV,	    /**< unknown conversion size */
} serial_Err_Types_t;

/** Growing data-buffer to assemble message from fragments and unpack content */
struct PS_DataBuffer {
    char *buf;           /**< Actual data-buffer */
    size_t size;         /**< Current size of @ref buf */
    size_t used;         /**< Used bytes of @ref buf */
    uint16_t nextFrag;   /**< Next fragment number to expect */
    char *unpackPtr;	 /**< Tracking top of unpacked bytes in @ref buf */
    int8_t unpackErr;	 /**< Error code if unpacking of content failed */
};

/** Growing data-buffer context to be created via @ref PSdbNew() */
typedef struct PS_DataBuffer * PS_DataBuffer_t;

/** Prototype of custom sender functions used by @ref initSerial() */
typedef ssize_t Send_Msg_Func_t(void *);

/**
 * @brief Get new initialized data-buffer
 *
 * Create a new data-buffer and set its internal buffer according to
 * @a buffer. @a bufSize must denote the actual amount of data within
 * @a buffer unless @a buffer is NULL.
 *
 * This function aims to setup a data-buffer on the fly that might be
 * used to unpack the content of @a buffer via @ref getFromBuf() and
 * friends.
 *
 * Data-buffers created by this function must be cleaned up via @ref
 * PSdbDelete() in order to not leak the data-buffer's memory.
 *
 * @param buffer Actual content used for the data-buffer's buffer
 *
 * @param bufSize Size of @a buffer
 *
 * @return On success a properly initialized data-buffer is returned
 * or NULL in case of error
 */
PS_DataBuffer_t PSdbNew(char *buffer, size_t bufSize);

/**
 * @brief Delete data-buffer
 *
 * Delete the data-buffer @a data and free() all memory allocated for
 * administrative data. However, the data-buffer's buffer will not be
 * touched!
 *
 * The rational behind this design is the fact that the data-buffer is
 * expected to be constructed via @ref PSdbNew(). Thus, the buffer
 * will be handled outside and might point to an addresss not at an
 * allocated boundary or even to memory outside of the dynamic
 * handling and e.g. is located on the stack.
 *
 * @param data Data-buffer to be deleted
 *
 * @return No return value
 */
void PSdbDelete(PS_DataBuffer_t data);

/**
 * @brief Clear data-buffer
 *
 * Clear the data-buffer @a data. For this the actual buffer is
 * free()ed and all administrative information is reset. The
 * data-buffer itself remains usable and might be filled with new
 * buffer data.
 *
 * @param data Data-buffer to be free()ed / reset
 *
 * @return No return value
 */
void PSdbClear(PS_DataBuffer_t data);

/**
 * @brief Duplicate data-buffer
 *
 * Duplicate the data-buffer @a data and return a pointer to the copy
 * of this data-buffer. For this, besides the actual buffer also all
 * administrative information is replicated. The only exception is the
 * data-buffer's @ref unpackPtr member which is reset in the duplicate
 * buffer such that reading will restart from the very beginning of
 * the received data.
 *
 * @param data Data-buffer to be duplicated
 *
 * @return On success, a pointer to the copy of the data-buffer is
 * returned; or NULL in case of error
 */
PS_DataBuffer_t PSdbDup(PS_DataBuffer_t data);

/**
 * @brief Rewind data-buffer
 *
 * Rewind the data-buffer @a data, i.e. start reading from the very
 * beginning again. Thus, future calls to @ref getFromBuf() and
 * friends will provide data from the start of the data-buffer again.
 *
 * @param data Data-buffer to rewind
 *
 * @return No return value
 */
void PSdbRewind(PS_DataBuffer_t data);

/**
 * @brief Get current size of data-buffer
 *
 * Get the current size of the data-buffer @a data, i.e. the allocated
 * space for the buffer itself. The buffer might not be fully
 * filled. The actual amount of data in the buffer might be determined
 * via @ref PSdbGetUsed().
 *
 * @param data Data-buffer to investigate
 *
 * @return Return the allocated size of the data-buffer's buffer
 */
size_t PSdbGetSize(PS_DataBuffer_t data);

/**
 * @brief Get total amount of data in data-buffer
 *
 * Get the total amount of data in the data-buffer @a data, i.e. the
 * data actually present in the buffer itself. Not all data might be
 * available any more, since it was already read.
 *
 * The amount of data still available for reading can be determined
 * via @ref PSdbGetAvail(). All data can be made available again by
 * calling PSdbRewind().
 *
 * @param data Data-buffer to investigate
 *
 * @return Return the total amount of data in data-buffer
 */
size_t PSdbGetUsed(PS_DataBuffer_t data);

/**
 * @brief Get remaining amount of data in data-buffer
 *
 * Get the remaining amount of data in the data-buffer @a data,
 * i.e. the data actually available for reading. There might be more
 * data in @a data that was already read. This data can be made
 * available again by calling PSdbRewind().
 *
 * @param data Data-buffer to investigate
 *
 * @return Return the remaining amount of data in data-buffer still
 * available for reading
 */
size_t PSdbGetAvail(PS_DataBuffer_t data);

/**
 * @brief Get data-buffer's error state
 *
 * Get the error state of the data-buffer @a data. The error state
 * indicates if all calls to @ref getFromBuf() and friends were
 * successful. In case at least one call failed, the result will
 * indicate the failure of the first unsuccessful call.
 *
 * @param data Data-buffer to investigate
 *
 * @retrun Return the error state of the data-buffer @a data; if @a
 * data is NULL, E_PSSERIAL_PARAM will be returned
 */
serial_Err_Types_t PSdbGetErrState(PS_DataBuffer_t data);

/**
 * @brief Prototype for @ref __recvFragMsg()'s callback
 *
 * This callback will be used if called via the @ref recvFragMsg() or
 * @ref tryRecvFragMsg() define.
 *
 * @param msg Message header (including the type) of the last
 * fragment; @warning the buffer of this last fragment is invalidated
 * and part of @a data
 *
 * @param data Data-buffer presenting payload assembled from all fragments
 */
typedef void SerialRecvCB_t(DDTypedBufferMsg_t *msg, PS_DataBuffer_t data);

/**
 * @brief Prototype for @ref __recvFragMsg()'s callback
 *
 * This callback will be used if called via the @ref recvFragMsgInfo()
 * define.
 *
 * It is pretty similar to the one used via @ref recvFragMsg()
 * (i.e. SerialRecvCB_t) besides the fact that a pointer to additional
 * information might be passed to this callback in @ref
 * recvFragMsgInfo().
 *
 * @param msg Message header (including the type) of the last
 * fragment; @warning the buffer of this last fragment is invalidated
 * and part of @a data
 *
 * @param data Data-buffer presenting payload assembled from all fragments
 *
 * @param info Pointer to additional information passed from the last
 * argument of recvFragMsgInfo()
 */
typedef void SerialRecvInfoCB_t(DDTypedBufferMsg_t *msg,
				PS_DataBuffer_t data, void *info);

/**
 * @brief Initialize buffer handling of the Psserial facility
 *
 * Initialize only the buffer handling of the Psserial facility
 * and enable it to use a default buffer-size of @a bufSize for the
 * send-buffer. This function shall be called before using any buffer
 * handling of Psserial.
 *
 * @param bufSize Size of the internal buffers. If this is 0, the
 * default size of 256 kB is used.
 *
 * @return If the buffer handling of Psserial is successfully
 * initialized, true is returned. Otherwise false is returned.
 */
bool initSerialBuf(size_t bufSize);

/**
 * @brief Initialize Psserial facility
 *
 * Initialize the Psserial facility and enable it to use a default
 * buffer-size of @a bufSize for send-buffers and @a func as the
 * sending function. Initialization includes allocating memory. This
 * function shall be called upon start of a program.
 *
 * A good choice for @a func is the ParaStation daemon's @ref
 * sendMsg() function.
 *
 * @param bufSize Size of the internal buffers. If this is 0, the
 * default size of 256 kB is used.
 *
 * @param func Sending function to use
 *
 * @return If Psserial is successfully initialized, true is
 * returned. Otherwise false is returned.
 */
bool initSerial(size_t bufSize, Send_Msg_Func_t *func);

/**
 * @brief Finalize Psserial facility
 *
 * Finalize the Psserial facility. This includes releasing all buffers
 * if this was the last user of the facility.
 *
 * @return No return value
 */
void finalizeSerial(void);

/** Fragment types */
typedef enum {
    FRAGMENT_PART = 0x01,   /* one fragment, more to follow */
    FRAGMENT_END  = 0x02,   /* the end of a fragmented message */
} FragType_t;

/**
 * @brief Initialize a fragmented message buffer
 *
 * Initialize the fragmented message buffer @a buffer. Each message
 * fragment will be sent in a DDTypedBufferMsg_t message with type @a
 * headType in the header and the sub-type @a msgType in the body. If
 * @a extra is different from NULL, data of size @a extraSize is
 * added to each fragment, too. Since the maximum value of @a extraSize
 * is 255, this limits to possible size of @a extra by intention.
 *
 * Since @a extra will be used until all fragments are sent, the
 * caller has to ensure that the memory @a extra is pointing to will
 * be available throughout this whole period.
 *
 * The structure of the messages to be sent will be as follows:
 *     <header>
 *   type element:
 *     <msgType (4 byte)>
 *   buf part:
 *     <fragType (FRAGMENT_PART|FRAGMENT_END) (1 byte)>
 *     <fragNum (2 byte)>
 *     <extraSize (1 byte)>
 *     <extra (extraSize bytes)>
 *     <payload fragment>
 *
 * @param buffer Buffer to handle
 *
 * @param headType Type of the messages used to send the fragments
 *
 * @param msgType Sub-type of the messages to send
 *
 * @param extra Further data to be added to each fragment
 *
 * @param extraSize Size of extra info to added to each fragment
 *
 * @return No return value
 */
void initFragBufferExtra(PS_SendDB_t *buffer, int16_t headType, int32_t msgType,
			 void *extra, uint8_t extraSize);

/** Backward compatibility, no extra data to be added */
#define initFragBuffer(buffer, headType, msgType)	\
    initFragBufferExtra(buffer, headType, msgType, NULL, 0)

/**
 * @brief Set an additional destination for fragmented messages
 *
 * Add the provided Task ID as an additional destination to send the
 * fragmented message to. This functions needs to be called before
 * using any functions to add data to the buffer. A good place is
 * right after the call to @ref initFragBuffer() or @ref
 * initFragBufferExtra().
 *
 * @param buffer The send buffer to use
 *
 * @param tid Destination task ID to add
 *
 * @return Returns true if the destination was added or false on error
 */
bool _setFragDest(PS_SendDB_t *buffer, PStask_ID_t tid, const char *func, const int line);

#define setFragDest(buf, tid) _setFragDest(buf, tid, __func__, __LINE__)

/**
 * @brief Set an additional unique destination for fragmented messages
 *
 * Add the provided Task ID as an additional destination to send the
 * fragmented message to. This functions needs to be called before
 * using any functions to add data to the buffer. A good place is
 * right after the call to @ref initFragBuffer() or @ref
 * initFragBufferExtra().
 *
 * In contrast to @ref setFragDest() this function ensures that the
 * destination is unique, i.e. that no multiple occurrences of the
 * same destination task ID appears in the list of destinations.
 *
 * @param buffer Send buffer to use
 *
 * @param tid Destination task ID to add
 *
 * @return Returns true if the destination was added or false on error
 * or if the destination was found amongst the already registered
 * destinations
 */
bool setFragDestUniq(PS_SendDB_t *buffer, PStask_ID_t tid);

/**
 * @brief Get number of destinations
 *
 * Get the number of destinations registered to @a buffer via @ref
 * setFragDest() or @ref setFragDestUniq().
 *
 * @param buffer Buffer to investigate
 *
 * @return Number of destinations registered to @a buffer or -1 if @a
 * buffer is invalid
 */
static inline int32_t getNumFragDest(PS_SendDB_t *buffer)
{
    if (!buffer) return -1;

    return buffer->numDest;
}

/**
 * @brief Fetch header information from fragment
 *
 * Fetch header information from the fragment contained in the message
 * @a msg. The header data is fetched with an offset given by @a used
 * according to the functionality in @ref PSP_getMsgBuf() et al. Thus,
 * @a *used shall be set to 0 before calling this function. This
 * function updates @a used such that it will point to the start of
 * the fragment's payload.
 *
 * Upon return @a fragType, @a fragNum, @a extra and @a extraSize are
 * updated accordingly in order to represent the fragment's type,
 * serial number, pointer to the extra header or the size of the extra
 * header, respectively. If any of @a fragType, @a fragNum, @a extra
 * or @a extraSize is NULL, the corresponding information will not be
 * provided.
 *
 * @param msg Message to fetch information from
 *
 * @param used Counter used to track the data offset as in @ref
 * PSP_getMsgBuf() et al.
 *
 * @param fragType Fragment's type (FRAGMENT_PART or FRAGMENT_END)
 *
 * @param fragNum Fragment's serial number
 *
 * @param extra Reference to fragment's extra header (if any)
 *
 * @param extraSize Size of fragment's extra header (if any)
 *
 * @return If information was fetched, return true; or false in case
 * of an error
 */
bool fetchFragHeader(DDTypedBufferMsg_t *msg, size_t *used, uint8_t *fragType,
		     uint16_t *fragNum, void **extra, size_t *extraSize);

/**
 * @brief Receive fragmented message
 *
 * Add the fragment contained in the message @a msg to the overall
 * message to receive stored in a separate message buffer. Upon
 * completion, i.e. after the last fragment arrived, the callback @a cb
 * will be called if given. As first parameter @a msg is passed and
 * the message buffer used to collect all fragments as the second
 * argument. If @a cb is NULL and @a infoCB is given, this will be
 * called with @a msg as the first parameter, the message buffer used
 * to collect all fragments as the second argument, and @a info as the
 * last argument.
 *
 * If the @a verbose flag is false, unexpected fragments will not be
 * reported in the syslog.
 *
 * @param msg Message to handle
 *
 * @param cb Callback function to be called upon message completion
 *
 * @param infoCB Optional callback to be called upon message
 * completion if @a cb is NULL
 *
 * @param info Pointer to additional information to be passed ot @a infoCB
 *
 * @param verbose Flag verbosity
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error
 */
bool __recvFragMsg(DDTypedBufferMsg_t *msg, SerialRecvCB_t *cb,
		   SerialRecvInfoCB_t *infoCB, void *info, bool verbose,
		   const char *caller, const int line);

#define recvFragMsg(msg, cb)						\
    __recvFragMsg(msg, cb, NULL, NULL, true, __func__, __LINE__)

#define tryRecvFragMsg(msg, cb)						\
    __recvFragMsg(msg, cb, NULL, NULL, false, __func__, __LINE__)

#define recvFragMsgInfo(msg, cb, info)					\
    __recvFragMsg(msg, NULL, cb, info, true, __func__, __LINE__)

/**
 * @brief Send fragmented message
 *
 * Send the message content found in the message buffer @a buffer to
 * the task ID(s) registered before using @ref setFragDest() or @ref
 * setFragDestUniq() as a series of fragments put into ParaStation
 * protocol messages of type @ref DDTypedBufferMsg_t. Each message is
 * of the ParaStation protocol type and the sub-type defined
 * previously by @ref initFragBuffer() or @ref initFragBufferExtra().
 *
 * The sender function which was registered before via @ref
 * initSerial() method is used to send the fragments.
 *
 * Each fragment holds its own meta-data used to put together the
 * overall message on the receiving side as required by @ref
 * __recvFragMsg()
 *
 * @param buffer Buffer to send
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return The total number of payload bytes sent is returned. Or -1
 * in case of sending one fragment failed. In the latter case the
 * amount of data received on the other side is undefined.
 */
int __sendFragMsg(PS_SendDB_t *buffer, const char *caller, const int line);

#define sendFragMsg(buffer) __sendFragMsg(buffer, __func__, __LINE__)

/**
 * @brief Set byte-order flag
 *
 * Set the global byte-order flag to @a flag. This flag steers the
 * modules byte-order awareness, i.e. if data is transferred in
 * network byte-order.
 *
 * The default mode is to transfer data in network byte-order.
 *
 * @param flag The byte-order mode to be used
 *
 * @return The old setting of the byte-order mode
 */
bool setByteOrder(bool flag);

/**
 * @brief Set type-info flag
 *
 * Set the type-info flag to @a flag. This flag steers the inclusion
 * of type information for each individual datum into the transferred
 * messages.
 *
 * The default mode is to not include type information.
 *
 * @param flag The type-info mode to be used
 *
 * @return The old setting of the type-info mode
 */
bool setTypeInfo(bool flag);

/**
 * @brief Get string representation of an error code
 *
 * @param err The error code to convert
 *
 * @return Returns the requested error messages.
 */
char *serialStrErr(serial_Err_Types_t err);

/**
 * @brief Write to data-buffer
 *
 * Write data from @a mem to the data-buffer @a buffer. The latter is
 * growing in size as needed.
 *
 * @param mem Pointer holding the data to write
 *
 * @param len Number of bytes to write
 *
 * @param buffer Data-buffer to write to
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @param Returns true on success and false on error
 */
bool __memToDataBuffer(void *mem, size_t len, PS_DataBuffer_t buffer,
		       const char *caller, const int line);

#define memToDataBuffer(mem, size, buffer) \
    __memToDataBuffer(mem, size, buffer, __func__, __LINE__)

/**
 * @brief Fetch data from buffer
 *
 * Fetch @a size bytes from a memory region addressed by @a data and
 * store it to @a val. The fetched data is expected to be of type @a
 * type. The latter is double-checked if the type-info flag is
 * set. @a data is expected to provide sufficient data and @a val
 * expected to have enough space to store the data read.
 *
 * If fetching was successful, the unpackPtr member of @a data will be
 * updated to point behind the last data read, i.e. prepared to fetch
 * the next data. Otherwise an error code is saved to the unpackErr
 * member of @a data.
 *
 * If the global @ref byteOrder flag is true, byte order of the
 * received data will be adapted form network to host byte-order.
 *
 * @param data Data-buffer to read from
 *
 * @param val Data buffer holding the result on return
 *
 * @param type Data type to be expected at @a data
 *
 * @param size Number of bytes to read
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If fetching was not successful, the unpackPtr member of @a
 * data will not be updated; instead its unpackErr member will be
 * set.
 */
bool getFromBuf(PS_DataBuffer_t data, void *val, PS_DataType_t type,
		size_t size, const char *caller, const int line);

#define getInt8(data, val) { int8_t *_x = val;			    \
	getFromBuf(data, _x, PSDATA_INT8, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getInt16(data, val) { int16_t *_x = val;		    \
	getFromBuf(data, _x, PSDATA_INT16, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getInt32(data, val) { int32_t *_x = val;		    \
	getFromBuf(data, _x, PSDATA_INT32, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getInt64(data, val) { int64_t *_x = val;		    \
	getFromBuf(data, _x, PSDATA_INT64, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getUint8(data, val) { uint8_t *_x = val;		    \
	getFromBuf(data, _x, PSDATA_UINT8, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getUint16(data, val) { uint16_t *_x = val;		    \
	getFromBuf(data, _x, PSDATA_UINT16, sizeof(*_x),	    \
		   __func__, __LINE__); }

#define getUint32(data, val) { uint32_t *_x = val;		    \
	getFromBuf(data, _x, PSDATA_UINT32, sizeof(*_x),	    \
		   __func__, __LINE__); }

#define getUint64(data, val) { uint64_t *_x = val;		    \
	getFromBuf(data, _x, PSDATA_UINT64, sizeof(*_x),	    \
		   __func__, __LINE__); }

#define getDouble(data, val) { double *_x = val;		    \
	getFromBuf(data, _x, PSDATA_DOUBLE, sizeof(*_x),	    \
		   __func__, __LINE__); }

#define getTime(data, val) { time_t *_x = val;			    \
	getFromBuf(data, _x, PSDATA_TIME, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getPid(data, val) { pid_t *_x = val;			    \
	getFromBuf(data, _x, PSDATA_PID, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getBool(data, val) { bool *_y = val; uint8_t _x;	    \
	getFromBuf(data, &_x, PSDATA_UINT8, sizeof(uint8_t),	    \
		   __func__, __LINE__);	*_y = _x; }

#define getTaskId(data, val) getInt64(data, val)

#define getNodeId(data, val) getInt32(data, val)

#define getResId(data, val)  getInt32(data, val)

#define getMem(data, buf, buflen)					\
    getFromBuf(data, buf, PSDATA_MEM, buflen, __func__, __LINE__)

/**
 * @brief Fetch data from buffer
 *
 * Fetch up to @a destSize bytes from a memory region addressed by @a
 * data and store it to @a dest. The fetched data is expected to be of
 * type @a type. The latter is double-checked if the type-info flag is
 * set. The actual number of bytes read from @a data and stored to @a
 * dest is provided in @a len.
 *
 * @a data is expected to provide data in the form of a leading item
 * describing the length of the actual data element followed by the
 * corresponding number of data items.
 *
 * If @a dest is NULL and @a destSize is 0, a buffer is dynamically
 * allocated. It will be large enough to hold all the data-items
 * announced in the length item within the memory region addressed by
 * @a data. After fetching a pointer to this buffer is returned. The
 * caller has to ensure that this buffer is released if it is no
 * longer needed.
 *
 * If fetching was successful, the unpackPtr member of @a data will be
 * updated to point behind the last data read, i.e. prepared to read
 * the next data. Otherwise an error code is saved in the unpackErr
 * member of @a data.
 *
 * If the global @ref byteOrder flag is true, byte order of the
 * received data will be adapted form network to host byte-order.
 *
 * @param data Data-buffer to read from
 *
 * @param dest Buffer holding the result on return
 *
 * @param destSize Size of the given buffer @a dest
 *
 * @param len Number of bytes read from @a data into @a dest
 *
 * @param type Data type to be expected at @a data
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success @a dest or the newly allocated buffer is
 * returned; in case of error NULL is returned. If reading was not
 * successful, the unpackPtr member of @a data might not be updated;
 * instead its unpackErr member will be set.
 */
void *getMemFromBuf(PS_DataBuffer_t data, char *dest, size_t destSize,
		    size_t *len, PS_DataType_t type, const char *caller,
		    const int line);

#define getStringM(data)						\
    getMemFromBuf(data, NULL, 0, NULL, PSDATA_STRING, __func__, __LINE__)

#define getStringML(data, len)						\
    getMemFromBuf(data, NULL, 0, len, PSDATA_STRING, __func__, __LINE__)

#define getDataM(data, len)						\
    getMemFromBuf(data, NULL, 0, len, PSDATA_DATA, __func__, __LINE__)

#define getString(data, buf, buflen)					\
    getMemFromBuf(data, buf, buflen, NULL, PSDATA_STRING, __func__, __LINE__)

#define getStringL(data, buf, buflen, len)				\
    getMemFromBuf(data, buf, buflen, len, PSDATA_STRING, __func__, __LINE__)

/**
 * @brief Fetch data array from buffer
 *
 * Fetch elements of @a size bytes from a memory region addressed by
 * @a data and store them to a dynamically allocated array. The
 * address of the latter is returned via @a val. The fetched data is
 * expected to be of type @a type. The latter is double-checked if the
 * global type-info flag is set. The actual number of elements read
 * from @a data and stored to @a val is provided in @a len. In the case
 * that @a len is NULL, no such information is provided.
 *
 * @a data is expected to provide data in the form of a leading length
 * item followed by the corresponding number of data items.
 *
 * If fetching was successful, the unpackPtr member of @a data will be
 * updated to point behind the last data read, i.e. prepared to read
 * the next data. Otherwise an error code is saved in the unpackErr
 * member of @a data.
 *
 * If the global @ref byteOrder flag is true, byte order of the
 * received data will be adapted form network to host byte-order.
 *
 * @param data Data-buffer to read from
 *
 * @param val Buffer holding the allocated data array on return
 *
 * @param len Number of elements read from @a data into the data array;
 *	      might be NULL
 *
 * @param type Data type to be expected for the elements to read
 *
 * @param size Number of bytes of each data element
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error.
 * If reading was not successful, the unpackPtr member of @a data
 * might not be updated; instead its unpackErr member will be set.
 */
bool getArrayFromBuf(PS_DataBuffer_t data, void **val, uint32_t *len,
		     PS_DataType_t type, size_t size, const char *caller,
		     const int line);

#define getUint16Array(data, val, len) { uint16_t **_x = val;		     \
	getArrayFromBuf(data, (void**)_x, len, PSDATA_UINT16, sizeof(**_x),  \
			__func__, __LINE__); }

#define getUint32Array(data, val, len) { uint32_t **_x = val;		     \
	getArrayFromBuf(data, (void **)_x, len, PSDATA_UINT32, sizeof(**_x), \
			__func__, __LINE__); }

#define getUint64Array(data, val, len) { uint64_t **_x = val;		     \
	getArrayFromBuf(data, (void **)_x, len, PSDATA_UINT64, sizeof(**_x), \
			__func__, __LINE__); }

#define getInt16Array(data, val, len) { int16_t **_x = val;		     \
	getArrayFromBuf(data, (void **)_x, len, PSDATA_INT16, sizeof(**_x),  \
			__func__, __LINE__); }

#define getInt32Array(data, val, len) { int32_t **_x = val;		     \
	getArrayFromBuf(data, (void **)_x, len, PSDATA_INT32, sizeof(**_x),  \
			__func__, __LINE__); }

#define getInt64Array(data, val, len) { int64_t **_x = val;		     \
	getArrayFromBuf(data, (void **)_x, len, PSDATA_INT64, sizeof(**_x),  \
			__func__, __LINE__); }

/**
 * @brief Fetch string array from buffer
 *
 * Fetch strings from a memory region addressed by @a data and store
 * them to a dynamically allocated, NULL terminated array of
 * strings. The address of the latter is returned via @a array. In
 * order to store the fetched strings dynamic memory is allocated for
 * each string. The actual number of elements read from @a data and
 * stored to the array is provided in @a len. In the case that @a len
 * is NULL, no such information is provided. Nevertheless, it might be
 * reconstructed utilizing the fact that @a array is NULL terminated.
 *
 * @a data is expected to provide data in the form of a leading length
 * item describing the number of strings followed by the corresponding
 * number of string items. Each string item consists of an individual
 * length item and the actual string.
 *
 * If fetching was successful, the unpackPtr member of @a data will be
 * updated to point behind the last data read, i.e. prepared to read
 * the next data. Otherwise an error code is saved in the unpackErr
 * member of @a data.
 *
 * If @a len is 0 upon return, array will be untouched.
 *
 * @param data Data-buffer to read from
 *
 * @param array Array of pointers addressing the actual strings received
 *
 * @param len Number of strings read from @a data; might be NULL
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error.
 * If reading was not successful, the unpackPtr member of @a data
 * might not be updated; instead its unpackErr member will be set.
 */
bool __getStringArrayM(PS_DataBuffer_t data, char ***array, uint32_t *len,
			const char *caller, const int line);

#define getStringArrayM(data, array, len)			\
    __getStringArrayM(data, array, len, __func__, __LINE__)

#define getArgV(data, argV) { char **argvP = NULL;		\
    __getStringArrayM(data, &argvP, NULL, __func__, __LINE__);	\
    argV = strvNew(argvP); };

#define getEnv(data, env) { char **envP = NULL;			\
    __getStringArrayM(data, &envP, NULL, __func__, __LINE__);   \
    env = envNew(envP); };

/**
 * @brief Add element to buffer
 *
 * Add an element of @a size bytes located at @a val to the data
 * buffer @a buffer. If the global type-info flag is set, the
 * element will be annotated to be of type @a type.
 *
 * If the data is of type PSDATA_STRING or PSDATA_DATA, it will be
 * annotated by an additional length item.
 *
 * If the global @ref byteOrder flag is true, the data will be
 * shuffled into network byte-order unless it is of type
 * PSDATA_STRING, PSDATA_DATA or PSDATA_MEM.
 *
 *
 * @param val Address of the element to add
 *
 * @param size Number of bytes of the element to add
 *
 * @param buffer Data buffer to save element to
 *
 * @param type Type of the element to add
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error.
 */
bool addToBuf(const void *val, const uint32_t size, PS_SendDB_t *buffer,
	      PS_DataType_t type, const char *caller, const int line);

#define addInt8ToMsg(val, msg) { int8_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_INT8,		\
		 __func__, __LINE__); }

#define addInt16ToMsg(val, msg) { int16_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_INT16,		\
		 __func__, __LINE__); }

#define addInt32ToMsg(val, msg) { int32_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_INT32,		\
		 __func__, __LINE__); }

#define addInt64ToMsg(val, msg) { int64_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_INT64,		\
		 __func__, __LINE__); }

#define addUint8ToMsg(val, msg) { uint8_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_UINT8,		\
		 __func__, __LINE__); }

#define addUint16ToMsg(val, msg) { uint16_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_UINT16,		\
		 __func__, __LINE__); }

#define addUint32ToMsg(val, msg) { uint32_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_UINT32,		\
		 __func__, __LINE__); }

#define addUint64ToMsg(val, msg) { uint64_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_UINT64,		\
		 __func__, __LINE__); }

#define addDoubleToMsg(val, msg) { double _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_DOUBLE,		\
		 __func__, __LINE__); }

#define addBoolToMsg(val, msg) { uint8_t _x = val;		\
	addToBuf(&_x, sizeof(uint8_t), msg, PSDATA_UINT8,	\
		 __func__, __LINE__); }

#define addTimeToMsg(val, msg) { time_t _x = val;		\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_TIME,		\
		 __func__, __LINE__); }

#define addPidToMsg(val, msg) { pid_t _x = val;			\
	addToBuf(&_x, sizeof(_x), msg, PSDATA_PID,		\
		 __func__, __LINE__); }

#define addTaskIdToMsg(val, msg) addInt64ToMsg(val, msg)

#define addNodeIdToMsg(val, msg) addInt32ToMsg(val, msg)

#define addResIdToMsg(val, msg) addInt32ToMsg(val, msg)

#define addMemToMsg(mem, len, msg)				\
    addToBuf(mem, len, msg, PSDATA_MEM, __func__, __LINE__)

#define addDataToMsg(buf, len, msg)				\
    addToBuf(buf, len, msg, PSDATA_DATA, __func__, __LINE__)

#define addStringToMsg(string, msg)				\
    addToBuf(string, PSP_strLen(string), msg, PSDATA_STRING,	\
		   __func__, __LINE__)

/**
 * @brief Add array of elements to buffer
 *
 * Add an array of @a num individual elements of @a size bytes located
 * at @a val to the data buffer @a buffer. If the global type-info flag
 * is set, each element will be annotated to be of type @a type.
 *
 * The overall data will be annotated by a leading element holding the
 * number of elements as provided via @a num.
 *
 * If the global @ref byteOrder flag is true, each element will be
 * shuffled into network byte-order.
 *
 * @param val Address of the elements to add
 *
 * @param num Number of elements to add
 *
 * @param buffer Data buffer to save the array to
 *
 * @param type Type of the elements to add
 *
 * @param size Number of bytes of the individual elements to add
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error.
 */
bool addArrayToBuf(const void *val, const uint32_t num, PS_SendDB_t *buffer,
		   PS_DataType_t type, size_t size,
		   const char *caller, const int line);

#define addUint16ArrayToMsg(val, num, msg) { uint16_t *_x = val;	\
	addArrayToBuf(_x, num, msg, PSDATA_UINT16, sizeof(*_x),		\
		      __func__, __LINE__); }

#define addUint32ArrayToMsg(val, num, msg) { uint32_t *_x = val;	\
	addArrayToBuf(_x, num, msg, PSDATA_UINT32, sizeof(*_x),		\
		      __func__, __LINE__); }

#define addUint64ArrayToMsg(val, num, msg) { uint64_t *_x = val;	\
	addArrayToBuf(_x, num, msg, PSDATA_UINT64, sizeof(*_x),		\
		      __func__, __LINE__); }

#define addInt16ArrayToMsg(val, num, msg) { int16_t *_x = val;		\
	addArrayToBuf(_x, num, msg, PSDATA_INT16, sizeof(*_x),		\
		      __func__, __LINE__); }

#define addInt32ArrayToMsg(val, num, msg) { int32_t *_x = val;		\
	addArrayToBuf(_x, num, msg, PSDATA_INT32, sizeof(*_x),		\
		      __func__, __LINE__); }

#define addInt64ArrayToMsg(val, num, msg) { int64_t *_x = val;		\
	addArrayToBuf(_x, num, msg, PSDATA_INT64, sizeof(*_x),		\
		      __func__, __LINE__); }

/**
 * @brief Add array of strings to buffer
 *
 * Add an array of strings stored in the NULL terminated @a array to
 * the data buffer @a buffer.
 *
 * If the global type-info flag is set, each element will be annotated
 * to be of type PSDATA_STRING. Generally, each element will be
 * annotated by an additional length item. The overall data will be
 * annotated by a leading element describing the number of string
 * elements in the array.
 *
 * The data format is suitable for the array of strings to be read
 * from the buffer with @ref getStringArrayM().
 *
 * If @a array is NULL, the behavior is identical to the case where an
 * empty array, i.e. an array consisting just of the terminating NULL
 * element would be added.
 *
 * @param array Address of the array of strings to add
 *
 * @param buffer Data buffer to save the string array to
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error.
*/
bool __addStringArrayToBuf(char **array, PS_SendDB_t *buffer,
			   const char *caller, const int line);

#define addStringArrayToMsg(array, msg)			\
    __addStringArrayToBuf(array, msg, __func__, __LINE__)

#endif  /* __PSSERIAL_H */
