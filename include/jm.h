/************************************************************************
 * 2001-07-02 Jens Hauke
 ************************************************************************/

#ifndef _JM_H_
#define _JM_H_

#include "ps_develop.h"
#include "ps_types.h"
#include "lanai_def.h"

#define SHARED_MEM_BASE 0x18000
#define SHARED_MEM_END  (0x18000 + (1024*1024))

#define JM_MEM_BASE SHARED_MEM_BASE



#define jm_DMA_SIZE	64

#define jm_MTU	(8192 + 256 )
#define jm_HalMaxXHeadSize	(256)
#define jm_HalMaxData		(jm_MTU- jm_HalMaxXHeadSize) 

#define jm_H2N_CMDQ_SIZE	1024
#define jm_N2H_CMDQ_SIZE	1024
//#define jm_H2N_CMDQ_SIZE	8
//#define jm_N2H_CMDQ_SIZE	8

#define jm_LANaiBufs_COUNT	100
#define jm_LANaiCtrlBufs_COUNT	100
#define jm_NNodes		4096
#define jm_NContext		128

#define jm_DMABufs_COUNT	10

#define jm_MinRecvPosted	40

#define jm_IT0			(4*1000*1000*2) /* 4 sec */


#define jm_H2NCmd_MASK		0xff000000
#define jm_H2NCmd_Shift			24

#define jm_H2NCmd_Param1	0x01000000 /* Save Lower bits */

#define jm_H2NCmd_Hello		0x02000000 /* Hello. Lower bits param */
/* Send buffer, ack to host after transmit */
#define jm_H2NCmd_SendAck	0x03000000 /* Send. Lower bits:address to LANaiBuf */
/* Send buffer, no ack to host allowed */
#define jm_H2NCmd_SendNoAck	0x04000000 /* Send. Lower bits:address to LANaiBuf */
#define jm_H2NCmd_AckFree	0x05000000 /* convert SendNoAck to SendAck */

#define jm_H2NCmd_Recv		0x06000000 /* Recv. Lower bits:address to LANaiBuf */

#define jm_H2NCmd_HALUNEXPECT	0x07000000 /* Send jm_HALACKT_UNEXPECT to dest. param,seqno in Param1*/
#define jm_H2NCmd_HALNEWCON	0x08000000 /* Send jm_HALACKT_NEWCON   to dest. param */



#define jm_N2HCmd_MASK		0xff000000
#define jm_N2HCmd_Shift			24

#define jm_N2HCmd_Hello		0x01000000 /* Unknown H2NCmd. Lower bits param */
#define jm_N2HCmd_Unknown	0x02000000 /* Unknown H2NCmd. Lower bits:Cmd >> 8 */

#define jm_N2HCmd_SendFail	0x03000000 /* Send Fail. Lower bits:address to LANaiBuf */
#define jm_N2HCmd_SendDone	0x04000000 /* Send Done. Lower bits:address to LANaiBuf */

#define jm_N2HCmd_RecvFail	0x05000000 /* Recv Fail. Lower bits:address to LANaiBuf */

#define jm_N2HCmd_RandomReject	0x06000000 /* no doc */

#define jm_HALACKT_ACK		0
#define jm_HALACKT_UNEXPECT	1
#define jm_HALACKT_NEWCON	3
#define jm_HALACKT_ALREADYSEEN	4
#define jm_HALACKT_SYNACK	5



typedef struct jm_NetRouting_T{
    INT32	r[2];
}jm_NetRouting_t;


typedef struct jm_NetRawHeader_T{
    INT16	type;
    INT16	len;
}jm_NetRawHeader_t;

typedef struct jm_NetData_T{
    INT16		type;
    UINT16		flags;	
    UINT8		data[jm_MTU];
}jm_NetData_t;

typedef struct psjm_halackheader_T{
    INT16		type;		/* == jm_PackType_HalAck */
    UINT16		acktype;	
    UINT16		srcnode;
    UINT16		ackseqno;
}psjm_halackheader_t;

typedef struct psjm_halheader_T{
    INT16		type;		/* == jm_PackType_HalData */
    UINT16		datalen;	/* Length of Packet */
    INT16		src;		/* Source */
    INT16		srcport;	/* Source Port No. */

    INT16		dest;		/* Destination */
    INT16		destport;	/* Destination Port No. */
    UINT8		xhlen;		/* len of extra header */
    /* rdp part */
    UINT8		flags;		/* RDP Flags*/

    UINT16		seqno;
//    UINT32		_reserve_;
}psjm_halheader_t;

typedef struct jm_NetSendParam_T{
    MCP_POINTER(void)	smp;
    UINT32		sa;
    MCP_POINTER(void)	smh;
    MCP_POINTER(void)	smlt;
}jm_NetSendParam_t;

typedef struct jm_NetRecvParam_T{
    UINT32	eah;
    UINT32	eal;
    UINT32	len;
    UINT32	bufferid;
}jm_NetRecvParam_t;

typedef struct jm_DMAMarker_T{
    UINT32	param;
    volatile UINT32	type_and_boff;
}jm_DMAMarker_t;


#define jm_LANaiBuf_State_FREE		0x00000000
#define jm_LANaiBuf_State_SENDQ		0x00000001	/* buffer enqueued */
#define jm_LANaiBuf_State_SENDING	0x00000002	/* buffer in transmit */
#define jm_LANaiBuf_State_SENDACK	0x00000004	/* want ack to host after transmit */
#define jm_LANaiBuf_State_SENDNOACK	0x00000008	/* no ack to host after transmit */
#define jm_LANaiBuf_State_SENDCTRLENQ	0x00000010	/* want ctrl buf enqueue after transmit */
#define jm_LANaiBuf_State_RECVQ		0x00000100
#define jm_LANaiBuf_State_RECEIVIG	0x00000200

#define jm_Conn_FLAG_SYN	0x01

#define jm_PackType_ErrBit		0x8000
//#define jm_PackType_RawData		0x0250 /* unused */
#define jm_PackType_HalData		0x0251
#define jm_PackType_HalAck		0X0252
#define jm_PackType_HalOOData		0x0253 /* used between host and net for out of order packs */

#define jm_DMAMarker_Type_Mask		0xffff0000
#define jm_DMAMarker_Type_Shift		16

#define jm_DMAMarker_Type_Raw_Short	0x00010000
#define jm_DMAMarker_Type_Raw_Large	0x00020000
#define jm_DMAMarker_Type_Hal_Short	0x00030000
#define jm_DMAMarker_Type_Hal_Large	0x00040000


#define jm_DMAMarker_boff_Mask		0x0000ffff
#define jm_DMAMarker_boff_Shift		0
#define jm_DMAMarker_rawlen_Mask	0x0000ffff
#define jm_DMAMarker_rawlen_Shift	0
#define jm_DMAMarker_lastbyte_Mask	0x00ff0000
#define jm_DMAMarker_lastbyte_Shift	16


typedef struct jm_LANaiBuf_T{
    MCP_POINTER(struct jm_LANaiBuf_T)
	ALIGN8(Next);
    UINT32		State;
    jm_NetRecvParam_t	NetRecvParam;
    jm_NetSendParam_t	NetSendParam;
    jm_NetRouting_t	Route;
    union{
	jm_NetRawHeader_t	RawHeader;
	jm_NetData_t		Data;
	psjm_halackheader_t	HALack;
	psjm_halheader_t	HALHeader;
    }u;
    jm_DMAMarker_t	_space1a_; /* used for crc32 or marker after data */
    UINT32		_space1b_; /* |room for rent :-) | crc8 and unused */
    UINT32		_space2a_; /* and additional double word  */ 
    UINT32		_space2b_; /* to prevent set buf_int_bit  */
}jm_LANaiBuf_t;


typedef struct jm_DMACtrlBlock_T{
    volatile UINT32	next_with_flags; /* Pointer to next control block */
    UINT32		csum;	/* ones complement cksum of this block */
    UINT32		len;	/* byte count */
    UINT32		lar;	/* LANai address */
    UINT32		eah;	/* high PCI address -- unused for 32bit PCI */
    UINT32		eal;	/* low 32bit PCI address */

    UINT32		notify1;
    UINT32		notify2;
}jm_DMACtrlBlock_t;

typedef struct jm_DMAq_T{
    MCP_POINTER(jm_DMACtrlBlock_t)	head;
    MCP_POINTER(jm_DMACtrlBlock_t)	tail;
    INT32				ucnt;
}jm_DMAq_t;

typedef struct jm_counter_T{
    UINT32	Recv;
    UINT32	RecvErr;
    UINT32	RecvCRCErr;
    UINT32	Send;
}jm_counter_t;


typedef struct jm_conn_info_T{
    jm_NetRouting_t	route;
    UINT8		align;
    UINT8		recvok;
    UINT16		recvseqno;
}jm_conn_info_t;

typedef struct jm_mem_T{
    UINT32			MagicStart;
    UINT32			_Align1_;
    /* DmaCtrlBlocks must be 8 byte align!!!*/
    jm_DMACtrlBlock_t		Dma[4][jm_DMA_SIZE];
    jm_DMAq_t			DmaQ[4];   

    INT32 volatile		handshake;
    UINT32			State;
    MCP_POINTER(jm_LANaiBuf_t)	SendQ;
    MCP_POINTER(MCP_POINTER(jm_LANaiBuf_t))	SendQTailP;
//    MCP_POINTER(jm_LANaiBuf_t)	SendRun;
    MCP_POINTER(jm_LANaiBuf_t)	RecvQ;
    MCP_POINTER(MCP_POINTER(jm_LANaiBuf_t))	RecvQTailP;
//    MCP_POINTER(jm_LANaiBuf_t)	RecvRun;

    MCP_POINTER(jm_LANaiBuf_t)	LANaiBufs;
    UINT32			LANaiBufs_Count;

    UINT32			H2NCmdQ[jm_H2N_CMDQ_SIZE];
    MCP_POINTER(UINT32)		H2NCmdQPos;
    struct{
	UINT32			Pos;
	UINT32			eah;
	UINT32			eal;
	UINT32			Q[jm_N2H_CMDQ_SIZE];
    }				N2HCmdQ;
    
    MCP_POINTER(void)		end_of_shared_mem;

    MCP_POINTER(jm_LANaiBuf_t)	LANaiCtrlBufPool;
    UINT32			NodeId;
    UINT32			RecvIntr; /* boolean */
    jm_conn_info_t		ci[jm_NNodes];

    /* Some debugging vars */
    MCP_POINTER(struct dispatch_table)
				dtp;       /*< Pointer to dispatch table*/
    volatile INT32		t[8];
    volatile INT32		c[8];
    volatile INT32		pt[16];

    jm_counter_t		Counter;

    volatile INT32		errcode; 
    volatile char		errmsg[64];
    UINT32			MagicEnd;
}jm_mem_t;











#endif /* _JM_H_ */

