/**********************************************************
 *                  ParaStation3
 *
 *       Copyright (c) 2002 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * PSPort: Communication Library for Parastation
 *
 * $Id: psport.h,v 1.30 2003/04/03 13:40:06 hauke Exp $
 *
 * @author
 *         Jens Hauke <hauke@par-tec.de>
 *         Thomas Moschny <moschny@ipd.uni-karlsruhe.de>
 * @file
 ***********************************************************/

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
 * Handle for a send or receive request.
 *
 * @see PSP_ISend().
 */
typedef struct PSP_RequestH * PSP_RequestH_t;

/**
 * Routines that do not return a handle or something similar, are
 * declared to return @ref PSP_Err_t to indicate errors.
 */
typedef enum {
  PSP_OK = 0,                /**< no error, operation successful */
  PSP_UNSPECIFIED_ERR = -1,  /**< there was some error, but we don't
				know the details */
  PSP_WRONG_ARG = -2,        /**< one of the arguments is invalid */
  PSP_CANCEL_FAILED = -3     /**< A @ref PSP_Cancel() failed */
} PSP_Err_t;

/**
 * The @ref PSP_Test() and @ref PSP_Wait() routines show the
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
  PSP_CANCELED = 2,          /**< request is complete (but the send or
				receive operation was canceled) */
  PSP_SHORTREAD = 4          /**< request is complete (but the receive
				operation truncate the message) */
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
    PSHALRecvHeader_t	HALHeader;   /**< the header of the HAL layer */
    PSP_MessageID_t	MessageID;   /**< the unique message ID */
    UINT32		FragOffset;  /**< the offset of this fragment */
    UINT32		MessageSize; /**< the size of this message */
    long		xheader[zeroarray];  /**< from here on, the extra
					header is placed */
} PSP_RecvHeader_t;

/**
 * General header to be used for send or receive requests.
 */
typedef struct PSP_Header_T {
    int                 state;       /**< the status */
    unsigned            xheaderlen;  /**< the length of the extra header,
					read-only. */
    unsigned            datalen;     /**< the lenght of the message data,
					read-only. */
    int			_space1_;    /**< align HALHeader to 8 byte on alpha */
    PSHALRecvHeader_t   HALHeader;   /**< the header of the HAL layer */
    PSP_MessageID_t     MessageID;   /**< the unique message ID */
    UINT32              FragOffset;  /**< the offset of this fragment */
    UINT32              MessageSize; /**< the size of this message */
    long		xheader[zeroarray];  /**< from here on, the extra
					header is placed */
} PSP_Header_t;

/**
 * Type of the callback to be passed to @ref PSP_IReceive().
 */
typedef int (PSP_RecvCallBack_t)
     (PSP_RecvHeader_t *header, unsigned xheaderlen, void *param);

/**
 * Type of the callback that is executed upon finishing a send or
 * receive request.
 *
 * @see PSP_IsendCB() and PSP_IReceiveCB().
 */
typedef void (PSP_DoneCallback_t)
     (PSP_RequestH_t req, void *param);

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
 * @return Returns 0 if the initialization was successful and
 * an error code otherwise.
 */
int PSP_Init(void);

/* ----------------------------------------------------------------------
 * PSP_HWList()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Get a list of supportet hardwaretypes.
 *
 * No parameters.
 *
 * @return Returns a NULL terminated list of strings with the names of
 * supported hardwaretypes.
 */
char **PSP_HWList(void);

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
 * port-number is not available. If portno is @ref PSP_ANYPORT, the
 * allocated port will get an arbitrary port-number. The actual number
 * can then be obtained from @ref PSP_GetPortNo()). The range for valid
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
 * @param porth handle of the port, from @ref PSP_OpenPort()
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
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @return Returns @ref PSP_OK if the port could be closed and (maybe) an
 * error code otherwise. A diagnostic message might have been written
 * to stderr.
 */
PSP_Err_t PSP_ClosePort(PSP_PortH_t porth);

/* ----------------------------------------------------------------------
 * PSP_RecvFrom(), PSP_RecvAny()
 * ----------------------------------------------------------------------
 */

/**
 * Already existing call-back functions for @ref PSP_IReceive().
 *
 */
PSP_RecvCallBack_t PSP_RecvAny;  /**< Receive from any sender */
PSP_RecvCallBack_t PSP_RecvFrom; /**< Receive from a certain sender */

/**
 *  Parameter for @ref PSP_RecvFrom().
 */
typedef struct PSP_RecvFrom_Param_T{
  INT16 srcnode;   /**< the source node */
  INT16 srcport;   /**< the source port */
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
 * be greater than) sizeof(@ref PSP_Header_t). It is up to the user to
 * provide enough room for any extra header data that might be
 * contained in the message (at least sizeof(@ref PSP_Header_t) +
 * @a xheaderlen).
 *
 * Both buffers must be valid until the receive request is completed
 * (see @ref PSP_Wait(), @ref PSP_Test() and @ref PSP_Cancel()) and
 * may not be used by the user for that time.
 *
 * A call-back function that is used to determine for a newly arrived
 * message whether it should be received by this receive-request can
 * be specified. Not more than buflen bytes are received from one
 * message.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param buf address of buffer for message data
 * @param buflen length of message data buffer, in bytes
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb call-back function that will be used to determine whether
 * a certain message is to be received by this receive-request
 * @param cp_param this pointer is passed to the call-back function
 * @param dcb call-back function that will be called upon completion
 * of the receive request
 * @param dcb_param this pointer is passed to dcb
 * @param sender one can specify the sender (node number) of the
 * message to be received here if already known, or @ref PSP_AnySender
 * @return Returns a handle for the request or NULL if there is an
 * error. The handle can be passed to @ref PSP_Test() and @ref PSP_Wait().
 *
 * @see PSP_Wait(), PSP_Test() and PSP_Cancel()
 */
PSP_RequestH_t PSP_IReceiveCBFrom(PSP_PortH_t porth,
				  void* buf, unsigned buflen,
				  PSP_Header_t* header, unsigned xheaderlen,
				  PSP_RecvCallBack_t* cb, void* cb_param,
				  PSP_DoneCallback_t* dcb, void* dcb_param,
				  int sender);

/** Any sender */
#define PSP_AnySender -1

/**
 * @brief Start a non-blocking receive.
 *
 * With this call, the communication library is prepared to receive a
 * message asynchronously from the application. @e Two buffers must be
 * provided: one for the message itself and one for the header data.
 *
 * The length of the memory header buffer has to be equal to (and may
 * be greater than) sizeof(@ref PSP_Header_t). It is up to the user to
 * provide enough room for any extra header data that might be
 * contained in the message (at least sizeof(@ref PSP_Header_t) +
 * @a xheaderlen).
 *
 * Both buffers must be valid until the receive request is completed
 * (see @ref PSP_Wait(), @ref PSP_Test() and @ref PSP_Cancel()) and
 * may not be used by the user for that time.
 *
 * A call-back function that is used to determine for a newly arrived
 * message whether it should be received by this receive-request can
 * be specified. Not more than buflen bytes are received from one
 * message.
 *
 * If the sender of the message to be received (i.e. it's node number)
 * is already kown, it can be specified in the @ref
 * PSP_IReceiveCBFrom() call. Fewer messages headers have to be
 * reviewed by the callback function then.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param buf address of buffer for message data
 * @param buflen length of message data buffer, in bytes
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb call-back function that will be used to determine whether
 * a certain message is to be received by this receive-request
 * @param cp_param this pointer is passed to the call-back function
 * @param dcb call-back function that will be called upon completion
 * of the receive request
 * @param dcb_param this pointer is passed to dcb
 * @return Returns a handle for the request or NULL if there is an
 * error. The handle can be passed to @ref PSP_Test() and @ref PSP_Wait().
 *
 * @see PSP_Wait(), PSP_Test() and PSP_Cancel()
 */
static inline
PSP_RequestH_t PSP_IReceiveCB(PSP_PortH_t porth,
			      void* buf, unsigned buflen,
			      PSP_Header_t* header, unsigned xheaderlen,
			      PSP_RecvCallBack_t* cb, void* cb_param,
			      PSP_DoneCallback_t* dcb, void* dcb_param)
{
    return PSP_IReceiveCBFrom(porth, buf, buflen, header, xheaderlen,
			      cb, cb_param, dcb, dcb_param, PSP_AnySender);
}

/**
 * @brief Start a non-blocking receive.
 *
 * With this call, the communication library is prepared to receive a
 * message asynchronously from the application. @e Two buffers must be
 * provided: one for the message itself and one for the header data.
 *
 * The length of the memory header buffer has to be equal to (and may
 * be greater than) sizeof(@ref PSP_Header_t). It is up to the user to
 * provide enough room for any extra header data that might be
 * contained in the message (at least sizeof(@ref PSP_Header_t) +
 * @a xheaderlen).
 *
 * Both buffers must be valid until the receive request is completed
 * (see @ref PSP_Wait(), @ref PSP_Test() and @ref PSP_Cancel()) and
 * may not be used by the user for that time.
 *
 * A call-back function that is used to determine for a newly arrived
 * message whether it should be received by this receive-request can
 * be specified. Not more than buflen bytes are received from one
 * message.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param buf address of buffer for message data
 * @param buflen length of message data buffer, in bytes
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb call-back function that will be used to determine whether
 * a certain message is to be received by this receive-request
 * @param cp_param this pointer is passed to the call-back function
 * @param sender one can specify the sender (node number) of the
 * message to be received here if already known, or @ref PSP_AnySender
 * @return Returns a handle for the request or NULL if there is an
 * error. The handle can be passed to @ref PSP_Test() and @ref PSP_Wait().
 *
 * @see PSP_Wait(), PSP_Test() and PSP_Cancel()
 */
static inline
PSP_RequestH_t PSP_IReceiveFrom(PSP_PortH_t porth,
				void* buf, unsigned buflen,
				PSP_Header_t* header, unsigned xheaderlen,
				PSP_RecvCallBack_t* cb, void* cb_param,
				int sender)
{
    return PSP_IReceiveCBFrom(porth, buf, buflen, header, xheaderlen,
			      cb, cb_param, NULL, NULL, sender);
}

/**
 * @brief Start a non-blocking receive.
 *
 * With this call, the communication library is prepared to receive a
 * message asynchronously from the application. @e Two buffers must be
 * provided: one for the message itself and one for the header data.
 *
 * The length of the memory header buffer has to be equal to (and may
 * be greater than) sizeof(@ref PSP_Header_t). It is up to the user to
 * provide enough room for any extra header data that might be
 * contained in the message (at least sizeof(@ref PSP_Header_t) +
 * @a xheaderlen).
 *
 * Both buffers must be valid until the receive request is completed
 * (see @ref PSP_Wait(), @ref PSP_Test() and @ref PSP_Cancel()) and
 * may not be used by the user for that time.
 *
 * A call-back function that is used to determine for a newly arrived
 * message whether it should be received by this receive-request can
 * be specified. Not more than buflen bytes are received from one
 * message.
 *
 * If the sender of the message to be received (i.e. it's node number)
 * is already kown, it can be specified in the @ref PSP_IReceiveFrom()
 * call. Fewer messages headers have to be reviewed by the callback
 * function then.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param buf address of buffer for message data
 * @param buflen length of message data buffer, in bytes
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb call-back function that will be used to determine whether
 * a certain message is to be received by this receive-request
 * @param cp_param this pointer is passed to the call-back function
 * @return Returns a handle for the request or NULL if there is an
 * error. The handle can be passed to @ref PSP_Test() and @ref PSP_Wait().
 *
 * @see PSP_Wait(), PSP_Test() and PSP_Cancel()
 */
static inline
PSP_RequestH_t PSP_IReceive(PSP_PortH_t porth,
			    void* buf, unsigned buflen,
			    PSP_Header_t* header, unsigned xheaderlen,
			    PSP_RecvCallBack_t* cb, void* cb_param)
{
    return PSP_IReceiveCBFrom(porth, buf, buflen, header, xheaderlen,
			      cb, cb_param, NULL, NULL, PSP_AnySender);
}

/* ----------------------------------------------------------------------
 * PSP_IProbe()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Test for availability of a message.
 *
 * This call returns true if a message is already available that would
 * be received by a call to @ref PSP_IReceive() with the same callback
 * function and parameters. The header data of this message is copied
 * to the given header buffer, then.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb callback function
 * @param cb_param this pointer is passed to the callback function
 * @param sender one can specify the sender (node number) of the
 * message to be received here if already known, or @ref PSP_AnySender
 * @return Returns true if a matching message is available (already
 * received) and false otherwise.
 */
int PSP_IProbeFrom(PSP_PortH_t porth,
		   PSP_Header_t* header, unsigned xheaderlen,
		   PSP_RecvCallBack_t *cb, void* cb_param,
		   int sender);

/**
 * @brief Test for availability of a message.
 *
 * This call returns true if a message is already available that would
 * be received by a call to @ref PSP_IReceive() with the same callback
 * function and parameters. The header data of this message is copied
 * to the given header buffer, then.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb callback function
 * @param cb_param this pointer is passed to the callback function
 * @return Returns true if a matching message is available (already
 * received) and false otherwise.
 */
static inline
int PSP_IProbe(PSP_PortH_t porth,
	       PSP_Header_t* header, unsigned xheaderlen,
	       PSP_RecvCallBack_t *cb, void* cb_param)
{
    return PSP_IProbeFrom(porth, header, xheaderlen, cb, cb_param,
			  PSP_AnySender);
}

/* ----------------------------------------------------------------------
 * PSP_Probe()
 * ----------------------------------------------------------------------
 */

/**
 * @brief Wait for availability of a message.
 *
 * This call returns when and if a message is available that would be
 * received by a call to @ref PSP_IReceive() with the same callback
 * function and parameters. The header data of this message is copied
 * to the given header buffer.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb callback function
 * @param cb_param this pointer is passed to the callback function
 * @param sender one can specify the sender (node number) of the
 * message to be received here if already known, or @ref PSP_AnySender
 * @return Returns true.
 */
int PSP_ProbeFrom(PSP_PortH_t porth,
		  PSP_Header_t* header, unsigned xheaderlen,
		  PSP_RecvCallBack_t *cb, void* cb_param,
		  int sender);

/**
 * @brief Wait for availability of a message.
 *
 * This call returns when and if a message is available that would be
 * received by a call to @ref PSP_IReceive() with the same callback
 * function and parameters. The header data of this message is copied
 * to the given header buffer.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param header address of buffer for header data
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param cb callback function
 * @param cb_param this pointer is passed to the callback function
 * @return Returns true.
 */
static inline
int PSP_Probe(PSP_PortH_t porth,
	      PSP_Header_t* header, unsigned xheaderlen,
	      PSP_RecvCallBack_t *cb, void* cb_param)
{
    return PSP_ProbeFrom(porth, header, xheaderlen, cb, cb_param,
			 PSP_AnySender);
}

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
 * to be equal to or greater than sizeof(@ref
 * PSP_Header_t). @a xheaderlen extra header bytes are sent along with
 * the message.
 *
 * Both buffers (for message data and message header) have to be valid
 * and their contents may not be altered anymore until the send
 * request is completed (see @ref PSP_Wait(), @ref PSP_Test() and @ref
 * PSP_Cancel()).
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param buf address of buffer for message data
 * @param length of message data buffer, in bytes
 * @param header address of header data buffer
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param dest the destination node ID
 * @param destport the destination port number
 * @param dcb call-back function that will be called upon completion
 * of the send request
 * @param dcb_param this pointer is passed to dcb
 * @param flags additional flags can be specified here
 * @return Returns a handle for the request or Null if there is an
 * error.
 *
 * @note The returned request handle can be passed to @ref PSP_Test(),
 * @ref PSP_Wait() and @ref PSP_Cancel() and is valid as long as the
 * user-provided memory for the header data is valid and not altered
 * (e.g. reused for another send or receive request).
 *
 * @see PSP_Wait(), PSP_Test() and PSP_Cancel()
 */
PSP_RequestH_t PSP_ISendCB(PSP_PortH_t porth,
			   void* buf, unsigned buflen,
			   PSP_Header_t* header, unsigned xheaderlen,
			   int dest, int destport,
			   PSP_DoneCallback_t *dcb, void* dcb_param,
			   int flags);

/**
 * @brief Start a non-blocking send.
 *
 * With this call, the communication library is advised to send a
 * message asynchronously from the application. Two addresses must be
 * provided: one points to the data itself and one to the header
 * data.
 *
 * The memory for header must be provided by the user. Its length has
 * to be equal to or greater than sizeof(@ref
 * PSP_Header_t). @a xheaderlen extra header bytes are sent along with
 * the message.
 *
 * Both buffers (for message data and message header) have to be valid
 * and their contents may not be altered anymore until the send
 * request is completed (see @ref PSP_Wait(), @ref PSP_Test() and @ref
 * PSP_Cancel()).
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param buf address of buffer for message data
 * @param length of message data buffer, in bytes
 * @param header address of header data buffer
 * @param xheaderlen length of message extra header buffer, in bytes
 * @param dest the destination node ID
 * @param destport the destination port number
 * @param flags additional flags can be specified here
 * @return Returns a handle for the request or Null if there is an
 * error.
 *
 * @note The returned request handle can be passed to @ref PSP_Test(),
 * @ref PSP_Wait() and @ref PSP_Cancel() and is valid as long as the
 * user-provided memory for the header data is valid and not altered
 * (e.g. reused for another send or receive request).
 *
 * @see PSP_Wait(), PSP_Test() and PSP_Cancel()
 */
static inline
PSP_RequestH_t PSP_ISend(PSP_PortH_t porth,
			 void* buf, unsigned buflen,
			 PSP_Header_t* header, unsigned xheaderlen,
			 int dest, int destport, int flags)
{
    return PSP_ISendCB(porth, buf, buflen, header, xheaderlen,
		       dest, destport, NULL, NULL, flags);
}

/** Flag this message to be very important. */
#define PSP_MSGFLAG_HIGHPRIO PSHAL_MSGFLAG_HIGHPRIO

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
 * @note A call to @ref PSP_Wait() or @ref PSP_Test() is always
 * necessary to know whether the buffers for message data and message
 * header may be reused.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param request handle of the send or receive request
 * @return Returns @ref PSP_NOT_COMPLETE, if the send or receive
 * request has not completed yet and information the status of the
 * send/receive operation otherwise.
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
 * @note A call to @ref PSP_Wait() or @ref PSP_Test() is always
 * necessary to know whether the buffers for message data and message
 * header may be reused.
 *
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param request handle of the send or receive request
 * @return Returns information about the status of the send/receive
 * operation. Unlike @ref PSP_Test() this call does not return
 * @ref PSP_NOT_COMPLETE.
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
 * @param porth handle of the port, from @ref PSP_OpenPort()
 * @param request handle of the send or receive request
 * @return @ref PSP_OK on Success, @ref PSP_CANCEL_FAILED otherwise.
 */
PSP_Err_t PSP_Cancel(PSP_PortH_t portH, PSP_RequestH_t request);
    
#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* _PSPORT_H_ */

/*
 * Local Variables:
 *   mode: c
 *   c-basic-offset: 4
 *   c-font-lock-extra-types: ( "\\sw+_t" "UINT16" "UINT32" "INT16")
 * End:
 */
