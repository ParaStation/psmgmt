/*
 * ParaStation
 *
 * Copyright (C) 2014-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2023 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_COMM
#define __PSSLURM_COMM

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include "list.h"
#include "pscpu.h"
#include "psnodes.h"
#include "psserial.h"
#include "pluginmalloc.h"

#include "slurmmsg.h"
#include "psslurmio.h"
#include "psslurmmsg.h"
#include "psslurmstep.h"

/** default slurmctld port */
#define PSSLURM_SLURMCTLD_PORT "6817"

/** magic socket to open a new connection to slurmctld */
#define SLURMCTLD_SOCK -1

/** magic UID to allow any user to decode the munge credentail */
#define RES_UID_ANY -1

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

/** structure holding connection management data */
typedef struct {
    list_t next;	    /**< used to put into connection-list */
    PS_DataBuffer_t data;   /**< buffer for received message parts */
    Connection_CB_t *cb;    /**< function to handle received messages */
    void *info;		    /**< additional info passed to callback */
    int sock;		    /**< socket of the connection */
    time_t recvTime;	    /**< time first complete message was received */
    struct timeval openTime;/**< time the connection was opened */
    bool readSize;	    /**< true if the message size was read */
    Msg_Forward_t fw;	    /**< message forwarding structure */
    Step_t *step;	    /**< set if connection is associated to a step */
    bool authByInMsg;       /**< connection validation by incoming message */
} Connection_t;

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
		   uid_t uid, const char *caller, const int line);

#define sendSlurmMsg(sock, type, body, uid) \
    __sendSlurmMsg(sock, type, body, uid, __func__, __LINE__)

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
 * @param req Request associated to this message if any
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the number of bytes written, -1 on error or -2 if
 * the message was stored and will be send out later
 */
int __sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_SendDB_t *body,
		     Req_Info_t *req, const char *caller, const int line);

#define sendSlurmMsgEx(sock, head, body, req)			\
    __sendSlurmMsgEx(sock, head, body, req, __func__, __LINE__)

/**
 * @brief Send a RPC request to the slurmctld
 *
 * Sends a request to the slurmctld and let the default
 * handler @ref handleSlurmctldReply() process the response.
 * An additional callback req->cb can be specified in the request structure
 * @req to handle expected response message type specified by
 * req->expRespType.
 *
 * The data is packed by calling the corresponding pack function for the
 * message type specified in the request information. The caller is responsible
 * to ensure the message type matches with the data to pack.
 * Also see @ref packSlurmReq().
 *
 * The request has to be allocated using ucalloc() and will be freed
 * automatically after use even in case of error
 *
 * @param req The request meta information
 *
 * @param data The request data to pack
 *
 * @param caller Function name of the calling function
 *
 * @param line Line number where this function is called
 *
 * @return Returns the number of bytes written, -1 on error or -2 if
 * the message was stored and will be send out later
 */
int __sendSlurmctldReq(Req_Info_t *req, void *data,
		       const char *caller, const int line);

#define sendSlurmctldReq(req, data) \
    __sendSlurmctldReq(req, data, __func__, __LINE__)

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
 * @brief Converter function of hexBitstr2List()
 *
 * A converter function for @ref hexBitstr2List(). The function will
 * be called for every value to be added to the list. It is currently
 * used to map the logical CPU representation from Slurm to a physical
 * CPU of the node.
 *
 * If the converter function returns -1 the current value is skipped
 * and will not be added to the list. The conversion process will
 * continue with the next value of the bitstring.
 *
 * @return Returns the converted value or -1 on error.
 */
typedef int32_t hexBitStrConv_func_t(int32_t);

/**
 * @brief Convert a hex bitstring to a comma separated list.
 *
 * This function is a wrapper for @ref hexBitstr2ListEx(). See
 * documentation there for further information.
 *
 * @param bitstr The bitstring to convert
 *
 * @param strBuf The list holding the result
 *
 * @param range If true compact the list using range syntax
 *
 * @return Returns true on success otherwise false
 */
bool hexBitstr2List(char *bitstr, StrBuffer_t *strBuf, bool range);

/**
 * @brief Convert a hex bitstring to a comma separated list.
 *
 * The provided bitstring @a bitstr is converted to a comma seperated
 * list to be stored in @a strBuf. The list is initialized at
 * start. If the @a range option is set to true the values are
 * compacted into range syntax. Otherwise every single value will lead
 * to an entry in the list. The convert function @a conv will be
 * called for every value before it is added.
 *
 * The list @a strBuf is grown using @ref __umalloc() and @ref
 * __urealloc(). The caller is responsible to free the memory using
 * @ref ufree().
 *
 * @param bitstr The bitstring to convert
 *
 * @param strBuf The list holding the result
 *
 * @param range If true compact the list using range syntax
 *
 * @param conv A convert function or NULL
 *
 * @return Returns true on success otherwise false
 */
bool hexBitstr2ListEx(char *bitstr, StrBuffer_t *strBuf, bool range,
		      hexBitStrConv_func_t *conv);

/**
 * @brief Convert a hex bitstring to a PSCPU_set_t
 *
 * @param bitstr The bitstring to convert
 *
 * @param set The set to store the hex bitstring to
 *
 * @return Returns true on success otherwise false
 */
bool hexBitstr2Set(char *bitstr, PSCPU_set_t set);

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
 * @param grank Source's global rank
 *
 * @param step The step to send the I/O message
 *
 * @param buf The buffer to send
 *
 * @param bufLen The length of the buffer
 *
 * @return Returns the number of bytes written or -1 on error
 */
int srunSendIO(uint16_t type, uint16_t grank, Step_t *step, char *buf,
		uint32_t bufLen);

/**
 * @brief Send an I/O message to srun
 *
 * @param sock The socket file descriptor
 *
 * @param ioh Slurm I/O header
 *
 * @param buf The buffer to send
 *
 * @param error If an error occurred it will hold the errno
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
 * The response from srun will be handled by @ref handleSrunMsg().
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
 * @brief Handle an I/O message from srun
 *
 * @param sock The socket file descriptor
 *
 * @param stepPtr Pointer holding the step
 *
 * @return Always returns 0
 */
int handleSrunIOMsg(int sock, void *stepPtr);

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
 * @param info Additional info passed to the callback
 *
 * @return Returns the connected socket or -1 on error.
 */
int openSlurmctldCon(void *info);

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

/**
 * @brief Register a Slurm socket
 *
 * Add a Slurm connection for a socket and monitor it for incoming
 * data.
 *
 * @param sock The socket to register
 *
 * @param cb The function to call to handle received messages
 *
 * @param info Pointer to additional information passed to @a cb. The
 * memory for @a info has to be allocated dynamically and will get
 * freed using @ref ufree(). Responsibility on @a info is passed to
 * the connection only upon success. Otherwise the caller remains
 * responsible.
 *
 * @return Returns the associated connection on success or NULL otherwise
 */
Connection_t *registerSlurmSocket(int sock, Connection_CB_t *cb, void *info);

/**
 * @brief Convert a Slurm return code to string
 *
 * @param rc The Slurm return code to convert
 *
 * @return Returns the requested return code as string
 */
const char *slurmRC2String(int rc);

/**
 * @brief Find a connection identified by a step
 *
 * @param Step The step associated with the connection to find
 *
 * @return On success the requested connection is returned or NULL otherwise
 */
Connection_t *findConnectionByStep(Step_t *step);

/**
 * @brief Visitor function
 *
 * Visitor function used by @ref Connection_traverse() in order to visit
 * each connection currently registered.
 *
 * The parameters are as follows: @a conn points to the connection to
 * visit. @a info points to the additional information passed to @ref
 * Connection_traverse() in order to be forwarded to each connection.
 *
 * If the visitor function returns true the traversal will be
 * interrupted and @ref Connection_traverse() will return to its calling
 * function.
 */
typedef bool ConnectionVisitor_t(Connection_t *conn, const void *info);

/**
 * @brief Traverse all connections
 *
 * Traverse all connections by calling @a visitor for each of the
 * registered connections. In addition to a pointer to the current
 * connection @a info is passed as additional information to @a
 * visitor.
 *
 * If @a visitor returns true, the traversal will be stopped
 * immediately and true is returned to the calling function.
 *
 * @param visitor Visitor function to be called for each connection
 *
 * @param info Additional information to be passed to @a visitor while
 * visiting the connections
 *
 * @return If the visitor returns true, traversal will be stopped and
 * true is returned. If no visitor returned true during the traversal,
 * false is returned.
 */
bool Connection_traverse(ConnectionVisitor_t visitor, const void *info);

#endif  /* __PSSLURM_COMM */
