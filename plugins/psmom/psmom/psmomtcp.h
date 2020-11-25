/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_TCP
#define __PS_MOM_TCP

/**
* @brief Bind to a privileged tcp socket.
*
* @param port The port to bind to.
*
* @return Returns the socket which is bind to the requested port
*   or -1 on error.
*/
int tcpBind(int port);

/**
* @brief Start a new tcp connection.
*
* @param port The port to connect to.
*
* @param addr The address to connect to.
*
* @param priv If set to 1 a priviled port will be used for the connection.
*
* @return Returns the socket associated with the new connection or -1 on error.
*/
int tcpConnect(int port, char *addr, int priv);

/**
* @brief Send the buffered messages.
*
* @param sock The socket to use.
*
* @return The number of bytes sent or -1 on error.
*/
int tcpDoSend(int sock, const char *caller);

/**
 * @brief Receive a new tcp message.
 *
 * @param sock The socket to read the message from.
 *
 * @param buffer The buffer to save the message in.
 *
 * @param size The size of the buffer.
 *
 * @return Returns the length of the received message in bytes or -1 on error.
 */
ssize_t tcpRead(int sock, char *buffer, ssize_t size, const char *caller);

/**
 * @brief Save a message into the tcp buffer.
 *
 * @param sock The socket to save the message for.
 *
 * @param msg The message to save.
 *
 * @param len The length of the message.
 *
 * @return Returns the length of the messsage saved or -1 on error.
 */
int tcpWrite(int sock, void *msg, size_t len, const char *caller);

/**
 * @brief Close a tcp connection.
 *
 * @param sock The socket to close.
 *
 * @return Returns 1 on success and -1 on error.
 */
int tcpClose(int sock);

#endif  /* __PS_MOM_TCP */
