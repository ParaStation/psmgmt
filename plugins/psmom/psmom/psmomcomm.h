/*
 * ParaStation
 *
 * Copyright (C) 2010-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_COMM
#define __PS_MOM_COMM

#include <sys/types.h>

#include "list.h"

#define MAX_ADDR_SIZE 20

typedef enum  {
    RPP_PROTOCOL = 0,
    TCP_PROTOCOL = 1,
    UNIX_PROTOCOL = 2
} Protocol_t;

typedef struct {
    Protocol_t type;
    int socket;
    int localPort;
    int remotePort;
    size_t bufSize;
    size_t dataSize;
    char *outBuf;
    char *inBuf;
    char addr[MAX_ADDR_SIZE];
    char remoteAddr[MAX_ADDR_SIZE];
    unsigned long lremoteAddr;
    int isAuth;
    char *jobid;
    char *info;
    int (*Write) (int, void*, size_t, const char *);
    ssize_t (*Read) (int, char*, ssize_t, const char*);
    int (*Close) (int);
    int (*Bind) (int);
    int (*DoSend) (int, const char *);
    int (*Reconnect) (int, const char*);
    int (*HandleFunc) (int, void*);
    struct list_head list;
} ComHandle_t;

/** the communication list */
extern ComHandle_t ComList;

/**
 * @brief Init the communication list.
 *
 * @return No return value.
 */
void initComList(void);

/**
 * @brief Find a communication handle.
 *
 * @param socket The socket of the connection.
 *
 * @param type The type of the protocol which is used for the connection.
 *
 * @return Returns the requested communication handle or NULL on error.
 */
ComHandle_t *findComHandle(int socket, Protocol_t type);

/**
 * @brief Find a communication handle identified by a jobid.
 *
 * @param jobid The jobid to identify the handle.
 *
 * @return Returns The first communication handle assosiated with the jobid or
 * NULL if no handle was found.
 */
ComHandle_t *findComHandleByJobid(char *jobid);

/**
 * @brief Get a communication handle for a connection.
 *
 * This function will search for an excisting communication handle and return
 * it. If no handle is found a new one will be created.
 *
 * @param socket The socket of the connection.
 *
 * @param type The type of the protocol to use for the connection.
 *
 * @return Always returns the a pointer to the requested communication handle.
 */
ComHandle_t *getComHandle(int socket, Protocol_t type);

/**
 * @brief Test if a pointer to a communication handle is still valid.
 *
 * A connection can be closed an various error conditions. When the connection
 * is closed the communication handle will be deleted and the pointer to it will
 * became invalid.
 *
 * @return Returns 1 if the handle is still vaild otherwise 0 is returned.
 */
int isValidComHandle(ComHandle_t *testCom);

/**
 * @brief Initialize a new connection.
 *
 * @param com The communication handle to identify the connection.
 *
 * @param type The type of the protocol to use for the connection.
 *
 * @param socket The socket of the connection to initialize.
 *
 * @return No return value.
 */
void initComHandle(ComHandle_t *com, Protocol_t type, int socket);

/**
 * @brief Close all connections and shutdown the rpp library.
 *
 * This function will shutdown the communiction and remove all job obit timer.
 *
 * @return No return value.
 */
void closeAllConnections(void);

/**
 * @brief Write data to a connection buffer.
 *
 * @param com The communication handle to identify the connection.
 *
 * @param msg The The message to write to the buffer.
 *
 * @param len The size of the message to write.
 *
 * @return Returns the number of bytes written or -1 on error.
 */
#define wWrite(com, msg, len) __wWrite(com, msg, len, __func__)
int __wWrite(ComHandle_t *com, void *msg, size_t len, const char *caller);

/**
 * @brief Read a defined number of bytes from a connection.
 *
 * Read a defined number of bytes from a connection. This function can block and
 * wait for new data to arrive.
 *
 * @param com The communication handle to identify the connection.
 *
 * @param buffer The buffer to write the read data to.
 *
 * @param len The number of bytes to read.
 *
 * @return Returns the number of bytes read or -1 on error.
 */
#define wRead(com, buffer, len) __wRead(com, buffer, len, __func__)
ssize_t __wRead(ComHandle_t *com, void *buffer, ssize_t len,
		    const char *caller);

/**
 * @brief Read a defined number of bytes from a connection.
 *
 * Read a defined number of bytes from a connection. This function can block and
 * wait for new data to arrive. The buffer will be terminated by a NULL
 * character.
 *
 * @param com The communication handle to identify the connection.
 *
 * @param buffer The buffer to write the read data to.
 *
 * @param bufSize The size of the buffer.
 *
 * @param len The number of bytes to read.
 *
 * @return Returns the number of bytes read or -1 on error.
 */
#define wReadT(com, buffer, bufSize, len) \
		__wReadT(com, buffer, bufSize, len, __func__)
ssize_t __wReadT(ComHandle_t *com, char *buffer, size_t bufSize, ssize_t len,
		const char* caller);

/**
 * @brief Read all available data from a connection.
 *
 * This function will read all data currently available. In opposite
 * to the function wRead() it will not wait until the buffer is completely
 * filled. This function will never block.
 *
 * @param com The communication handle to identify the connection.
 *
 * @param buffer The buffer to write the read data to.
 *
 * @param len The size of the buffer.
 *
 * @return Returns the number of bytes read or -1 on error.
 */
#define wReadAll(com, buffer, len) __wReadAll(com, buffer, len, __func__)
ssize_t __wReadAll(ComHandle_t *com, void *buffer, ssize_t len,
		    const char *caller);

/**
 * @brief Close a connection.
 *
 * @param com The communication handle to identify the connection.
 *
 * @return No return value.
 */
void wClose(ComHandle_t *com);

/**
 * @brief Empty the receive queue of a connection.
 *
 * This function will currently only have an affect for rpp connections.
 *
 * @param com The communication handle to identify the connection.
 *
 * @return No return value.
 */
void wEOM(ComHandle_t *com);

/**
 * @brief Flush a connection.
 *
 * This function will currently only have an affect for rpp connections.
 *
 * @param com The communication handle to flush the connection.
 *
 * @return No return value.
 */
void wFlush(ComHandle_t *com);

/**
 * @brief Send the buffered data.
 *
 * @param com The communicaton handle for the connection.
 *
 * @return Returns the number of bytes sent or -1 on error.
 */
#define wDoSend(com) __wDoSend(com, __func__)
int  __wDoSend(ComHandle_t *com, const char *caller);

/**
 * @brief Try to re-connect a broken connection.
 *
 * This will currently only have an affect on rpp connections which are
 * connect to the PBS server. On all other rpp connections will be closed.
 *
 * @param com The communication handle for the connection to re-connect.
 *
 * @return Returns 0 on success and -1 on error.
 */
#define wReconnect(com) __wReconnect(com, __func__)
int __wReconnect(ComHandle_t *com, const char *caller);

/**
 * @brief Enable a connection.
 *
 * This will start proccesing incoming data on the specific connection. This
 * will only have an affect if the connection was disabled before.
 *
 * @param com The communication handle for the connection to enable.
 *
 * @return No return value.
 */
void wEnable(ComHandle_t *com);

/**
 * @brief Disable a connection.
 *
 * This will stop proccesing incoming data on the specific connection.
 *
 * @param com The communication handle for the connection to disable.
 *
 * @return No return value.
 */
void wDisable(ComHandle_t *com);

/**
 * @brief Open a listening socket.
 *
 * @param port The port to bind to.
 *
 * @param type The protocol type to use for new connections.
 *
 * @return Returns the socket which will listen for new connections or
 * -1 on error.
 */
int wBind(int port, Protocol_t type);

/**
 * @brief Open a new connection.
 *
 * @param port The port to connect to.
 *
 * @param addr The address to connect to.
 *
 * @param type The protocol type to use for this connection.
 *
 * @return Returns a pointer to a communication handle representing the new
 * connection or NULL on error.
 */
ComHandle_t *wConnect(int port, char *addr, Protocol_t type);

/**
 * @brief Get the remote ip-address and port for a socket.
 *
 * @param type The protocol type of the connection.
 *
 * @param socket The socket to get the information for.
 *
 * @param Iaddr The buffer to write the ip-address to.
 *
 * @param addrSize The size of the Iaddr buffer.
 *
 * @param Iport A pointer to an integer where the port information will be
 * saved.
 *
 * @param lAddr Pointer which will receive the resolved address in binary form.
 * Can be ignored by passing NULL.
 *
 * @return Returns 0 on success and 1 on error.
 */
int getRemoteAddrInfo(Protocol_t type, int socket, char *Iaddr,
			size_t addrSize, int *Iport, unsigned long *lAddr);

/**
 * @brief Get the local ip-address and port for a socket.
 *
 * @param socket The socket to get the information for.
 *
 * @param Iaddr The buffer to write the ip-address to.
 *
 * @param addrSize The size of the Iaddr buffer.
 *
 * @param Iport A pointer to an integer where the port information will be
 * saved.
 *
 * @return Returns 0 on success and 1 on error.
 */
int getLocalAddrInfo(int socket, char *Iaddr, size_t addrSize, int *Iport);

/**
 * @brief Convert a protocol type into a string.
 *
 * @param type The protocol type to convert.
 *
 * @returns the string representation or NULL on error.
 */
char *protocolType2String(int type);

/**
 * @brief Convert a connection type to string.
 *
 * @param type The connection type to convert.
 *
 * @return Returns the requested string or NULL on error.
 */
char *ComHType2String(int type);

#endif  /* __PS_MOM_COMM */
