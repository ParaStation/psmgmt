/************************************************************************
  Include Files
************************************************************************/

#include "ps_develop.h"
#include "ps_types.h"
#include "lanai_def.h"

#define SHARED_MEM_BASE 0x18000
#define SHARED_MEM_END  (0x18000 + (1024*1024))

#define SIS_MEM_BASE SHARED_MEM_BASE


typedef struct SendBuf_T{
#ifdef ENABLE_SIS_USERLEVEL	    
    volatile UINT16	Type;
#else
    UINT16	Type;
#endif
    UINT16	Len;
    UINT8	Data[100];
}SendBuf_t;

typedef struct RecvBuf_T{
    UINT16	Type;
    UINT16	Len;
    UINT8	Data[100];
}RecvBuf_t;

typedef struct SendCtrl_T{
    UINT32	Fire;
}SendCtrl_t;


typedef struct sis_mem_T{
    DMA_CONTROL_BLOCK		Dma;
    MCP_POINTER(RecvBuf_t)	Recv;
    MCP_POINTER(SendBuf_t)	Send;
    SendCtrl_t			SendCtrl;
    INT32 volatile		handshake;
    MCP_POINTER(void)		end_of_shared_mem;
    volatile INT32		errcode; 
    volatile char		errmsg[64];
}sis_mem_t;

