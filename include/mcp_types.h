
/*
 * mcp_types.h	globel type definitions for ParaStation3
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * Version 1.0:	 Aug 2000: initial implementation
 *
 *
 *
 *
 *
 */

#ifndef _mcp_types_h_
#define _mcp_types_h_


#define MCP_VERSION	0x100
#include "lanai_def.h"
#include "ps_types.h"

/********************************************************************
 *  ParaStation3 Packet Format
 *
 *  |  Byte3  |  Byte2  |  Byte1  |  Byte0  |
 *  |----------------------------------------
 * 0|             R O U T E
 *  |----------------------------------------
 * 4|             R O U T E
 *  |----------------------------------------
 * 8|        CONNID     |     TYPE
 *  |----------------------------------------
 * c|       DATA LEN    |     FLAGS
 *  |----------------------------------------
 *10|        SRC        |   SRC_PORT
 *  |----------------------------------------
 *14|       DEST        |  DEST_PORT
 *  |----------------------------------------
 *18|     MSG_TAG       |    xHeader LEN 
 *  |----------------------------------------
 *1c|        SEQNO      |     ACKNO           
 *  |---------------------------------------- <- Header LEN Starts here
 *20|      Extra Header (up to 256 bytes)
 *  |       filled to get 4 byte alignement
 *  |---------------------------------------- 
 *  |       0 |       0 |       0 |       0 | optional to get
 *  |                                         PSM_PACKBLOCK_ALIGN of Data
 *  |---------------------------------------- 
 *  |      DMA Marker
 *  |---------------------------------------- <- DATA LEN Starts here
 *  | (PSM_PACKBLOCK_ALIGNed)
 *  |
 *  |      Data (up to 8192 bytes)
 *  |
 *  |
 *  |
 *  |---------------------------------------- <- DATA LEN Ends here
 *  |                  CRC				 
 *  |----------------------------------------
 *********************************************************************/


/* 8 Byte RouteInfo == MAX 8 HOPS !! */
#define MAX_HOST_PAGESIZE	8192    /* Max PAGESIZE of OS */
#define MAX_NR_OF_NODES		4096
#define MAX_NR_OF_HOPS		8
#define NR_OF_RWORDS		2
#define PSM_MAX_XHDATA		256	/* Max xheader size */
#define PSM_MAX_MTU		8192	/* Max data size */
#define PSM_PACKBLOCK_ALIGN	8	/* Alignement of Data */

#define NR_OF_MCPCONTEXTS	4


#define MCP_TIMEOUT_N_SHORT	16  /* #short entrys  (Power of 2!)*/
#define MCP_TIMEOUT_INTERVAL	(1*256) /* RTC Ticks (Power of 2!)*/



/***************************************************************
 * MCP Parameter
 ***************************************************************/


/* Value of MCPParameter name :*/
#define MCPP(name)	_MCPP(name)

/* describing string of MCPParam name:
 * additional "_Fix" for fixed parameters */ 
#define MCPP_NAME(name)  _MCPP_NAME(name)

/* USAGE 0 mean : runtime changeable
 *       1 mean : fixed at compiletime (faster)*/

#define MCP_MAX_MCP_PARAMS	8

#define MCP_PARAM_SPS		0	/* Small packet size */
#define MCP_PARAM_SPS_INIT	2000	/* 50 bytes are short */
#define MCP_PARAM_SPS_USAGE	0

#define MCP_PARAM_RTO		1	/* Retransmission timeout */
#define MCP_PARAM_RTO_INIT	(2*1024)/*  1 ms  :max Time between First Netsend and Ack */
#define MCP_PARAM_RTO_USAGE	1	/* 1->0 ca. 0.2 us */

#define MCP_PARAM_MRC		2	/* No of unsuccessful retransm. until link named dead */
#define MCP_PARAM_MRC_INIT	(50000)	/* 50 sec :MRC * RTO = time to dead */
#define MCP_PARAM_MRC_USAGE	0

#define MCP_PARAM_CTO		3	/* Capacity timeout */
#define MCP_PARAM_CTO_INIT	(4096)	/*  Capacity timeout */
#define MCP_PARAM_CTO_USAGE	1


#define MCP_PARAM_HNPEND	4	/* No of pending free sendbufs */
#define MCP_PARAM_HNPEND_INIT	2	/* 0 = send each event immediatly to host */
#define MCP_PARAM_HNPEND_USAGE	0	/* 0 kostet fast nix */

#define MCP_PARAM_ACKPEND	5	/* No of pending ACKS */
#define MCP_PARAM_ACKPEND_INIT	1	/* 0 = ACK every packed */
#define MCP_PARAM_ACKPEND_USAGE	0	/* 0 kostet fast nix */

#define MCP_PARAM_LOCAL		0x100	/* Add this to MCP_PARAM_xxx for context local params */

/* Dont try to understand :-) */
#define _MCPP(name)	__MCPP(MCP_PARAM_##name##_USAGE,name)
#define __MCPP(usage,name)	___MCPP(usage,name)
#define ___MCPP(usage,name)	_MCPP_##usage(name)
#define _MCPP_0(name)	(mcp_mem.MCPParams[MCP_PARAM_##name])
#define _MCPP_1(name)	(MCP_PARAM_##name##_INIT)
#define _MCPP_NAME(name) __MCPP_NAME(MCP_PARAM_##name##_USAGE,name)
#define __MCPP_NAME(usage,name)	___MCPP_NAME(usage,name)
#define ___MCPP_NAME(usage,name) _MCPP_NAME_##usage(name)
#define _MCPP_NAME_0(name)	#name
#define _MCPP_NAME_1(name)	#name "_Fix"

    

/********************************************************************
 * ParaStation3 Route Information
 * 
 * Needed Space:	
 *		 128 nodes ==  1152 Byte
 *             	 256 nodes ==  2304 Byte
 *             	 512 nodes ==  4608 Byte
 *              1024 nodes ==  9216 Byte
 *              2048 nodes == 18432 Byte
 *              4096 nodes == 36864 Byte
 *********************************************************************/

typedef struct MCP_Route_T {
    INT32	Route[NR_OF_RWORDS];
    INT8	Align;
} MCP_Route_t;


/********************************************************************
 * ParaStation3 Paket Types
 ********************************************************************/

#define NR_OF_PROTOCOLS 8
#define MCP_NULL        0x0230
#define MCP_ACK         0x0231
#define MCP_CAP_NACK    0x0232
#define MCP_SEQ_NACK    0x0233

#define MCP_SYN         0x0234
#define MCP_SYNACK      0x0235
#define MCP_SYNNACK     0x0236
//#define PSHAL_SYN_MASK  0x0214
#define isSYNPkt(t)     (((t)&(~0x3))==MCP_SYN)
// Error : #define isSYNPkt(t)     ((t)&PSHAL_SYN_MASK)

#define PSHAL_PTYPE_MASK        0x0240
#define PSHAL_RAWDATA   0x0240
#define PSHAL_UDP       0x0241
#define PSHAL_TCP       0x0242
#define PSHAL_PORT      0x0243
#define PSHAL_AM        0x0244
//#ifdef ENABLE_DEBUG_MSG	
#define PSHAL_DEBUGMSG  0x0245
//#endif

#define isDataPkt(t)    (((t)&(~0x07))==PSHAL_PTYPE_MASK)
//#define getProtNr(t)    ((t)&0xff)

/********************************************************************
 * ParaStation3 Host Notifications (User)
 ********************************************************************/
#define MCP_NOTIFY_MASK		0xffff0000
/* One free sendbuffer */
#define MCP_NOTIFY_FREESEND	0x00010000/*< Notification about free sendbuf*/
/* Combined free sendbufs ( Bit encoded )*/
#define MCP_NOTIFY_CFREESEND	0x00020000/*< Notification about free sendbuf*/
/* Connection to Dest is dead (No connection)*/
#define MCP_NOTIFY_CDEAD_NC	0x00030000/*< Notification about Dead Link*/
/* Connection to Dest is dead (Unrecoverable sequence-error)*/
#define MCP_NOTIFY_CDEAD_SE	0x00040000
/* Uninitialized HostSendBuffer */
#define MCP_NOTIFY_UI_HostSendBuf	0x00050000
/* Buffer Overflow */
#define MCP_NOTIFY_BufOF		0x00060000
/* Buffer Busy */
#define MCP_NOTIFY_BufBusy		0x00070000
/* Sequenceerror detected */
#define MCP_NOTIFY_SE		0x00080000

/* Result of PortBind */
#define MCP_NOTIFY_PortBind	0x00090000 /* 0:0k 1:no more ports 2:port busy */

/* H2NDMA finished (zero copy dma only) */
#define MCP_NOTIFY_H2NDMASEND_FIN	0x000A0000 /* lower word: MCPSendBuffer */

/********************************************************************
 * ParaStation3 Host Notifications (Kernel)
 ********************************************************************/
#define MCP_NOTIFYK_MASK	0xff000000
/* One free sendbuffer */
#define MCP_NOTIFYK_RECV	0x01000000/*< Notification about incomming msg */
#define MCP_NOTIFYK_RECV_P(PrNo,PoNo) (MCP_NOTIFYK_RECV|(PoNo&0xffff)|((((PrNo) - PSHAL_RAWDATA)&0xff)<<16))
#define MCP_NOTIFYK_RECV_G(PrNo,PoNo,Val) (PoNo)=(UINT16)(Val);(PrNo)= (((Val) >> 16)& 0xff) + PSHAL_RAWDATA;

#define MCP_NOTIFYK_PNB		0x02000000/*< Port Not Bound */
#define MCP_NOTIFYK_PNB_P(PrNo,PoNo) (MCP_NOTIFYK_PNB|(PoNo&0xffff)|((((PrNo) - PSHAL_RAWDATA)&0xff)<<16))
#define MCP_NOTIFYK_PNB_G(PrNo,PoNo,Val) (PoNo)=(UINT16)(Val);(PrNo)= (((Val) >> 16)& 0xff) + PSHAL_RAWDATA;


#define MCP_NOTIFYK_LOWRECV	0x03000000/*< Notification about low ressource on port */
#define MCP_NOTIFYK_LOWRECV_P(PrNo,PoNo) (MCP_NOTIFYK_LOWRECV|(PoNo&0xffff)|((((PrNo) - PSHAL_RAWDATA)&0xff)<<16))
#define MCP_NOTIFYK_LOWRECV_G(PrNo,PoNo,Val) (PoNo)=(UINT16)(Val);(PrNo)= (((Val) >> 16)& 0xff) + PSHAL_RAWDATA;

#define MCP_NOTIFYK_LICINVAL	0x04000000/*< Notification about wrong Licensekey */
#define MCP_NOTIFYK_NOP 	0x05000000/*< No Message, Only Intr */

#define MCP_NOTIFYK_CTXRST	0x06000000/*< Notification Context reset */
#define MCP_NOTIFYK_CTXRST_P(context) (MCP_NOTIFYK_CTXRST|((UINT16)(context)))
#define MCP_NOTIFYK_CTXRST_G(context,Val) ((context)=(UINT16)(Val));

#define MCP_NOTIFYK_LOWMCPRECV	0x07000000/*< Notification about low mcp ressource */
#define MCP_NOTIFYK_LOWMCPRECV_P(McpCtx) (MCP_NOTIFYK_LOWMCPRECV|(McpCtx&0xffff))
#define MCP_NOTIFYK_LOWMCPRECV_G(McpCtx,Val) (McpCtx)=(UINT16)(Val);

#define MCP_NOTIFYK_HIGHPRIO	0x08000000/*< Notification about high prior. message on port */
#define MCP_NOTIFYK_HIGHPRIO_P(PrNo,PoNo) (MCP_NOTIFYK_HIGHPRIO|(PoNo&0xffff)|((((PrNo) - PSHAL_RAWDATA)&0xff)<<16))
#define MCP_NOTIFYK_HIGHPRIO_G(PrNo,PoNo,Val) (PoNo)=(UINT16)(Val);(PrNo)= (((Val) >> 16)& 0xff) + PSHAL_RAWDATA;


/********************************************************************
 * ParaStation3 Seqence Numbers
 ********************************************************************/
/* SEQCMP: Compare two sequence numbers
 * result of a - b      relationship in sequence space
 *      -               a precedes b
 *      0               a equals b
 *      +               a follows b
 */
#define SEQCMP(a,b)     ( (INT16)(((INT16)a) - ((INT16)b)) )



/********************************************************************
 * ParaStation3 Packet Header Definition
 * CAUTION: sizeof(Net_Send_Header) has to be a multiple of 8 !!
 * CAUTION: sizeof(Net_Recv_Header) has to be a multiple of 8 !!
 *          DMA Engines require packets to be 64bit aligned
 *              (32bit are enough) !!
 ********************************************************************/

/*
  PSHALRecvHeader_t and MCPHostRecvHeader_t must have the same
  structure !!!
  (in pshal.h)
*/

typedef struct MCPNetSendHeader_T {
    INT32		ALIGN8(route[NR_OF_RWORDS]); 	/* Route Info  (up to 8 Bytes)*/

    INT16		type;			/* packet Type */
    INT16		connid;			/* ConnID */
    INT16		flags;			/* Flags (Retransmission) */
    UINT16		datalen;		/* Length of Packet */

    INT16		src;			/* Source */
    INT16		srcport;		/* Source Port No. */
    INT16		dest;			/* Destination */
    INT16		destport;		/* Destination Port No. */

    INT16		msgtag;			/* Message Tag */
    UINT16		xhlen;			/* len of extra header */
    INT16		seqno;			/* Sequnece Number */
    INT16		ackno;			/* ACK Number */
} MCPNetSendHeader_t;


typedef struct MCPNetRecvHeader_T {
    INT16		ALIGN8(type);			/* packet Type */
    INT16		connid;			/* ConnID */
    INT16		flags;			/* Flags (Retransmission) */
    UINT16		datalen;		/* Length of Packet */

    INT16		src;			/* Source */
    INT16		srcport;		/* Source Port No. */
    INT16		dest;			/* Destination */
    INT16		destport;		/* Destination Port No. */

    INT16		msgtag;			/* Message Tag */
    UINT16		xhlen;			/* len of extra header */
    INT16		seqno;			/* Sequnece Number */
    INT16		ackno;			/* ACK Number */
    /* (UINT32)(seqno,ackno) = PortSeqNo on Receive */
    /* *(UINT8*)(&connid) = Last Databyte for DataPick */
} MCPNetRecvHeader_t;


//  typedef struct MCPHostRecvHeader_T {
//      INT16		ALIGN8(type);		/* packet Type */
//      INT16		_reserved1;
//      INT16		_reserved2;
//      INT16		datalen;		/* Length of Packet */

//      INT16		src;			/* Source */
//      INT16		srcport;		/* Source Port No. */
//      INT16		dest;			/* Destination */
//      INT16		destport;		/* Destination Port No. */

//      INT16		msgtag;			/* Message Tag */
//      INT16		xhlen;			/* len of extra header */

//      INT16		_reserved3;
//      INT16		_reserver4;
//  } MCPHostRecvHeader_t;

typedef struct MCPDmaMarker_T {
    UINT16	type;
    UINT16	offset;
}MCPDmaMarker_t;


#define MCP_DMAMARKER_SMALLPACK		1
#define MCP_DMAMARKER_LARGEPACK		2
#define MCP_DMAMARKER_LARGEPACKPICK	3
#define MCP_DMAMARKER_RAWPACK		4



#define PSM_PACK_DATA_OFFSET_USER(xhlen)				\
  (ROUND_UP(PSM_PACKBLOCK_ALIGN,((xhlen)+sizeof(MCPDmaMarker_t))))

   
#define PSM_PACKSIZE_USER(xhlen,datalen)	\
   (PSM_PACK_DATA_OFFSET_USER((xhlen))+(datalen))

#define PSM_PACKSIZE(xhlen,datalen)				\
  (PSM_PACKSIZE_USER((xhlen),(datalen)) + sizeof(MCPNetRecvHeader_t))

#define PSM_PACK_DATA_OFFSET(xhlen)					\
  (PSM_PACK_DATA_OFFSET_USER((xhlen)) + sizeof(MCPNetRecvHeader_t))

#define PSM_MAX_PACKSIZE_USER  PSM_PACKSIZE_USER(PSM_MAX_XHDATA,PSM_MAX_MTU)
#define PSM_MAX_PACKSIZE       PSM_PACKSIZE(PSM_MAX_XHDATA,PSM_MAX_MTU)









/********************************************************************
 * ParaStation3 Context Definition
 * That's what an application sees as it's Send interface to the MCP
 ********************************************************************/

#define PSM_MAX_SENDBUFS	8

#define PSM_MAX_HSENDBUFS	512        /*< Maximal No of Hostbuffers */
#define PSM_MIN_HSENDBUFS	PSM_MAX_HSENDBUFS   /*< Minimal No of Hostbuffers */
#define PSM_MAX_HSENDHEADERS	PSM_MAX_HSENDBUFS  /* Same buffers as HSENDBUFS */
//#define PSM_MAX_HRECVBUFS	PSM_MAX_HSENDBUFS-100  /* Same buffers as HSENDBUFS */
#define PSM_MAX_RECVBUFS	64 /* Context Shared MCP recv bufs */
#define PSM_MAX_REGRECVBUFS	PSM_MAX_HSENDBUFS  /* Same or less than PSM_MAX_HSENDHEADERS */
#define PSM_MAX_NOTIFYENTRYS	(MAX_HOST_PAGESIZE / sizeof(MCPHostNotifyEntry_t))

/* local communication */
#define PSM_MAX_LRBUFS		256 /*# local communication shared recvbufs */


#define PSM_NBIT_SENDBUFS	NBIT(PSM_MAX_SENDBUFS)
#define PSM_NBIT_HSENDBUFS	NBIT(PSM_MAX_HSENDBUFS)
#define PSM_NBIT_HSENDHEADERS   NBIT(PSM_MAX_HSENDHEADERS)
#define PSM_NBIT_HRECVBUFS	NBIT(PSM_MAX_HRECVBUFS)
#define PSM_NBIT_RECVBUFS	NBIT(PSM_MAX_RECVBUFS)
#define PSM_NBIT_SENDBUFLEN	NBIT(PSM_MAX_MTU) /* PSM_MAX_MTU = sizeof(HostSendBuf_t)*/

#define DMACHAIN0_SIZE (16)
#define DMACHAIN1_SIZE 0
#define DMACHAIN2_SIZE (16)
#define DMACHAIN3_SIZE 0

#define MCP_MAX_NOTIFYENTRYS    (DMACHAIN0_SIZE + DMACHAIN1_SIZE +	\
                                 DMACHAIN2_SIZE + DMACHAIN3_SIZE + 1 )

typedef struct MCPSendBuf_T {
    MCPNetSendHeader_t	Header;	/* Protocol header */
    char		Data[ PSM_MAX_PACKSIZE_USER ]; 	/* xheader,dmamarker and Data */
} MCPSendBuf_t;


typedef struct MCPHostSendBuf_T {
    char		Data[PSM_MAX_MTU - sizeof(MCPDmaMarker_t)]; 
    MCPDmaMarker_t	dmamarker;
} MCPHostSendBuf_t; /* size is multiple of PAGESIZE !*/

//  typedef
//  union MCPHostBuf_T {
//      char		Data[PSM_MAX_MTU]; /* size is multiple of PAGESIZE !*/
//      MCPNetRecvHeader_t  Header;
//  } MCPHostBuf_t;



typedef struct MCPHostNotifyEntry_T {
    volatile UINT32	Message;
} MCPHostNotifyEntry_t;


typedef struct PSMHostNotify_T { /* Notify Buffer on Host  (dest)*/
    /* size should be PAGEALIGNED */
    MCPHostNotifyEntry_t Entrys[ PSM_MAX_NOTIFYENTRYS ];
} PSMHostNotify_t;

typedef struct MCPHostNotify_T { /* Notify Buffer on LANai (src)*/
    MCP_POINTER(struct MCPHostNotifyEntry_T) Actual;
    MCPHostNotifyEntry_t Entrys[ MCP_MAX_NOTIFYENTRYS ];
} MCPHostNotify_t;



#define CTRLQBUFS	(NR_OF_MCPCONTEXTS * 8)


typedef struct MCP_CtrlPacketQEntry_T {
    /* dont change without change MCP_CtrlPacket_T !!*/
    UINT32		Type; /* 0 for normal CtrlPacks, 1 for raw sending */
    MCP_POINTER(struct MCP_CtrlPacket_T)   Next;
} MCP_CtrlPacketQEntry_t;


typedef struct MCP_CtrlPacket_T {
    /* dont change without change MCP_CtrlPacketQEntry_T !!*/
    UINT32		Type; /* 0 for normal CtrlPacks */
    MCP_POINTER(struct MCP_CtrlPacket_T)   Next;
    UINT32		_fill_;	/* Header must be 8 byte aligned */
    UINT32		Align; /* Route Alignement */
    MCPNetSendHeader_t	Header;
} MCP_CtrlPacket_t;

typedef struct MCP_CtrlPacketQ_T {
    MCP_POINTER(MCP_CtrlPacket_t)	Head;
    MCP_POINTER(MCP_CtrlPacket_t)	Tail;
    MCP_POINTER(MCP_CtrlPacket_t)	InTransaction;
} MCP_CtrlPacketQ_t;



enum MCP_SendBufState {CFREE=0x0, CH2NDMA=0x01, CSENDING=0x02,CACKEXP=0x03,CSENDRUN=0x04};


typedef struct MCP_SendCtrl_T {
    MCP_POINTER(struct MCPSendBuf_T)
			SendBuf;	/* Pointer to Packet header (and data)
					 * enum MCP_SendBufState State; * State of SendBuf
					 * dont use enum. enum is int and int somtimes 32 sometimes 64 bit!*/
    INT32		State;/* State of SendBuf */
    INT16		BufNo;		/* my buffer number */
    INT16		CtxNo;		/* my context number */
    
    INT16		Align;		/* Align of the route */
    MCP_POINTER(void)	Smlt;	/* Precalced SMLT val (end of Packet)
				   (begin+header+xheader+data) */
    INT16		SeqNo;		/* Backup of Seq No */
    UINT32		Timestamp;	/* timestamp for retransmission */
    MCP_POINTER(struct MCP_SendCtrl_T)
			Next;		/* pointer to next send Q entry */
    MCP_CtrlPacketQEntry_t RawQ;	/* ptr to next CtrlPacketQEntry (RawQ.Type must set to 1) */
} MCP_SendCtrl_t;


typedef struct MCP_HostBufInfo_T {
    UINT32	eah;
    UINT32	eal;
} MCP_HostBufInfo_t;



typedef struct MCPRecvBuf_T {
    struct {
	MCPNetRecvHeader_t Header;	/* Protocol header */
	char		Data[PSM_MAX_PACKSIZE_USER]; 	/* xheader,dmamarker and Data */
	UINT32		_space1a_; /* used for crc32 or marker after data */
	UINT32		_space1b_; /* |room for rent :-) | crc8 and unused */
	UINT32		_space2a_; /* and additional double word  */ 
	UINT32		_space2b_; /* to prevent set buf_int_bit  */
    } Packet;
    MCP_POINTER(struct MCPRecvBuf_T) Next;
} MCPRecvBuf_t;


typedef struct MCP_Context_T {
    /* Control infos for NetDMA (infos to SendBuf) */
    MCP_SendCtrl_t	SendCtrl[PSM_MAX_SENDBUFS];
    /* Info for DMA Buffer on Host for notification */
    MCP_HostBufInfo_t	HostNotifyInfo;
    /* Position for next message */
    UINT32		HostNotifyPos;
    /* Intr on HostNotification: */
    UINT32		HostNotifyIntr;
    /* ACKS for host pending */
    UINT32		HostBufsFreePending;
    UINT32		HostBufsFreeMask;
      
    /* Info for DMA Buffers on host */
    MCP_HostBufInfo_t	HostBufInfo[PSM_MAX_HSENDBUFS];
    /* see: NetRecvDataPacket(). Initialized with MCPP(SPS) on MCPContextReset()*/
    UINT16		param_sps;
    /* see: NotifyHost_FreeBuf(). Initialized with MCPP(HNPEND) on MCPContextReset()*/
    UINT16		param_hnpend;
    /* List of registerd recv.buffers. < 0 no buffer,else hostSendBufNo*/
    struct{
	INT16		Buf[PSM_MAX_REGRECVBUFS];
	MCP_POINTER(
	    INT16)	Head; /*< ptr to RegRecvBuf */
	MCP_POINTER(
	    INT16)	Tail; /*< ptr to RegRecvBuf */
    }RegRecv;
    /* MCP sendbuffers */
    MCP_POINTER(MCPSendBuf_t) SendBuf;

    /* LargeData */
    MCP_POINTER(MCPRecvBuf_t) RecvBufs;
    MCP_POINTER(
	MCP_POINTER(MCPRecvBuf_t)) RecvBufsTail;

	/* Permissions for this context (eg set routingtable) */
    UINT32		Permissions;
    /* Last Intr RTC to for irq flooding prevention */
    UINT32		LastIRQTime;
} MCP_Context_t;



/********************************************************************
 *  ParaStation Queues (virtual, because it's just a linked list)
 *  Old packets are accessible through the HEAD-pointer, while
 *  new packets will be enqueued after the current TAIL pointer.
 *  This gives a FIFO - List structure used for ACK-Q, Send-Q and ReTrans-Q
 ********************************************************************/



typedef struct MCP_SENDQ_T {
    MCP_POINTER(struct MCP_SendCtrl_T) Head;	/* Head of Send/Ack Queue */ 
    MCP_POINTER(struct MCP_SendCtrl_T) Actual;	/* Next packet to send */
    MCP_POINTER(struct MCP_SendCtrl_T) Tail;	/* Tail of Send Queue */
}MCP_SENDQ_t;

typedef struct MCP_SENDRING_T {
    MCP_POINTER(struct MCP_ConnInfo_T) Head;	/* Head of SendRing */
    MCP_POINTER(struct MCP_SendCtrl_T) InTransaction; /* actual NetSendDMA*/
}MCP_SENDRING_t;

typedef struct MCP_TimeoutEntry_T {
    MCP_POINTER(struct MCP_TimeoutEntry_T) Next;
    MCP_POINTER(struct MCP_TimeoutEntry_T) Prev;
#ifdef __lanai__
    void	(*Call)(struct MCP_TimeoutEntry_T *);
#else
    UINT32	Call;
#endif
    UINT32	Time;
}MCP_TimeoutEntry_t;


typedef struct MCP_TIMEOUTQ_T {
    int			ShortTimePos;
    MCP_TimeoutEntry_t  ShortTime[MCP_TIMEOUT_N_SHORT];
    MCP_TimeoutEntry_t  LongTime;
} MCP_TIMEOUTQ_t;


typedef struct MCPDMAControlBlock_T {
    DMA_CONTROL_BLOCK		ALIGN8( dma );
    MCP_POINTER( UINT32 )	notify;
} MCPDMAControlBlock_t;




/********************************************************************
 * ParaStation3 Connection Info Definition
 * This structure holds all Information to manage implicit connections
 * between the nodes (one structure per node)
 ********************************************************************/

enum MCP_ConnState {
    /* Connection states */
    CS_ACTIVE		= 0x00000000,
    CS_SYN_SENT		= 0x00000001,
    CS_SYN_RECVD	= 0x00000002,
    CS_CLOSED		= 0x00000003,

    CS_MASK		= 0x00000003,
    /* Connection attribs */
    CSA_PackToInTOQ	= 0x00000004, /* Packet Timeout is Enqueued */
    CSA_Sleep		= 0x00000008, /* Dont send until sleep cleared
					 inside Timeout */
// unused   CSA_CNACK_SEND	= 0x00000010  /* Send CNACKS instead SNACKs */
};

#define CS_SET(state,cstate) (state) = ((state) & ~CS_MASK)|(cstate)
#define CSA_SET(state,attrib) (state) |= (attrib)
#define CSA_CLR(state,attrib) (state) &= ~(attrib)

typedef struct MCP_ConnInfoCounter_T{
    UINT16	_Recv;	/* Count received packets (unused)*/
    UINT16	_Send;	/* Count Packs send (unused)*/
    UINT16	SendCtrl; /* Count CtrlPacks prepared to send */
    UINT16	Resend;	/* Count retransmissions */
}MCP_ConnInfoCounter_t;

    
//CUNDEF=0x0, CSYN_SENT=0x01, CSYN_RECVD=0x02,
//		    CACTIVE=0x04, CCLOSED=0x08, CDEAD=0x10 };

typedef struct MCP_ConnInfo_T{
/*     enum	MCP_ConnState State;
       Dont use enum: sizeof(enum)==sizeof(int)!=sizeof(alpha_int) */
    INT32	State;		/* State of connection */	
    INT32	Dest;		/* DestNo */

    INT16	ConnID_in;	/* Connection ID to recognize that node */
    INT16	ConnID_out;	/* My Connection ID to that node */

    INT16	NextFrameToSend;/* Seq Nr for next frame going to that host */
    INT16	FrameExpected;	/* Expected Seq Nr for msg coming from that host */

    UINT32	ResendCnt;	/* No of unsuccessful resends */
    MCP_SENDQ_t Sendq;		/* Pointer to corresponing SEND Queue (head and tail)*/
    MCP_TimeoutEntry_t Timeout;
    MCP_POINTER(struct MCP_ConnInfo_T) Prev_srq;/* pointer to prev sendring entry */
    MCP_POINTER(struct MCP_ConnInfo_T) Next_srq;/* pointer to next sendring entry */
    MCP_ConnInfoCounter_t Counter; /* Counters */

    MCP_Route_t	Route;		/* Route to destination node */
    UINT8	Acks_pending;	/* No of pending acks */
/*Unused:*/
//    INT32	PartnerID;	/* last 32 bit of 48 bit network id */
//    INT32	Lastpacket;	/* timestamp (RTC) of last received packet */
//    INT32	Misscounter;	/* Nr. of unanswered ping requests */
//    INT16	AckExpected;	/* Expected Ack Nr for msg's pending to that hosts */
//    INT16	Stall;		/* Transmission pipeline stalled (due to NACK) */
//    INT32	ConnTimeout;	/* Timeout in between SYN Packets */
}MCP_ConnInfo_t;


#ifdef __lanai__

extern inline void AddContextRecvBuf(MCP_Context_t *context,MCPRecvBuf_t *recv)
{
    recv->Next = NULL;
    *context->RecvBufsTail = recv;
    context->RecvBufsTail = &recv->Next;
}

extern inline void DelFirstContextRecvBuf(MCP_Context_t *context)
{
    if (context->RecvBufs){
	context->RecvBufs = context->RecvBufs->Next;
	if (!context->RecvBufs){
	    // Last entry removed
	    context->RecvBufsTail =
		&context->RecvBufs;
	}
    }
}


//#define IS_SENDQ_EMPTY( sendqp ) (!(sendqp)->Actual)

extern inline void ENQ_SEND(MCP_SENDQ_t * sendq,MCP_SendCtrl_t * SCtrl){
    /* if Tail is set this dont mean there is the tail!
       Tail only modified in this function, not in DEQ */
    if (sendq->Actual){
	/* Actual is set, we are not the first to send */
	sendq->Tail->Next = SCtrl;
	sendq->Tail = SCtrl;
    }else if (sendq->Head){
	/* Actual not set, we are the first to send,and acks waiting */
	sendq->Actual = SCtrl;
	sendq->Tail->Next = SCtrl;
	sendq->Tail = SCtrl;
    }else{
	/* Actual not set, we are the first to send,no acks waiting */
	sendq->Tail = SCtrl;
	sendq->Actual = SCtrl;
	sendq->Head = SCtrl;
    }
    // ToDo: Check if SCtrl already enqueued...
    SCtrl->Next = NULL;
}

extern inline void DEQ_SEND(MCP_SENDQ_t * sendq){
    sendq->Head->State = CFREE;
    sendq->Head = sendq->Head->Next;
}


#define IS_CI_IN_SENDRING(conninfop) ((conninfop)->Next_srq)

extern inline void ENQ_SENDRING(MCP_SENDRING_t * sendring,MCP_ConnInfo_t * conninfop){
    if (sendring->Head){
	conninfop->Prev_srq=sendring->Head->Prev_srq;
	conninfop->Next_srq=sendring->Head;
	sendring->Head->Prev_srq->Next_srq=conninfop;
	sendring->Head->Prev_srq=conninfop;
    }else{
	/* First element in Q */
	conninfop->Prev_srq=conninfop;
	conninfop->Next_srq=conninfop;
	sendring->Head = conninfop;
    }
}

extern inline void DEQ_SENDRING(MCP_SENDRING_t * sendring,MCP_ConnInfo_t * conninfop){
    if (sendring->Head == conninfop){
	if (conninfop->Next_srq == conninfop){
	    /* last element in Q */
	    sendring->Head = 0;
	}else{
	    /* Set Head to next */
	    sendring->Head = sendring->Head->Next_srq;
	    conninfop->Prev_srq->Next_srq = conninfop->Next_srq;
	    conninfop->Next_srq->Prev_srq = conninfop->Prev_srq;
	}
    }else{
	conninfop->Prev_srq->Next_srq = conninfop->Next_srq;
	conninfop->Next_srq->Prev_srq = conninfop->Prev_srq;
    }
    conninfop->Next_srq = 0;
}

#if 1
/* new send version */
/* ToDo: add a new state for sendbuffers */
extern inline void CHECK_SENDRING_INTRANSIT(MCP_SENDRING_t * sendring){
    if (sendring->InTransaction && (ISR & SEND_INT_BIT)){
	sendring->InTransaction->State=CACKEXP;
	sendring->InTransaction = NULL; 
    }
}

extern inline void ADD_SENDRING_INTRANSIT(MCP_SENDRING_t * sendring,struct MCP_SendCtrl_T *ctrlp){
    if (sendring->InTransaction){
	sendring->InTransaction->State=CACKEXP;
    }
    sendring->InTransaction=ctrlp;
    ctrlp->State=CSENDRUN;
}

extern inline void CLR_SENDRING_INTRANSIT(MCP_SENDRING_t * sendring){
    if (sendring->InTransaction){
	sendring->InTransaction->State=CACKEXP;
    }
    sendring->InTransaction=NULL;
}

#else
/* old non working version */
extern inline void CHECK_SENDRING_INTRANSIT(MCP_SENDRING_t * sendring){
}

extern inline void ADD_SENDRING_INTRANSIT(MCP_SENDRING_t * sendring,struct MCP_SendCtrl_T *ctrlp){
    ctrlp->State=CACKEXP;
}
#endif

#define ENQ_CTRLPACKET( CtrlP )				\
        if (mcp_mem.CtrlPacketQ.Head){			\
	    mcp_mem.CtrlPacketQ.Tail->Next = CtrlP;	\
	}else{						\
	    mcp_mem.CtrlPacketQ.Head = CtrlP;		\
	}						\
	mcp_mem.CtrlPacketQ.Tail = CtrlP;               \
	CtrlP->Next = 0;				\
        mcp_mem.State |= SENDQ_C_INT_BIT;

//  #define ENQ_TIMEOUT (toq, conninfop)
//  	{ conninfop->next_toq=NIL; 
//  	  conninfop->prev_toq=toq.tail; 
//  	  toq.tail->next_toq = conninfop; }

//  #define DEQ_TIMEOUT () 
//          { if (ctrlp->prev_toq != NIL) ctrlp->prev.next_toq = ctrlp->next_toq; 
//            if (ctrlp->next_toq != NIL) ctrlp->next.prev_toq = ctrlp->prev_toq; }

#define _RECVPTR2NO( recvp ) (( (((long)(recvp))-((long)mcp_mem._RecvDebug))/ sizeof(MCPRecvBuf_t)))

#define _RECVDEBUG( name )				\
TRACE(name,_RECVPTR2NO(mcp_mem.RecvPacket));		\
TRACE(name,_RECVPTR2NO(mcp_mem.RecvPool));		\
TRACE(name,_RECVPTR2NO(mcp_mem.RecvPool->Next));



#define GET_RECVBUF( recvp )			\
    (recvp) = mcp_mem.RecvPool;			\
    mcp_mem.RecvPool = mcp_mem.RecvPool->Next;  
//RECVDEBUG("RECVGET");




#define PUT_RECVBUF( recvp ) 			\
    (recvp)->Next = mcp_mem.RecvPool;		\
    mcp_mem.RecvPool = (recvp);		       
//RECVDEBUG("RECVPUT");

#endif /* __lanai__ */



/*****************
 * Fifo commands 
 * Commands and Parameters for doorbellaccess */

#if ( PSM_NBIT_SENDBUFS + PSM_NBIT_HSENDBUFS + 9 + (PSM_NBIT_SENDBUFLEN-2)) > 32
#error to much bits for FIFOH2NCPY()
#endif 


/* Send all free Sendbufs via Notify Host to application */
#define FIFOCMD_SEND_FREE_BUFS	101	/* No params */

/* Copy from host to LANai */    
#define FIFOCMD_H2N_COPY	102	/* Used with FIFOH2NCPY */
/* Copy from host to LANai and send to net */    
#define FIFOCMD_H2N_COPY_AND_SEND 103	/* Used with FIFOH2NCPY */
/* send to net */
#define FIFOCMD_SEND		104	/* Used with FIFOSEND */

/* PortBind */
#define FIFOCMD_PORTBIND	105	/* Used with FIFOPORTBIND */
#define FIFOCMD_PORTDELETE	106	/* Used with FIFOPORTBIND */
/* Register REcv bufs */
#define FIFOCMD_REGRECVBUF	107	/* Used with FIFOREGRECVBUF*/
/* Reset all settings of context */
#define FIFOCMD_RESET_CONTEXT	108	/* No params */
/* Unknown :-) */
#define FIFOCMD_SAY_HELLO	109
/* Call Intr on next Recv for this context,Protocol,Port */
/* Dont call another INTR_1 between INTR_1 and INTR_2
   (disable Interrupts(or schedule) between 1 and 2 */
#define FIFOCMD_WANT_INTR_1	110	/* Send PortSeqNo */
#define FIFOCMD_WANT_INTR_2	111	/* Used with FIFOPORTBIND */
/* Interrupt on context notify */
#define FIFOCMD_WANT_NOTIFY_INTR	112	/* Param 0 or 1 (bool) */

#define FIFOCMD_OPEN_CONTEXT	113	/* Param  context permissions */

#define FIFOCMD_H2N_DMA		114	/* Used with FIFOH2NDMA */
#define FIFOCMD_H2N_DMA_AND_SEND 115	/* Used with FIFOH2NDMA */

/* Get RecvBuffer (use dbxparam for offset and eal) */
#define FIFOCMD_N2H_DMA		116	/* Used with FIFON2HDMA */
#define FIFOCMD_N2H_DMA_AND_FIN 117	/* Used with FIFON2HDMA */
/* Get RecvBuffer (use next hostbuf) */
#define FIFOCMD_N2H_RECV	118	/* Used with FIFON2HDMAUSR */

#define FIFOCMD_SENDRAW		119	/* Used with FIFOSENDRAW */

/*--------------------------------------------------------------------*/
// Copy Host Buffer srch to LANai buffer desl,
// use byteoffset desoff(0-511) and dwordlen (len= des32len*4+4)
#define FIFOH2NCPY( desl , srch , des32len ,desoff )	\
htonl( BITENC4( PSM_NBIT_SENDBUFS	, desl,		\
		PSM_NBIT_HSENDBUFS	, srch,		\
		PSM_NBIT_SENDBUFLEN-2	, des32len,	\
		9			, desoff ))

#define FIFOH2NCPY_GETP( val , desl , srch , des32len ,desoff )		\
BITDEC4( val, 								\
	 PSM_NBIT_SENDBUFS	, desl,					\
	 PSM_NBIT_HSENDBUFS	, srch,					\
	 PSM_NBIT_SENDBUFLEN-2	, des32len,				\
	 9			, desoff )
/*--------------------------------------------------------------------*/
#define FIFOSEND( srcl )			\
htonl( BITENC1( PSM_NBIT_SENDBUFS	, srcl))	


#define FIFOSEND_GETP( val , srcl )		\
BITDEC1( val, 					\
	 PSM_NBIT_SENDBUFS	,srcl	)
/*--------------------------------------------------------------------*/
#define FIFOPORTBIND( protocolno , portno)		\
htonl( BITENC2( 16		, (protocolno),	\
		16		, (portno) ))

#define FIFOPORTBIND_GETP( val, protocolno , portno)		\
BITDEC2( val,							\
	 16		, (protocolno),				\
	 16		, (portno) )
/*--------------------------------------------------------------------*/
#define FIFOREGRECVBUF( bufno )					\
htonl( BITENC1( PSM_NBIT_HSENDBUFS	, bufno))	

#define FIFOREGRECVBUF_GETP( val, bufno )	\
BITDEC1( val,					\
	 PSM_NBIT_HSENDBUFS	, bufno)
/*--------------------------------------------------------------------*/
/* copy deslen bytes from host to lanai.(1 <= deslen <= PSM_MAX_MTU)
   send buffer desl, if used with FIFOCMD_H2N_DMA_AND_SEND after dma.
   Expect 2 Parameters in dbxparam ( UINT32 lar,UINT32 eal ) */
#define FIFOH2NDMA( desl , deslen )			\
htonl( BITENC2( PSM_NBIT_SENDBUFS	, desl,		\
		PSM_NBIT_SENDBUFLEN	, deslen-1))

#define FIFOH2NDMA_GETP( val , desl , deslen )	\
{BITDEC2( val,					\
	 PSM_NBIT_SENDBUFS	, desl,		\
	 PSM_NBIT_SENDBUFLEN	, deslen );	\
 deslen++;					\
}

/*--------------------------------------------------------------------*/
/* copy len bytes from lanai context->RecvBufs to host.(1 <= len <= PSM_MAX_MTU)
   Expect 2 Parameters in dbxparam ( UINT32 dataoff,UINT32 eal ) */
#define FIFON2HDMA( len )			\
htonl( BITENC1( PSM_NBIT_SENDBUFLEN	, len-1))

#define FIFON2HDMA_GETP( val , len )		\
{BITDEC1( val,					\
	 PSM_NBIT_SENDBUFLEN	, len );	\
 len++;						\
}

/*--------------------------------------------------------------------*/
/* copy lanai context->RecvBufs to hostbuf bufno.*/

#define FIFON2HDMAUSR( bufno )					\
htonl( BITENC1( PSM_NBIT_HSENDBUFS	, bufno))	

#define FIFON2HDMAUSR_GETP( val, bufno )	\
BITDEC1( val,					\
	 PSM_NBIT_HSENDBUFS	, bufno)

/*--------------------------------------------------------------------*/
#define FIFOSENDRAW( srcl , len)			\
htonl( BITENC2( PSM_NBIT_SENDBUFS	, srcl,		\
		PSM_NBIT_SENDBUFLEN     , len))	


#define FIFOSENDRAW_GETP( val , srcl , len)	\
BITDEC2( val,					\
	 PSM_NBIT_SENDBUFS	, srcl,		\
	 PSM_NBIT_SENDBUFLEN	, len)

/*--------------------------------------------------------------------*/


#define FIFOCMD_context(cmd)		\
 (((cmd) - (DOORBELL_ADDR_OFF+DOORBELL_USER_CONTEXT_OFF))>> (NBIT_DOORBELL_CONTEXTSIZE))

#define FIFOCMD_cmd(cmd)		\
 (((cmd) & (DOORBELL_CONTEXTSIZE-1)) >> 2)




#define DBX_PARAM_SIZE	FIFO_ENTRIES


#define MSGFLAG_HIGHPRIO 0x0001



/***************************************************************
 * Global port Structure Definitions
 ***************************************************************/
#define MAXPORTS	1024	/*< Maximal count of portbindings */
#define PORTHASHSIZE    64	/*< Power of 2 ! */
#define PORTHASHFUNCTION(channel) (channel.PortId.PortNo&(PORTHASHSIZE-1))


typedef struct MCP_PortElement_T{
    union Channel_T{
	struct MCP_PortId_T{
	    UINT16	PortNo;
	    UINT16	ProtocolNo;		
	}		PortId;
	UINT32		PortIdU;			/*< Unique Id of this binding */
    }Channel;

    UINT16		MCPContext;
    UINT16		Options;		/*<  Options of this binding for example
						 *   Interrupt on receive
						 * for now 0=no intr,!=0 intr
						 */
    INT32		PortSeqNo;
    MCP_POINTER(struct MCP_PortElement_T)	Next;
}MCP_PortElement_t;

typedef union Channel_T Channel_t;

//  enum{
//      MCPPortOptionDoInterrupt = 0x01  /* interrupt the host when new 
//  					message arrives */
//  	};
//  typedef struct _MCPPortElement_t{
//      char protocol;    /* protocolid of the Element */
//      char adressspace; /* Adressspace number of the Port */
//      char options;     /* options like OP_DOINTERRUPT */
//      char spare;
//      short  portid;      /* the portnumber (global adress) of the Element*/
//      short  next;        /* index of next into the Portstable */
//  }MCPPortElement_t;
//  typedef MCPPortElement_t* MCPPortElement_p;



#endif /* _mcp_types_h_ */















