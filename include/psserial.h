/*
 * ParaStation
 *
 * Copyright (C) 2012-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
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

/** Growing data-buffer to assemble messages */
typedef struct {
    char *buf;           /**< Actual data-buffer */
    size_t size;         /**< Current size of @ref buf */
    size_t used;         /**< Used bytes of @ref buf */
    uint16_t nextFrag;   /**< Next fragment number to expect */
} PS_DataBuffer_t;

/** Prototype of custom sender functions used by @ref initSerial() */
typedef ssize_t Send_Msg_Func_t(void *);

/**
 * @brief Prototype for @ref __recvFragMsg()'s callback
 *
 * @param msg Message header (including the type) of the last
 * fragment. The buffer of this last fragment is invalidated and part
 * of @a data
 *
 * @param data Data buffer presenting payload assembled from all fragments
 */
typedef void PS_DataBuffer_func_t(DDTypedBufferMsg_t *msg,
				  PS_DataBuffer_t *data);

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
 * If the destination's protocol version is < 344, <extraSize> and
 * <extra> will be omitted for backward compatibility.
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
bool setFragDest(PS_SendDB_t *buffer, PStask_ID_t tid);

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
 * complete receive of the message, i.e. after the last fragment
 * arrived, the callback @a func will be called with @a msg as the
 * first parameter and the message buffer used to collect all
 * fragments as the second argument. If the @a verbose flag is false,
 * unexpected fragments will not be reported in the syslog.
 *
 * @param msg Message to handle
 *
 * @param func Callback function to be called upon message completion
 *
 * @param verbose Flag verbosity
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error
 */
bool __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		   bool verbose, const char *caller, const int line);

#define recvFragMsg(msg, func) __recvFragMsg(msg, func, true,		\
					     __func__, __LINE__)

#define tryRecvFragMsg(msg, func) __recvFragMsg(msg, func, false,	\
						__func__, __LINE__)

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
 * Set plugincomm's byte-order flag to @a flag. This flag steers the
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
 * Set plugincomm's type-info flag to @a flag. This flag steers the
 * inclusion of type information for each individual datum into the
 * transferred messages.
 *
 * The default mode is to not include type information.
 *
 * @param flag The type-info mode to be used
 *
 * @return The old setting of the type-info mode
 */
bool setTypeInfo(bool flag);

/**
 * @brief Free data buffer
 *
 * Free the data buffer @a data. For this the actual data buffer is
 * free()ed and all administrative information is reset.
 *
 * @param data Data buffer to be free()ed / reset
 *
 * @return No return value
 */
void freeDataBuffer(PS_DataBuffer_t *data);

/**
 * @brief Duplicate data buffer
 *
 * Duplicate the data buffer @a data and return a pointer to the copy
 * of this data buffer. For this not only the actual buffer but also
 * all administrative information is replicated.
 *
 * @param data Data buffer to be duplicated
 *
 * @return On success, a pointer to the copy of the data buffer is
 * returned. Or NULL in case of error.
 */
PS_DataBuffer_t *dupDataBuffer(PS_DataBuffer_t *data);

/**
 * @brief Write to data buffer
 *
 * Write data from @a mem to the @a buffer. The buffer is
 * growing in size as needed.
 *
 * @param mem Pointer holding the data to write
 *
 * @param len Number of bytes to write
 *
 * @param buffer The buffer to write to
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @param Returns true on success and false on error
 */
bool __memToDataBuffer(void *mem, size_t len, PS_DataBuffer_t *buffer,
		       const char *caller, const int line);

#define memToDataBuffer(mem, size, buffer) \
    __memToDataBuffer(mem, size, buffer, __func__, __LINE__)

/**
 * @brief Read data from buffer
 *
 * Read @a size bytes from a memory region addressed by @a ptr and
 * store it to @a val. Data is expected to be of type @a type. The
 * latter is double-checked if plugincomm's @ref typeInfo flag is
 * true. @a ptr is expected to provide sufficient data and @a val
 * expected to have enough space to store the data read.
 *
 * If reading is successful, @a ptr will be updated to point behind
 * the last data read, i.e. prepared to read the next data from it.
 *
 * If the global @ref byteOrder flag is true, byte order of the
 * received data will be adapted form network to host byte-order.
 *
 * @param ptr Data buffer to read from
 *
 * @param val Data buffer holding the result on return
 *
 * @param type Data type to be expected at @a ptr
 *
 * @param size Number of bytes to read
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error. If reading was not successful, @a ptr might be not updated.
 */
bool getFromBuf(char **ptr, void *val, PS_DataType_t type,
		size_t size, const char *caller, const int line);

#define getInt8(ptr, val) { int8_t *_x = val;			    \
        getFromBuf(ptr, _x, PSDATA_INT8, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getInt16(ptr, val) { int16_t *_x = val;			    \
        getFromBuf(ptr, _x, PSDATA_INT16, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getInt32(ptr, val) { int32_t *_x = val;			    \
	getFromBuf(ptr, _x, PSDATA_INT32, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getInt64(ptr, val) { int64_t *_x = val;			    \
	getFromBuf(ptr, _x, PSDATA_INT64, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getUint8(ptr, val) { uint8_t *_x = val;			    \
	getFromBuf(ptr, _x, PSDATA_UINT8, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getUint16(ptr, val) { uint16_t *_x = val;		    \
	getFromBuf(ptr, _x, PSDATA_UINT16, sizeof(*_x),	    \
		   __func__, __LINE__); }

#define getUint32(ptr, val) { uint32_t *_x = val;		    \
	getFromBuf(ptr, _x, PSDATA_UINT32, sizeof(*_x),	    \
		   __func__, __LINE__); }

#define getUint64(ptr, val) { uint64_t *_x = val;		    \
	getFromBuf(ptr, _x, PSDATA_UINT64, sizeof(*_x),	    \
		   __func__, __LINE__); }

#define getDouble(ptr, val) { double *_x = val;			    \
	getFromBuf(ptr, _x, PSDATA_DOUBLE, sizeof(*_x),	    \
		   __func__, __LINE__); }

#define getTime(ptr, val) { time_t *_x = val;			    \
	getFromBuf(ptr, _x, PSDATA_TIME, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getPid(ptr, val) { pid_t *_x = val;			    \
	getFromBuf(ptr, _x, PSDATA_PID, sizeof(*_x),		    \
		   __func__, __LINE__); }

#define getBool(ptr, val) { bool *_x = val;			    \
	getFromBuf(ptr, _x, PSDATA_UINT8, sizeof(uint8_t),	    \
		   __func__, __LINE__); }

#define getTaskId(ptr, val) getInt32(ptr, val)

#define getNodeId(ptr, val) getInt16(ptr, val)

#define getResId(ptr, val) getInt32(ptr, val)

/**
 * @brief Read data from buffer
 *
 * Read up to @a dataSize bytes from a memory region addressed by @a
 * ptr and store it to @a data. Data is expected to be of type @a
 * type. The latter is double-checked if plugincomm's @ref typeInfo
 * flag is true. The actual number of bytes read from @a ptr and
 * stored to @a data is provided in @a len.
 *
 * @a ptr is expected to provide data in the form of a leading item
 * describing the length of the actual data element followed by the
 * corresponding number of data items.
 *
 * If @a data is NULL and @a dataSize is 0, a buffer is dynamically
 * allocated. It will be big enough to hold all the data-items
 * announced in the length item within the memory region addressed by
 * @a ptr. A pointer to this buffer is returned. The caller has to
 * ensure that this buffer is released if it is no longer needed.
 *
 * If reading is successful, @a ptr will be updated to point behind
 * the last data read, i.e. prepared to read the next data from it.
 *
 * @param ptr Data buffer to read from
 *
 * @param data Buffer holding the result on return
 *
 * @param dataSize Size of the given buffer @a data
 *
 * @param len Number of bytes read from @a ptr into @a data
 *
 * @param type Data type to be expected at @a ptr
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success @a data or the newly allocated buffer is
 * returned; in case of error NULL is returned. If reading was not
 * successful, @a ptr might be not updated.
 */
void *getMemFromBuf(char **ptr, char *data, size_t dataSize, size_t *len,
		    PS_DataType_t type, const char *caller, const int line);

#define getStringM(ptr)							\
    getMemFromBuf(ptr, NULL, 0, NULL, PSDATA_STRING, __func__, __LINE__)

#define getStringML(ptr, len)						\
    getMemFromBuf(ptr, NULL, 0, len, PSDATA_STRING, __func__, __LINE__)

#define getDataM(ptr, len)						\
    getMemFromBuf(ptr, NULL, 0, len, PSDATA_DATA, __func__, __LINE__)

#define getString(ptr, buf, buflen)					\
    getMemFromBuf(ptr, buf, buflen, NULL, PSDATA_STRING, __func__, __LINE__)

/**
 * @brief Read data array from buffer
 *
 * Read elements of @a size bytes from a memory region addressed by @a
 * ptr and store them to a dynamically allocated array. The address of
 * the latter is returned via @a val. Data is expected to be of type
 * @a type. The latter is double-checked if plugincomm's @ref typeInfo
 * flag is true. The actual number of elements read from @a ptr and
 * stored to @a val is provided in @a len.
 *
 * @a ptr is expected to provide data in the form of a leading length
 * item followed by the corresponding number of data items.
 *
 * If reading is successful, @a ptr will be updated to point behind
 * the last data read, i.e. prepared to read the next data from it.
 *
 * @param ptr Data buffer to read from
 *
 * @param val Data buffer holding allocated array on return
 *
 * @param len Number of elements read from @a ptr into the data array
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
 * If reading was not successful, @a ptr might be not updated.
 */
bool getArrayFromBuf(char **ptr, void **val, uint32_t *len, PS_DataType_t type,
		     size_t size, const char *caller, const int line);

#define getUint16Array(ptr, val, len) { uint16_t **_x = val;		    \
	getArrayFromBuf(ptr, (void**)_x, len, PSDATA_UINT16, sizeof(**_x),  \
			__func__, __LINE__); }

#define getUint32Array(ptr, val, len) { uint32_t **_x = val;		    \
	getArrayFromBuf(ptr, (void **)_x, len, PSDATA_UINT32, sizeof(**_x), \
			__func__, __LINE__); }

#define getUint64Array(ptr, val, len) { uint64_t **_x = val;		    \
	getArrayFromBuf(ptr, (void **)_x, len, PSDATA_UINT64, sizeof(**_x), \
			__func__, __LINE__); }

#define getInt16Array(ptr, val, len) { int16_t **_x = val;		    \
	getArrayFromBuf(ptr, (void **)_x, len, PSDATA_INT16, sizeof(**_x),  \
			__func__, __LINE__); }

#define getInt32Array(ptr, val, len) { int32_t **_x = val;		    \
	getArrayFromBuf(ptr, (void **)_x, len, PSDATA_INT32, sizeof(**_x),  \
			__func__, __LINE__); }

#define getInt64Array(ptr, val, len) { int64_t **_x = val;		    \
	getArrayFromBuf(ptr, (void **)_x, len, PSDATA_INT64, sizeof(**_x),  \
			__func__, __LINE__); }


/**
 * @brief Read string array from buffer
 *
 * Read strings from a memory region addressed by @a ptr and store
 * them to a dynamically allocated array. The address of the latter is
 * returned via @a array. In order to store the strings read dynamic
 * memory is allocated for each string. The actual number of elements
 * read from @a ptr and stored to the array is provided in @a len.
 *
 * @a ptr is expected to provide data in the form of a leading length
 * item describing the number of strings followed by the corresponding
 * number of string items. Each string item consists of an individual
 * length item and the actual string.
 *
 * If reading is successful, @a ptr will be updated to point behind
 * the last data read, i.e. prepared to read the next data from it.
 *
 * If @a len is 0, upon return array will be untouched.
 *
 * @param ptr Data buffer to read from
 *
 * @param array Array of pointers addressing the actual strings received
 *
 * @param len Number of strings read from @a ptr
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error.
 * If reading was not successful, @a ptr might be not updated.
 */
bool __getStringArrayM(char **ptr, char ***array, uint32_t *len,
			const char *caller, const int line);

#define getStringArrayM(ptr, array, len)			\
    __getStringArrayM(ptr, array, len, __func__, __LINE__)


/**
 * @brief Add element to buffer
 *
 * Add an element of @a size bytes located at @a val to the data
 * buffer @a data. If the global flag @ref typeInfo is true, the
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
 * @param data Data buffer to save data to
 *
 * @param type Type of the element to add
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error.
 */
bool addToBuf(const void *val, const uint32_t size, PS_SendDB_t *data,
	      PS_DataType_t type, const char *caller, const int line);

#define addInt8ToMsg(val, data) { int8_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_INT8,		\
		 __func__, __LINE__); }

#define addInt16ToMsg(val, data) { int16_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_INT16,		\
		 __func__, __LINE__); }

#define addInt32ToMsg(val, data) { int32_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_INT32,		\
		 __func__, __LINE__); }

#define addInt64ToMsg(val, data) { int64_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_INT64,		\
		 __func__, __LINE__); }

#define addUint8ToMsg(val, data) { uint8_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_UINT8,		\
		 __func__, __LINE__); }

#define addUint16ToMsg(val, data) { uint16_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_UINT16,		\
		 __func__, __LINE__); }

#define addUint32ToMsg(val, data) { uint32_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_UINT32,		\
		 __func__, __LINE__); }

#define addUint64ToMsg(val, data) { uint64_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_UINT64,		\
		 __func__, __LINE__); }

#define addDoubleToMsg(val, data) { double _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_DOUBLE,		\
		 __func__, __LINE__); }

#define addBoolToMsg(val, data) { bool _x = val;		\
	addToBuf(&_x, sizeof(uint8_t), data, PSDATA_UINT8,	\
		 __func__, __LINE__); }

#define addTimeToMsg(val, data) { time_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_TIME,		\
		 __func__, __LINE__); }

#define addPidToMsg(val, data) { pid_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_PID,		\
		 __func__, __LINE__); }

#define addTaskIdToMsg(val, data) addInt32ToMsg(val, data)

#define addNodeIdToMsg(val, data) addInt16ToMsg(val, data)

#define addResIdToMsg(val, data) addInt32ToMsg(val, data)

#define addMemToMsg(mem, len, data)				\
    addToBuf(mem, len, data, PSDATA_MEM, __func__, __LINE__)

#define addDataToMsg(buf, len, data)				\
    addToBuf(buf, len, data, PSDATA_DATA, __func__, __LINE__)

#define addStringToMsg(string, data)				\
    addToBuf(string, PSP_strLen(string), data, PSDATA_STRING,	\
		   __func__, __LINE__)

/**
 * @brief Add array of elements to buffer
 *
 * Add an array of @a num individual elements of @a size bytes located
 * at @a val to the data buffer @a data. If the global flag @ref
 * typeInfo is true, each element will be annotated to be of type @a
 * type.
 *
 * The overall data will be annotated by a leading element describing
 * the number of elements provided via @a num.
 *
 * If the global @ref byteOrder flag is true, each element will be
 * shuffled into network byte-order.
 *
 * @param val Address of the elements to add
 *
 * @param num Number of elements to add
 *
 * @param data Data buffer to save data to
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
bool addArrayToBuf(const void *val, const uint32_t num, PS_SendDB_t *data,
		   PS_DataType_t type, size_t size,
		   const char *caller, const int line);

#define addUint16ArrayToMsg(val, num, data) { uint16_t *_x = val;	\
	addArrayToBuf(_x, num, data, PSDATA_UINT16, sizeof(*_x),	\
		      __func__, __LINE__); }

#define addUint32ArrayToMsg(val, num, data) { uint32_t *_x = val;	\
	addArrayToBuf(_x, num, data, PSDATA_UINT32, sizeof(*_x),	\
		      __func__, __LINE__); }

#define addUint64ArrayToMsg(val, num, data) { uint64_t *_x = val;  	\
	addArrayToBuf(_x, num, data, PSDATA_UINT64, sizeof(*_x),	\
		      __func__, __LINE__); }

#define addInt16ArrayToMsg(val, num, data) { int16_t *_x = val;		\
	addArrayToBuf(_x, num, data, PSDATA_INT16, sizeof(*_x),		\
		      __func__, __LINE__); }

#define addInt32ArrayToMsg(val, num, data) { int32_t *_x = val; 	\
	addArrayToBuf(_x, num, data, PSDATA_INT32, sizeof(*_x),		\
		      __func__, __LINE__); }

#define addInt64ArrayToMsg(val, num, data) { int64_t *_x = val;		\
	addArrayToBuf(_x, num, data, PSDATA_INT64, sizeof(*_x),		\
		      __func__, __LINE__); }

/**
 * @brief Add array of strings to buffer
 *
 * Add an array of strings stored in the NULL terminated @a array to
 * the data buffer @a data.
 *
 * If the global flag @ref typeInfo is true, each element will be
 * annotated to be of type PSDATA_STRING. Generally, each element will
 * be annotated by an additional length item. The overall data will be
 * annotated by a leading element describing the number of string
 * elements in the array.
 *
 * The data format is suitable for the array of strings to be read
 * from the buffer with @ref getStringArrayM().
 *
 * @param array Address of the array of strings to add
 *
 * @param data Data buffer to save data to
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an error.
*/
bool __addStringArrayToBuf(char **array, PS_SendDB_t *data,
			   const char *caller, const int line);

#define addStringArrayToMsg(array, data)			\
    __addStringArrayToBuf(array, data, __func__, __LINE__)

#endif  /* __PSSERIAL_H */
