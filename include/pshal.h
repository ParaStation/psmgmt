/*
 *
 *
 *      $Id: pshal.h,v 1.19 2002/04/12 20:38:31 hauke Exp $	
 *
 *      written by Jens Hauke
 *
 * This the interface definition between the HAL (Hardware Abstraction Layer)
 * of ParaStation and the upper parts of the protocols.
 *
 *  History
 *
 *   000911 Joe Creation (Joachim Blum)
 *   001023 Jens Implementation
 */
#ifndef __pshal_h
#define __pshal_h

#ifdef __cplusplus
extern "C" {
#if 0
} // <- just for emacs indentation
#endif
#endif
    
//#include <machine.h>
#include <ps_types.h>

#define PSHAL_MSGSIZE    8192

// like MSGFLAG_HIGHPRIO in mcp_types.h
#define PSHAL_MSGFLAG_HIGHPRIO 0x0001

/*------------------------------------------------------------------------------
 *  PSHALSendHeader_t  */
/**
 * Header passed to the PSHALMsgSend Routine to send messages.
 */
typedef struct PSHALSendHeader_T{
    char     protocol;    /**< used protocol                   */
    UINT8    xheaderlen;  /**< length of the header. Data is at 
			     the end of the header             */
    INT16    datalen;     /**< length of the data              */
    INT16    dstport;     /**< destination port of the message */
    INT16    dstnode;     /**< destination node of the message */
    INT16    srcport;     /**< source port of the message      */
    INT16    srcnode;     /**< source node of the message      */
    INT16    tag;         /**< protocol specific used tag      */
    INT16    flags;       /**< packet flags                    */
    void     *xheader;	  /**< ptr to begining of extra header */
    
}PSHALSendHeader_t;

/*------------------------------------------------------------------------------
 *  PSHALRecvHeader_t  */
/**
 * Header returned by the PSHALMsgGet Routine to receive messages.
 * Elements stored in Host Byte order.
 */

/*
  MCPHostRecvHeader_t and PSHALRecvHeader_t must have the same
  structure !!!
*/
typedef struct PSHALRecvHeader_T {
    INT16		protocol;	/**< used protocol                   */
    UINT8		lastdatabyte;
    UINT8		_reserved1;
    INT16		_reserved2;
    INT16		datalen;	/**< length of the data              */

    INT16		srcnode;	/**< source node of the message      */
    INT16		srcport;	/**< source port of the message      */
    INT16		dstnode;	/**< destination port of the message */
    INT16		dstport;	/**< destination port of the message */

    INT16		tag;		/**< protocol specific used tag      */
    INT16		xheaderlen;	/**< length of the extra header      */
    INT32		portseqno;	/**< SequenceNo of Port		     */
    long		xheader[zeroarray];	/**< begining of extra header */
} PSHALRecvHeader_t;


typedef struct PSHALSYSMCPRouting_T{
    INT16		dstnode;	/**< route for node dstnode */
    char		RLen;		/**< route len */
    char		Route[8];	/**< route from Route[0]
					   to Route[RLen-1] filled up with
					   zeros */
}PSHALSYSRouting_t;

typedef struct PSHALMCPRoutings_T{
    int			nmemb;		/**< num routing entries */
    PSHALSYSRouting_t	*Routes;	/**< ptr to routing entries */
}PSHALSYSRoutings_t;

#define MAX_PSHAL_INFO_COUNTER 32

typedef struct PSHALInfoCounter_T{
    unsigned		n; /* in: max counters out: recved counters */
    struct {
	char		name[32];
	unsigned	value;
    }counter[MAX_PSHAL_INFO_COUNTER];
}PSHALInfoCounter_t;

    
/*------------------------------------------------------------------------------
 * int PSHALFifoFree()
 */
/**
 * Check if a send buffer is available.
 * 
 * @param PSHALSendHeader_t* header : The header of the message 
 *                 to be sent. With this header it is decided 
 *                 whether the communication is local or remote.
 *
 * @return  = 0 if no buffer is available<br>
 * @return  = 1 a buffer is available
 * 
 * This function checks if a buffer for sending is available.
 * As soon as a buffer is available PSHALMsgSend*() can be called.
 * In the local communication case, it is possible that 
 * PSAHLFifoFree() returns true to two different processes. 
 * If resources are low, then one of the proceeding receives
 * may not succeed and returns an error.
 * To be thread save the PSHAL_SendLock must be held, otherwise
 * the internal datastructures can be corrupt.
 * The Lock should be hold until the data is send with PSHALMsgSend*().
 */
int PSHALFifoFree(PSHALSendHeader_t* header);


/*------------------------------------------------------------------------------
 * int PSHALMsgSend()
 */
/**
 * Sends a message to a destination.
 * @param 
 *        header : dstport,srcport, headerlen,datalen,protocolno,msgtag
 * @param
 *        data   : pointer to the data.
 * @param
 *	  ack    : pointer to ack variable. 
 *
 * @return  < 0 on error<br>
 * @return  >= 0 on success (1 = Send via PIO, 0=Send via DMA)
 * 
 * This function sends a message to a destination. The length of data (header->datalen) 
 * is in maximum PSM_MAXMTU. Before calling
 * PSHALMsgSend() you must check whether a buffer is available 
 * with PSHALFifoFree().
 * In the local communication case, it is possible that 
 * PSAHLFifoFree() returns true to two different processes. 
 * If resources are low, then one of the proceeding receives
 * may not succeed and returns an error.
 * It is not checked whether the areas a readable. 
 * SEGFAULT are possible if invalid parameters are given.
 * To be thread save the PSHAL_SendLock must be held, otherwise
 * the internal datastructures can be corrupt.
 * if ack is NULL, PSHALMsgSend send always blocking. Increment ack
 * by one for non-blocking send and decrement by one, if non-blocking
 * send is finished. 
 * 
 */
int PSHALMsgSend(PSHALSendHeader_t* header,void*data,int *ack);

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
 * This function use always DMA transfer between Host and LANai.
 * You must not use bufno after call MsgSendPinned.
 */
int PSHALMsgSendPinned(PSHALSendHeader_t* header,int bufno);

/*
 * Undocumented function
 */
int PSHALMsgSendRaw(void *data,int len);

/*------------------------------------------------------------------------------
 * int PSHALMsgAvailable()
 */
/**
 * Check whether new messages are available.
 * @return   0 no msg available<br>
 * @return  !=0 msg available
 *
 * This function checks the HAL if a new msg is available.
 * To be thread save the PSHAL_ReceiveLock must be held, otherwise
 * the return value could be out of date, if another thread allready
 * received the new msg.
 */
int PSHALMsgAvailable(void);


/*------------------------------------------------------------------------------
 * void* PSHALMsgGet()
 */
/**
 * Get the next messages available.
 * @param	header	ptr to ptr to messageheader
 * @param	data	ptr to ptr to messagedata
 * @return	0	on success
 * @return	!=0	no msg available <br>
 *
 * This function checks the HAL if a new msg is available and 
 * return a pointer to the header and a pointer to data.
 * Header and data must be freed with PSHALMsgHeaderPut and PSHALMsgDataPut
 * after use, except data is NULL.
 * if *data is NULL, call PSHALMsgDataGetHeaderPut() to receive message data
 * and free header.
 * if no message is available header and data set to NULL.
 * To be thread save the PSHAL_ReceiveLock must 
 * be held, otherwise manipulation of datastructures inside could be corrupt.
 */
int PSHALMsgGet( PSHALRecvHeader_t **header,void **data);


/*------------------------------------------------------------------------------
 * int PSHALMsgHeaderPut(PSHALRecvHeader_t *header);
 */
/**
 * Frees the header of a message.
 * @param    header  the header to be freed 
 * @return  < 0 on error<br>
 * @return  = 0 on success
 *
 * This function returns the msgheader to the pool of free headers.
 * After return, the header is reused by the HAL/MCP to
 * receive new messages. This function only effects the header.
 * To be thread save the PSHAL_ReceiveLock must be held, otherwise 
 * manipulation of datastructures inside could be corrupt.
 */
int PSHALMsgHeaderPut(PSHALRecvHeader_t *header);

/*------------------------------------------------------------------------------
 * int PSHALMsgBodyPut(void *data);
 */
/**
 * Frees the body of a message.
 * @param	data	ptr to data to be freed
 * @return  < 0 on error<br>
 * @return  = 0 on success
 *
 * This function returns the msgbody to the pool of free pinned pages.
 * After return, the buffer can be reused by the HAL/MCP to
 * receive new messages. 
 * To be thread save the PSHAL_ReceiveLock must be held, otherwise 
 * manipulation of datastructures inside could be corrupt.
 */
int PSHALMsgBodyPut(void *data);

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
 * int PSHALMsgDataGetHeaderPut(PSHALRecvHeader_t *header,void *data,int len)
 */
/**
 * Gets data from PSHALMsgGet() call where data was NULL and frees header
 * @param  header        the header from PSHALMsgGet().
 * @param  data          ptr to user allocated data buffer.
 * @param  len           len of data
 * @return    0          on success
 *
 * With data == NULL discard packet.
 */
int PSHALMsgDataGetHeaderPut(PSHALRecvHeader_t *header,void *data,int len);

void PSHALCopyAndFree(PSHALRecvHeader_t *header,void *srcdata,void *destdata,int len);

/*------------------------------------------------------------------------------
 * void PSHALEvaluateNotifications(void)
 */
/**
 * Evaluate Notifications from MCP
 *
 * This function evaluate all Notifications from MCP. E.g. Notifications
 * about free MCPSendBufs.
 * PSHALFifoFree() call this function. You need not call this function by
 * yourself.
 */
void PSHALEvaluateNotifications(void);

struct PSHALMCPCount_T;

#define PSHAL_INITIAL_RECV_BUFFERS_POSTED	PSM_MAX_HSENDBUFS
extern unsigned PSHALMaxRecvBuffersPosted; /**< How many RecvBufs should be posted to MCP
					 *  Initialized with PSHAL_INITIAL_RECV_BUFFERS */
extern unsigned PSHALMinFreeSendBuffers; /**< Initialized with PSM_MAX_SENDBUFS(for send) */
#define PSHAL_INITIAL_MAX_PIO_BYTES	250
extern unsigned PSHALMaxPIOBytes;	/**< How many bytes send with PIO? More bytes with dma*/


#ifndef __KERNEL__
/* Copy from Hostmem to Hostmem */
#define PSHALCopy(src,dest,size) bcopy (src,dest,size)

/* Copy from Hostmem to PCImem */    
#define PSHALCopyH2N(src,dest,size) memcpy (dest,src,size)

/* Copy from Hostmem to PCImem, with save space after dest+size*/    
#define PSHALCopyH2Nplus(src,dest,size) memcpy (dest,src,ROUND_UP(sizeof(long),size))

#else  /* __KERNEL__ */
/* Copy from Hostmem to Hostmem */
#define PSHALCopy(src,dest,size) memcpy (dest,src,size)
/* Copy from Hostmem to PCImem */    
#define PSHALCopyH2N(src,dest,size) memcpy (dest,src,size)
/* Copy from Hostmem to PCImem, with save space after dest+size*/    
#define PSHALCopyH2Nplus(src,dest,size) memcpy (dest,src,ROUND_UP(sizeof(long),size))
#endif /* __KERNEL__ */



#ifndef __KERNEL__

/** mmapped structure to psm */
extern struct psm_mcpif_mmap_struct * pshal_mcpif;
/** Required MCP for this program. This Name would be verifyed against the loaded
    MCP in PSHALStartUp()*/
extern char * pshal_default_mcp;

#endif /* !__KERNEL__ */


/*------------------------------------------------------------------------------
 * int PSHALStartUp(int syslogerror)
 */
/**
 * Open PSM device.
 * @param	syslogerror	write errors to syslog
 * @return	0 on success,ERRNO on error
 *
 * Open PSM device. This function initialize PSHAL and should called first.
 */
int PSHALStartUp(int syslogerror);


/*------------------------------------------------------------------------------
 * void PSHALCleanup(int syslogerror)
 */
/**
 * Close PSM device.
 *
 */
void PSHALCleanup(void);


/*------------------------------------------------------------------------------
 * int  PSHALOpenContext(void)
 */
/**
 * Open an Context for communication via PSHAL.
 * @return	>=0	on success (allocated portNo)
 * @return	ERRNO	on error(eg. No Contexts left)
 *
 * Get an unused Context and mmap the required structures. This function fail,
 * if no more contexts available. Call this after PSHALStartUp.
 */
int PSHALOpenContext(void);

/*------------------------------------------------------------------------------
 * int PSHALPortBind( int ProtocolNo,int Port )
 */
/**
 * Bind Port to Application
 * if (Port == -1) search for a free port and bind it
 *
 * @param	ProtocolNo	0..x
 * @param	Port		PortNo to bind
 * @return	allocated Port	on success
 * @return	< 0	on error
 *
 */
int PSHALPortBind( int ProtocolNo,int Port );

/*------------------------------------------------------------------------------
 * int PSHALPortDelete( int ProtocolNo,int Port )
 */
/**
 * Delete Port previous bind with PSHALPortBind
 * @param	ProtocolNo	0..x
 *				PSHAL_PORT, PSHAL_AM
 * @param	Port		PortNo to bind
 * @return	0	on success
 * @return	ERRNO	on error
 *
 */
int PSHALPortDelete( int ProtocolNo,int Port );


/*------------------------------------------------------------------------------
 * int PSHALPortSleep( int ProtocolNo,int Port, int SeqNo );
 */
/**
 * Sleep until recv data for Port
 * @param	ProtocolNo	0..x
 * @param	Port		PortNo
 * @param       SeqNo		ToDo: Doc
 *
 * @return	0	on success
 * @return	ERRNO	on error
 *
 */
int PSHALPortSleep( int ProtocolNo,int Port, int SeqNo );

#ifdef __KERNEL__
/*------------------------------------------------------------------------------
 * int PSHALPortMarkIntr( int ProtocolNo,int Port, int SeqNo );
 */
/**
 * Generate Intr on recv
 * @param	ProtocolNo	0..x
 * @param	Port		PortNo
 * @param       SeqNo		ToDo: Doc
 *
 * @return	0	on success
 * @return	ERRNO	on error
 *
 */
int PSHALPortMarkIntr( int ProtocolNo,int Port, int SeqNo );

#endif

/*------------------------------------------------------------------------------
 * void PSHALSleepForEvent();
 */
/**
 * Sleep until Event from MCP arrives (eg. Low ressources)
 * Intend for background process
 */
void PSHALSleepForEvent(void);

/*------------------------------------------------------------------------------
 * void PSHALSendEvent();
 */
/**
 * Send Event to the process who called SleepForEvent()
 */
void PSHALSendEvent(void);

/*------------------------------------------------------------------------------
 * int PSHALSYSSetID(int ID)
 */
/**
 * Set the license key.
 * @return	0	on success
 * @return	ERRNO	on error
 *
 * Set the ParaStation license key. User process must have root permissions.
 */
int PSHALSYSSetLicKey(char *LicKey);


/*------------------------------------------------------------------------------
 * int PSHALSYSSetID(int ID)
 */
/**
 * Set the ID of this node.
 * @return	0	on success
 * @return	ERRNO	on error
 *
 * Set the ParaStation ID of this node. User process must have root permissions.
 */
int PSHALSYSSetID(int ID);

/*------------------------------------------------------------------------------
 * int PSHALSYSGetID(void)
 */
/**
 * Get the ID of this node.
 * @return	NodeID	on success
 * @return	-1	on error
 *
 * Get the ParaStation ID of this node.
 */
int PSHALSYSGetID(void);



/*------------------------------------------------------------------------------
 * int PSHALSYSSetRoutes(int nmemb,PSHALSYSRouting_t *routes)
 */
/**
 * Set routingtable
 * @return	0	on success
 * @return	!=0	on error
 *
 * Set routingtable
 */
int PSHALSYSSetRoutes(int nmemb,PSHALSYSRouting_t *routes);


    
/*------------------------------------------------------------------------------
 * int PSHALSYSGetDevHandle(void)
 */
/**
 * Get the file descriptor of the PSM device
 * @return	fd	filedescriptor
 * @return	-1	on error
 *
 * Get the file descriptor of the PSM device. This value is set after call
 * PSHALStartUp().
 */
int PSHALSYSGetDevHandle(void);



/*------------------------------------------------------------------------------
 * int PSHALSYSGetCounter( PSHAL_MCP_COUNT * counter )
 */
/**
 * Get debugging counters c[] and t[]
 * @return	0	on success
 * @return	ERRNO	on error
 *
 */
int PSHALSYSGetCounter( struct PSHALMCPCount_T * counter );

/*------------------------------------------------------------------------------
 * PSHALInfoCounter_t * counter PSHALSYSGetInfoCounter(void)
 */
/**
 * Get ptr to info counter
 *
 * @return	0	on error
 * @return	infoptr	on success
 *
 */
PSHALInfoCounter_t * PSHALSYSGetInfoCounter(void);


/*------------------------------------------------------------------------------
 * int PSHALSYSGetDTime( int eventnr,UINT32 * count,UINT32 * time)
 */
/**
 * Get dispatch timer.
 * @return	0	on success
 * @return	ERRNO	on error
 *
 * Get count and time for event eventnr. Kernelmodule and MCP must be compiled with
 * #define DISPATCHTIMER, else the result is always zero. if count == time == NULL
 * print result to stdout.
 */
int PSHALSYSGetDTime( int eventnr,UINT32 * count,UINT32 * time);


/*------------------------------------------------------------------------------
 * void PSHAL_DumpDispatchtable(void)
 */
/**
 * Dump the dispatchtable to stdout.
 *
 */
void PSHALSYSDumpDispatchtable(void);

/*------------------------------------------------------------------------------
 * int PSHALSYSGetMessage( int * code, char * message );
 */
/**
 * Get (debug)message from MCP.
 * @param	code	ptr to code of message
 * @param	message	ptr to user allocated memory for the message.
 * @return	0	on success
 * @return	ERRNO	on error
 *
 * Get the last (debug)message from MCP. if code == message == NULL, print
 * Message to stdout. if code =! 0 this mean (mostly) message is a error
 * message (eg. MCP Panic)
 *
 */
int PSHALSYSGetMessage( int * code, char * message );



    

/*------------------------------------------------------------------------------
 * int PSHALSYSGetRTC( void )
 */
/**
 * Get RTC from LANai
 * @return	RTC
 *
 */
int PSHALSYSGetRTC( void );

/*------------------------------------------------------------------------------
 * int PSHALSYSGetISR( void )
 */
/**
 * Get ISR from LANai
 * @return	ISR
 *
 */
int PSHALSYSGetISR( void );


/*------------------------------------------------------------------------------
 * char * PSHALSYSMCPName(void)
 */
/**
 * Get the name of the actual loaded MCP
 * @return	mcpname
 *
 */
char * PSHALSYSMCPName(void);

/*------------------------------------------------------------------------------
 * int PSHALSYS_GetSmallPacketSize(void)
 */
/**
 * Get small packet size (boundary between PIO and DMA)
 * @return	packetsize
 *
 */
int PSHALSYS_GetSmallPacketSize(void);



/*------------------------------------------------------------------------------
 * void PSHALSYS_SetSmallPacketSize(int size)
 */
/**
 * Set small packet size (boundary between PIO and DMA)
 *
 */
void PSHALSYS_SetSmallPacketSize(int size);
    
/*------------------------------------------------------------------------------
 * int PSHALSYS_GetResendTimeout(void)
 */
/**
 * Get resend timeout in us
 * @return	resendtimeout
 *
 */
int PSHALSYS_GetResendTimeout(void);

/*------------------------------------------------------------------------------
 * void PSHALSYS_SetResendTimeout(int t);
 */
/**
 * Set resend timeout in us
 *
 */
void PSHALSYS_SetResendTimeout(int t);
    
/*------------------------------------------------------------------------------
 * int PSHALSYS_SetMCPParam(int Param,int Value)
 */
/* ToDo: Doc
 * 
 *
 */
int PSHALSYS_SetMCPParam(int Param,int Value);

/*------------------------------------------------------------------------------
 * int PSHALSYS_GetMCPParam(int Param,int *Value)
 */
/* ToDo: Doc (You can use NULL for Name20)
 * 
 *
 */
int PSHALSYS_GetMCPParam(int Param,unsigned int *Value,char *Name20);

    
#ifdef __cplusplus
}/* extern "C" */
#endif

#endif














