/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2003 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * name: Description
 *
 * $Id: psport4shm.c,v 1.4 2003/04/07 16:48:42 hauke Exp $
 *
 * @author
 *         Jens Hauke <hauke@par-tec.de>
 *
 * @file
 ***********************************************************/
/*
 * 2001-07-20: initial implementation <Jens Hauke>
 */

static char vcid[] __attribute__(( unused )) =
"$Id: psport4shm.c,v 1.4 2003/04/07 16:48:42 hauke Exp $";

#ifdef XREF
#include <sys/uio.h>
#define size_t int
#endif

#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#include "psport4.h"
#include "pshwtypes.h"

#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <netdb.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/poll.h>
#include "list.h"

#define PSP_DPRINT_LEVEL 0

#define PSP_VER FE
//#define PSP_VER 4

//#define USE_SIGURGCLONE
//#define USE_SIGURGSOCK

#ifdef USE_SIGURGCLONE
#define USE_SIGURG
#else
#ifdef USE_SIGURGSOCK
#define USE_SIGURG
#endif
#endif


//#define DP_SR(fmt,arg... ) printf( fmt ,##arg)
#define DP_SR(fmt,arg... )

//#define DP_CLONE(fmt,arg... ) printf( fmt ,##arg)
#define DP_CLONE(fmt,arg... )

#define DPRINT(level,fmt,arg... ) do{					\
    if ((level)<=env_debug){						\
	printf( "<PSP:"fmt">\n" ,##arg);				\
    }									\
}while(0);

#define SENDMSG
#define RECVMSG


/* Set Debuglevel */
#define ENV_DEBUG     "PSP_DEBUG"
/* Socket options */
#define ENV_SO_SNDBUF "PSP_SO_SNDBUF"
#define ENV_SO_RCVBUF "PSP_SO_RCVBUF"
#define ENV_TCP_NODELAY "PSP_TCP_NODELAY"
#define ENV_SHAREDMEM "PSP_SHAREDMEM"
/* Dont start bgthread (boolean) */
//#define ENV_NOBGTHREAD "PSP_NOBGTHREAD"

/* Debugoutput on signal SIGQUIT (i386:3) (key: ^\) */ 
#define ENV_SIGQUIT "PSP_SIGQUIT"
#define ENV_READAHEAD "PSP_READAHEAD"

static int env_debug = 0;
static int env_so_sndbuf = 16384;
static int env_so_rcvbuf = 16384;
static int env_tcp_nodelay = 1;
static int env_sharedmem = 1;
//static int env_nobgthread = 0;
static int env_sigquit = 0;
static int env_readahead = 100;


/* Check Request Usage, PSP_DPRINT_LEVEL should be >= 1 */
//#define PSP_ENABLE_MAGICREQ

#define PSP_REQ_STATE_FREE		0
/* Recv request */
#define PSP_REQ_STATE_RECV		0x0001
/* Send request */
#define PSP_REQ_STATE_SEND		0x0002

/* PROCESSED is set, if request is finished */
#define PSP_REQ_STATE_PROCESSED		0x8000


#define PSP_REQ_STATE_MASK		0x00f0

/* Recv request states: */
#define PSP_REQ_STATE_RECVPOSTED	0x0010
#define PSP_REQ_STATE_RECVING		0x0020
#define PSP_REQ_STATE_RECVCANCELED	0x0030
#define PSP_REQ_STATE_RECVSHORTREAD     0x0040
#define PSP_REQ_STATE_RECVOK		0x0050
#define PSP_REQ_STATE_RECVGEN		0x0060
#define PSP_REQ_STATE_RECVING2		0x0070

/* Send request states: */
#define PSP_REQ_STATE_SENDPOSTED	0x0010
#define PSP_REQ_STATE_SENDING		0x0020
#define PSP_REQ_STATE_SENDOK		0x0040
#define PSP_REQ_STATE_SENDNOTCON	0x0050

#ifdef PSP_ENABLE_MAGICREQ
#define PSP_MAGICREQ_MASK		0x7fff0000
#define PSP_MAGICREQ_VALID		0x3e570000
#else
#define PSP_MAGICREQ_VALID		0x00000000
#endif

#define PSP_MSGQ_HASHSIZE	4096 /* Power of 2 !!! */
#define PSP_MSGQ_HASH( src )    (( src) & (PSP_MSGQ_HASHSIZE -1))


#define PSP_MIN(a,b) ((a)<(b)?(a):(b))
#define PSP_MAX(a,b) ((a)>(b)?(a):(b))

#define PSP_malloc(size) malloc(size)
#define PSP_free(ptr) free(ptr)

#ifdef USE_SIGURG
static void sigurg( int signal );
static int sigurglock = 0;
static int sigurgretry = 0;

#define PSP_LOCK_INIT  signal( SIGURG, sigurg )   
#define PSP_LOCK    sigurglock = 1
#define PSP_UNLOCK  do{   \
    sigurglock = 0;       \
    if (sigurgretry)      \
	raise( SIGURG );  \
}while(0)

#else
#define PSP_LOCK_INIT   
#define PSP_LOCK   
#define PSP_UNLOCK 
#define PSP_LOCK_DESTROY
#endif


#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#define unused_var(var)     (void)(var);
#define FD_SET2(fd, fdsetp, nfdsp ) do{					\
    FD_SET ((fd), (fdsetp));						\
    *(nfdsp) = PSP_MAX( *(nfdsp), fd + 1 );				\
}while(0)								\

static inline
void FD_CLR2(int fd, fd_set *fds, int *n)
{
    FD_CLR(fd, fds);
    if ((fd + 1) == *n) {
	while ((*n > 0) && !FD_ISSET((*n) - 1, fds))
	    (*n)--; 
    }
}

#define REQ_TO_HEADER( req ) list_entry( (req), PSP_Header_t, Req )
#define REQ_TO_HEADER_NET( req ) PSP_HEADER_NET( REQ_TO_HEADER( req ))


typedef struct PSP_GenRecvReq_T {
    PSP_Request_t	*GenReqHead;
    PSP_Request_t	**GenReqTailP;
    struct PSP_GenRecvReq_T *NextHash;
    int			InHashList; /* 0 if not enqueued in GenReqHostList */
}PSP_GenRecvReq_t;

typedef struct PSP_RecvReqList_T{
    PSP_Request_t	*PostedRecvRequests;
    PSP_Request_t	**PostedRecvRequestsTailP;
    PSP_GenRecvReq_t	*GenReqHashHead;
    PSP_GenRecvReq_t    **GenReqHashTailP;
    PSP_GenRecvReq_t	GenReqHash[PSP_MSGQ_HASHSIZE];
}PSP_RecvReqList_t;


#define PSP_PORTIDMAGIC  (*(uint32_t *)"port")
#define PSP_MAX_CONNS 4096

typedef struct ReadAhead_s{
    int	 n;	/*< Used bytes */
    int	 len;	/*< Allocated bytes */
    char *buf;	/*< Ptr to buffer */
}ReadAhead_t;


/*
 * Shared memory structs
 */

typedef struct shm_msg_s {
    uint32_t len;
    volatile uint32_t msg_type;
} shm_msg_t;

#define SHM_BUFS 8
#define SHM_BUFLEN (8192 - sizeof(shm_msg_t))

#define SHM_MSGTYPE_NONE 0
#define SHM_MSGTYPE_STD	 1

#define SHM_DATA(buf, len) ((char*)(&(buf)->header) - len)

typedef struct shm_buf_s {
    uint8_t _data[SHM_BUFLEN];
    shm_msg_t header;
} shm_buf_t;

typedef struct shm_ctrl_s {
    volatile uint8_t	used;
} shm_ctrl_t;

typedef struct shm_com_s {
    shm_buf_t	buf[SHM_BUFS];
    shm_ctrl_t	ctrl[SHM_BUFS];
} shm_com_t;

typedef struct shm_info_s {
    struct list_head next;
    struct list_head next_send;
    shm_com_t *local_com; /* local */
    shm_com_t *remote_com; /* remote */
    int local_id;
    int remote_id;
    int recv_cur;
    int send_cur;
} shm_info_t;

/*
 * Port struct
 */

#define CON_TYPE_UNUSED	0
#define CON_TYPE_FD	1
#define CON_TYPE_SHM	2

typedef struct con_s {
    int	con_type;
    int con_idx;
    union {
	struct {
	    int		fd;
	    ReadAhead_t	readahead; /*< store data between two reads */
	    int		ufd_idx;
	}		fd;
	shm_info_t	shm;
    } u;
	    
    PSP_Request_t	*PostedSendRequests;
    PSP_Request_t	**PostedSendRequestsTailP;
    PSP_Request_t	*RunningRecvRequest;
} con_t;

typedef struct PSP_Port_s {
    struct PSP_Port_s	*NextPort;
    union {
	struct {
	    uint32_t	magic;
	    uint32_t	portno;
	} id;
    } portid;
    PSP_RecvReqList_t	ReqList;
    int			port_fd;

    con_t conns[PSP_MAX_CONNS];

    struct pollfd ufds[PSP_MAX_CONNS];
    con_t	*ufds_user[PSP_MAX_CONNS];
    int		nufds;
    
    int		min_connid;
//    fd_set	fds_read;
//    fd_set	fds_write;
//    int		nfds;

    struct list_head shm_list;
    struct list_head shm_list_send;
} PSP_Port_t;


typedef struct init_msg_s {
    uint32_t	con_type;
    union {
	struct {
	} fd;
	struct {
	    int64_t shmid;
	} shm;
    } u;
} init_msg_t;

//  #define READAHEADXHEADLENDEFAULT 100
//  static unsigned int ReadAheadXHeadlen;
//  PSP_Header_Net_t *ReadAheadBuf = NULL;

#define TRASHSIZE 65536
char *trash;

static PSP_Port_t *Ports;

/* Bottom half for the requests with "done callbacks" which
 * must be called without any PSP lock */
static PSP_Request_t *ReqBH = NULL;
static PSP_Request_t **ReqBHTailP = &ReqBH;

static int GenReqs = 0;
static int GenReqsUsed = 0;
static int PostedRecvReqs = 0;
static int PostedRecvReqsUsed = 0;
static int PollCalls = 0;


//  static
//  void PSP_SetReadAhead( int len )
//  {
//      ReadAheadXHeadlen = len;
//      ReadAheadBuf = (PSP_Header_Net_t *)realloc(
//  	ReadAheadBuf, sizeof( PSP_Header_Net_t ) + ReadAheadXHeadlen );
//  }

static
char *inetstr( int addr )
{
    static char ret[16];
    sprintf( ret, "%u.%u.%u.%u",
	     (addr >> 24) & 0xff, (addr >> 16) & 0xff,
	     (addr >>  8) & 0xff, (addr >>  0) & 0xff);
    return ret;
}

static
char *dumpstr( void *buf, int size )
{
    static char *ret=NULL;
    char *tmp;
    int s;
    char *b;
    if (ret) free(ret);
    ret = (char *)malloc(size * 5 + 4);
    tmp = ret;
    s = size; b = (char *)buf;
    for (; s ; s--, b++){
	    tmp += sprintf( tmp, "<%02x>", (unsigned char)*b );
    }
#if 0
    *tmp++ = '\'';
    s = size; b = (char *)buf;
    for (; s ; s--, b++){
	    *tmp++ = isprint( *b ) ? *b: '.';
    }
    *tmp++ = '\'';
#endif
    *tmp++ = 0;
    return ret;
}

static
void ReadAHInit(ReadAhead_t *rh, int len)
{
    rh->n = 0;
    rh->len = len;
    rh->buf = (char *)malloc(len);
}

static
void ReadAHCleanup(ReadAhead_t *rh)
{
    if (rh->buf) free(rh->buf);
    rh->buf = NULL;
    rh->n = 0;
    rh->len = 0;
}

/* copy maximal len bytes from from to the header.
   return the number of bytes left in the header */
static inline
int copy_to_header(PSP_Header_t *header, void *from, size_t len)
{
    struct msghdr *mh;

    mh = &header->Req.msgheader;

    while (mh->msg_iovlen) {
	if (mh->msg_iov->iov_len <= len) {
	    memcpy(mh->msg_iov->iov_base, from, mh->msg_iov->iov_len);
	    len            -= mh->msg_iov->iov_len;
	    ((char *)from) += mh->msg_iov->iov_len;
	    mh->msg_iov->iov_len = 0;
	    mh->msg_iov++;
	    mh->msg_iovlen--;
	} else {
	    memcpy(mh->msg_iov->iov_base, from, len);
	    ((char*)mh->msg_iov->iov_base) += len;
	    mh->msg_iov->iov_len           -= len;
	    return 0;
	}
	if (len == 0)
	    return 0;
    };
    if (len) {
	if (header->Req.skip > len) {
	    header->Req.skip -= len;
	    return 0;
	}else{
	    len -= header->Req.skip;
	    header->Req.skip = 0;
	    return len;
	}
    } else {
	return 0;
    }
}

static inline
void AdjustHeader(PSP_Header_t *h, PSP_Header_Net_t *nh)
{
    /* shrink iovec */
    if (h->Req.iovec[0].iov_len > nh->xheaderlen +
	sizeof(PSP_Header_Net_t)) {
	h->Req.iovec[0].iov_len = nh->xheaderlen +
	    sizeof(PSP_Header_Net_t);
    }
    /* skip is always >= 0 */
    h->Req.skip = nh->xheaderlen + sizeof(PSP_Header_Net_t) -
	h->Req.iovec[0].iov_len;

    if (h->Req.iovec[1].iov_len > nh->datalen) {
	h->Req.iovec[1].iov_len = nh->datalen;
    }
    /* skip is always >= 0 */
    h->Req.skip += nh->datalen - h->Req.iovec[1].iov_len;

    /* if skip is > 0 you loose the end of the message.
       In the case the provided xheader is not large enough,
       the databuffer will be corrupted!!! */
    /* ToDo: This wont work (SHORTREAD is not used as a bit) !!! :*/
    if (h->Req.skip) h->Req.state |= PSP_REQ_STATE_RECVSHORTREAD;
}

static inline
void proceed_header(PSP_Header_t *header, size_t len)
{
    struct msghdr *mh;

    mh = &header->Req.msgheader;

    while (mh->msg_iovlen && len) {
	if (mh->msg_iov->iov_len <= len) {
	    len            -= mh->msg_iov->iov_len;
	    mh->msg_iov->iov_len = 0;
	    mh->msg_iov++;
	    mh->msg_iovlen--;
	} else {
	    ((char*)mh->msg_iov->iov_base) += len;
	    mh->msg_iov->iov_len           -= len;
	    break;
	}
    };
}

/**********************************************************************
 *
 *  fd stuff for poll()
 *
 **********************************************************************/

static
void ufd_add(PSP_Port_t *port, con_t *con)
{
    int idx;

    idx = port->nufds;
    port->nufds++;
    
    port->ufds[idx].fd = con->u.fd.fd;
    port->ufds[idx].events = POLLIN;
    port->ufds[idx].revents = 0;
    port->ufds_user[idx] = con;

    con->u.fd.ufd_idx = idx;
}

static
void ufd_add_listen(PSP_Port_t *port)
{
    int idx;

    idx = port->nufds;
    port->nufds++;
    
    port->ufds[idx].fd = port->port_fd;
    port->ufds[idx].events = POLLIN;
    port->ufds[idx].revents = 0;
    port->ufds_user[idx] = NULL;
}

static
void ufd_del(PSP_Port_t *port, con_t *con)
{
    int idx;

    if (con) {
	idx = con->u.fd.ufd_idx;
	con->u.fd.ufd_idx = -1;
    } else {
	/* listener? */
	idx = -1;
	for (idx = 0; idx < port->nufds; idx++) {
	    if (port->ufds_user[idx] == NULL)
		break;
	}
	if (idx < 0) goto err_intern;
    }
    
    port->nufds--;
    
    /* move list down. */
    for (;idx < port->nufds; idx++) {
	port->ufds[idx] = port->ufds[idx + 1];
	port->ufds_user[idx] = port->ufds_user[idx + 1];
	if (port->ufds_user[idx]) {
	    port->ufds_user[idx]->u.fd.ufd_idx = idx;
	}
    }
    return;
 err_intern:
    DPRINT(0, __FUNCTION__ "() : Internal error!\n");
    return;
}

static
void ufd_write_set(PSP_Port_t *port, con_t *con)
{
    if (con->u.fd.ufd_idx >= 0) {
	port->ufds[con->u.fd.ufd_idx].events |= POLLOUT;
    } else {
	DPRINT(0, __FUNCTION__ "() : Internal error!\n");
    }
}

static
void ufd_write_clr(PSP_Port_t *port, con_t *con)
{
    if (con->u.fd.ufd_idx >= 0) {
	port->ufds[con->u.fd.ufd_idx].events &= ~POLLOUT;
    } else {
	DPRINT(0, __FUNCTION__ "() : Internal error!\n");
    }
}

/**********************************************************************
 *
 *  Request List
 *
 **********************************************************************/

static
void InitRequestList(PSP_RecvReqList_t *rl)
{
    int i;
    rl->PostedRecvRequests =NULL;
    rl->PostedRecvRequestsTailP = &rl->PostedRecvRequests;

    rl->GenReqHashHead = NULL;
    rl->GenReqHashTailP = &rl->GenReqHashHead;

    for( i = 0; i < PSP_MSGQ_HASHSIZE; i++) {
	rl->GenReqHash[i].GenReqHead = NULL;
	rl->GenReqHash[i].GenReqTailP = &rl->GenReqHash[i].GenReqHead;
	rl->GenReqHash[i].NextHash = NULL;
	rl->GenReqHash[i].InHashList = 0;
    }
}

static inline
void AddRecvRequest(PSP_RecvReqList_t *rl, PSP_Request_t *req)
{
    req->Next = NULL;
    *rl->PostedRecvRequestsTailP = req;
    rl->PostedRecvRequestsTailP = &req->Next;
    PostedRecvReqs++;
}

static inline
PSP_Request_t *GetPostedRequest(PSP_RecvReqList_t *rl, PSP_Header_Net_t *nh,
				int nhlen, int from)
{
    PSP_Request_t **prev_p;
    PSP_Request_t *req;

    prev_p = &rl->PostedRecvRequests;
    req    =  rl->PostedRecvRequests;

    while (req && (!req->cb(nh, from, req->cb_param))) {
	prev_p = &req->Next;
	req    =  req->Next;
    }

    if (req) {
	// dequeue
	if (!req->Next) {
	    rl->PostedRecvRequestsTailP = prev_p;
	}
	*prev_p = req->Next;
	req->Next = NULL;

	/* Copy already received data: */
	AdjustHeader(REQ_TO_HEADER(req), nh);
	copy_to_header(REQ_TO_HEADER(req), nh, nhlen);
	PostedRecvReqsUsed++;
    }
    return req;
}

static inline
void AddSendRequest(PSP_Port_t *port, PSP_Request_t *req)
{
    unsigned int dest = REQ_TO_HEADER(req)->addr.to;
    con_t *con = &port->conns[dest];
    /* ToDo: Rangecheck and connect check of dest */
    if (!con->PostedSendRequests) {
	if (con->con_type == CON_TYPE_SHM) {
	    list_add_tail(&con->u.shm.next_send, &port->shm_list_send);
	} else if (con->con_type == CON_TYPE_FD) {
	    ufd_write_set(port, con);
	}
    }
    req->Next = NULL;
    *con->PostedSendRequestsTailP = req;
    con->PostedSendRequestsTailP = &req->Next;
}

static inline
void DelFirstSendRequest(PSP_Port_t *port, PSP_Request_t *req)
{
    int dest = REQ_TO_HEADER(req)->addr.to;
    con_t *con = &port->conns[dest];
#ifdef DEBUG
    if (req != PostedSendRequests) {
	fprintf(stderr, "DelFirstSendRequest() error\n");
	exit(1);
    }
#endif
    DP_SR("SFIN xh: %8d dat: %8d\n",
	  REQ_TO_HEADER_NET(req)->xheaderlen,
	  REQ_TO_HEADER_NET(req)->datalen);

    if (req->Next == NULL){ // same as: *PostedSendRequestsTailP == req
	// SendRequestQueue is now empty
	con->PostedSendRequestsTailP = &con->PostedSendRequests;
	if (con->con_type == CON_TYPE_SHM) {
	    list_del(&con->u.shm.next_send);
	} else if (con->con_type == CON_TYPE_FD) {
	    ufd_write_clr(port, con);
	}
    }
    con->PostedSendRequests = req->Next;
    req->Next = NULL;
}

static inline
void AddReqBH( PSP_Request_t *req)
{
    req->Next = NULL;
    *ReqBHTailP = req;
    ReqBHTailP = &req->Next;
}

static inline
PSP_Request_t *GetReqBH( void )
{
    PSP_Request_t *req;
    req = ReqBH;
    if (req->Next == NULL){
	// now ReqBH is empty
	ReqBHTailP = &ReqBH; 
    }
    ReqBH = req->Next;
    return req;
}

#define PSP_NDCBS 10

static inline
void ExecBHandUnlock()
{
    struct {
	PSP_DoneCallback_t	*dcb;
	void			*dcb_param;
	PSP_Request_t		*Req;
    } dcbs[PSP_NDCBS];
    int ndcbs;
    int i;
 restart:
    ndcbs = 0;
    while (ReqBH) {
	PSP_Request_t *req = GetReqBH();
	/* if (req->dcb){ alway true for ReqBH requests */
	dcbs[ndcbs].dcb = req->dcb;
	dcbs[ndcbs].dcb_param = req->dcb_param;
	dcbs[ndcbs].Req = req;
	ndcbs++;
	if (ndcbs == PSP_NDCBS) {
	    break;
	}
    }
    PSP_UNLOCK;

    /* execute the done callbacks (without any lock) */
    for(i = 0; i < ndcbs; i++) {
	dcbs[i].dcb(REQ_TO_HEADER(dcbs[i].Req), dcbs[i].dcb_param);
	dcbs[i].Req->state |= PSP_REQ_STATE_PROCESSED;
    }
    if (ReqBH) {
	/* There are more requests left */
	PSP_LOCK;
	goto restart;
    }
}


static inline
void FinishRequest(PSP_Port_t *port, PSP_Request_t * req)
{
    unused_var(port);
    DP_SR("%pFINI xh: %8d dat: %8d           %s\n",
	  req,
	  REQ_TO_HEADER_NET(req)->xheaderlen,
	  REQ_TO_HEADER_NET(req)->datalen,
	  dumpstr(REQ_TO_HEADER_NET(req)->xheader,16)
	);
    
    if (req->dcb)
	AddReqBH(req);
    else {
	req->state |= PSP_REQ_STATE_PROCESSED;
    }
}

static inline
void FinishRunRecvReq(PSP_Port_t *port, PSP_Request_t *req, con_t *con)
{
    /* Deq the req */
    con->RunningRecvRequest = NULL;
    FinishRequest(port, req);
}




/*********************************************
 * Generated Requests
 */
					      
static inline
PSP_Request_t *SearchGeneratedRequestFromHash(PSP_GenRecvReq_t *hash_,
					      PSP_RecvCallBack_t *cb, void* cb_param)
{					     
    PSP_Request_t *req;
    req = hash_->GenReqHead;

    while (req &&
	   (!cb(REQ_TO_HEADER_NET(req),
		REQ_TO_HEADER(req)->addr.from, cb_param))) {
	req = req->NextGen;
    }
    return req;
}

/* Find a request */
static inline
PSP_Request_t * SearchForGeneratedRequest(PSP_RecvReqList_t *rl,
					  PSP_RecvCallBack_t *cb,
					  void* cb_param, int from)
{
    if (from != PSP_AnySender) {
	return SearchGeneratedRequestFromHash(&rl->GenReqHash[ PSP_MSGQ_HASH( from )],
					      cb, cb_param );
    } else {
	/* Search in all Hashlists */
	PSP_GenRecvReq_t *act;
	PSP_Request_t *req;
	
	act = rl->GenReqHashHead;
	while (act) {
	    if (act->GenReqHead) {
		req = SearchGeneratedRequestFromHash(act, cb, cb_param);
		if (req) {
		    return req;
		}
	    }
	    act = act->NextHash;
	}
	return NULL;
    }
}

static inline
PSP_Request_t *GetGeneratedRequestFromHash(PSP_GenRecvReq_t *hash_,
					   PSP_RecvCallBack_t *cb, void* cb_param)
{					     
    PSP_Request_t **prev_p;
    PSP_Request_t *req;
    
    prev_p = &hash_->GenReqHead;
    req    =  hash_->GenReqHead;

    while (req &&
	   (!cb(
	       REQ_TO_HEADER_NET(req),
	       REQ_TO_HEADER(req)->addr.from, cb_param))) {
	prev_p = &req->NextGen;
	req    =  req->NextGen;
    }
    
    if (req) {
	// dequeue
	*prev_p = req->NextGen;
	if (req->NextGen == NULL) {
	    /* that was the last enqueued request in the list*/
	    hash_->GenReqTailP = prev_p; /* adjust tail */
	    
	    /* In the case, that the list is now empty
	       ( h->GenReqTailP == &h->GenReqHead ) the next call
	       of GetGeneratedRequest(,, PSP_AnySender) should remove h */
	}
	req->NextGen = NULL;
    }
    return req;
}

/* Find and deq a request */
static inline
PSP_Request_t * GetGeneratedRequest(
    PSP_RecvReqList_t *rl, PSP_RecvCallBack_t *cb, void* cb_param, int from)
{
    if (from != PSP_AnySender) {
	return GetGeneratedRequestFromHash(&rl->GenReqHash[ PSP_MSGQ_HASH( from )],
					   cb, cb_param);
    }else{
	/* Search in all Hashlists */
	PSP_GenRecvReq_t **prev_p;
	PSP_GenRecvReq_t *act;
	PSP_Request_t *req;

	prev_p = &rl->GenReqHashHead;
	act    =  rl->GenReqHashHead;

	while (act) {
	    if (act->GenReqHead) {
		req = GetGeneratedRequestFromHash(act, cb, cb_param);
		if (req) {
#define PSP_RECV_PREDICTION
#ifdef  PSP_RECV_PREDICTION
		    if (rl->GenReqHashHead != act) {
			/* i am not the first in the list. */
			/* deq */
			*prev_p = act->NextHash;
			if ( act->NextHash == NULL ){
			    /* that was the end of the hash list */
			    rl->GenReqHashTailP = prev_p;
			}
			/* enq */
			act->NextHash = rl->GenReqHashHead;
			rl->GenReqHashHead = act;
		    }
#endif
		    return req;
		}
		prev_p = &act->NextHash;
		act = act->NextHash;
	    }else{
		/* Empty hashlist. Dequeue it. */
		*prev_p = act->NextHash;
		act->NextHash = NULL;
		act->InHashList = 0;
		
		if (*prev_p == NULL) {
		    /* that was the end of the hash list (Trallala this is the end)*/
		    rl->GenReqHashTailP = prev_p;
		    return NULL; /* We already searched in all lists */
		}

		act = *prev_p; /* equals act->NextHash befor zeroing */
	    }
	}
	return NULL;
    }
}


static inline void
GenReqEnq( PSP_RecvReqList_t *rl, PSP_Request_t *req, int from)
{
    PSP_GenRecvReq_t *h = &rl->GenReqHash[ PSP_MSGQ_HASH( from )];
    /* Enq in Hash */
    req->NextGen = NULL;
    *h->GenReqTailP = req;
    h->GenReqTailP  = &req->NextGen;

    /* Enq in Hashlist (if needed) */
    if (!h->InHashList) {
	h->InHashList = 1;
	h->NextHash = NULL;
	*rl->GenReqHashTailP = h;
	rl->GenReqHashTailP = &h->NextHash;
    }
}

/**
 * @brief Allocate memmory for a generated request.
 *
 * @param rl
 * @param nh pointer to the network PSP header
 * @param nlen len of *nh 
 * @return Returns the generated request
 */
static inline
PSP_Request_t *GenerateRequest(PSP_RecvReqList_t *rl,
			       PSP_Header_Net_t *nh, int nhlen, int from)
{
    PSP_Header_t *header;
    PSP_Request_t *req;
    int packsize;

    packsize = nh->xheaderlen + nh->datalen;
    header = (PSP_Header_t *)PSP_malloc(sizeof(*header) + packsize);
    req = &header->Req;

    DP_SR("%pGENE xh: %8d dat: %8d from %d\n",
	  &header->Req,
	  nh->xheaderlen, nh->datalen, from
  	);

    /* initialize */

    header->Req.Next = NULL;
    header->Req.NextGen	= NULL;
    header->Req.state = PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV | PSP_REQ_STATE_RECVGEN;
    header->Req.UseCnt = 0;
    
    header->Req.iovec[0].iov_base = PSP_HEADER_NET(header);
    header->Req.iovec[0].iov_len  = PSP_HEADER_NET_LEN + nh->xheaderlen;
    header->Req.iovec[1].iov_base = ((char*)PSP_HEADER_NET(header)) +
	PSP_HEADER_NET_LEN + nh->xheaderlen;
    header->Req.iovec[1].iov_len  = nh->datalen;

    header->Req.msgheader.msg_name = NULL;
    header->Req.msgheader.msg_namelen = 0;
    header->Req.msgheader.msg_iov = &header->Req.iovec[0];
    header->Req.msgheader.msg_iovlen = 2;
    header->Req.msgheader.msg_control = NULL;
    header->Req.msgheader.msg_controllen = 0;
    header->Req.msgheader.msg_flags = 0;

    header->Req.skip = 0;
    header->Req.cb = NULL;
    header->Req.cb_param = NULL;
    header->Req.dcb = NULL;
    header->Req.dcb_param = NULL;
    header->addr.from = from;
    
    /* Copy the first part of the message to the new header/data */
    copy_to_header(header, nh, nhlen);
    
    /* Enq req to list */
    GenReqEnq(rl , req, from);
    
    req->UseCnt++;
    GenReqs++;
    return req;
}
    

static inline
void FreeGenRequest(PSP_Request_t * req)
{
    GenReqsUsed++;
    free(REQ_TO_HEADER(req));
}




static
void CleanupRequestList(PSP_Port_t *port, PSP_RecvReqList_t *rl)
{
    PSP_Request_t *req,*nreq;

    req = rl->PostedRecvRequests;
    while (req) {
	req->state =
	    PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV |
	    PSP_REQ_STATE_RECVCANCELED;
	nreq=req->Next;
	FinishRequest(port, req);
	req=nreq;
    }

    while ((req = GetGeneratedRequest(rl, PSP_RecvAny, 0, PSP_AnySender))) {
  	req->state =
  	    PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV |
  	    PSP_REQ_STATE_RECVCANCELED;
  	FinishRequest(port, req);
    }
}


/**********************************************************************
 *
 *  iovec
 *
 **********************************************************************/

/* iovlen : number of blocks in iov. return bytelen of iov */
static inline
size_t iovec_len(struct iovec *iov, size_t iovlen)
{
    size_t len = 0;
    while (iovlen) {
	len += iov->iov_len;
	iov++;
	iovlen--;
    }
    return len;
}

static
void memcpy_fromiovec(char *data, struct iovec *iov, size_t len)
{
    int err = -EFAULT; 
    
    while (len > 0) {
	if (iov->iov_len) {
	    int copy = PSP_MIN(len, iov->iov_len);
	    memcpy(data, iov->iov_base, copy);
	    len -= copy;
	    data += copy;
	    iov->iov_base += copy;
	    iov->iov_len -= copy;
	}
	iov++;
    }
}

static
void iovec_compact(struct iovec **iov, size_t *iovlen)
{
    while (*iovlen && (!(*iov)->iov_len)) {
	(*iov)++;
	(*iovlen)--;
    }
}


/**********************************************************************
 *
 *  Shared Memory
 *
 **********************************************************************/

static
int shm_initrecv(shm_info_t *shm)
{
    int shmid;
    void *buf;
    
    shmid = shmget(/*key*/0, sizeof(shm_com_t), IPC_CREAT | 0777);
    if (shmid == -1) goto err;
    
    buf = shmat(shmid, 0, 0 /*SHM_RDONLY*/);
    if (((int)buf == -1) || !buf) goto err_shmat;

    shmctl(shmid, IPC_RMID, NULL); /* remove shmid after usage */

    memset(buf, 0, sizeof(shm_com_t)); /* init */
    
    shm->local_id = shmid;
    shm->local_com = (shm_com_t *)buf;
    shm->recv_cur = 0;
    return 0;
 err_shmat:
    shmctl(shmid, IPC_RMID, NULL);
 err:
    return -1;
}

static
int shm_initsend(shm_info_t *shm, int rem_shmid)
{
    void *buf;
    buf = shmat(rem_shmid, 0, 0);
    if (((int)buf == -1) || !buf) goto err_shmat;

    shm->remote_id = rem_shmid;
    shm->remote_com = buf;
    shm->send_cur = 0;
    return 0;
 err_shmat:
    return -1;
}


static inline
int shm_cansend(shm_info_t *shm)
{
    return !shm->local_com->ctrl[shm->send_cur].used;
}

static
void shm_send(shm_info_t *shm, char *buf, int len)
{
    int cur = shm->send_cur;
    shm_buf_t *shmbuf = &shm->remote_com->buf[cur];
//    /* wait for unused sendbuffer */
//    while (shm->local_com->ctrl[cur].used) sched_yield();
    shm->local_com->ctrl[cur].used = 1;

    /* copy to sharedmem */
    memcpy(SHM_DATA(shmbuf, len), buf, len);
    /* Notify the new message */
    shmbuf->header.len = len;
    shmbuf->header.msg_type = SHM_MSGTYPE_STD;
    shm->send_cur = (shm->send_cur + 1) % SHM_BUFS;
}

/* send iov.
   Call only after successful shm_cansend() (no check inside)!
   len must be smaller or equal SHM_BUFLEN!
*/
static
void shm_iovsend(shm_info_t *shm, struct iovec *iov, int len)
{
    int cur = shm->send_cur;
    shm_buf_t *shmbuf = &shm->remote_com->buf[cur];

//    /* wait for unused sendbuffer */
//    while (shm->local_com->ctrl[cur].used) sched_yield();
    shm->local_com->ctrl[cur].used = 1;

    /* copy to sharedmem */
    memcpy_fromiovec(SHM_DATA(shmbuf, len), iov, len);

//    printf("Send SHM %d %s\n", len, dumpstr(SHM_DATA(shmbuf, len), PSP_MIN(40, len)));

    /* Notify the new message */
    shmbuf->header.len = len;
    shmbuf->header.msg_type = SHM_MSGTYPE_STD;
    shm->send_cur = (shm->send_cur + 1) % SHM_BUFS;
}

static inline
int shm_canrecv(shm_info_t *shm)
{
    return shm->local_com->buf[shm->recv_cur].header.msg_type
	!= SHM_MSGTYPE_NONE;
}

/* receive.
   Call only after successful shm_canrecv() (no check inside)!
*/
static
void shm_recvstart(shm_info_t *shm, char **buf, int *len)
{
    int cur = shm->recv_cur;
    shm_buf_t *shmbuf = &shm->local_com->buf[cur];
//    while (shm->local_com->buf[cur].header.msg_type == SHM_MSGTYPE_NONE)
//	sched_yield();
    *len = shmbuf->header.len;
    *buf = SHM_DATA(shmbuf, *len);
}

static
void shm_recvdone(shm_info_t *shm)
{
    int cur = shm->recv_cur;
    shm_buf_t *shmbuf = &shm->local_com->buf[cur];

    shmbuf->header.msg_type = SHM_MSGTYPE_NONE;
    /* free buffer */
    shm->remote_com->ctrl[cur].used = 0;
    shm->recv_cur = (shm->recv_cur + 1) % SHM_BUFS;
}



/**********************************************************************
 *
 *  Port Handling
 *
 **********************************************************************/
/*
 *
 * typical scenario:
 * port = AllocPortStructInitialized()
 * port-> ...
 * AddPort(Port);
 * ...
 * port=FindPort(PortNo);
 * ...
 * DelPort(Port);
 * FreePort(Port);
 *
 */




static
void InitPorts()
{
    Ports = NULL;
}

static inline
PSP_Port_t *AllocPortStructInitialized(void)
{
    PSP_Port_t *port;
    port = (PSP_Port_t *)PSP_malloc(sizeof(PSP_Port_t));

    port->NextPort = NULL;
    port->port_fd = -1;
    memset(&port->portid, 0, sizeof(port->portid));
    port->portid.id.magic = PSP_PORTIDMAGIC;
        
    InitRequestList(&port->ReqList);

    {
	int i;
	for(i = 0; i < PSP_MAX_CONNS; i++) {
	    con_t *con = &port->conns[i];
	    con->con_type = CON_TYPE_UNUSED;
	    con->PostedSendRequests = NULL;
	    con->PostedSendRequestsTailP = &con->PostedSendRequests;
	    con->RunningRecvRequest = NULL;
	}

	/* fd */
	port->nufds = 0;
	port->min_connid = PSP_MAX_CONNS;
	/* shm */
	INIT_LIST_HEAD(&port->shm_list);
	INIT_LIST_HEAD(&port->shm_list_send);
    }

    return port;    
}


static inline
void FreePortStruct(PSP_Port_t * port)
{
    PSP_free( port );
}


/* return PortStruct for Next Port, 0 if PortStruct not found */
static inline
PSP_Port_t *NextPort( PSP_Port_t *Port)
{
    if (Port) {
	return Port->NextPort;
    }else{
	return 0;
    }
}

static inline
void AddPort( PSP_Port_t *Port)
{
    if (Port){
	Port->NextPort = Ports;
	Ports = Port;
    }
}

/* return the deleted Port structure, or NULL */
static inline
PSP_Port_t *DelPort(PSP_Port_t *Port)
{
    PSP_Port_t *hash;
    PSP_Port_t **prev_p;

    prev_p=&Ports;
    hash  = Ports;
    
    while (hash && (hash != Port)) {
	prev_p = &hash->NextPort;
	hash   =  hash->NextPort;
    }

    if (hash) {
	/* Dequeue */
	*prev_p = hash->NextPort;
    }
    return hash;
}



static
void ConfigureConnection(PSP_Port_t *port, con_t *con, int fd)
{
    int ret;
    int val;

    con->con_type = CON_TYPE_FD;
    con->u.fd.fd = fd;
    ReadAHInit(&con->u.fd.readahead, env_readahead);

    ufd_add(port, con);
    
    if (env_so_sndbuf) {
	errno = 0;
	val = env_so_sndbuf;
	ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
	DPRINT( 2, "setsockopt(%d, SOL_SOCKET, SO_SNDBUF, [%d], %ld) = %d : %s",
		fd, val, (long)sizeof(val), ret, strerror(errno));
    }
    if (env_so_rcvbuf) {
	errno = 0;
	val = env_so_rcvbuf;
	ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
	DPRINT( 2, "setsockopt(%d, SOL_SOCKET, SO_RCVBUF, [%d], %ld) = %d : %s",
		fd, val, (long)sizeof(val), ret, strerror(errno));
    }
    errno = 0;

    val = env_tcp_nodelay;
    ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    DPRINT( 2, "setsockopt(%d, IPPROTO_TCP, TCP_NODELAY, [%d], %ld) = %d : %s",
	    fd, val, (long) sizeof(val), ret, strerror(errno));
}

static
int GetUnusedCon(PSP_Port_t *port)
{
    int con;
    /* Find a free conn */
    for(con = PSP_MAX_CONNS - 1; con >= 0 ;con--) {
	if (port->conns[con].con_type == CON_TYPE_UNUSED) {
	    port->min_connid = PSP_MIN(port->min_connid, con);
	    port->conns[con].con_idx = con;
	    break;
	}
    }
    return con >= 0 ? con : -1;
}

static
void PSP_CloseCon(PSP_Port_t *port, con_t *con)
{
    switch (con->con_type) {
    case CON_TYPE_UNUSED:
	/* nothing to do */
	break;
    case CON_TYPE_FD:
	/* Close fd */
	if (con->u.fd.fd >= 0) {
	    shutdown(con->u.fd.fd, 2);
	    close(con->u.fd.fd);
	    ufd_del(port, con);
	    con->u.fd.fd = -1;
	}
	ReadAHCleanup(&con->u.fd.readahead);
	break;
    case CON_TYPE_SHM:
	if (con->u.shm.local_com) shmdt(con->u.shm.local_com);
	con->u.shm.local_com = NULL;
	if (con->u.shm.remote_com) shmdt(con->u.shm.remote_com);
	con->u.shm.remote_com = NULL;
	list_del(&con->u.shm.next);
	list_del(&con->u.shm.next_send);
	break;
    default:
	/* ??? */
	break;
    }
    con->con_type = CON_TYPE_UNUSED;
}

static
int writeall(int fd, const void *buf, int count) {
    int len;
    int c = count;

    while (c > 0) {
        len = write(fd, buf, c);
        if (len < 0) {
	    if ((errno == EINTR) || (errno == EINTR))
		continue;
	    else		
		return -1;
	}
        c -= len;
        ((char*)buf) += len;
    }

    return count;
}

static
int readall(int fd, void *buf, int count) {
    int len;
    int c = count;

    while (c > 0) {
        len = read(fd, buf, c);
        if (len <= 0) {
	    if (len < 0) {
		if ((errno == EINTR) || (errno == EAGAIN))
		    continue;
		else		
		    return -1;
	    } else {
		return 0;
	    }
	}
        c -= len;
        ((char*)buf) += len;
    }

    return count;
}

static
void accept_msg_fd(PSP_Port_t *port, con_t *con, init_msg_t *msg)
{
    return; /* nothing to do */
}

static
void init_msg_fd(PSP_Port_t *port, con_t *con)
{
    init_msg_t msg;
    memset(&msg, 0, sizeof(msg));
	   
    msg.con_type = CON_TYPE_FD;
    /* Send init message. Ignore errors. Dont expect answer. */
    writeall(con->u.fd.fd, &msg, sizeof(msg));
}

static
void accept_msg_shm(PSP_Port_t *port, con_t *con, init_msg_t *msg)
{
    shm_info_t shm;
    init_msg_t msg_answ;
    memset(&msg_answ, 0, sizeof(msg_answ));
    
    if (shm_initrecv(&shm)) goto err_shminit;
    if (shm_initsend(&shm, msg->u.shm.shmid)) goto err_shminitsend;

    msg_answ.con_type = CON_TYPE_SHM;
    msg_answ.u.shm.shmid = shm.local_id;

    if (writeall(con->u.fd.fd, &msg_answ, sizeof(msg_answ)) <= 0) goto err_write;
    if (readall(con->u.fd.fd, &msg_answ, sizeof(msg_answ)) <= 0) goto err_read;

    if (msg_answ.con_type != CON_TYPE_SHM) goto err_remote; /* remote cant bind to my id */

    /* shared mem initialised. */
    PSP_CloseCon(port, con); /* close fd */
        
    con->con_type = CON_TYPE_SHM;
    con->u.shm = shm;
    list_add_tail(&con->u.shm.next, &port->shm_list);
    DPRINT(2, "SHAREDMEM conn %d (local shmid %6d/ remote shmid %6d)", con->con_idx,
	   shm.local_id, shm.remote_id);
    
    return;
    /* --- */
 err_remote:
    shmdt(shm.remote_com);
    shmdt(shm.local_com);
    return;
    /* --- */
 err_read:
 err_write:
    shmdt(shm.remote_com);
 err_shminitsend:
    shmdt(shm.local_com);
 err_shminit:
    /* dont accept shared mem. Revert to fd. */
    msg_answ.con_type = CON_TYPE_FD;
    writeall(con->u.fd.fd, &msg_answ, sizeof(msg_answ));
    return;
}

static
void init_msg_shm(PSP_Port_t *port, con_t *con)
{
    init_msg_t msg;
    shm_info_t shm;
	   
    if (shm_initrecv(&shm)) goto err_shminit;

    memset(&msg, 0, sizeof(msg));
    msg.con_type = CON_TYPE_SHM;
    msg.u.shm.shmid = shm.local_id;
    
    /* Send/recv init message. */
    if (writeall(con->u.fd.fd, &msg, sizeof(msg)) <= 0) goto err_write;
    if (readall(con->u.fd.fd, &msg, sizeof(msg)) <= 0) goto err_read;

    if (msg.con_type != CON_TYPE_SHM) goto err_remote;

    if (shm_initsend(&shm, msg.u.shm.shmid)) goto err_shminitsend;

    /* Shared mem initialised. Send ACK. */
    if (writeall(con->u.fd.fd, &msg, sizeof(msg)) <= 0) goto err_write2;

    PSP_CloseCon(port, con);
        
    con->con_type = CON_TYPE_SHM;
    con->u.shm = shm;
    list_add_tail(&con->u.shm.next, &port->shm_list);

    DPRINT(2, "SHAREDMEM conn %d (local shmid %6d/ remote shmid %6d)",
	   con->con_idx, shm.local_id, shm.remote_id);
    
    return;
    /* --- */
 err_shminitsend:
    init_msg_fd(port, con);
 err_remote:
 err_read:
 err_write:
 err_write2:
    shmdt(shm.local_com);
    return;
    /* --- */
 err_shminit:
    init_msg_fd(port, con); /* fd fallback */
    return;
}



static 
void DoAccept(PSP_Port_t *port)
{
    struct sockaddr_in si;
    int len = sizeof(si);
    int con_fd;
    int con_idx;
    con_t *con;
    init_msg_t msg;
    
    con_fd = accept(port->port_fd, (struct sockaddr*)&si, &len);
    if (con_fd < 0) goto err_accept;
    
    con_idx = GetUnusedCon(port);
    if (con_idx < 0) goto err_nocon;
    con = &port->conns[con_idx];

    DPRINT(1, "ACCEPT conn %d from %s:%d", con_idx,
	   inetstr(ntohl(si.sin_addr.s_addr)), ntohs(si.sin_port));
    
    ConfigureConnection(port, con, con_fd);

    /* recv init message. */
    if (readall(con_fd, &msg, sizeof(msg)) <= 0) goto err_read;

    switch (msg.con_type) {
    case CON_TYPE_FD:
	accept_msg_fd(port, con, &msg);
	break;
    case CON_TYPE_SHM:
	accept_msg_shm(port, con, &msg);
	break;
    default:
	DPRINT(0, "ERROR: Receive unkown init message %d!", msg.con_type);
	break;
    }

    return;
    /* --- */
 err_read:
    PSP_CloseCon(port, con);
 err_nocon:
    shutdown(con_fd, 2);
    close(con_fd);
    DPRINT(1, "ACCEPT failed (no free connections)");
    /* --- */
 err_accept:
    DPRINT(1, "ACCEPT failed (accept)");
    return;
}



/* Try to fill the readaheadbuffer with maximal max bytes by reading from
   con_fd, return the number of bytes in the buffer,
   or -1 on problems with the connection */
int ReadAHreadahead(ReadAhead_t *rh, int con_fd, int min, int max)
{
    int len;
    if (max > rh->len) {
	rh->buf = (char*)realloc(rh->buf, max);
    }
    if (rh->n < min) {
#ifdef RECVMSG
	len = recv(con_fd, rh->buf + rh->n,
		   max - rh->n, MSG_NOSIGNAL | MSG_DONTWAIT);
#else
	len = read(con_fd, rh->buf + rh->n,
		   max - rh->n );
#endif
	if (len <= 0) {
	    if (len == 0) errno = ECONNREFUSED;
	    return -1;
	}
	rh->n += len;
    }
    return rh->n;
}

static inline
void ReadAHRemove(ReadAhead_t *rh, int d)
{
    if (rh->n - d > 0) {
	memmove(rh->buf, rh->buf + d, rh->n - d);
    }
    rh->n = rh->n - d;
}

static inline
void ReadAHClear(ReadAhead_t *rh)
{
    rh->n = 0;
}

/* Receive (at least) the beginning of a message.
   return 0, if maybe more data available.
   The readahead buffer is only used inside DoRecvNewMessage.
   If DoRecvNewMessage enqueue a new request in RunningRecvRequest,
   the readaheaed buffer is alway empty!
*/
static inline
int DoRecvNewMessage(PSP_Port_t *port, con_t *con)
{
    int ret;
    PSP_Request_t *req;
    int con_fd = con->u.fd.fd;
    ReadAhead_t *rh = &con->u.fd.readahead;
    int hlen; /*< headerlen ( PSP_Header_Net_t + xhlen ) */
    int rlen; /*< length of the readahbuf */
    int mlen; /*< lenght of the whole message ( PSP_Header_Net_t + xhlen +data ) */
    int minrm; /*< min( rlen, mlen ) */
    int con_idx;

    /* Read at least the PSP_Header_Net_t and the xheader !!! */

    /* Read psport header */
    ret = ReadAHreadahead(rh, con_fd, sizeof(PSP_Header_Net_t), env_readahead);
    if (ret < (int)sizeof(PSP_Header_Net_t))
	goto unfinished;
    
    /* PSP_Header_Net_t is complete. Now read the xheader.
       With luck the first readahead call already read this data. */
    hlen = sizeof(PSP_Header_Net_t) + ((PSP_Header_Net_t *)rh->buf)->xheaderlen;
    ret = ReadAHreadahead(rh, con_fd, hlen, hlen);
    
    if (ret < hlen)
	goto unfinished;

    /* also the xheader is complete and maybe some data from the
       first ReadAHreadahead */

    mlen = hlen + ((PSP_Header_Net_t *)rh->buf)->datalen;
    rlen = ret;
 more:
    minrm = PSP_MIN(rlen, mlen);

    con_idx = con->con_idx;
    /* Search or generate the Request */
    req = GetPostedRequest(&port->ReqList, (PSP_Header_Net_t *)rh->buf, minrm, con_idx);
    if (!req){
	/* receive without posted RecvRequest */
	req = GenerateRequest(&port->ReqList , (PSP_Header_Net_t *)rh->buf,
			      minrm, con_idx );
    }

    REQ_TO_HEADER(req)->addr.from = con_idx;

    if (mlen <= rlen) {
	/* Message is complete */
	/* "port->conns[ con ].RunningRecvRequest = 0" already done */
	FinishRequest(port, req);
	/* remove the used data */
	ReadAHRemove(rh, mlen);
	if (rh->n >= (int)sizeof(PSP_Header_Net_t)) {
	    hlen = sizeof(PSP_Header_Net_t) + ((PSP_Header_Net_t *)rh->buf)->xheaderlen;
	    mlen = hlen + ((PSP_Header_Net_t *)rh->buf)->datalen;
	    rlen = rh->n;
	    if (rh->n >= mlen) {
		/* There is a complet message in the readahead buffer.
		   We MUST read this now, else we will block
		   in the next select forever. */
		goto more;
	    }
	}
	return 1;
    }else{
	/* Enq running req */
	con->RunningRecvRequest = req;
	/* whole readahead buffer used */
	ReadAHClear(rh);
	return 0;
    }
    
 unfinished:
    /* PSP_Header_Net_t not complete */
    if (ret >= 0) {
	return 1;
    }else{
	if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)) {
	    con_idx = con->con_idx;
	    DPRINT(1, "STOPPED conn %d (recvnew : %s)", con_idx,
		   strerror(errno));
	    PSP_CloseCon(port, con);
	}
	return 1;
    }
}


static inline
void DoRecv(PSP_Port_t *port, con_t *con)
{
    PSP_Request_t *req;
    int ret;
 trymore3:
    req = con->RunningRecvRequest;
    if (req) {
	/* continue this request */
	if (req->msgheader.msg_iovlen) {
	trymore1:
#ifdef RECVMSG
	    ret = recvmsg(con->u.fd.fd,
			  &req->msgheader, MSG_NOSIGNAL | MSG_DONTWAIT);
#else
	    if (req->msgheader.msg_iovlen >= 0) {
		ret = read(con->u.fd.fd,
			   req->msgheader.msg_iov->iov_base,
			   req->msgheader.msg_iov->iov_len);
	    }else{
		ret = 0;
	    }
#endif
	    if (ret <= 0) goto err_recvmsg; 
	    proceed_header(REQ_TO_HEADER(req), ret);
	    
	    if ((req->msgheader.msg_iovlen == 0) &&
		(con->RunningRecvRequest->skip == 0)) {
		/* Request Finished */
		FinishRunRecvReq(port, req, con);
	    }else{
		goto trymore1;
	    }
	}else{
	trymore2:
	    /* skip the last bytes from the message (user buffer was to small)*/
	    ret = recv(con->u.fd.fd,
		       trash,
		       PSP_MIN(req->skip, TRASHSIZE),
		       MSG_NOSIGNAL | MSG_DONTWAIT);
	    if (ret <= 0) goto err_recv; 
	    con->RunningRecvRequest->skip -= ret;

	    if (con->RunningRecvRequest->skip == 0) {
		/* Request Finished */
		FinishRunRecvReq(port, req, con);
	    }else{
		goto trymore2;
	    }
	}
    }else{
	/* New message */
	if (!DoRecvNewMessage(port, con) && con->RunningRecvRequest) {
	    goto trymore3;
	}
    }
    
    return;
 err_recv:
 err_recvmsg:
    if (ret == 0) errno = ECONNREFUSED;
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)) {
	DPRINT(1, "STOPPED conn %d (recv : %s)", con->con_idx,
	       strerror(errno));
	PSP_CloseCon(port, con);
    }
    return;
}

static inline
void DoSend(PSP_Port_t *port, con_t *con)
{
    PSP_Request_t *req = con->PostedSendRequests;
    int ret;

#ifdef SENDMSG
    ret = sendmsg(con->u.fd.fd,
		  &req->msgheader, MSG_NOSIGNAL |
#ifdef USE_SIGURGSOCK
		  MSG_OOB|
#endif
		  MSG_DONTWAIT);
#else
    ret = writev(con->u.fd.fd,
		 req->msgheader.msg_iov,
		 req->msgheader.msg_iovlen);
#endif
    
    if (ret < 0) goto err_sendmsg; 

    proceed_header(REQ_TO_HEADER(req), ret);

    if (req->msgheader.msg_iovlen == 0) {
	/* Request Finished */
	req->state |= PSP_REQ_STATE_PROCESSED;
	DelFirstSendRequest(port, req);
    }
    return;
 err_sendmsg:
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)) {
	DPRINT( 1, "STOPPED conn %d (send : %s)", con->con_idx,
		strerror(errno));
	PSP_CloseCon(port, con);
    }
    return;
}

static inline
void DoRecvShm(PSP_Port_t *port, con_t *con)
{
    PSP_Request_t *req;
    int ret;
    char *buf;
    unsigned int size;

    shm_recvstart(&con->u.shm, &buf, &size);
    
    req = con->RunningRecvRequest;
    if (!req) {
	/* New message. Read the header. */
	PSP_Header_Net_t *nethead = (PSP_Header_Net_t *)buf;
        int nhlen = nethead->xheaderlen + PSP_HEADER_NET_LEN;

	/* Search or generate the Request */
	req = GetPostedRequest(&port->ReqList, nethead, nhlen, con->con_idx);
	if (!req) {
	    /* receive without posted RecvRequest */
	    req = GenerateRequest(&port->ReqList, nethead, nhlen, con->con_idx);
	}

	REQ_TO_HEADER(req)->addr.from = con->con_idx;

	buf += nhlen;
	size -= nhlen;

	if (!size) {
	    FinishRequest(port, req);
	    goto out;
	}
	/* Enq running req */
	con->RunningRecvRequest = req;
    }
    
    /* continue this request */
    if (req->msgheader.msg_iovlen) {
	ret = PSP_MIN(size, req->msgheader.msg_iov->iov_len);
	memcpy(req->msgheader.msg_iov->iov_base, buf, ret);
	buf += ret;
	size -= ret;

	proceed_header(REQ_TO_HEADER(req), ret);
	req->skip -= size;
	
	if ((req->msgheader.msg_iovlen == 0) &&
	    (req->skip == 0)) {
	    /* Request Finished */
	    FinishRunRecvReq(port, req, con);
	}
    }else{
	/* skip the last bytes from the message (user buffer was to small)*/
	req->skip -= size;
	if (req->skip == 0) {
	    /* Request Finished */
	    FinishRunRecvReq(port, req, con);
	}
    }
 out:
    shm_recvdone(&con->u.shm);
    return;
}

static inline
void DoSendShm(PSP_Port_t *port, con_t *con)
{
    PSP_Request_t *req = con->PostedSendRequests;
    size_t len;
    
    len = iovec_len(req->msgheader.msg_iov, req->msgheader.msg_iovlen);
    len = PSP_MIN(len, SHM_BUFLEN);

    shm_iovsend(&con->u.shm, req->msgheader.msg_iov, len);

    iovec_compact(&req->msgheader.msg_iov, &req->msgheader.msg_iovlen);
    
    if (req->msgheader.msg_iovlen == 0) {
	/* Request Finished */
	req->state |= PSP_REQ_STATE_PROCESSED;
	DelFirstSendRequest(port, req);
    }
    return;
}

static
int DoSendRecvShm(PSP_Port_t *port)
{
    struct list_head *pos, *next;
    int ret = 0;
    list_for_each_safe(pos, next, &port->shm_list_send) {
	con_t *con = list_entry(pos, con_t, u.shm.next_send);
	if (shm_cansend(&con->u.shm)) {
	    DoSendShm(port, con);
	}
    }
    list_for_each_safe(pos, next, &port->shm_list) {
	con_t *con = list_entry(pos, con_t, u.shm.next);
	if (shm_canrecv(&con->u.shm)) {
	    DoRecvShm(port, con);
	    ret = 1;
	}
    }
    return ret;
}

static 
int DoSendRecvTo(PSP_Port_t *port, int timeout) 
{
    int nfds;
    int i;

    if (!list_empty(&port->shm_list)) {
	/* look inside sharemem regions */
	if (DoSendRecvShm(port))
	    return 1;
	/* switch to polling mode */
	sched_yield();
	timeout = 0;
    }

    if (!port->nufds) return 0;
#if 0
    { /* Hack to stop the listener after some communication */
	static stopped = 0;
	if (!stopped && (PostedRecvReqs > 1000)) {
	    PSP_StopListen((PSP_PortH_t)port);
	    printf("Stop Listen\n");
	    stopped = 1;
	}
    }
#endif
    
    PollCalls++;
    nfds = poll(port->ufds, port->nufds, timeout);

    if (nfds <= 0) return 0;

    for (i = 0; i < port->nufds; i++) {
	if (port->ufds[i].revents & POLLOUT) {
	    con_t *con = port->ufds_user[i];
	    if (con) {
		DoSend(port, con);
	    }
	    if (!(--nfds)) return 1;
	}
	if (port->ufds[i].revents & POLLIN) {
	    con_t *con = port->ufds_user[i];
	    if (con) {
		DoRecv(port, con);
	    } else {
		/* listener */
		if (port->port_fd >= 0) {
		    DoAccept(port);
		}
	    }
	    if (!(--nfds)) return 1;
	}
    }
    return 1;
}

static inline
void DoSendRecv(PSP_Port_t *port)
{
    int ret;
//  do{
    ret = DoSendRecvTo(port, 0);
//  } while (ret);
}

static inline
void DoSendRecvWait(PSP_Port_t *port)
{
    int ret;
    /* ToDo: Call DoSendRecvTo with NULL! */
//  do{
    ret = DoSendRecvTo(port, -1);
//  } while (ret);
}

#ifdef USE_SIGURG
static
void sigurg(int signal)
{
    PSP_Port_t *port;
    unused_var(signal);
    if (!sigurglock) {
	sigurgretry = 0;
	DP_CLONE(" +++++++++ SIGURG START+++++++++ \n");
	for (port = Ports; port; port = port->NextPort) {
	    DoSendRecv(port);
	}
	DP_CLONE(" +++++++++ SIGURG END  +++++++++ \n");
    } else {
	DP_CLONE(" +++++++++ SIGURG IGNORE  ++++++ \n");
	sigurgretry = 1;
    }	
}
#endif

static
void sigquit(int signal)
{
    PSP_Port_t *port;
    int i;
    unused_var(signal);
    printf(" +++++++++ SIGQUIT START ++++ \n");
    printf(" GenReq:%d (%d) PostedReq: %d(%d) fd_polls: %d\n",
	   GenReqs-GenReqsUsed, GenReqs,
	   PostedRecvReqs-PostedRecvReqsUsed, PostedRecvReqs,
	   PollCalls);

    for (port = Ports; port; port = port->NextPort) {
	printf(" npollfd: %d (listener: %s)\n", port->nufds,
	       port->port_fd >= 0 ? "yes" : "no");

	for (i = 0; i < PSP_MAX_CONNS; i++) {
	    con_t *con = &port->conns[i];
	    int cnt = 0;
	    if (con->con_type != CON_TYPE_UNUSED) { /* Count send requests */
		PSP_Request_t *req = con->PostedSendRequests;
		while (req) {
		    req = req->Next;
		    cnt++;
		}
	    }
	    
	    switch (con->con_type) {
	    case CON_TYPE_UNUSED:
		break;
	    case CON_TYPE_FD:
		printf("port: %6d/%3d snd %d FD %3d rhbuf: %s\n",
		       port->portid.id.portno, i, cnt,
		       con->u.fd.fd,
		       dumpstr(con->u.fd.readahead.buf, con->u.fd.readahead.n));
		break;
	    case CON_TYPE_SHM:
		printf("port: %6d/%3d snd %d SHM %6d - %6d\n",
		       port->portid.id.portno, i, cnt,
		       con->u.shm.local_id, con->u.shm.remote_id);
		break;
	    default:
		printf("port: %6d/%3d snd %d UNKNOWN\n",
		       port->portid.id.portno, i, cnt);
		break;
	    }		
	}
    }
    printf(" +++++++++ SIGQUIT END ++++++ \n");
}


#ifdef USE_SIGURGCLONE
#include <sched.h>

#define bg_thread_stack_size 8192
static
char bg_thread_stack[bg_thread_stack_size];
//       int  __clone(int (*fn) (void *arg), void *child_stack, int
//      flags, void *arg)

int bg_pid = -1;

static
int PSP_bg_thread(void *arg)
{
    int parent = (int)arg;
    fd_set fds_read;
    fd_set fds_write;
    int nfds;
    struct timeval to;
    bg_pid = getpid();
    
    while (1){
	if (Ports){
	    nfds = Ports->nfds;
	    fds_read = Ports->fds_read;
	    fds_write = Ports->fds_write;
	    to.tv_sec = 5;
	    to.tv_usec = 0;
	    DP_CLONE("PSP_bg_thread() sellect  ##########\n");
	    nfds = select( nfds, &fds_read, &fds_write, NULL, &to );
	    if (nfds > 0){
		DP_CLONE("PSP_bg_thread() wakeup   ##########\n");
		kill( parent, SIGURG );
	    }else{
		/* timeout happens */
		DP_CLONE("PSP_bg_thread() timeout  ##########\n");
	    }
	}else{
	    DP_CLONE("PSP_bg_thread() sleep    ##########\n");
	    sleep(1);
	    DP_CLONE("PSP_bg_thread() slwakeup ##########\n");
	}
    }
}

	   
void init_clone()
{
    clone(PSP_bg_thread,&bg_thread_stack[bg_thread_stack_size-8],
	  CLONE_VM | CLONE_FS | CLONE_FILES ,(void*)getpid());

}
#endif

static void
intgetenv(int *val, char *name)
{
    char *aval;

    aval = getenv(name);
    if (aval) {
	*val = atoi(aval);
	DPRINT(1, "set %s = %d", name, *val);
    } else {
	DPRINT(2, "default %s = %d", name, *val);
    }
}

static
void init_env(void)
{
    intgetenv(&env_debug, ENV_DEBUG);
    DPRINT(1,"# Version(PS4SHM): %s", vcid);
    intgetenv(&env_so_sndbuf, ENV_SO_SNDBUF);
    intgetenv(&env_so_rcvbuf, ENV_SO_RCVBUF);
    intgetenv(&env_tcp_nodelay, ENV_TCP_NODELAY);
    intgetenv(&env_sharedmem, ENV_SHAREDMEM);
//    intgetenv(&env_nobgthread, ENV_NOBGTHREAD);
    intgetenv(&env_sigquit, ENV_SIGQUIT);
    intgetenv(&env_readahead, ENV_READAHEAD);
    env_readahead = PSP_MAX(env_readahead, (int)sizeof(PSP_Header_Net_t));
}



/**********************************************************************/
int PSP_Init()
/**********************************************************************/
{
    static int init=0;

    if (init) return 0;
    init = 1;

    init_env();
#ifdef USE_SIGURGCLONE
    init_clone();
#endif    
    PSP_LOCK_INIT;
    PSP_LOCK;

    if (env_sigquit)
	signal(SIGQUIT, sigquit);

    /* Initialize Ports */
    InitPorts();
//    PSP_SetReadAhead( READAHEADXHEADLENDEFAULT );
    trash = (char *)malloc(TRASHSIZE);
    PSP_UNLOCK;
    
    return 0;
}

/* deprecated Interface */
unsigned int PSP_UsedHW(void)
{
    return 0x0000; /* Any */
}

char **PSP_HWList(void)
{
    static char *HWList[] = {
	PSHW_NAME_ETHERNET,
	NULL
    };
    
    return HWList;
}


/**********************************************************************/
int PSP_GetNodeID(void)
/**********************************************************************/
{
    struct hostent *mhost;
    char myname[256];
    static uint32_t id = 0;

    if (!id) {
	/* ToDo: This was psid code: Maybe buggy! */
	/* Lookup hostname */
	gethostname(myname, sizeof(myname));
	
	/* Get list of IP-addresses */
	mhost = gethostbyname(myname);
	
	if (!mhost) goto err_nohostent;
	
	/*
	  while (*mhost->h_addr_list) {
	  printf( " -Addr: %08x\n", 
	  ((struct in_addr *) *mhost->h_addr_list)->s_addr);
	  mhost->h_addr_list++;
	  }
	*/
	id = ntohl(*(int *)*mhost->h_addr_list);
    }
    return id;
 err_nohostent:
    fprintf(stderr, __FUNCTION__ "(): gethostbyname() failed\n");
    exit(1);
}


/**********************************************************************/
PSP_PortH_t PSP_OpenPort(int portno)
/**********************************************************************/
{
    PSP_Port_t * port;
    int i;
    int ret;
    
    PSP_LOCK;

    port = AllocPortStructInitialized();
    if (!port) goto err_noport;
    
    if (portno == PSP_ANYPORT) {
	srandom(getpid());
	port->portid.id.portno = (uint16_t)random();
    } else {
	port->portid.id.portno = portno;
    }

    port->port_fd = socket(PF_INET , SOCK_STREAM, 0);
    
    if (port->port_fd < 0) goto err_nosocket;
    
    for (i = 0; i < 300; i++) {
	struct sockaddr sa;
	struct sockaddr_in *si= (struct sockaddr_in *)&sa;
	si->sin_family = PF_INET;
	si->sin_port = htons(port->portid.id.portno);
	si->sin_addr.s_addr = INADDR_ANY;
	ret = bind( port->port_fd, &sa, sizeof(sa));
	if (!ret)
	    ret = listen(port->port_fd, 64);
	if (!ret) {
	    ufd_add_listen(port);
	    break; /* Bind and listen ok */
	}
	if (portno != PSP_ANYPORT) break; /* Bind failed */
	/* try another number */
	port->portid.id.portno = (uint16_t)random();
    }

    if (ret) goto err_bind;

    AddPort(port);

    PSP_UNLOCK;
    return (PSP_PortH_t)port;
    
 err_bind:
    errno = -ret;
    close(port->port_fd);
 err_nosocket:
    FreePortStruct(port);
 err_noport:
    PSP_UNLOCK;
    return (PSP_PortH_t)NULL;
}


/**********************************************************************/
void PSP_StopListen(PSP_PortH_t porth)
/**********************************************************************/
{
    PSP_Port_t *port = (PSP_Port_t *)porth;

    if (port->port_fd < 0) return;

    close(port->port_fd);

    ufd_del(port, NULL);

    port->port_fd = -1;
}



/**********************************************************************/
int PSP_ClosePort(PSP_PortH_t porth)
/**********************************************************************/
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
    int i;
    
    PSP_LOCK;

    for (i = 0; i < PSP_MAX_CONNS; i++) {
	PSP_CloseCon(port, &port->conns[i]);
    }
    
    DelPort(port);
    CleanupRequestList(port, &port->ReqList);
    close(port->port_fd);
    FreePortStruct(port);

    ExecBHandUnlock();
    
    return 0;
}

static
int mtry_connect(int sockfd, const struct sockaddr *serv_addr,
		 socklen_t addrlen)
{
/* In the case the backlog (listen) is smaller than the number of
   processes, the connect could fail with ECONNREFUSED even though
   there is a linstening socket. mtry_connect() retry four times
   the connect after one second delay.
*/
    int i;
    int ret = 0;
    struct sockaddr_in *sa = (struct sockaddr_in*)serv_addr;
    for (i = 0; i < 4; i++) {
	ret = connect(sockfd, serv_addr, addrlen);
	if (ret >= 0) break;
	if (errno != ECONNREFUSED) break;
	sleep(1);
	DPRINT(2, "Retry %d CONNECT to %s:%d",
	       i + 1,
	       inetstr(ntohl(sa->sin_addr.s_addr)),
	       ntohs(sa->sin_port));
    }
    return ret;
}


int PSP_Connect_(PSP_PortH_t porth, struct sockaddr *sa, socklen_t addrlen)
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
    int con_idx;
    con_t *con;
    int con_fd;
    int rem_id;
    rem_id = ntohl(((struct sockaddr_in*)sa)->sin_addr.s_addr);
    /* Find a free conn */
    con_idx = GetUnusedCon(port);
    if (con_idx < 0) goto err_nofreecon;

    con = &port->conns[con_idx];
    
    /* Open the socket */
    con_fd = socket(PF_INET , SOCK_STREAM, 0);
    if (con_fd < 0) goto err_socket;

    /* Connect */
    if (mtry_connect(con_fd, sa, addrlen) < 0) goto err_connect;

    ConfigureConnection(port, con, con_fd);

    DPRINT(1, "CONNECT conn %d to %s:%d",
	   con_idx, inetstr(rem_id),
	   ntohs(((struct sockaddr_in*)sa)->sin_port));

    if ((rem_id == PSP_GetNodeID()) && env_sharedmem) {
	/* Try shared mem */
	init_msg_shm(port, con);
    } else {
	init_msg_fd(port, con);
    }

    return con_idx;
 err_connect:
 err_socket:
    return -1;
 err_nofreecon:
    errno = ENOMEM;
    return -1;
}


int PSP_Connect(PSP_PortH_t porth, int nodeid, int portno)
{
    struct sockaddr_in si;

    si.sin_family = PF_INET;
    si.sin_port = htons(portno);
    si.sin_addr.s_addr = htonl(nodeid);

    /* ToDo: Change to the communication network (dependend on some environment) */
    
    return PSP_Connect_(porth, (struct sockaddr *)&si, sizeof(si));
}


/**********************************************************************/
int PSP_GetPortNo(PSP_PortH_t porth)
/**********************************************************************/
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
    if (port)
	return port->portid.id.portno;
    else
	return -1;
}

static inline
PSP_RequestH_t ISendLoopback(PSP_PortH_t porth,
			     void* buf, unsigned buflen,
			     PSP_Header_t* header, unsigned xheaderlen)
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
    PSP_Request_t *req;
    header->Req.state = PSP_MAGICREQ_VALID | PSP_REQ_STATE_SEND |
	PSP_REQ_STATE_SENDOK| PSP_REQ_STATE_PROCESSED;
    header->addr.to = PSP_DEST_LOOPBACK;
    header->xheaderlen = xheaderlen;
    header->datalen = buflen;
    
    PSP_LOCK;

    /* Search or generate the Request */
    req = GetPostedRequest(&port->ReqList, PSP_HEADER_NET(header),
			   sizeof(PSP_Header_Net_t) + xheaderlen,
			   PSP_DEST_LOOPBACK);
    if (!req) {
	/* receive without posted RecvRequest */
	req = GenerateRequest(&port->ReqList , PSP_HEADER_NET(header),
			      sizeof(PSP_Header_Net_t) + xheaderlen,
			      PSP_DEST_LOOPBACK);
    }
    
    REQ_TO_HEADER(req)->addr.from = PSP_DEST_LOOPBACK;
    
    /*
      If datalen equal zero, the request is finished.
      This is equivalent to:
      (iovec[1].iov_len == 0) && (skip == 0))
    */
    if (buflen) {
	/* Copy the data to req */
	copy_to_header(REQ_TO_HEADER(req), buf, buflen);
	/* Now (iovec[1].iov_len == 0) && (skip == 0)) is true */
    }
    
    FinishRequest(port, req);

    ExecBHandUnlock();

    return (PSP_RequestH_t) header;
}


/**********************************************************************/
PSP_RequestH_t PSP_ISend(PSP_PortH_t porth,
			 void* buf, unsigned buflen,
			 PSP_Header_t* header, unsigned xheaderlen,
			 int dest, int flags)
/**********************************************************************/
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
#ifdef PSP_ENABLE_MAGICREQ
    if ((header->state & (PSP_MAGICREQ_MASK | PSP_REQ_STATE_PROCESSED))
	 == PSP_MAGICREQ_VALID) {
	PSP_DPRINT(1, "WARNING: PSP_ISendCB called with used header\n");
    }
#endif
    unused_var(flags);
    DP_SR("SEND xh: %8d dat: %8d to %d            %s\n",
	  xheaderlen, buflen, dest,
	  dumpstr(header->xheader, 16)
  	);

    if ((unsigned int)dest >= PSP_MAX_CONNS) {
	if (dest == PSP_DEST_LOOPBACK) {
	    return ISendLoopback(porth, buf, buflen, header, xheaderlen);
	}else{
	    errno = EINVAL;
	    goto err_inval;
	}
    }
    
    header->Req.Next = NULL;
    header->Req.state = PSP_MAGICREQ_VALID | PSP_REQ_STATE_SEND | PSP_REQ_STATE_SENDPOSTED;
    header->Req.UseCnt = 0;
    header->Req.iovec[0].iov_base = PSP_HEADER_NET(header);
    header->Req.iovec[0].iov_len  = xheaderlen + PSP_HEADER_NET_LEN;
    header->Req.iovec[1].iov_base = buf;
    header->Req.iovec[1].iov_len  = buflen;
    header->Req.msgheader.msg_name = NULL;
    header->Req.msgheader.msg_namelen = 0;
    header->Req.msgheader.msg_iov = &header->Req.iovec[0];
    header->Req.msgheader.msg_iovlen = 2;
    header->Req.msgheader.msg_control = NULL;
    header->Req.msgheader.msg_controllen = 0;
    header->Req.msgheader.msg_flags = 0;

    header->addr.to = dest;
    header->xheaderlen = xheaderlen;
    header->datalen = buflen;

    if (header->Req.iovec[1].iov_len == 0) {
	/* Allow datalen == 0 */
	header->Req.msgheader.msg_iovlen = 1;
    }

//    printf("ISend %d %s", xheaderlen + PSP_HEADER_NET_LEN,
//	   dumpstr(PSP_HEADER_NET(header), PSP_MIN(32, xheaderlen + PSP_HEADER_NET_LEN)));
//    printf(" data %d %s\n", buflen, dumpstr(buf, PSP_MIN(buflen, 32)));
    
    PSP_LOCK;

    if (port->conns[dest].con_type == CON_TYPE_UNUSED) goto err_notconnected; 
    
    AddSendRequest(port, &header->Req);
    DoSendRecv(port);

    ExecBHandUnlock();

    /* req not valid hereafter ! */
    
    return (PSP_RequestH_t) header;
 err_notconnected:
    errno = ECONNREFUSED;
    PSP_UNLOCK;
 err_inval:
    DPRINT(1, "SEND to conn %d failed : %s", dest, strerror(errno));
    header->Req.state = PSP_MAGICREQ_VALID |
	PSP_REQ_STATE_SENDNOTCON | PSP_REQ_STATE_PROCESSED;
    return (PSP_RequestH_t) header;
}


int PSP_RecvAny(PSP_Header_Net_t* header, int from, void *param)
{
    unused_var(header);
    unused_var(from);
    unused_var(param);
    return 1;
}


    
int PSP_RecvFrom(PSP_Header_Net_t* header, int from, void *param)
{
    PSP_RecvFrom_Param_t *p = (PSP_RecvFrom_Param_t *)param;
    unused_var(header);
    return from == p->from;
}

PSP_RequestH_t PSP_IReceiveCBFrom(PSP_PortH_t porth,
				  void* buf, unsigned buflen,
				  PSP_Header_t* header, unsigned xheaderlen,
				  PSP_RecvCallBack_t *cb, void* cb_param,
				  PSP_DoneCallback_t *dcb, void* dcb_param,
				  int sender)
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
    PSP_Request_t *req;

#ifdef PSP_ENABLE_MAGICREQ
    if ((header->state & (PSP_MAGICREQ_MASK | PSP_REQ_STATE_PROCESSED))
	 == PSP_MAGICREQ_VALID) {
	PSP_DPRINT(1, "WARNING: PSP_IReceiveCB called with used header\n");
    }
#endif
    DP_SR("%pRECV xh: %8d dat: %8d from %d %s\n",
	  &header->Req, xheaderlen, buflen, sender,
	  dumpstr(header->xheader, 16)
  	);

    if (!cb)
	cb = PSP_RecvAny;

    /* Initialize the header */
    /* ToDo: Move most of the initialisation to the headerfile and
       reduce the number of parameters of this function */
    header->Req.Next = NULL;
    header->Req.state = PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV | PSP_REQ_STATE_RECVPOSTED;
    header->Req.UseCnt = 0;
    header->Req.iovec[0].iov_base = PSP_HEADER_NET(header);
    header->Req.iovec[0].iov_len  = xheaderlen + PSP_HEADER_NET_LEN;
    header->Req.iovec[1].iov_base = buf;
    header->Req.iovec[1].iov_len  = buflen;

    header->Req.msgheader.msg_name = NULL;
    header->Req.msgheader.msg_namelen = 0;
    header->Req.msgheader.msg_iov = &header->Req.iovec[0];
    header->Req.msgheader.msg_iovlen = 2;
    header->Req.msgheader.msg_control = NULL;
    header->Req.msgheader.msg_controllen = 0;
    header->Req.msgheader.msg_flags = 0;

    header->Req.cb = cb;
    header->Req.cb_param = cb_param;
    header->Req.dcb = dcb;
    header->Req.dcb_param = dcb_param;
    header->addr.from = sender;

    PSP_LOCK;

    req = GetGeneratedRequest(&port->ReqList, cb, cb_param, sender);
    if (req) {
	/* Message already partially arrived: copy this part. */
	AdjustHeader(header, REQ_TO_HEADER_NET(req));
	copy_to_header(header, REQ_TO_HEADER_NET(req),
		       sizeof( PSP_Header_Net_t ) +
		       REQ_TO_HEADER_NET(req)->xheaderlen +
		       REQ_TO_HEADER_NET(req)->datalen -
		       req->iovec[0].iov_len -
		       req->iovec[1].iov_len);
	header->addr.from = REQ_TO_HEADER(req)->addr.from;
	header->Req.state = req->state;

	if ((header->addr.from != PSP_DEST_LOOPBACK) &&
	    (port->conns[header->addr.from].RunningRecvRequest == req)) {
	    /* RunningRecvRequest == req means: The request is not finished */
	    /* Replace the running request */
	    port->conns[header->addr.from].RunningRecvRequest =
		&header->Req;
	} else {
	    /* Finish the request. Clear PSP_REQ_STATE_PROCESSED,
	       maybe we have to call a done callback before setting
	       PSP_REQ_STATE_PROCESSED. */
	    req->state &= ~PSP_REQ_STATE_PROCESSED;
	    FinishRequest(port, &header->Req);
	}
	FreeGenRequest(req);
    } else {
	// Enqueue new Request.
	AddRecvRequest(&port->ReqList, &header->Req);
    }
    ExecBHandUnlock();
    
    return (PSP_RequestH_t) header;
}




/**********************************************************************/
int PSP_IProbeFrom(PSP_PortH_t porth,
		   PSP_Header_t* header, unsigned xheaderlen,
		   PSP_RecvCallBack_t *cb, void* cb_param, int sender)
/**********************************************************************/
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
    PSP_Request_t *req;
    int ret=0;

#ifdef PSP_ENABLE_MAGICREQ
    if ((header->state & (PSP_MAGICREQ_MASK | PSP_REQ_STATE_PROCESSED))
	== PSP_MAGICREQ_VALID) {
	PSP_DPRINT(1, "WARNING: PSP_IReceiveCB called with used header\n");
    }
#endif

    if (!cb) cb = PSP_RecvAny;
    
    PSP_LOCK;
    
    DoSendRecv(port); /* important */
    req = SearchForGeneratedRequest(&port->ReqList, cb, cb_param, sender);
    if (req) {
	memcpy(PSP_HEADER_NET(header),
	       REQ_TO_HEADER_NET(req),
	       PSP_HEADER_NET_LEN +
	       PSP_MIN(xheaderlen, REQ_TO_HEADER(req)->xheaderlen));
	header->addr.from = REQ_TO_HEADER(req)->addr.from;
	ret = 1;
    }
    
    ExecBHandUnlock();
    return ret;
}



/**********************************************************************/
int PSP_ProbeFrom(PSP_PortH_t porth,
		  PSP_Header_t* header, unsigned xheaderlen,
		  PSP_RecvCallBack_t *cb, void* cb_param,
		  int sender)
/**********************************************************************/
{
    PSP_Port_t *port = (PSP_Port_t *)porth;

    while (!PSP_IProbeFrom(porth, header, xheaderlen, cb, cb_param, sender)) {
	PSP_LOCK;
	DoSendRecvWait(port);
	ExecBHandUnlock();
    }
    return 1;
}


/**********************************************************************/
PSP_Status_t PSP_Test(PSP_PortH_t porth, PSP_RequestH_t request)
/**********************************************************************/
{
    PSP_Header_t* header = (PSP_Header_t*)request;
    PSP_Port_t *port=(PSP_Port_t *)porth;
#ifdef PSP_ENABLE_MAGICREQ
    if ((header->state & (PSP_MAGICREQ_MASK))
	 != PSP_MAGICREQ_VALID) {
	PSP_DPRINT(1, "WARNING: PSP_Test called with uninitialized request\n");
    }
#endif
    PSP_LOCK;
    DoSendRecv(port);
    ExecBHandUnlock();
//    PSP_UNLOCK;
    if (header->Req.state & PSP_REQ_STATE_PROCESSED) {
//	PSP_DPRINT(3,"PSP_Test: PSP_SUCCESS\n");
	return PSP_SUCCESS;
    }else{
//	PSP_DPRINT(3,"PSP_Test: PSP_NOT_COMPLETE\n");
	return PSP_NOT_COMPLETE;
    }
}


/**********************************************************************/
PSP_Status_t PSP_Wait(PSP_PortH_t portH, PSP_RequestH_t request)
/**********************************************************************/
{
    PSP_Port_t *port=(PSP_Port_t *)portH;
    PSP_Header_t* header = (PSP_Header_t*)request;
#ifdef PSP_ENABLE_MAGICREQ
    if ((header->state & (PSP_MAGICREQ_MASK))
	 != PSP_MAGICREQ_VALID){
	PSP_DPRINT(1, "WARNING: PSP_Wait called with uninitialized request\n");
    }
#endif

//      PSP_LOCK;
//      DoSendRecv(port);
//      ExecBHandUnlock();

    while (!(header->Req.state & PSP_REQ_STATE_PROCESSED)) {
	PSP_LOCK;
	DoSendRecvWait(port);
	ExecBHandUnlock();
    }

    return PSP_SUCCESS;
}





/**********************************************************************/
PSP_Status_t PSP_Cancel(PSP_PortH_t port, PSP_RequestH_t request)
/**********************************************************************/
{
// ToDo: Implement PSP_Cancel
    static int cancelwarn = 0;
    unused_var(port);
    unused_var(request);
    if (!cancelwarn)
	fprintf(stderr, "PSP_Cancel() not implementet yet\n");
    cancelwarn = 1;
    return PSP_SUCCESS;
}


/*
  Local Variables:
  c-basic-offset: 4
  c-backslash-column: 72
  End:
*/
