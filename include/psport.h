/**
 * PSPort: Communication Library for Parastation
 *
 * $Id: psport.h,v 1.9 2001/06/07 13:00:03 moschny Exp $
 *
 * @author
 * Jens Hauke <hauke@par-tec.com>,
 * Thomas Moschny <moschny@ira.uka.de>
 *
 * @file
 */
#ifndef _PSPORT_H_
#define _PSPORT_H_

#include "pshal.h"

#ifdef __cplusplus
extern "C" {
#if 0
} // <- just for emacs indentation
#endif
#endif

/**
 * Handle to identify an open port.
 */
typedef struct PSP_PortH * PSP_PortH_t;

/**
 * Handle for a send or receive request. See PSP_ISend().
 */
typedef struct PSP_RequestH * PSP_RequestH_t;

/**
 * Routines that do not return a handle or something similar, are
 * declared to return PSP_Err_t to indicate errors.
 */
typedef enum {
  PSP_OK = 0,                /**< no error, operation successful */
  PSP_UNSPECIFIED_ERR = -1,  /**< there was some error, but we don't
				know the details */
  PSP_WRONG_ARG = -2,        /**< one of the arguments is invalid */
} PSP_Err_t;

/**
 * The PSP_Test(), PSP_Wait() and PSP_Cancel() routines show the
 * status of the request and, if the request is complete, the status
 * of the operation itself.
 *
 * @note The documentation differentiates between the (send or
 * receive) @e request and the (send or receive) @e operation. The
 * first term denotes the operations this library performs while
 * trying to send or receive something, whereas the latter stands for
 * the more abstract operation itself. The request is always
 * completed, but the operation may fail.
 */
typedef enum {
  PSP_NOT_COMPLETE = 0,      /**< request is pending */
  PSP_SUCCESS = 1,           /**< request is complete and the send or
				receive operation was successful */
  PSP_CANCELED = 2           /**< request is complete (but the send or
				receive operation was canceled) */
} PSP_Status_t;

/**
 * All fragments of one message carry the same MessageID. Normally the
 * user need not care about message id's, because fragments are
 * assembled by the library.
 */
typedef UINT16 PSP_MessageID_t;

/**
 * Receive header.
 */
typedef struct PSP_RecvHeader_T {
    PSHALRecvHeader_t	HALHeader;
    PSP_MessageID_t	MessageID;
    UINT32		FragOffset;
    UINT32		MessageSize;
    char		xheader[0];  /**< from here on, the extra
					header is placed */
} PSP_RecvHeader_t;

/**
 * General header to be used for send or receive requests.
 */
typedef struct PSP_Header_T {
    int                 state;
    unsigned            xheaderlen;  /**< len of the extra header,
					read-only. */
    unsigned            datalen;     /**< len of message data,
					read-only. */
    
    PSHALRecvHeader_t	HALHeader;
    PSP_MessageID_t     MessageID;
    UINT32              FragOffset;
    UINT32              MessageSize;
    char                xheader[0];  /**< from here on, the extra
					header is placed */
} PSP_Header_t;

/**
 * Type of the callback to be passed to PSP_IReceive().
 */
typedef int (PSP_RecvCallBack_t)
     (PSP_RecvHeader_t* header, unsigned xheaderlen, void *param);

/** Number of receives without recv request */
extern unsigned PSP_GenReqCount;
/** Number of uses of generated requests */
extern unsigned PSP_GenReqUsedCount;

/* ----------------------------------------------------------------------
 * PSP_Init()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Initialize the library.
 *
 * No parameters. This function must be called before any other call
 * to the library.
 *
 * @return Returns PSP_OK if the initialization was successful and
 * (maybe) an error code otherwise. Additionally, some diagnostics
 * might have been written to stderr.
 */
PSP_Err_t PSP_Init(void);

/* ----------------------------------------------------------------------
 * PSP_GetNodeID()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Get the ID of this node.
 *
 * Get the ParaStation ID of this node.
 * 
 * @return	NodeID	on success and
 * @return	-1	on error
 */
int PSP_GetNodeID(void);

/* ----------------------------------------------------------------------
 * PSP_OpenPort()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Open a communication port.
 *
 * In order to do any communication, a port must be opened. The port
 * can be addressed by its number from other nodes later.
 *
 * Note: Every port-number can only be used once on every node. It is
 * an error to do a fork() with open ports. This is different from
 * standard Unix behavior.
 * 
 * @param portno the desired port-number. The call will fail if this
 * port-number is not available. If portno is PSP_ANYPORT, the
 * allocated port will get an arbitrary port-number. The actual number
 * can then be obtained from PSP_GetPortNo()). The range for valid
 * port-numbers is 0..MAXINT.
 * @return Returns a handle for the port if open was successful and 0
 * otherwise. The port-handle must be passed to the communication calls.
 */
PSP_PortH_t PSP_OpenPort(int portno);
#define PSP_ANYPORT -1 /**< When used as a port-number, stands for any
			  port (wildcard). */

/* ----------------------------------------------------------------------
 * PSP_GetPortNo()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Get the number of an open port.
 *
 * The number of a previously opened port can be obtained by calling
 * this function with the port-handle.
 *
 * @param porth handle of the port, from PSP_OpenPort()
 * @return Returns the number of the port. A value <0 is returned, if
 * the number could not be determined, especially if the port-handle is
 * invalid. Currently, no error codes are returned.
 */
int PSP_GetPortNo(PSP_PortH_t porth);

/* ----------------------------------------------------------------------
 * PSP_ClosePort()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Close a port.
 *
 * A port can be closed if it is not used any longer. Outstanding
 * send/receive requests are canceled (implicitly) before the port is
 * really closed.
 *
 * @param porth handle of the port, from PSP_OpenPort()
 * @return Returns PSP_OK if the port could be closed and (maybe) an
 * error code otherwise. A diagnostic message might have been written
 * to stderr.
 */
PSP_Err_t PSP_ClosePort(PSP_PortH_t porth);

/* ----------------------------------------------------------------------
 * PSP_RecvFrom(), PSP_RecvAny()
 * ----------------------------------------------------------------------
 */

/**
 * Already existing call-back functions for PSP_IReceive().
 *
 */
PSP_RecvCallBack_t PSP_RecvAny;  /**< Receive from any sender */
PSP_RecvCallBack_t PSP_RecvFrom; /**< Receive from a certain sender */

/**
 *  Parameter for PSP_RecvFrom().
 */
typedef struct PSP_RecvFrom_Param_T{
  INT16 srcnode;
  INT16 srcport;
} PSP_RecvFrom_Param_t;

/* ----------------------------------------------------------------------
 * PSP_IReceive()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Start a non-blocking receive.
 *
 * With this call, the communication library is prepared to receive a
 * message asynchronously from the application. @e Two buffers must be
 * provided: one for the message itself and one for the header data.
 *
 * The length of the memory header buffer has to be equal to (and may
 * be greater than) sizeof(PSP_Header_t). It is up to the user to
 * provide enough room for any extra header data that might be
 * contained in the message. (at least sizeof(PSP_Header_t) + xheaderlen)
 * The whole header will never occupy more than PSP_MAX_HEADERSIZE bytes.
 *
 * Both buffers must be valid until the receive request is completed
 * (see PSP_Wait(), PSP_Test() and PSP_Cancel()) and may not be used
 * by the user for that time.
 *
 * A call-back function that is used to determine for a newly arrived
 * message whether it should be received by this receive-request can
 * be specified. Not more than buflen bytes are received from one
 * message.
 *
 * @param porth handle of the port, from PSP_OpenPort()
 * @param buf address of buffer for message data
 * @param buflen length of message data buffer, in bytes
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb call-back function that will be used to determine whether
 * a certain message is to be received by this receive-request
 * @param cp_param this pointer is passed to the call-back function
 * @return Returns a handle for the request or NULL if there is an
 * error. The handle can be passed to PSP_Test() and PSP_Wait().
 */
PSP_RequestH_t PSP_IReceive(PSP_PortH_t porth,
			    void* buf, unsigned buflen,
			    PSP_Header_t* header, unsigned xheaderlen,
			    PSP_RecvCallBack_t* cb, void* cb_param);

/* ----------------------------------------------------------------------
 * PSP_ISend()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Start a non-blocking send.
 *
 * With this call, the communication library is advised to send a
 * message asynchronously from the application. Two addresses must be
 * provided: one points to the data itself and one to the header
 * data.
 *
 * The memory for header must be provided by the user. Its length has
 * to be equal to or greater than sizeof(PSP_Header_t). xheaderlen extra
 * header bytes are sent along with the message.
 *
 * Both buffers (for message data and message header) have to be valid
 * and their contents may not be altered anymore until the send
 * request is completed (see PSP_Wait(), PSP_Test() and PSP_Cancel()).
 *
 * @param porth handle of the port, from PSP_OpenPort()
 * @param buf address of buffer for message data
 * @param length of message data buffer, in bytes
 * @param header address of header data buffer
 * @param xheaderlen length of message extra header buffer, in bytes
 * @return Returns a handle for the request or Null if there is an
 * error.
 *
 * @note The returned request handle can be passed to PSP_Test(),
 * PSP_Wait() and PSP_Cancel() and is valid as long as the
 * user-provided memory for the header data is valid and not altered
 * (e.g. reused for another send or receive request.)
 */
PSP_RequestH_t PSP_ISend(PSP_PortH_t porth,
			 void* buf, unsigned buflen,
			 PSP_Header_t* header, unsigned xheaderlen,
			 int dest, int destport);

/* ----------------------------------------------------------------------
 * PSP_Test()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Test for the completion of a non-blocking send/receive
 * request.
 *
 * This call can be used to test whether the processing of a send or
 * receive request has been completed or stopped. Note: The send or
 * receive operation itself might have been unsuccessful or canceled;
 * the return code provides information about that fact.
 *
 * @note A call to PSP_Wait() or PSP_Test() is always necessary to
 * know whether the buffers for message data and message header may be
 * reused.
 *
 * @param porth handle of the port, from PSP_OpenPort()
 * @param request handle of the send or receive request
 * @return Returns PSP_NOT_COMPLETE, if the send or receive request has
 * not completed yet and information the status of the send/receive
 * operation otherwise.
 */
PSP_Status_t PSP_Test(PSP_PortH_t porth, PSP_RequestH_t request);

/* ----------------------------------------------------------------------
 * PSP_Wait()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Wait for the completion of a non-blocking send/receive
 * request.
 *
 * This call can be used to wait until the processing of the send or
 * receive request specified via its request-handle has completed or
 * stopped. Note: The send or receive operation itself might have been
 * unsuccessful or canceled; the return code provides information
 * about that fact.
 *
 * @note A call to PSP_Wait() or PSP_Test() is always necessary to
 * know whether the buffers for message data and message header may be
 * reused.
 *
 * @param porth handle of the port, from PSP_OpenPort()
 * @param request handle of the send or receive request
 * @return Returns information about the status of the send/receive
 * operation. Unlike PSP_Test() this call doesn't return
 * PSP_NOT_COMPLETE.
 */
PSP_Status_t PSP_Wait(PSP_PortH_t porth, PSP_RequestH_t request);

/* ----------------------------------------------------------------------
 * PSP_Cancel()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Try to cancel a non-blocking send/receive request.
 *
 * The user can try to cancel a send or receive request, however,
 * there is no guaranty that this is successful. 
 *
 * @param porth handle of the port, from PSP_OpenPort()
 * @param request handle of the send or receive request
 * @return Returns information about the status of the send/receive
 * operation. If this call returns PSP_NOT_READY, the request could
 * not canceled.
 */
PSP_Status_t PSP_Cancel(PSP_PortH_t porth, PSP_RequestH_t request);
    
#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* _PSPORT_H_ */

/*
 * Local Variables:
 *   mode: c
 *   c-basic-offset: 4
 *   c-font-lock-extra-types: ( "\\sw+_t" "UINT16" "UINT32" )
 * End:
 */
