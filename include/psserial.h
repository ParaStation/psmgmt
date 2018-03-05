/*
 * ParaStation
 *
 * Copyright (C) 2012-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PLUGIN_LIB_COMM
#define __PLUGIN_LIB_COMM

#include <stdbool.h>
#include <stdint.h>

#include "pscommon.h"
#include "psprotocol.h"
#include "pstaskid.h"

/** Data type information used to tag data in messages */
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
    uint32_t bufSize;    /**< Current size of @ref buf */
    uint32_t bufUsed;    /**< Used bytes of @ref buf */
} PS_DataBuffer_t;

/** Receive header providing meta-information on a single fragment */
typedef struct {
    uint8_t fragType;	/**< Type of this fragment */
    uint16_t fragNum;   /**< Sequence number of this fragment*/
} PS_Frag_Msg_Header_t;

/**
 * Send data-buffer for fragmented and regular messages
 *
 * In order to set things up for messages not using the fragmentation
 * mechanism @a useFrag has to be set to false and @a bufUsed to 0.
 *
 * If fragmentation shall be used the corresponding structure has to
 * be initializied using @ref initFragBuffer().
 */
typedef struct {
    int32_t headType;		/**< message header type */
    int32_t msgType;		/**< message (sub-)type */
    char *buf;			/**< buffer for non fragmented msg */
    uint32_t bufUsed;		/**< the size of the buffer */
    bool useFrag;		/**< if true use fragmentation */
    PS_Frag_Msg_Header_t fhead; /**< fragmentation header */
    PSnodes_ID_t nrOfNodes;	/**< the number of destinations */
} PS_SendDB_t;

/** Prototype of custom sender functions used by @ref initSerial() */
typedef int Send_Msg_Func_t(void *);

/** Prototype for @ref __recvFragMsg()'s callback */
typedef void PS_DataBuffer_func_t(DDTypedBufferMsg_t *msg,
				  PS_DataBuffer_t *data);

/**
 * @brief Initialize Psserial facility
 *
 * Initialize Psserial facility and enable it to use a default
 * buffer-size of @a bufSize for send- and receive-buffers and @a func
 * as the sending funtion. Initialization includes allocating memory.
 * This function shall be called upon start of a program.
 *
 * A good choice for @a func is the ParaStation daemon's @ref
 * sendMsg() function.
 *
 * @param bufSize Size of the internal buffers. It this is 0, the
 * default size of 256 kB is used.
 *
 * @param func Sending function to use
 *
 * @return If anything was initialized, true is returned. Otherwise
 * false is returned. The latter might happen is the Psserial facility
 * was initialized before.
 */
bool initSerial(size_t bufSize, Send_Msg_Func_t *func);

/**
 * @brief Finalize Psserial facility
 *
 * Finalize the Psserial facility. This includes releasing all buffers.
 *
 * @return No return value
 */
void finalizeSerial(void);

/**
 * @brief Initialize a fragmented message buffer.
 *
 * @param buffer The buffer to use
 *
 * @param headType Type of the messages used to send the fragments
 *
 * @param msgType Sub-type of the messages to send
 */
void initFragBuffer(PS_SendDB_t *buffer, int32_t headType, int32_t msgType);

/**
 * @brief Set an additional destination for fragmented messages.
 *
 * Add the provided Task ID as an additional destination to send
 * the fragmented message to. This functions needs to be called
 * before using any functions to add data to the buffer. A good place
 * is right after the call to @ref initFragBuffer.
 *
 * @param buffer The send buffer to use
 *
 * @param id The Task ID to add
 *
 * @return Returns true if the destition was added or false on error
 */
bool setFragDest(PS_SendDB_t *buffer, PStask_ID_t id);

/**
 * @brief Receive fragmented message
 *
 * Add the fragment contained in the message @a msg to the overall
 * message to receive stored in a separate message buffer. Upon
 * complete receive of the message, i.e. after the last fragment
 * arrived, the callback @a func will be called with @a msg as the
 * first parameter and the message buffer used to collect all
 * fragments as the second argument.
 *
 * @param msg Message to handle
 *
 * @param func Callback function to be called upon message completion
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return On success true is returned or false in case of an
 * error.
 */
bool __recvFragMsg(DDTypedBufferMsg_t *msg, PS_DataBuffer_func_t *func,
		   const char *caller, const int line);

#define recvFragMsg(msg, func) __recvFragMsg(msg, func, __func__, __LINE__)

/**
 * @brief Send fragmented message
 *
 * Send the message content found in the message buffer @a buffer to
 * the task ID(s) registered before using @ref setFragDest() as a series
 * of fragments put into ParaStation protocol messages of type
 * @ref DDTypedBufferMsg_t. Each message is of RDPType and the
 * sub-type defined previously by @ref initFragBuffer().
 *
 * The sender function which was registered before via @ref
 * initFragBuffer() method is used to send the fragments.
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
 * @brief Switch file-descriptor's blocking mode
 *
 * If the flag @a block is true, the file-descriptor @a fd is brought
 * into blocking mode, i.e. the @ref O_NONBLOCK flag is removed from
 * the file-descriptor. If block is false, the flag is set.
 *
 * @param fd File-descriptor to manipulate
 *
 * @param block Flag the blocking mode
 *
 * @return No return value
 */
void setFDblock(int fd, bool block);

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
PS_DataBuffer_t * dupDataBuffer(PS_DataBuffer_t *data);

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
 * @brief Write data to file descriptor
 *
 * Write data from @a buffer to the file descripter @a fd. A total of
 * @a toWrite bytes is written. The actual number of bytes written is
 * reported in @a written. Writing will be retried on minor
 * errors until all data was written if the @a pedantic flag is set to
 * true. Otherwise, the function will return as soon as the first
 * write() fails. In all cases @a written will reflect the
 * number of bytes written so far.
 *
 * Unless @a infinite flags true a total of 20 retries are
 * undertaken. Otherwise the function will try inifinitely to write
 * the data.
 *
 * @param fd File descriptor to write to
 *
 * @param buffer Buffer holding data to write
 *
 * @param toWrite Number of bytes to write
 *
 * @param written Total number of bytes written upon return
 *
 * @param func Funtion name of the calling function
 *
 * @param pedantic Flag to be pedantic
 *
 * @param infinite Flag to retry infinitely
 *
 * @return Returns the number of bytes written or -1 on error. In the
 * latter cases the number of bytes written anyhow is reported in @a written.
 */
int __doWriteEx(int fd, void *buffer, size_t toWrite, size_t *written,
		const char *func, bool pedantic, bool infinite);

#define doWriteEx(fd, buffer, toWrite, written) \
    __doWriteEx(fd, buffer, toWrite, written, __func__, false,  false)

#define doWriteExP(fd, buffer, toWrite, written) \
    __doWriteEx(fd, buffer, toWrite, written, __func__, true, false)

#define doWriteExF(fd, buffer, toWrite, written) \
    __doWriteEx(fd, buffer, toWrite, written, __func__, true, true)

/**
 * @brief Write data to file descriptor
 *
 * Write data from @a buffer to the file descripter @a fd. A total of
 * @a toWrite bytes is written. Writing will be retried on minor
 * errors until all data was written if the @a pedantic flag is set to
 * true. Otherwise, the function will return as soon as the first
 * write() fails.
 *
 * Unless @a infinite flags true a total of 20 retries are
 * undertaken. Otherwise the function will try inifinitely to write
 * the data.
 *
 * This is mainly a wrapper around @ref __doWriteEx() hiding the @a
 * written parameter.
 *
 * @param fd File descriptor to write to
 *
 * @param buffer Buffer holding data to write
 *
 * @param toWrite Number of bytes to write
 *
 * @param func Funtion name of the calling function
 *
 * @param pedantic Flag to be pedantic
 *
 * @param infinite Flag to retry infinitely
 *
 * @return Returns the number of bytes written or -1 on error.
 */
int __doWrite(int fd, void *buffer, size_t toWrite, const char *func,
	      bool pedantic, bool infinite);

#define doWrite(fd, buffer, toWrite) __doWrite(fd, buffer, toWrite,	\
					       __func__, false,  false)

#define doWriteP(fd, buffer, toWrite) __doWrite(fd, buffer, toWrite,	\
						__func__, true, false)

#define doWriteF(fd, buffer, toWrite) __doWrite(fd, buffer, toWrite,	\
						__func__, true, true)

/**
 * @brief Read data from file descriptor
 *
 * Read up to @a toRead bytes from the file descriptor @a fd to the
 * memory @a buffer is pointing to. The actual number of bytes read is
 * reported in @a numRead. Reading will be retried up to 20 times on
 * minor errors until all data was read if the @a pedantic flag is set
 * to true. Otherwise, the function will return as soon as the first
 * read() fails. Nevertheless, in the latter case read() will be done
 * in a blocking fashion. In all cases @a numRead will reflect the
 * number of bytes read so far.
 *
 * @param fd File descriptor to read from
 *
 * @param buffer Buffer to store data to
 *
 * @param toRead Number of bytes to read
 *
 * @param numRead Total number of bytes read upon return
 *
 * @param func Funtion name of the calling function
 *
 * @param pedantic Flag to be pedantic. If false, read() will be
 * called in a blocking fashion.
 *
 * @return Returns the number of bytes read, 0 if the file descriptor
 * closed or -1 on error. In the latter cases the number of bytes read
 * anyhow is reported in @a numRead.
 */
int __doReadExt(int fd, void *buffer, size_t toRead, size_t *numRead,
		const char *func, bool pedantic);

#define doReadExt(fd, buffer, toread, ret) __doReadExt(fd, buffer, toread, \
						       ret, __func__, 0)

#define doReadExtP(fd, buffer, toread, ret) __doReadExt(fd, buffer, toread, \
							ret, __func__, 1)

/**
 * @brief Read data from file descriptor
 *
 * Read up to @a toRead bytes from the file descriptor @a fd to the
 * memory @a buffer is pointing to.
 *
 * This is mainly a wrapper around @ref __doReadExt() hiding the @a
 * numRead parameter.
 *
 * @param fd File descriptor to read from
 *
 * @param buffer Buffer to store data to
 *
 * @param toRead Number of bytes to read
 *
 * @param func Funtion name of the calling function
 *
 * @param pedantic Flag to be pedantic. If false, read() will be
 * called in a blocking fashion.
 *
 * @return Returns the number of bytes read, 0 if the file descriptor
 * closed or -1 on error
 */
int __doRead(int fd, void *buffer, size_t toRead, const char *func,
	     bool pedantic);

#define doRead(fd, buffer, toRead) __doRead(fd, buffer, toRead, __func__, false)

#define doReadP(fd, buffer, toRead) __doRead(fd, buffer, toRead, __func__, true)

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
 * If the global @ref byteOrder flag is true byte order of the
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

#define getInt8(ptr, val) getFromBuf(ptr, val, PSDATA_INT8,		\
				     sizeof(int8_t), __func__, __LINE__)

#define getInt16(ptr, val) getFromBuf(ptr, val, PSDATA_INT16,		\
				      sizeof(int16_t), __func__, __LINE__)

#define getInt32(ptr, val) getFromBuf(ptr, val, PSDATA_INT32,		\
				      sizeof(int32_t), __func__, __LINE__)

#define getInt64(ptr, val) getFromBuf(ptr, val, PSDATA_INT64,		\
				      sizeof(int64_t), __func__, __LINE__)

#define getUint8(ptr, val) getFromBuf(ptr, val, PSDATA_UINT8,		\
				      sizeof(uint8_t), __func__, __LINE__)

#define getUint16(ptr, val) getFromBuf(ptr, val, PSDATA_UINT16,		\
				       sizeof(uint16_t), __func__, __LINE__)

#define getUint32(ptr, val) getFromBuf(ptr, val, PSDATA_UINT32,		\
				       sizeof(uint32_t), __func__, __LINE__)

#define getUint64(ptr, val) getFromBuf(ptr, val, PSDATA_UINT64,		\
				       sizeof(uint64_t), __func__, __LINE__)

#define getDouble(ptr, val) getFromBuf(ptr, val, PSDATA_DOUBLE,		\
				       sizeof(double), __func__, __LINE__)

#define getTime(ptr, val) getFromBuf(ptr, val, PSDATA_TIME,		\
				     sizeof(time_t), __func__, __LINE__)

#define getPid(ptr, val) getFromBuf(ptr, val, PSDATA_PID,		\
				    sizeof(pid_t), __func__, __LINE__)


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
 * @a ptr. A pointer to the buffer is returned. The caller has to
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

#define getUint16Array(ptr, val, len)					\
    getArrayFromBuf(ptr, (void **)val, len, PSDATA_UINT16, sizeof(uint16_t), \
		    __func__, __LINE__)

#define getUint32Array(ptr, val, len)					\
    getArrayFromBuf(ptr, (void **)val, len, PSDATA_UINT32, sizeof(uint32_t), \
		    __func__, __LINE__)

#define getInt16Array(ptr, val, len)					\
    getArrayFromBuf(ptr, (void **)val, len, PSDATA_INT16, sizeof(int16_t), \
		    __func__, __LINE__)

#define getInt32Array(ptr, val, len)					\
    getArrayFromBuf(ptr, (void **)val, len, PSDATA_INT32, sizeof(int32_t), \
		    __func__, __LINE__)


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
 * If @a len is 0 upon return array will be untouched.
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
 * buffer @a data. If the global flag @ref typeInfo is true the
 * element will be annotated to be of type @a type.
 *
 * If the data is of type PSDATA_STRING or PSDATA_DATA it will be
 * annotated by an additional length item.
 *
 * If the global @ref byteOrder flag is true the data will be shuffled
 * into network byte-order unless it is of type PSDATA_STRING,
 * PSDATA_DATA or PSDATA_MEM.
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

#define addTimeToMsg(val, data) { time_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_TIME,		\
		 __func__, __LINE__); }

#define addPidToMsg(val, data) { pid_t _x = val;		\
	addToBuf(&_x, sizeof(_x), data, PSDATA_PID,		\
		 __func__, __LINE__); }

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
 * typeInfo is true each element will be annotated to be of type @a
 * type.
 *
 * The overall data will be annotated by a leading element describing
 * the number of elements provided via @a num.
 *
 * If the global @ref byteOrder flag is true each element will be shuffled
 * into network byte-order.
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

#define addUint16ArrayToMsg(val, num, data)				\
    addArrayToBuf(val, num, data, PSDATA_UINT16, sizeof(uint16_t),	\
		  __func__, __LINE__)

#define addUint32ArrayToMsg(val, num, data)				\
    addArrayToBuf(val, num, data, PSDATA_UINT32, sizeof(uint32_t),	\
		  __func__, __LINE__)

#define addInt16ArrayToMsg(val, num, data)				\
    addArrayToBuf(val, num, data, PSDATA_INT16, sizeof(int16_t),	\
		  __func__, __LINE__)

#define addInt32ArrayToMsg(val, num, data)				\
    addArrayToBuf(val, num, data, PSDATA_INT32, sizeof(int32_t),	\
		  __func__, __LINE__)



/**
 * @brief Add element to message buffer
 *
 * Add an element of @a size bytes located at @a val to the buffer of
 * the message @a msg. If the global flag @ref typeInfo is true the
 * element will be annotated to be of type @a type.
 *
 * If the data is of type PSDATA_STRING it will be annotated by an
 * additional length item.
 *
 * If the global @ref byteOrder flag is true the data will be shuffled
 * into network byte-order unless it is of type PSDATA_STRING.
 *
 * This function uses @ref PSP_putTypedMsgBuf(), i.e. the len element
 * of the messages header is updated appropriately.
 *
 * @param msg Message to add data to
 *
 * @param val Address of the data to add
 *
 * @param size Number of bytes of the element to add
 *
 * @param type Type of the element to add
 *
 * @param caller Function name of the calling function
 *
 * @return On success true is returned or false in case of an error.
 */
bool addToMsgBuf(DDTypedBufferMsg_t *msg, void *val, uint32_t size,
		 PS_DataType_t type, const char *caller);

#define addStringToMsgBuf(msg, str)					\
    addToMsgBuf(msg, str, PSP_strLen(str), PSDATA_STRING, __func__)

#define addDataToMsgBuf(msg, data, len)			\
    addToMsgBuf(msg, data, len, PSDATA_DATA, __func__)

#define addTimeToMsgBuf(msg, time) { time_t _x = time;			\
	addToMsgBuf(msg, &_x, sizeof(_x), PSDATA_TIME, __func__); }

#define addInt32ToMsgBuf(msg, val) { int32_t _x = val;			\
	addToMsgBuf(msg, &_x, sizeof(_x), PSDATA_INT32, __func__); }

#define addUint8ToMsgBuf(msg, val) { uint8_t _x = val;			\
	addToMsgBuf(msg, &_x, sizeof(_x), PSDATA_UINT8, __func__); }

#define addUint16ToMsgBuf(msg, val) { uint16_t _x = val;		\
	addToMsgBuf(msg, &_x, sizeof(_x), PSDATA_UINT16, __func__); }

#define addUint32ToMsgBuf(msg, val) { uint32_t _x = val;		\
	addToMsgBuf(msg, &_x, sizeof(_x), PSDATA_UINT32, __func__); }


#endif  /* __PLUGIN_LIB_COMM */
