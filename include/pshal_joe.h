/*
 *
 *      @(#)pshal.h    1.00 (Karlsruhe) 08/15/2000
 *
 *      written by Joachim Blum
 *
 *
 * This the interface definition between the HAL (Hardware Abstraction Layer)
 * of ParaStation and the upper parts of the protocols.
 *
 *  History
 *
 *   000911 Joe Creation
 */
#ifndef __pshal_h
#define __pshal_h


#include <machine.h>
#include <ps-types.h>

/*------------------------------------------------------------------------------
 *  PSHALSendHeader_t  */
/**
 * Header passed to the PSHALMsgSend Routine to send messages.
 */
typedef struct PSHALSendHeader_T{
  char     protocol;    /**< used protocol                   */
  char     headerlen;   /**< length of the header. Data is at 
			   the end of the header             */
  INT16    datalen;     /**< length of the data              */
  INT16    dstport;     /**< destination port of the message */
  INT16    dstnode;     /**< destination node of the message */
  INT16    srcport;     /**< source port of the message      */
  INT16    srcnode;     /**< source node of the message      */
  INT16    tag;         /**< protocol specific used tag      */
}PSHALSendHeader_t;

/*------------------------------------------------------------------------------
 *  PSHALReceiveHeader_t  */
/**
 * Header returned by the PSHALMsgGet Routine to receive messages.
 * Elements stored in Network Byte order. Use ntoh*() functions to 
 * decard numbers.
 */
typedef struct PSHALReceiveHeader_T{
  char     protocol;    /**< used protocol                   */
  char     headerlen;   /**< length of the header            (HostByteOrder)*/
  INT16    datalen;     /**< length of the data              (HostByteOrder)*/
  INT16    dstport;     /**< destination port of the message (HostByteOrder)*/
  INT16    dstnode;     /**< destination node of the message (HostByteOrder)*/
  INT16    srcport;     /**< source port of the message      (HostByteOrder)*/
  INT16    srcnode;     /**< source node of the message      (HostByteOrder)*/
  INT16    tag;         /**< protocol specific used tag      */
  long     bufno;       /**< buffer number if the data is in a pinned buffer */
}PSHALReceiveHeader_t;


/*------------------------------------------------------------------------------
 * int PSHALFifoFree()
 */
/**
 * Check if a send buffer is available.
 *
 * @return  = 0 if no buffer is available<br>
 * @return  = 1 a buffer is available
 * 
 * This function checks if a buffer for sending is available.
 * As soon as a buffer is available PSHALMsgSend*() can be called.
 * To be thread save the PSHAL_SendLock must be held, otherwise
 * the internal datastructures can be corrupt.
 * The Lock should be hold until the data is send with PSHALMsgSend*().
 */
int PSHALFifoFree();


/*------------------------------------------------------------------------------
 * int PSHALMsgSend()
 */
/**
 * Sends a message to a destination.
 * @param 
 *        header : dstport,srcport, headerlen,datalen,protocolno,msgtag
 * @param
 *        data   : pointer to the data.
 *
 *
 * @return  < 0 on error<br>
 * @return  = 0 on success
 * 
 * This function sends a message to a destination. The length of data (header->datalen) 
 * is in maximum PSM_MAXMTU. Before calling
 * PSHALMsgSend() you must check whether a buffer is available 
 * with PSHALFifoFree().
 * It is not checked whether the areas a readable. 
 * SEGFAULT are possible if invalid parameters are given.
 * To be thread save the PSHAL_SendLock must be held, otherwise
 * the internal datastructures can be corrupt.
 */
int PSHALMsgSend(PSHALSendHeader_t* header,void*data);

/*------------------------------------------------------------------------------
 * int PSHALMsgSendPinned()
 */
/**
 * Sends a message to a destination from a pinned down memory.
 * @param 
 *        header : dstport,srcport, headerlen,datalen,protocolno,msgtag
 * @param
 *        bufno   : bufno of pinned data.
 *
 *
 * @return  < 0 on error<br>
 * @return  = 0 on success
 * 
 * This function sends a message to a destination. Before calling
 * PSHALMsgSendPinned() you must check whether a buffer is available 
 * with PSHALFifoFree().
 * It is not checked whether the areas a readable. 
 * SEGFAULT are possible if invalid parameters are given.
 * To be thread save the PSHAL_SendLock must be held, otherwise
 * the internal datastructures can be corrupt.
 */
int PSHALMsgSendPinned(PSHALSendHeader_t* header,int bufno);


/*------------------------------------------------------------------------------
 * int PSHALMsgAvail()
 */
/**
 * Check whether new messages are available.
 * @return  < 0 no msg available<br>
 * @return  >=0 type of msg available
 *
 * This function checks the HAL if a new msg is available.
 * To be thread save the PSHAL_ReceiveLock must be held, otherwise
 * the return value could be out of date, if another thread allready
 * received the new msg.
 */
int PSHALMsgAvail();


/*------------------------------------------------------------------------------
 * void* PSHALMsgGet()
 */
/**
 * Get the next messages available.
 * @return  NULL   no msg available <br>
 * @return  !=NULL pointer to the msg header
 *
 * This function checks the HAL if a new msg is available and 
 * return a pointer to the header. To be thread save the PSHAL_ReceiveLock must 
 * be held, otherwise manipulation of datastructures inside could be corrupt.
 */
void* PSHALMsgGet();

/*------------------------------------------------------------------------------
 * int PSHALMsgHeaderPut(void* msgheader)
 */
/**
 * Frees the header of a message.
 * @param    msgheader  the header to be freed 
 * @return    0          on success<br>
 * @return   -ERRNO     on failure
 *
 * This function returns the msgheader to the pool of free headers.
 * After a successful return, the header is reused by the HAL/MCP to
 * receive new messages. This function only effects the header. If
 * there are references to a msgbody then these have to be freed
 * in extra calls.
 * To be thread save the PSHAL_ReceiveLock must be held, otherwise 
 * manipulation of datastructures inside could be corrupt.
 */
void* PSHALMsgHeaderPut(void* msgheader);

/*------------------------------------------------------------------------------
 * void* PSHALMsgBodyGet(int bufno)
 */
/**
 * Gets the address of a pinned buffer.
 * @param  bufno  the buffer number of the pinned buffer.
 * @return    NULL      bufno is not valid<br>
 * @return   !=NULL     the virtual address of the buffer bufno
 *
 * This function returns the virtual address of a pinned buffer.
 */
void* PSHALMsgBodyGet(int bufno);

/*------------------------------------------------------------------------------
 * int PSHALMsgBodyPut(int bufno)
 */
/**
 * Frees the body of a message.
 * @param  bufno  the number of the msgbody to be freed 
 * @return    0          on success<br>
 * @return   -ERRNO     on failure
 *
 * This function returns the msgbody to the pool of free pinned pages.
 * After a successful return, the buffer can be reused by the HAL/MCP to
 * receive new messages. 
 * To be thread save the PSHAL_ReceiveLock must be held, otherwise 
 * manipulation of datastructures inside could be corrupt.
 */
int PSHALMsgBodyPut(int bufno);

/*------------------------------------------------------------------------------
 * void* PSHALMsgBodyAddress(int bufno)
 */
/**
 * Gets the address of a msgbody.
 * @param  bufno      the bufno of the msgbody.
 * @return    <>NULL     (address) on success<br>
 * @return   NULL        on failure
 *
 * This function returns the virtual user address of the msgbody of 
 * a specific bufno.
 */
void* PSHALMsgBodyAddress(int bufno);


/*------------------------------------------------------------------------------
 * int PSHALSYSSwapoutDataArea()
 */
/**
 * Unpinns the buffers in the dataarea.
 * @param  dataarea  the dataarea, which holds the pinned buffers 
 * @return    0          on success<br>
 * @return   -ERRNO     on failure
 *
 * This function unpinns all pinned buffers in the dataarea.
 * After that all buffers in the dataarea are regular user buffers,
 * which are no more accessible by DMA. This helps to "swap" out
 * large messages.
 */
void* PSHALSYSSwapoutDataArea(PSDataArea_t* dataarea);


#endif
