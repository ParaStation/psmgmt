/*
 * ParaStation
 *
 * Copyright (C) 2014-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_COMM
#define __PSSLURM_COMM

#include "psslurmjob.h"
#include "psslurmio.h"
#include "plugincomm.h"
#include "slurmmsg.h"

typedef int Connection_CB_t(Slurm_Msg_t *msg);

typedef struct {
    PSnodes_ID_t *nodes;
    uint32_t nodesCount;
    uint32_t res;
    PS_DataBuffer_t body;
    Slurm_Msg_Header_t head;
} Connection_Forward_t;

/**
 * @brief Close all connections and free used memory
 */
void clearConnections(void);

/**
 * @brief Initialize a Slurm message
 *
 * @param msg The message to initialize
 */
void initSlurmMsg(Slurm_Msg_t *msg);

/**
 * @brief Free a Slurm message
 *
 * Close the associated connection, free used memory
 * and reset tracking information.
 *
 * @param sMsg The message to free
 */
void freeSlurmMsg(Slurm_Msg_t *sMsg);

/**
 * @brief Duplicate SLURM message
 *
 * Create a duplicate of the SLURM message @a sMsg and return a
 * pointer to it. All data buffers @a sMsg is referring to are
 * duplicated, too. In order to cleanup the duplicate appropriately
 * @ref releaseSlurmMsg() shall be called when the duplicate is not
 * needed any longer.
 *
 * @param sMsg SLURM messages to duplicate
 *
 * @return Upon success a pointer to the duplicate message is
 * returned. Or NULL in case of error.
 */
Slurm_Msg_t * dupSlurmMsg(Slurm_Msg_t *sMsg);

/**
 * @brief Release a duplicate SLURM message
 *
 * Release the duplicate SLURM message @a sMsg. This will also release
 * all data buffers @a sMsg is referring to. @a sMsg has to be created
 * by @ref dupSlurmMsg().
 *
 * @warning If @a sMsg is not the results of a call to @ref
 * dupSlurmMsg() the result is undefined and might lead to major
 * memory inconsistencies.
 *
 * @param sMsg SLURM messages to release
 *
 * @return No return value
 */
void releaseSlurmMsg(Slurm_Msg_t *sMsg);

/**
 * @brief Initialize a Slurm message header
 *
 * @param head The header to initialize
 */
void initSlurmMsgHead(Slurm_Msg_Header_t *head);

/**
 * @brief Free a Slurm message header
 *
 * @param head The header to free
 */
void freeSlurmMsgHead(Slurm_Msg_Header_t *head);

/**
 * @brief Send a Slurm message
 *
 * Generate a Slurm message header and set the given message type.
 * Assemble the Slurm message starting with the header followed by
 * the munge authentication and finalized with the give message body.
 * The message is send out using the provided socket. If the socket is
 * lesser 0 a new TCP connection to the slurmctld will be opened and used
 * to send the message.
 *
 * @param sock The socket file descriptor
 *
 * @param type The Slurm message type to send
 *
 * @param body The message body to send
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the number of bytes written or -1 on error
 */
int __sendSlurmMsg(int sock, slurm_msg_type_t type, PS_DataBuffer_t *body,
		    const char *caller, const int line);

#define sendSlurmMsg(sock, type, body) \
    __sendSlurmMsg(sock, type, body, __func__, __LINE__)

/**
 * @brief Send a Slurm message
 *
 * Assemble the Slurm message starting with the header followed by
 * the munge authentication and finalized with the give message body.
 * The message is send out using the provided socket. If the socket is
 * lesser 0 a new TCP connection to the slurmctld will be opened and used
 * to send the message.
 *
 * @param sock The socket file descriptor
 *
 * @param head The message header to send
 *
 * @param body The message body to send
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the number of bytes written or -1 on error
 */
int __sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_DataBuffer_t *body,
			const char *caller, const int line);

#define sendSlurmMsgEx(sock, head, body) \
    __sendSlurmMsgEx(sock, head, body, __func__, __LINE__)

/**
 * @brief Save result of a fowarded message
 *
 * @param sMsg The forwarded message to save
 *
 * @param error Error code to save
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 */
void __saveFrwrdMsgRes(Slurm_Msg_t *sMsg, uint32_t error, const char *func,
		       const int line);

#define saveFrwrdMsgRes(sMsg, error) \
    __saveFrwrdMsgRes(sMsg, error, __func__, __LINE__);

/**
 * @brief Handle a broken connection
 *
 * If the connection which broke has outstanding forwarded
 * messages they will be cancelled.
 *
 * @param nodeID The ID of the broken node
 */
void handleBrokenConnection(PSnodes_ID_t nodeID);

/**
 * @brief Start listen for Slurm connections
 *
 * @param port The port to listen to
 *
 * @return Returns the listen socket or -1 on error
 */
int openSlurmdSocket(int port);

/**
 * @brief Close Slurm listen socket
 */
void closeSlurmdSocket(void);

/**
 * @brief Convert a Slurm message type from integer
 * to string representation
 *
 * @param type The message type to convert
 *
 * @return Returns the result or an empty string on error
 */
const char *msgType2String(int type);

/**
 * @brief Read a bitstring from buffer
 *
 * Read a bit string from the provided data buffer.
 * The memory is allocated using umalloc(). The caller is responsible
 * to free the memory using ufree().
 *
 * @param ptr Data buffer to read from
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the result or NULL on error.
 */
char *__getBitString(char **ptr, const char *func, const int line);

#define getBitString(ptr) __getBitString(ptr, __func__, __LINE__)

/**
 * @brief Convert a hex bitstring to a comma separated list.
 *
 * To start with an empty list the caller needs to initialize
 * list and listSize with zero.The list is grown using @ref __umalloc()
 * and @ref __urealloc. The caller is responsible to free the memory
 * using @ref ufree().
 *
 * @param bitstr The bitstring to convert
 *
 * @para list The list holding the result
 *
 * @param listSize The size of the list
 *
 * @return Returns true on success otherwise false
 */
bool hexBitstr2List(char *bitstr, char **list, size_t *listSize);

/**
 * @brief Open a TCP connection
 *
 * Open a new TCP connection to the provided address and port.
 * Currently a maximum of 10 connections retries are attempted.
 *
 * @param addr The address to connect to
 *
 * @param port The port to connect to
 *
 * @return Returns the connected socket or -1 on error
 */
int tcpConnect(char *addr, char *port);

/**
 * @brief Open a TCP connection
 *
 * Open a new TCP connection to the provided address and port.
 * Currently a maximum of 10 connections retries are attempted.
 *
 * @param addr The address to connect to
 *
 * @param port The port to connect to
 *
 * @return Returns the connected socket or -1 on error
 */
int tcpConnectU(uint32_t addr, uint16_t port);

/**
 * @brief Open a control connection to srun
 *
 * @param step The step to open a connection for
 *
 * @return Returns the connected socket or -1 on error
 */
int srunOpenControlConnection(Step_t *step);

/**
 * @brief Open an I/O connection to srun
 *
 * @param step The step to open a connection for
 *
 * @param addr The address to connect to
 *
 * @param port The port to connect to
 *
 * @param sig Slurm I/O signature
 *
 * @return Returns the connected socket or -1 on error
 */
int srunOpenIOConnectionEx(Step_t *step, uint32_t addr, uint16_t port,
			    char *sig);

#define srunOpenIOConnection(step, sig) \
    srunOpenIOConnectionEx(step, 0, 0, sig)

/**
 * @brief Open a PTY connection to srun
 *
 * @param step The step to open a connection for
 *
 * @return Returns the connected socket or -1 on error
 */
int srunOpenPTYConnection(Step_t *step);

/**
 * @brief Start processing I/O messages from srun
 *
 * Process message from the I/O socket connected
 * to srun. Additionally close the stdin for
 * ranks requested by the user.
 *
 * @param step The step to enable the I/O for
 */
void srunEnableIO(Step_t *step);

/**
 * @brief Send an I/O message to srun
 *
 * @param type The I/O type of the message
 *
 * @param taskid The source task ID
 *
 * @param step The step to send the I/O message
 *
 * @param buf The buffer to send
 *
 * @param bufLen The length of the buffer
 *
 * @return Returns the number of bytes written or -1 on error
 */
int srunSendIO(uint16_t type, uint16_t taskid, Step_t *step, char *buf,
		uint32_t bufLen);

/**
 * @brief Send an I/O message to srun
 *
 * @param type The I/O type of the message
 *
 * @param taskid The source task ID
 *
 * @param sock The socket file descriptor
 *
 * @param buf The buffer to send
 *
 * @param bufLen The length of the buffer
 *
 * @return Returns the number of bytes written or -1 on error
 */
int srunSendIOEx(int sock, Slurm_IO_Header_t *ioh, char *buf, int *error);

/**
 * @brief Send a message to srun
 *
 * Open a new control connection to srun if the sock argument is
 * smaller than 0 or use the already connected socket otherwise.
 * Send a message with the provided type and message body.
 * The response from srun will be handled by handleSrunMsg().
 *
 * @param sock The socket file descriptor
 *
 * @param step The step to send the message
 *
 * @param type The Slurm message type
 *
 * @param body The message body to send
 *
 * @return Returns the number of bytes written or -1 on error
 */
int srunSendMsg(int sock, Step_t *step, slurm_msg_type_t type,
		PS_DataBuffer_t *body);

/**
 * @brief Handle a message from srun
 *
 * @param sock The socket file descriptor
 *
 * @param data Pointer holding the step
 *
 * @return Always returns 0
 */
int handleSrunMsg(int sock, void *data);

/**
 * @brief Close all connections associated
 * with the provided step
 *
 * @param step The step to close the connections for
 */
void closeAllStepConnections(Step_t *step);

#endif  /* __PSSLURM_COMM */
