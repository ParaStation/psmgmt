/*****************************************************************
 * mcpif.h	MCP Interface for ParaStation3
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * Version 1.0:	 Jun 2000: initial implementation
 *
 *		Jens Hauke
 *           Thomas M. Warschko
 *
 *************************************************************fdb*
 * MCP Interface 
 *  public structures und consts from MCP (used by psm_mcpif.c)
 *************************************************************fde*/

#ifndef _MCPIF_H_
#define _MCPIF_H_

#include "ps_types.h"
#include "ps_develop.h"
#include "mcp_types.h"
#include "license_pub.h"
#include "lanai_def.h"


/************************/
/* Parastation 3 Consts */
/************************/



 /* Size of doorbelregion for one ctxt*/
#define DOORBELL_CONTEXTSIZE 8192
#define DOORBELL_USER_CONTEXT_OFF (1*DOORBELL_CONTEXTSIZE)
#define NBIT_DOORBELL_CONTEXTSIZE NBIT(DOORBELL_CONTEXTSIZE)


#define LAST_STATE_BIT  12

#define EVENT_IDLE         0
#define EVENT_TIMEOUT0     1
#define EVENT_TIMEOUT1     2
#define EVENT_TIMEOUT2     3
#define EVENT_WAKE         4
#define EVENT_NET_RECV_OK  5
#define EVENT_NET_RECV_ERR 6
#define EVENT_NET_SEND_D   7
#define EVENT_NET_SEND_C   8

#define NR_EVENT_FUNC      9


#define IMR   ( WAKE_INT_BIT |	\
		RECV_INT_BIT|	\
		BUFF_INT_BIT|	\
		TIME0_INT_BIT|	\
		SEND_INT_BIT )
/* Set SENDQ_D_INT_BIT if Sendq is not empty*/
#define SENDQ_D_INT_BIT			( 1 << 4 )
/* Set SENDQ_C_INT_BIT if CtrlQ not empty*/
#define SENDQ_C_INT_BIT			( 1 << 5 )

#define USR_INT_BITS (SENDQ_D_INT_BIT | SENDQ_C_INT_BIT)

#if ( (IMR & USR_INT_BITS) != 0)
#error Overlapping UserIntBits!
#endif

#if ( (IMR | USR_INT_BITS) > (1 << (LAST_STATE_BIT+1)))
#error  LAST_STATE_BIT to small !
#endif



#define MEM_ALLIGN( addr , align ) (((addr) + ((align)-1))&(~((align)-1)))



/* FIFO Control Bits: */
#define ENABLE_DOORBELL_LIMITCHECK 0x10
#define ENABLE_MULTIQ   0x08

#define DOORBELL_ADDR_OFF 0x00808000




/***********************************************************************
  SRAM :
  
  Address
  From                 Size              To                  Desc
  -------------------  --------------    ------------     
  0x00000000           ????????                            MCP-Programm

  SHARED_MEMORY_BASE   
  MCP_MEM_BASE         MCP_MEM_SIZE      MCP_MEM_END       Shared Vars

  FIFO0_BASE           FIFO_SIZE         FIFO0_END         FIFO for Dorbell
  
  DYNAMIC_BASE                           DYNAMIC_END       for mcp_getmem

if USEFIFOCNT:
  FIFOCNT_BASE         FIFOCNT_SIZE      FIFOCNT_END       FIFO Counter

  MCP_SRAM_SIZE                                            End SRam


************************************************************************/



/************************************************************************
  Where the Shared memory region in the LANai starts (in bytes).
  CAUTION: if SHARED_MEMORY_BASE_BYTES is modified, you have to
           modify crt0.s !!!    
************************************************************************/

#ifdef __lanai__
extern void * shared_mem_start;
#endif
#define SHARED_MEMORY_BASE  0x18000

#define MCP_MEM_BASE SHARED_MEMORY_BASE
#define MCP_MEM_SIZE sizeof(MCPmem_t)
#define MCP_MEM_END  ( MCP_MEM_BASE + MCP_MEM_SIZE )

#define FIFO0_BASE MEM_ALLIGN( MCP_MEM_END , FIFO_SIZE)
#define FIFO0_END  (FIFO0_BASE + FIFO_SIZE)
#define FIFO_SIZE    ( ( 1 << FIFO_QUEUE_SIZE_SELECTION ) *4096 ) 
#define FIFO_ENTRIES ( ( 1 << FIFO_QUEUE_SIZE_SELECTION ) *512 ) 

#define DYNAMIC_BASE ( FIFO0_BASE + FIFO_SIZE )
#ifdef USEFIFOCNT
#define DYNAMIC_END  FIFOCNT_BASE 

#define FIFOCNT_BASE  (MCP_SRAM_SIZE - FIFOCNT_SIZE)
#define FIFOCNT_SIZE  4096
#define FIFOCNT_END   MCP_SRAM_SIZE
#define FIFOCNT_ENTRIES (FIFOCNT_SIZE / sizeof(FIFO_COUNTER))
#else
#define DYNAMIC_END  MCP_SRAM_SIZE
#endif


#define MCP_SRAM_SIZE (mcp_mem.SRamSize /*1*1024*1024*/)      /* 1MByte SRAM ?  */

// Now in mcp_types.h
//  #define DMACHAIN0_SIZE 8
//  #define DMACHAIN1_SIZE 0
//  #define DMACHAIN2_SIZE 0
//  #define DMACHAIN3_SIZE 0


/**********************************************************
 * FIFO
 **********************************************************
  *
  * Queue size select
  * 2 1 0 Bit                    Value
  * -----                        ------
  * 1 1 1 512KB = 64K entries    7
  * 1 1 0 256KB = 32K entries    6
  * 1 0 1 128KB = 16K entries    5
  * 1 0 0 64KB = 8K entries      4
  * 0 1 1 32KB = 4K entries      3  
  * 0 1 0 16KB = 2K entries      2
  * 0 0 1 8KB = 1K entries       1
  * 0 0 0 4KB = 512 entries      0
  */

#define FIFO_QUEUE_SIZE_SELECTION  0   /* 4 kByte */

#ifdef USEFIFOCNT
#define FIFO_CONTROLREG_BITS   ( FIFO_QUEUE_SIZE_SELECTION	\
                                 | ENABLE_DOORBELL_LIMITCHECK    \
				 /*|  ENABLE_MULTIQ */ )
#else
#define FIFO_CONTROLREG_BITS   ( FIFO_QUEUE_SIZE_SELECTION	\
				 /*|  ENABLE_MULTIQ */ )
#endif




/**********************************************************
 * TRACE
 **********************************************************/


#ifdef ENABLETRACE
#  define TRACESIZE 512

#  define TRACEVARS				\
int trace_pos;					\
struct {					\
    MCP_POINTER(char)	file;			\
    UINT32		line;			\
    UINT32              time;                   \
    MCP_POINTER(char)	name;			\
    UINT32		val;			\
    UINT32		seqno;			\
} trace[ TRACESIZE ];
    

extern void McpTraceNext(void);
//  #ifdef ENABLETRACE
//  void McpTraceNext(void)
//  {
//      mcp_mem.trace_pos = (mcp_mem.trace_pos+1) & (TRACESIZE-1);  
//      mcp_mem.trace[mcp_mem.trace_pos].file = 0;
//  #ifdef ENABLETRACENOLOST    
//      if (!mcp_mem.trace_pos){
//  	TRACE("(int)BREAK",0);
//      }
//  #endif
//  }
//  #endif


#define _TRACE(  name_ , val_  )					\
{									\
    char * __file = __FILE__;						\
    mcp_mem.trace[mcp_mem.trace_pos].file = __file;			\
    mcp_mem.trace[mcp_mem.trace_pos].line = __LINE__;			\
    mcp_mem.trace[mcp_mem.trace_pos].time = RTC;			\
    mcp_mem.trace[mcp_mem.trace_pos].name = name_;			\
    mcp_mem.trace[mcp_mem.trace_pos].val  = (UINT32) val_;		\
    mcp_mem.trace[mcp_mem.trace_pos].seqno++;				\
    McpTraceNext();							\
/*    mcp_mem.trace_pos = (mcp_mem.trace_pos+1) & (TRACESIZE-1);*/	\
/*    mcp_mem.trace[mcp_mem.trace_pos].file = 0;		*/	\
}

#ifdef ENABLESLOWTRACE
#  define TRACE( name_,val_) {_TRACE( name_,val_);udelay(5000);}
#else
#  define TRACE( name_,val_) _TRACE( name_,val_);
#endif

#  define TRACEONE( name_,val_ )					\
{									\
    if (mcp_mem.trace[(mcp_mem.trace_pos-1)& (TRACESIZE-1)].name !=	\
	("_"name_) ){							\
	TRACE( "_"name_,val_ );						\
    }									\
}
	




#else /* !ENABLETRACE */
#  define TRACEVARS
#  define TRACE( name_,val_ )
#  define TRACEONE( name_,val_ )
#endif/* ENABLETRACE */


#ifdef ENABLEBREAK
#define BREAK(name_,type_,val_)			\
{     TRACE("("type_")"name_,(val_));		\
      MCP_Handshake(255);			\
}
#else
#  define BREAK(name_,type_,val_)		\
{     TRACE(name_,(val_));		\
} 
#endif


typedef struct MCPTimer_T{
    UINT32	LastRTC;
    UINT32	Count;
    UINT32	Time;
}MCPTimer_t;


#ifndef CPUC
#define CPUC RTC
#endif

#ifdef __lanai__
#  ifdef ENABLE_TIMER
#    define TIMER_START(No) mcp_mem.Timer[No].LastRTC=CPUC;ASMC("Start Timer "#No)
#    define TIMER_STOP(No)						\
         mcp_mem.Timer[No].Time += (CPUC - mcp_mem.Timer[No].LastRTC);	\
         mcp_mem.Timer[No].Count++;					\
         ASMC("Stop Timer "#No)
#  else /* !ENABLE_TIMER */
#    define TIMER_START(No)
#    define TIMER_STOP(No)
#  endif
#endif



/* Diagnostic Struct */
typedef struct MCP_CountSendIntBit_T{
    int Count;
    int Set;
}MCP_CountSendIntBit_t;


typedef struct MCP_Timestamps_T{
    UINT16	ts[6];
}MCP_Timestamps_t;

#define MCP_TIMESTAMP(ptr) ((MCP_Timestamps_t *)(ptr))->ts



typedef struct MCP_Counter_T{
    UINT32	Recv;		// Count all Recv (inclusive errors)
    UINT32	RecvCRCErr;	// Count CRC Errors
    UINT32	RecvDestErr;	// Count Packs with wrong destinationadd.
    UINT32	RecvConnIdErr;	// Count Packs with wrong ConnId
    UINT32	RecvUnknown;	// Count Unkown Packs
    UINT32	SNACK;		// Count Sequence NACKs
    UINT32	CNACK_HostRecv;	// CNACK because no HostRecvBufs
    UINT32	CNACK_MCPRecv;	// CNACK because no MCPRecvBufs
    UINT32	CNACK_Resend;	// Resend counter for CNACKs
    UINT32	ConnReset;      // Count Connection Resets 
    UINT32	DeadLink;       // Count Timeouts from dead links
    UINT32	UnrecSeqErr;    // Count Unrecoverble Sequence Error 
} MCP_Counter_t;



/************************************************************************
 Layout of the internal LANai memory 
************************************************************************/

typedef struct MCPmem_T {
    UINT32			MagicStart;
    volatile INT32		handshake; /* won't work without volatile */
#if ( DMACHAIN0_SIZE !=0 )
    MCPDMAControlBlock_t	DMAChain0[ DMACHAIN0_SIZE ];
    struct {
	/* DMAq 0 dont use notify nor head! */
	MCP_POINTER(MCPDMAControlBlock_t) head;
	MCP_POINTER(MCPDMAControlBlock_t) tail;
        INT32			notifycnt;
    } DMAChain0Info;
#endif
#if ( DMACHAIN2_SIZE !=0 )
    MCPDMAControlBlock_t	DMAChain2[ DMACHAIN2_SIZE ];
    struct {
	MCP_POINTER(MCPDMAControlBlock_t) head;
	MCP_POINTER(MCPDMAControlBlock_t) tail;
        INT32			notifycnt;
    } DMAChain2Info;
#endif
    UINT32                      State;
    INT32                       myid;	   /* HOST-ID (0 to nr_of_nodes) */
//    INT32			nr_of_nodes;	   /* Nr of nodes in Cluster */
    INT16			ConnID_out;	/* My Connection ID to that node */
    INT16			_fill1_;
    MCP_ConnInfo_t		ci[MAX_NR_OF_NODES];
    INT32			HasSwitch;
    INT32			LogLevel;	/* Used for some kernel notifications */
    INT32			rand_seed;
    MCP_POINTER(FIFO_ENTRY)     fifo0p;
    MCP_POINTER(FIFO_ENTRY)     fifo1p; /*unused*/
    MCP_POINTER(FIFO_ENTRY)     fifo2p; /*unused*/
    MCP_POINTER(FIFO_ENTRY)     fifo3p; /*unused*/
    UINT32			dbxparam[DBX_PARAM_SIZE];/* doorbell x parameter */
    MCP_POINTER(UINT32)		dbxparamp;

    MCP_Context_t               Context[ NR_OF_MCPCONTEXTS ];

    MCP_CtrlPacket_t		CtrlPackets[ CTRLQBUFS ];

    MCP_POINTER(MCP_CtrlPacket_t)
				CtrlPacketPool;
    MCP_CtrlPacketQ_t		CtrlPacketQ;

    MCP_SENDRING_t		SendRing;
    MCP_TIMEOUTQ_t		Timeoutq;
    
    MCP_POINTER(MCPRecvBuf_t)	RecvPool;  /* Pool of RecvBuffers, must not be empty*/
    MCP_POINTER(MCPRecvBuf_t)	RecvPacket;/* Packet current prepared for receiving */
//    MCP_POINTER(MCPRecvBuf_t)	_RecvDebug;
    MCPHostNotify_t		HostNotify;// notify context or kernel
    MCP_PortElement_t		Ports[MAXPORTS];/*< static array of ports     */
    MCP_POINTER(
	MCP_PortElement_t)	PortHash[PORTHASHSIZE];/*< Hashtable in Ports */
    MCP_POINTER(
	MCP_PortElement_t)	PortPool;  /*< List of unused Ports(entrys)*/
	

    /* Kernel messages */
    MCP_HostBufInfo_t		HostNotifyInfo;
    /* Position for next message */
    UINT32			HostNotifyPos;

//    MCP_POINTER(void)		HDummyBuf; /* Pointer to HostMem */
    UINT32			SRamSize;
    MCP_POINTER(void)		end_of_shared_mem;

    /* Some debugging vars */
    MCP_POINTER(struct dispatch_table)
				dtp;       /*< Pointer to dispatch table*/
    UINT32			MCPParams[ MCP_MAX_MCP_PARAMS ];
    pslic_binpub_t		LicPub;   /* Encoded Licensekey */
    INT32			dummy;
    volatile INT32		t[8];
    volatile INT32		c[8];
    volatile INT32		pt[16];

    volatile INT32		errcode; 
    volatile char		errmsg[64];
    UINT8			mac[6 + 2];/* +2 be aligned */
    UINT32			Date;
    MCPTimer_t			Timer[8];
    MCP_Counter_t		Counter;
    TRACEVARS
    UINT32			MagicEnd;
} MCPmem_t; 

#endif /* _MCPIF_H_*/


