

/*************************************************************fdb*
 * definition of parastation 3 ioctrls   
 *							      
 *************************************************************fde*/

#ifndef _PSM_IOCTL_H_
#define _PSM_IOCTL_H_

#include "pshal.h"
#define PSHAL_GROUP             'P'

/********************************************************/
/*							*/
/* Global ioctls (offset 100)                           */
/*							*/
/********************************************************/

typedef struct PSHALMCPDef_T {
    unsigned int	lanaiversion;   /* LANai ver f.e. 0x700 */
    char		cname[20];	/* canonical name */
    char		desc_name[20];	/* mcp description */
    char		desc_desc[60];
    char		desc_date[40];
    unsigned int	zlength;	/* compressed length */
    char		* zimage;	/* compressed mcp image */
    char		mcpifname[20];	/* mcpifname */
    unsigned long	mcpifparam;	/* mcpif parameter */
}PSHALMCPDef_t;

typedef char PSHAL_MCP_CNAME[20];

#define PSHAL_UNLOCKMODULE	_IO(PSHAL_GROUP,100)
#define PSHAL_DUMPSRAM		_IOR(PSHAL_GROUP,101,int)
//#define PSHAL_GETCOUNT		_IOR(PSHAL_GROUP,0,PSHAL_COUNT)
/* register a new mcp */
#define PSHAL_REGMCP		_IOR(PSHAL_GROUP,102,PSHALMCPDef_t)
#define PSHAL_LOADMCP		_IOR(PSHAL_GROUP,103,int)
#define PSHAL_GETRTC            _IOR(PSHAL_GROUP,104,int)
#define PSHAL_GETISR            _IOR(PSHAL_GROUP,105,int)
#define PSHAL_GETMCPNAME	_IOW(PSHAL_GROUP,106,PSHAL_MCP_CNAME)
#define PSHAL_RELOADMCP		_IO(PSHAL_GROUP,107)

/********************************************************/
/*							*/
/* default MCP ioctls (offset 200)			*/
/*							*/
/********************************************************/

typedef struct PSHALMCPMessage_T{
    INT32	code;
    char	message[64];
}PSHALMCPMessage_t;

typedef struct PSHALMCPDTimer_T{
    UINT16	eventnr; /* eventnr (W)*/
    UINT32	count;   /* event counter (R)*/
    UINT32	time;    /* event time (R)*/
}PSHALMCPDTime_t;

typedef struct PSHALMCPCount_T{
  UINT32 t[8];
  UINT32 c[8];
}PSHALMCPCount_t;

typedef struct PSHALMCPTrace_T{
    char	filename[200]; // filename[0]==0 -> No Entry
    int		line;
    char	name[200];
    int		tracepos;
    int		time;
    int		val;
}PSHALMCPTrace_t;

typedef struct PSHALMCPPortBind_T{
    int		ProtocolNo;
    int		Port;
}PSHALMCPPortBind_t;

typedef struct PSHALMCPParam_T{
    UINT32	ParamNo;
    UINT32	Value;
    char	Name[20];
}PSHALMCPParam_t;


typedef struct PSMLC_SendMsg_T{
    PSHALSendHeader_t	*Header;
    void		*Data;
}PSMLC_SendMsg_t;

typedef struct PSHALMCPSleepOnPort_T{
    PSHALMCPPortBind_t	Port;
    unsigned int	MCPPortSeqNo;
    unsigned int	Timeout;
}PSHALMCPSleepOnPort_t;

typedef struct PSHALMCPDMASend_T{
    void			*data;
    unsigned int		size;
    unsigned int		dest; /* SendBufNo */
    unsigned int		destoff; /* offset */
}PSHALMCPDMASend_t;

typedef struct PSHALMCPDMARecv_T{
    void			*data;
    unsigned int		size;
}PSHALMCPDMARecv_t;

/* Get Debug Message or MCP Panic */
#define PSHAL_MCP_GETMESSAGE	_IOR( PSHAL_GROUP,200,PSHALMCPMessage_t)

/* Get Dispatchtimer and counter of event eventnr */
/* if  #define DISPATCHTIMER else return 0xffffffff*/
#define PSHAL_MCP_GETDTIME	_IOWR(PSHAL_GROUP,201,PSHALMCPDTime_t)

/* Get Counter c[] and t[] */
#define PSHAL_MCP_GETCOUNT          _IOR(PSHAL_GROUP,202,PSHALMCPCount_t)

/* Get/Set Local ID of LANai */
#define PSHAL_MCP_GETID		_IOR(PSHAL_GROUP,203,int)
#define PSHAL_MCP_SETID		_IOW(PSHAL_GROUP,204,int)
#define PSHAL_MCP_GETTRACE	_IOW(PSHAL_GROUP,205,PSHALMCPTrace_t)
#define PSHAL_MCP_OPEN_CONTEXT	_IO(PSHAL_GROUP,206)
#define PSHAL_MCP_PORT_BIND	_IOW(PSHAL_GROUP,207,PSHALMCPPortBind_t)
#define PSHAL_MCP_PORT_DELETE	_IOW(PSHAL_GROUP,208,PSHALMCPPortBind_t)
#define PSHAL_MCP_SET_ROUTES	_IOW(PSHAL_GROUP,209,PSHALSYSRoutings_t)
//#define PSHAL_MCP_GET__ROUTES	_IOR(PSHAL_GROUP,210,PSHALSYSRoutings_t)
#define PSHAL_MCP_SET_PARAM	_IOW(PSHAL_GROUP,211,PSHALMCPParam_t)
#define PSHAL_MCP_GET_PARAM	_IOR(PSHAL_GROUP,212,PSHALMCPParam_t)
#define PSHAL_MCP_SLEEP_ON_PORT _IOR(PSHAL_GROUP,213,PSHALMCPSleepOnPort_t)
#define PSHAL_MCP_SETLIC	_IOW(PSHAL_GROUP,214,pslic_binpub_t)
#define PSHAL_MCP_SLEEP_FOR_EVENT _IO(PSHAL_GROUP,215)
#define PSHAL_MCP_SEND_EVENT	_IO(PSHAL_GROUP,216)
#define PSHAL_MCP_DMASEND	_IOR(PSHAL_GROUP,216,PSHALMCPDMASend_t)
#define PSHAL_MCP_DMARECV	_IOR(PSHAL_GROUP,217,PSHALMCPDMARecv_t)
#define PSHAL_MCP_MARK_INTR_ON_PORT _IOR(PSHAL_GROUP,218,PSHALMCPSleepOnPort_t)


/* Local Communication */
#define PSHAL_MCP_LOC_SEND	_IOW(PSHAL_GROUP,300,PSMLC_SendMsg_t)
#define PSHAL_MCP_LOC_FREEB	_IOW(PSHAL_GROUP,301,int)

/* Kernel ioctl (only useable for kernelif not for userland!) */
#define PSHAL_MCP_GET_BASEPTRS	_IOW(PSHAL_GROUP,400,psm_mcpif_struct_t)
#define PSHAL_MCP_SET_NOTIFY_INTR _IOR(PSHAL_GROUP,401,int)

//#define PSHAL_GETHWID           _IOR(PSHAL_GROUP,0,int)
//#define PSHAL_GETAMCCSREG       PSHAL_GETHWID           
//#define PSHAL_GETSREG           _IOR(PSHAL_GROUP,1,int)
//#define PSHAL_GETCREG           _IOR(PSHAL_GROUP,2,int)
//#define PSHAL_GETRTC            PSHAL_GETCREG
//#define PSHAL_PUTCREG           _IOW(PSHAL_GROUP,3,int)



#endif /* _PSM_IOCTL_H_ */












