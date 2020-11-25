/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_RPP
#define __PS_MOM_RPP

#include "psmomcomm.h"

/**
 * @brief Init and bind a rpp connection.
 *
 * @param port The port to listen to.
 *
 * @return Returns -1 on error, 1 on success.
 */
int rppBind(int port);

/**
 * @brief Open an rpp connection.
 *
 * @param port The port to connect to.
 *
 * @param host The hostname to connect to.
 *
 * @return Returns the rpp stream with the new connection or -1 on error.
 */
int rppOpen(int port, char *host);

/**
 * @brief Do a reconnect on an exisiting connection.
 *
 * @param stream The stream to do a reconnect for.
 *
 * @return Returns 0 on success, -1 on error.
 */
int rppReconnect(int stream, const char *caller);

/**
 * @brief Send the cached rpp message.
 *
 * @param stream The stream which holds the cached message.
 *
 * @return Returns 0 on success, -1 on error.
 */
int rppDoSend(int stream, const char *caller);

/**
 * @brief Poll and handle new data on all rpp streams.
 *
 * @return Returns 1 on success, -1 on error.
 */
int rppPoll(int fd, void *data);

/**
 * @brief Read from a rpp stream.
 *
 * @param stream The stream to read from.
 *
 * @param buffer The buffer to write the red data to.
 *
 * @param len The length of the buffer.
 *
 * @return Returns the number of bytes received or -1 on error.
 */
ssize_t rppRead(int stream, char *buffer, ssize_t len, const char *caller);

/**
 * @brief Write data to a rpp stream buffer.
 *
 * @param stream The buffer to write the data to, identified by the stream.
 *
 * @param msg The message to write.
 *
 * @param len The size of the message.
 *
 * @return Returns the number of bytes written or -1 on error.
 */
int rppWrite(int stream, void *msg, size_t len, const char *caller);

/**
 * @brief Empty the receive queue of a rpp stream.
 *
 * @param stream The stream to empty the queue for.
 *
 * @return Returns 0 on success, -1 on error.
 */
int rppEOM(int stream);

/**
 * @brief Flush all data on a stream.
 *
 * @param stream The stream to flush.
 *
 * @return Returns 0 on success, -1 on error.
 */
int rppFlush(int stream);

/**
 * @brief Close a rpp connection.
 *
 * @param stream The stream to close.
 *
 * @return Returns 0 on success, -1 on error.
 */
int rppClose(int stream);

/**
 * @brief Flush all buffers and close all open streams.
 *
 * @return No return value.
 */
void rppShutdown(void);

/**
 * Get the address of a rpp stream.
 *
 * @param stream The stream to return the address for.
 *
 * @return Returns the requested address or NULL on error.
 */
struct sockaddr_in* rppGetAddr(int stream);

#endif  /* __PS_MOM_RPP */
