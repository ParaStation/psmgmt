/*
 * ParaStation
 *
 * Copyright (C) 2014-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_COMM
#define __PSSLURM_COMM

#include "psslurmjob.h"
#include "psslurmio.h"
#include "psserial.h"
#include "psslurmmsg.h"
#include "psslurmauth.h"

#define SLURMCTLD_SOCK -1

/** callback function of a connection structure */
typedef int Connection_CB_t(Slurm_Msg_t *msg, void *info);

/** structure to track message forwarding for a connection */
typedef struct {
    PSnodes_ID_t *nodes;	/** all nodes the message is forwarded to
				    (including myself) */
    uint32_t nodesCount;	/** size of the nodes array */
    uint32_t numRes;		/** number of forwarded message results */
    PS_DataBuffer_t body;	/** message body holding local result */
    Slurm_Msg_Header_t head;	/** header with saved results for
				    each forwarded node */
} Msg_Forward_t;

/**
 * @brief Initialize the Slurm communication facility
 *
 * Initialize the facility handling communication of the Slurm
 * protocol. This includes opening the slurmd socket for
 * srun/slurmctld communication and sending an initial
 * node registration request to the slurmctld. For this
 * all slurmctl daemons are extracted from the configuration
 * and there PS node ID is resolved.
 *
 * @return On success true is returned or false otherwise
 */
bool initSlurmCon(void);

/**
 * @brief Close all Slurm connections and free used memory
 */
void clearSlurmCon(void);

/**
 * @brief Get the host index
 *
 * @param id The PS node ID to get the index for
 *
 * @return Returns the requested host index on success
 * or -1 otherwise
 */
int getCtlHostIndex(PSnodes_ID_t id);

/**
 * @brief Get the PS node ID by index
 *
 * @param index The index to get the PS node ID for
 *
 * @return Returns the requested PS node ID on success
 * or -1 otherwise
 */
PSnodes_ID_t getCtlHostID(int index);

/**
 * @brief Close a Slurm connection
 *
 * Close a Slurm connection and free used memory.
 *
 * @param socket The socket for the connection to close
 */
void closeSlurmCon(int socket);

/**
 * @brief Send a Slurm message
 *
 * Generate a Slurm message header and set the given message type.
 * Assemble the Slurm message starting with the header followed by
 * the munge authentication and finalized with the given message body.
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
int __sendSlurmMsg(int sock, slurm_msg_type_t type, PS_SendDB_t *body,
		    const char *caller, const int line);

#define sendSlurmMsg(sock, type, body) \
    __sendSlurmMsg(sock, type, body, __func__, __LINE__)

/**
 * @brief Send a Slurm message
 *
 * Assemble the Slurm message starting with the header followed by
 * the munge authentication and finalized with the given message body.
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
 * @return Returns the number of bytes written, -1 on error or -2 if
 * the message was stored and will be send out later
 */
int __sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_SendDB_t *body,
		     const char *caller, const int line);

#define sendSlurmMsgEx(sock, head, body) \
    __sendSlurmMsgEx(sock, head, body, __func__, __LINE__)

/**
 * @brief Send a RPC request to the slurmctld
 *
 * @param type The RPC type to send
 *
 * @param body The message body
 *
 * @param cb The callback to handle a response
 *
 * @param info Additional information passed to the callback
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the number of bytes written, -1 on error or -2 if
 * the message was stored and will be send out later
 */
int __sendSlurmReq(slurm_msg_type_t type, PS_SendDB_t *body,
		   Connection_CB_t *cb, void *info, const char *caller,
		   const int line);

#define sendSlurmReq(type, body, cb, info) \
    __sendSlurmReq(type, body, cb, info, __func__, __LINE__)

/**
 * @brief Send a PS data buffer
 *
 * @param sock The socket file descriptor
 *
 * @param data The data buffer to send
 *
 * @param offset Number of bytes skipped at the beginning of data
 *
 * @param written Total number of bytes written upon return
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the number of bytes written or -1 on error. In the
 * latter cases the number of bytes written anyhow is reported in @a written.
 */
int __sendDataBuffer(int sock, PS_SendDB_t *data, size_t offset,
		     size_t *written, const char *caller, const int line);

#define sendDataBuffer(sock, data, offset, written) \
    __sendDataBuffer(sock, data, offset, written, __func__, __LINE__)

/**
 * @brief Handle the result of a forwarded RPC message
 *
 * This function will only be used on the root node of the forwarding tree.
 * If a new Slurm RPC message enables forwarding, handleFrwrdMsgReply() will
 * collect and handle the results of the foward process. The sMsg will hold
 * the information where the RPC was executed and error will hold the result.
 * The forward process is tracked in the connection object from the original
 * RPC request. If all RPC results from the involved nodes were collected the
 * original RPC is answered holding the results from all nodes imbedded
 * in the message header.
 *
 * @param sMsg The forwarded message to save
 *
 * @param error Error code to save
 *
 * @param func Function name of the calling function
 *
 * @param line Line number where this function is called
 */
void __handleFrwrdMsgReply(Slurm_Msg_t *sMsg, uint32_t error, const char *func,
			   const int line);

#define handleFrwrdMsgReply(sMsg, error) \
    __handleFrwrdMsgReply(sMsg, error, __func__, __LINE__);

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
int srunSendIOEx(int sock, IO_Slurm_Header_t *ioh, char *buf, int *error);

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
		PS_SendDB_t *body);

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

/**
 * @brief Open a new connection to slurmctld
 *
 * Open a new connection to the slurmctld. If the connection
 * can't be established a new connection attempt to the backup
 * controller is made. On success a selector to the function
 * handleSlurmctldReply() is registered for the connected socket.
 *
 * This is basically a wrapper for @ref openSlurmctldConEx().
 *
 * @return Returns the connected socket or -1 on error.
 */
int openSlurmctldCon(void);

/**
 * @brief Open a new connection to slurmctld
 *
 * Open a new connection to the slurmctld. If the connection
 * can't be established a new connection attempt to the backup
 * controller is made. On success a selector to the connection
 * callback @a cb is registered for the connected socket.
 * Additional information is passed to the callback via @a
 * info.
 *
 * @param cb Callback to handle a reply from the slurmctld
 *
 * @param info Additional info passed to the callback
 *
 * @return Returns the connected socket or -1 on error.
 */
int openSlurmctldConEx(Connection_CB_t *cb, void *info);

#endif  /* __PSSLURM_COMM */
