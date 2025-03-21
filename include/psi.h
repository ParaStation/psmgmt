/*
 * ParaStation
 *
 * Copyright (C) 1999-2003 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/** @file User-functions for interaction with the ParaStation system */
#ifndef __PSI_H
#define __PSI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "psnodes.h"     // IWYU pragma: export
#include "pstask.h"      // IWYU pragma: export
#include "psprotocol.h"  // IWYU pragma: export

/**
 * @brief Initialize PSI
 *
 * Initialize the PSI stuff within the current process. This includes
 * to try to connect the local daemon and to register to this
 * daemon. Furthermore some basic setup is made.
 *
 * If it is impossible to connect the local daemon, an error is returned.
 *
 * The fashion which is used to connect the daemon depends on @a
 * taskGroup. If this is @ref TG_ADMIN which usually denotes a
 * psiadmin(1) process, it will be first tried to contact the
 * daemon. If this fails, the attempt to start the daemon via the
 * (x)inetd(8) and to contact it again will be made for 5 times. If
 * there is finally no contact, an error is returned. This behavior
 * might be switched off by setting the __PSI_DONT_START_DAEMON
 * environment variable before calling this function.
 *
 * For all other values of @a taskGroup only one attempt to contact
 * the local daemon is made without trying to start the daemon.
 *
 * @param taskGroup The kind of task trying to initialize the PSI
 * stuff. This is used to register to the local daemon.
 *
 * @return If a connection to the local daemon could be established,
 * true is returned, or false otherwise.
 */
bool PSI_initClient(PStask_group_t taskGroup);

/**
 * @brief Exit PSI
 *
 * Shutdown PSI. This will close the connection to the local daemon
 * and reset everything in a way that the daemon might be connected
 * again. Since no releasing towards the daemon is done, other
 * processes within the parallel task might be killed after calling
 * this function.
 *
 * @return 1 is always returned.
 */
int PSI_exitClient(void);

/**
 * @brief Flag heterogeneous protocol versions
 *
 * Flag if we live in a cluster with heterogeneous protocol versions
 *
 * @return If more than one protocol version is active in the local
 * cluster, true is returned. Otherwise all members of the local
 * cluster will run the very same protocol version as the local node.
 */
bool PSI_mixedProto(void);

/**
 * @brief Get protocol version
 *
 * Get the protocol version supported by the ParaStation daemon
 * running on the node with ParaStation ID @a id.
 *
 * This function implements caching and makes use of @ref
 * PSI_mixedProto() in order to determine if any version requests have
 * to be sent to remote nodes at all.
 *
 * @param id ParaStation ID of the node to ask
 *
 * @return If @a id is valid and the node's protocol version could be
 * determined, this version number is returned; or -1 if an error
 * occurred
 */
int PSI_protocolVersion(PSnodes_ID_t id);

/**
 * @brief Send a message
 *
 * Send the message @a amsg to the destination defined therein. This
 * is done by sending it to the local ParaStation daemon. @a amsg is
 * expected to point to a message compliant to a @ref DDMsg_t message.
 *
 * @param amsg Pointer to the message to send.
 *
 * @return On success, the number of bytes written are returned. On
 * error, -1 is returned, and errno is set appropriately.
 */
ssize_t PSI_sendMsg(void *amsg);

/**
 * @brief Test for available message
 *
 * Test, if a message is available from the local ParaStation
 * daemon. For this, select() is called with a zero timeout on the
 * connection to the local daemon.
 *
 * @return The return value of the select call. This might be 1, if a
 * message is available, or 0 without any available message. In case
 * of an error -1 is returned and errno is set appropriately.
 */
int PSI_availMsg(void);

/**
 * @brief Handler prototype for PSI messages
 *
 * Prototype for handlers of PSI messages to be utilized while calling
 * @ref PSI_recvMsg(). Handlers for specific message types might be
 * registered via @ref PSI_addRecvHandler(). If a message of this type
 * is received in @ref PSI_recvMsg() the corresponding handler will be
 * called with the message as first argument, the name of the caller
 * as the second argument and an @a info argument that was provided
 * while registering the handler via @ref PSI_addRecvHandler() as the
 * third argument.
 *
 * Depending on the return value of the handler @ref PSI_recvMsg()
 * will either (in the case of true) continue waiting for appropriate
 * messages or return to the calling functions indicating no
 * appropriate message was received by returning -1 and setting errno
 * to ENOMSG.
 *
 * @param msg Message to be handled by the message handler
 *
 * @param caller Name of the caller of @ref PSI_recvMsg()
 *
 * @param info Pointer to additional information that was registered
 * alongside the handler itself
 *
 * @return If the handler returns true, @ref PSI_recvMsg() will wait
 * for further messages; otherwise @ref PSI_recvMsg() will return
 * immediately indicating no appropriate message was found by setting
 * @ref errno to ENOMSG
 */
typedef bool(PSI_handlerFunc_t)(DDBufferMsg_t *msg, const char *caller,
				void *info);

/**
 * Generic handler to just drop a message type in @ref PSI_recvMsg()
 * unless its @a xpctdType parameter is -1. If PSI_LOG_COMM is part of
 * PSI_logger's mask a warning will be emitted.
 */
PSI_handlerFunc_t ignoreMsg;

/**
 * Generic handler to pass messages of specific types to the caller of
 * @ref PSI_recvMsg() without further ado. @ref PSI_recvMsg() will
 * return -1 with errno set to ENOMSG. The received message will be
 * passed in the buffer @a msg.
 */
PSI_handlerFunc_t acceptMsg;

/**
 * @brief Register handler for PSI messages
 *
 * Register the message handler function @a handler to handle PSI
 * messages of type @a msgType within @ref PSI_recvMsg(). For this,
 * @ref PSI_recvMsg() will call @a handler if a message of type @a
 * msgType is received. Besides the message itself a pointer to
 * additional information @a info is passed to the handler.
 *
 * @param msgType Message type to get handled
 *
 * @param handler Message handler to call upon receive of a message of
 * type @a msgType
 *
 * @param info Pointer to additional information to be passed to @a
 * handler upon receive of a message of type @a msgType
 *
 * @return Return true on success or false in case of error
*/
bool PSI_addRecvHandler(int16_t msgType, PSI_handlerFunc_t handler, void *info);

/**
 * @brief Clear handler for PSI messages
 *
 * Stop handling PSI messages of type @a msgType by the message
 * handler function @a handler. Future messages of this type will be
 * either ignored or handled as unexpected messages by @ref
 * PSI_recvMsg().
 *
 * @param msgType Message type to be ignored in the future
 *
 * @param handler Message handler to be removed
 *
 * @return Return true on success (i.e. on removal of the
 * corresponding handler) or false otherwise
 */
bool PSI_clrRecvHandler(int16_t msgType, PSI_handlerFunc_t handler);

/**
 * @brief Receive a message
 *
 * Receive a message from the local ParaStation daemon and store it to
 * the buffer @a msg. At most @a size bytes are written to @a msg. The
 * message to receive is expected to be of type @a xpctdType. If the
 * type of the received message matches or @a xpctdType is -1, the
 * message is received and stored to @a msg unless the buffer turns
 * out to be too small which leads to an error. In the case the
 * message was received successfully the number of bytes received and
 * stored to @a msg is returned.
 *
 * Otherwise the message might be handled by a handler to was
 * registered via @ref PSI_addRecvHandler() to the according message
 * type. Depending on the handler's return value this function will
 * continue receiving messages (in case the handler returns true) or
 * return -1 with errno set to ENOMSG (in case the handler returns
 * false). In the latter case the received message is stored to @a msg
 * and the number of bytes received can be deduced from @ref
 * msg->header.len.
 *
 * If no handler was registered for the received message type the
 * behavior depends on the flag @a ignUnxpctd. If set to true a
 * warning will be emitted and this function continues to wait for
 * further messages. Otherwise it will return the number of bytes
 * received and the message is stored to @a msg.
 *
 * Keep in mind that the behavior of @a xpctdType == -1 is different
 * from @a ignUnxpctd == false: while in the first case any message
 * that was received is returned immediately in the latter case the
 * message will first be handled by a registered message handler if
 * any. Thus, the message might still be hidden by a handler that
 * returns true.
 *
 * @param msg Buffer to store the message to
 *
 * @param size Maximum length of the message, i.e. size of @a msg
 *
 * @param xpctdType Message type to accept; if -1, any message is
 * accepted; otherwise first the message is tried to be handled by a
 * corresponding message handler or the behavior depends on @a
 * ignUnxpctd
 *
 * @param wait Flag to wait for a message; if this is false, @ref
 * PSI_availMsg() will be used to check the availability; in case no
 * message is available, -1 is returned and errno set to ENODATA
 *
 * @param ignUnxpctd Flag to ignore messages of unexpected type (but
 * emitting a warning) or to deliver any type of message
 *
 * @param caller Name of the calling function that will be passed to
 * the message handler to call
 *
 * @return Returns the number of bytes received on success or -1
 * otherwise; in the latter case @ref errno is set appropriately;
 * errno == ENOMSG will flag the case that an according handler
 * returns false in order to pass the received message to the caller
 * even though @a xpctdType does not match and ignUnxpctd is true;
 * errno == ENODATA flags the case that @a wait is false and no
 * message available
 */
ssize_t __PSI_recvMsg(DDBufferMsg_t *msg, size_t size, int16_t xpctdType,
		      bool wait, bool ignUnxpctd, const char *caller);

#define PSI_recvMsg(msg, size, xpctdType, ignUnxpctd)		\
    __PSI_recvMsg(msg, size, xpctdType, true, ignUnxpctd, __func__)

#define PSI_tryRecvMsg(msg, size, xpctdType, ignUnxpctd)		\
    __PSI_recvMsg(msg, size, xpctdType, false, ignUnxpctd, __func__)


/**
 * @brief Register for notification of foreign processes death
 *
 * Register for notification of a foreign processes death. I.e. send
 * me the signal @a sig, as soon as the foreign process with task ID
 * @a tid dies, expectedly or unexpectedly.
 *
 * A process dies expectedly, if it has called PSI_release() before
 * terminating its execution.
 *
 * It is not necessary to register child processes since they will
 * send a special signal, which is SIGTERM as default, to their
 * parents anyway. The type of signal might be changed via the
 * registration of the wanted signal to the @a tid of 0L using this
 * function.
 *
 * @param tid Task ID of the process whose death I'm interested in
 *
 * @param sig Signal to be sent when the process dies
 *
 * @return On success 0 is returned; or -1 if an error occurred
 */
int PSI_notifydead(PStask_ID_t tid, int sig);

/**
 * @brief Release a process
 *
 * Release the process with task ID @a tid from sending a signal to me
 * on its death. This might be used to cancel prior PSI_notifydead()
 * calls with the same task ID.
 *
 * The special case where @a tid is PSC_getMyTID() will release the
 * local process from receiving any signal and furthermore from
 * sending a signal to its parent process. Usually the parent process
 * will get a special signal if any child will die. A call to this
 * function will suppress this signal and usually keep the parent
 * alive.
 *
 * This is typically used upon the correct end of a processes being
 * part of a parallel task.
 *
 * @param tid The task ID of the process to get released.
 *
 * @return On success, 0 is returned. In case of an error -1 is
 * returned, and errno is set appropriately.
 */
int PSI_release(PStask_ID_t tid);

/**
 * @brief Request signal's sender.
 *
 * Request which local or foreign process sent the signal @a sig to me
 * recently. This will only work when the signal was send via
 * ParaStation, i.e. if the signal was initiated by the death of the
 * sending process or via an explicit call to PSI_kill() from the
 * sending process.
 *
 * @param sig The signal recently received which sender should be
 * determined.
 *
 * @return On success, the senders task ID is returned, or -1 if the
 * sender could not be determined.
 */
PStask_ID_t PSI_whodied(int sig);

/**
 * @brief Send a finish message.
 *
 * Send a PSP_CD_SPAWNFINISH message to the process with task ID @a
 * parenttid, which is usually the parent process listening to this
 * kind of messages within PSI_recvFinish().
 *
 * This might be used in order to inform the parent process about the
 * successful finalization of a child's process as it might needed
 * within e.g. a MPI_Finalize().
 *
 * @param parenttid The task ID of the process sending to.
 *
 * @return On success, 0 is returned. Or -1, if an error occurred.
 */
int PSI_sendFinish(PStask_ID_t parenttid);

/**
 * @brief Receive finish messages
 *
 * Receive a total of @a num PSP_CD_SPAWNFINISH messages from various
 * processes, which are usually child processes of the actual process
 * sending them via PSI_sendFinish().
 *
 * This might be used in order to inform the parent process about the
 * successful finalization of child processes as it might needed
 * within e.g. a MPI_Finalize().
 *
 * @param num Number of messages expected to receive
 *
 * @return Return true on success or false if an error occurred
 */
bool PSI_recvFinish(int num);

/**
 * @brief Transform to psilogger
 *
 * Transforms the current process to a psilogger process. If @a
 * command is different from NULL, it will be executed using the
 * system(3) call after the logger has done his job, i.e. after all
 * clients have finished.
 *
 * @param command Command to execute after by the logger after all
 * clients have closed their connection.
 *
 * @return No return value
 */
__attribute__ ((noreturn))
void PSI_execLogger(const char *command);

/**
 * @brief Propagate environment
 *
 * Propagate a list of environment variables into the ParaStation
 * environment and thus to all client processes spawned.
 *
 * At the time the following variables are propagated:
 *
 * HOME, USER, SHELL, TERM, LD_LIBRARY_PATH, LD_PRELOAD, LIBRARY_PATH,
 * PATH and all variable holding the regular expressions PSP_*,
 * __PSI_* or OMP_*.
 *
 * @return No return value
 */
void PSI_propEnv(void);

/**
 * @brief Propagate list of environments
 *
 * Propagate a list of environment variables to the next level of
 * spawning. For this the environment variable @a listName containing
 * a comma-separated list of environment variable names is
 * analyzed. The environment variable @a listName itself and each
 * environment variable mentioned therein is put into the
 * PSI_environment.
 *
 * @param listName Name of the environment containing a
 * comma-separated list of environment variables to be propagated
 *
 * @return No return value
 */
void PSI_propEnvList(char *listName);

/**
 * @brief Propagate list of environments
 *
 * Propagate a list of environment variables to the next level of
 * spawning. For this the string @a listStr containing a
 * comma-separated list of environment variable names is
 * analyzed. Each environment variable mentioned in this list is put
 * into the PSI_environment
 *
 * @param list Comma-separated list of environment variable names to
 * be propagated
 *
 * @return No return value
 */
void PSI_propList(char *listStr);

/**
 * @brief Get the fd which is connected to the local daemon.
 *
 * @return Returns the requested file descriptor or -1 on error
 */
int PSI_getDaemonFD(void);

#endif  /* __PSI_H */
