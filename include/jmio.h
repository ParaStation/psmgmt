

#ifndef _JMIO_H_
#define _JMIO_H_

#include "psm_ioctl.h"

typedef struct psjm_recv_raw_T{
    void	*buf;
    unsigned	len;
//    unsigned	timeout; /* in usec */
}psjm_recv_raw_t;

typedef struct psjm_send_raw_T{
    void	*buf;
    unsigned	len;
}psjm_send_raw_t;

//  typedef struct psjm_route_T{
//      unsigned		dstnode;	/**< route for node dstnode */
//      char		RLen;		/**< route len */
//      char		Route[8];	/**< route from Route[0]
//  					   to Route[RLen-1] filled up with
//  					   zeros */
//  }psjm_route_t;
typedef PSHALSYSRouting_t  psjm_route_t;
typedef PSHALSYSRoutings_t psjm_routes_t;

typedef struct psjm_halport_T{
    int		NodeId;
    int		PortNo;
}psjm_halport_t;

typedef struct psjm_halsend_T{
    psjm_halport_t	Rcpt;
    char		*data;
    char		*xhead;
    unsigned		len;
    unsigned		xlen;
}psjm_halsend_t;

typedef struct psjm_halrecv_T{
    psjm_halport_t	From;
    char		*data;
    char		*xhead;
    int			len;
    int			xlen;
}psjm_halrecv_t;

#define PSJM_SEND_RAW		_IOR (PSHAL_GROUP,500,psjm_send_raw_t)
#define PSJM_RECV_RAW		_IOR (PSHAL_GROUP,501,psjm_recv_raw_t)
#define PSJM_NOP		_IOR (PSHAL_GROUP,502,int)
#define PSJM_SET_ROUTE		PSHAL_MCP_SET_ROUTES
#define PSJM_OPEN_HALPORT	_IOWR(PSHAL_GROUP,504,psjm_halport_t)
#define PSJM_CLOSE_HALPORT	_IOR (PSHAL_GROUP,505,psjm_halport_t)
#define PSJM_HALSEND		_IOWR(PSHAL_GROUP,506,psjm_halsend_t)
#define PSJM_HALRECV		_IOWR(PSHAL_GROUP,507,psjm_halrecv_t)
#define PSJM_HALRECV_POLL	_IOWR(PSHAL_GROUP,508,psjm_halrecv_t)
#define PSJM_HALRECV_BLOCK	_IOWR(PSHAL_GROUP,509,psjm_halrecv_t)
#define PSJM_NOP_COPY		_IOWR(PSHAL_GROUP,510,psjm_halrecv_t)
#define PSJM_HALRECV_DATA	_IOWR(PSHAL_GROUP,511,void *)
#define PSJM_GET_NODEID		PSHAL_MCP_GETID
#define PSJM_SET_NODEID		PSHAL_MCP_SETID
#define PSJM_SETLIC		PSHAL_MCP_SETLIC


#endif /* _JMIO_H_ */




