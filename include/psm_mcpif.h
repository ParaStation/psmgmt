

/*************************************************************fdb*
 * structures from psm_mcpif    
 *  not visible to LANai! Used for mapping by psm_mcpif.
 *************************************************************fde*/


#ifndef _PSM_MCPIF_H_
#define _PSM_MCPIF_H_

#include "mcpif.h"
#include "fifo.h"

//#include "psm_osif.h"
#if ( MAX_HOST_PAGESIZE < PAGE_SIZE )
 . error MAX_HOST_PAGESIZE to small !!
#endif

#ifdef __GNUC__
#define ALIGNPAGE( var ) ALIGN(MAX_HOST_PAGESIZE,var)
#endif
#ifdef __DECC 
/* There is no pragma for ALIGNPAGE on DECC compiler.
   We must do the Alignement by hand!
   Be carefull with ALIGNPAGE!!!
*/
#define ALIGNPAGE( var ) var
#endif
 
extern struct psm_mcpif psm_mcpif;

typedef struct host_send_buf * host_send_buf_p ;

typedef struct PSMLocRecvInfo_T{
    // sizeof(PSMLocRecvInfo_t) must be a multiple of PAGE_SIZE !!
    PS_FIFO_STRUCT(int,PSM_MAX_LRBUFS)
	ALIGNPAGE(Recv); /* LocRecvBufferNoS */
//    char	_fill_[ MAX_HOST_PAGESIZE - sizeof(int)]; /* sizeof(PSMLocRecvInfo_t=MAX_HOST_PAGESIZE)*/
}PSMLocRecvInfo_t;

/* Unused. Preparation for Cluster global Data */
typedef struct PSMClusterInfo_T{
    // sizeof(PSMClusterInfo_t) must be a multiple of PAGE_SIZE !! 
    INT32	ALIGNPAGE(NodeID);
    INT32	LCFreeBufs;
//    char	_fill_[ MAX_HOST_PAGESIZE - sizeof(INT32)]; 
}PSMClusterInfo_t;


typedef struct Context_Host_Info {
    struct MCPHostSendBuf_T	*host_send_bufs[ PSM_MAX_HSENDBUFS ]; /* kernel virt.ram.addr*/
    struct MCPSendBuf_T		*mcp_send_bufs;        /* kernel virt.addr*/
    struct PSMHostNotify_T	*host_notify;
    int		psm_contextNo;
} Context_Host_Info;

#ifdef __GNUC__
struct psm_mcpif_mmap_struct {
    /* All Entrys must be page aligned !!! */
    UINT32			ALIGNPAGE(doorbell[DOORBELL_CONTEXTSIZE/sizeof(UINT32)]);
    struct MCPHostSendBuf_T	ALIGNPAGE(host_send_buf[PSM_MAX_HSENDBUFS]);
    struct MCPSendBuf_T		ALIGNPAGE(mcp_send_buf[PSM_MAX_SENDBUFS]);
    struct PSMHostNotify_T	ALIGNPAGE(host_notify);

    struct PSMClusterInfo_T	ALIGNPAGE(ClusterInfo);
    /* structs for local communication */
    struct PSMLocRecvInfo_T	ALIGNPAGE(LocRecvInfo);
    struct MCPHostSendBuf_T	ALIGNPAGE(LocRecvBuf[PSM_MAX_LRBUFS]);
    
//#ifdef DEBUG_MCPMEM
//    struct MCPmem_T		ALIGNPAGE(mcp_mem);
//#endif
};
#endif

#ifdef __DECC
/* There is no pragma for ALIGNPAGE on DECC compiler. We must do the Alignement by hand */
struct psm_mcpif_mmap_struct {
    /* All Entrys must be page aligned !!! */
    UINT32			doorbell[DOORBELL_CONTEXTSIZE/sizeof(UINT32)];
    struct MCPHostSendBuf_T	host_send_buf[PSM_MAX_HSENDBUFS];
    struct MCPSendBuf_T		mcp_send_buf[PSM_MAX_SENDBUFS];
    char _fill_[ ROUND_UP(MAX_HOST_PAGESIZE
			  ,(sizeof(struct MCPSendBuf_T)*PSM_MAX_SENDBUFS)  )-
	       (sizeof(struct MCPSendBuf_T)*PSM_MAX_SENDBUFS)  ];
    struct PSMHostNotify_T	host_notify;

    struct PSMClusterInfo_T	ClusterInfo;
    /* structs for local communication */
    struct PSMLocRecvInfo_T	LocRecvInfo;
    struct MCPHostSendBuf_T	LocRecvBuf[PSM_MAX_LRBUFS];
    
//#ifdef DEBUG_MCPMEM
//    struct MCPmem_T		ALIGNPAGE(mcp_mem);
//#endif
};
#endif

struct psm_mcpif_struct { /* ptrs to the entrys of psm_mcpif_mmap_struct for kernelif */
    UINT32			*doorbell;//[DOORBELL_CONTEXTSIZE/sizeof(UINT32)]);
    struct MCPHostSendBuf_T	*host_send_buf[PSM_MAX_HSENDBUFS];
    struct MCPSendBuf_T		*mcp_send_buf;//[PSM_MAX_SENDBUFS]);
    struct PSMHostNotify_T	*host_notify;

    struct PSMClusterInfo_T	*ClusterInfo;
    /* structs for local communication */
    struct PSMLocRecvInfo_T	*LocRecvInfo;
    struct MCPHostSendBuf_T	*LocRecvBuf[PSM_MAX_LRBUFS];
    
//#ifdef DEBUG_MCPMEM
//    struct MCPmem_T		ALIGNPAGE(mcp_mem);
//#endif
};

typedef struct psm_mcpif_struct psm_mcpif_struct_t;


/* external callbacks on IRQ */
typedef void  psm_ext_irq_func( unsigned int irq,void * ptr);
int psm_register_external_irq( psm_ext_irq_func *handler);
void psm_unregister_external_irq( psm_ext_irq_func *handler);

#ifdef ENABLE_DEBUG_MSG
void psm_mcpif_print(char *str);
#define MCPIF_PRINT(fmt,rest...) {char b[256];sprintf(b,fmt,##rest);psm_mcpif_print(b);}

#else
#define psm_mcpif_print(str)
#ifndef NO_MACRODOTDOT
#define MCPIF_PRINT(fmt,rest...)
#else
static inline void MCPIF_PRINT(char *fmt,...){}
#endif
#endif


#endif













