/***********************************************************
 *                  ParaStation4
 *
 *       Copyright (c) 2002 ParTec AG Karlsruhe
 *       All rights reserved.
 ***********************************************************/
/**
 * name: Description
 *
 * $Id: psport4.c,v 1.12 2003/02/25 11:14:12 hauke Exp $
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
"$Id: psport4.c,v 1.12 2003/02/25 11:14:12 hauke Exp $";

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
/* Dont start bgthread (boolean) */
#define ENV_NOBGTHREAD "PSP_NOBGTHREAD"

/* Debugoutput on signal SIGQUIT (i386:3) (key: ^\) */ 
#define ENV_SIGQUIT "PSP_SIGQUIT"
#define ENV_READAHEAD "PSP_READAHEAD"

static int env_debug = 0;
static int env_so_sndbuf = 16384;
static int env_so_rcvbuf = 16384;
static int env_tcp_nodelay = 1;
static int env_nobgthread = 0;
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

#define unused_var( var )     (void)(var);
#define FD_SET2(fd, fdsetp, nfdsp ) do{					\
    FD_SET ((fd), (fdsetp));						\
    *(nfdsp) = PSP_MAX( *(nfdsp), fd + 1 );				\
}while(0)								\

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
#if PSP_VER == 4
    PSP_Request_t	*RunningRequests;
#endif
    PSP_GenRecvReq_t	*GenReqHashHead;
    PSP_GenRecvReq_t    **GenReqHashTailP;
    PSP_GenRecvReq_t	GenReqHash[PSP_MSGQ_HASHSIZE];
}PSP_RecvReqList_t;


#define PSP_PORTIDMAGIC  (*(uint32_t *)"port")
#define PSP_FE_MAX_CONNS 1024

typedef struct ReadAhead_s{
    int	 n;	/*< Used bytes */
    int	 len;	/*< Allocated bytes */
    char *buf;	/*< Ptr to buffer */
}ReadAhead_t;


typedef struct PSP_Port_s{
    struct PSP_Port_s	*NextPort;
    union{
#if PSP_VER == 4
	p4_addr_t	portid;
#endif
	struct{
	    uint32_t	magic;
	    uint32_t	portno;
	}id;
    } portid;
    PSP_RecvReqList_t	ReqList;
    int			port_fd;
#if PSP_VER == FE
    struct{
	int	con_fd;
	ReadAhead_t	readahead; /*< store data between two reads */
	PSP_Request_t	*PostedSendRequests;
	PSP_Request_t	**PostedSendRequestsTailP;
	PSP_Request_t	*RunningRecvRequest;
    } conns[PSP_FE_MAX_CONNS];
    int		min_connid;
    fd_set	fds_read;
    fd_set	fds_write;
    int		nfds;
#endif
#if PSP_VER == 4
    PSP_Request_t	*PostedSendRequests;
    PSP_Request_t	**PostedSendRequestsTailP;
#endif
}PSP_Port_t;


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

static inline void ReadAHInit( ReadAhead_t *rh, int len )
{
    rh->n = 0;
    rh->len = len;
    rh->buf = (char *)malloc( len );
}

/* copy maximal len bytes from from to the header.
   return the number of bytes left in the header */
static inline
int copy_to_header( PSP_Header_t *header, void *from, size_t len )
{
    struct msghdr *mh;

    mh = &header->Req.msgheader;

    while ( mh->msg_iovlen ){
	if ( mh->msg_iov->iov_len <= len ){
	    memcpy( mh->msg_iov->iov_base, from, mh->msg_iov->iov_len);
	    len            -= mh->msg_iov->iov_len;
	    ((char *)from) += mh->msg_iov->iov_len;
	    mh->msg_iov->iov_len = 0;
	    mh->msg_iov++;
	    mh->msg_iovlen--;
	}else{
	    memcpy( mh->msg_iov->iov_base, from, len );
	    ((char*)mh->msg_iov->iov_base) += len;
	    mh->msg_iov->iov_len           -= len;
	    return 0;
	}
	if ( len == 0 )
	    return 0;
    };
    if (len){
	if ( header->Req.skip > len ){
	    header->Req.skip -= len;
	    return 0;
	}else{
	    len -= header->Req.skip;
	    header->Req.skip = 0;
	    return len;
	}
    }else{
	return 0;
    }
}

static inline
void AdjustHeader( PSP_Header_t *h, PSP_Header_Net_t *nh )
{
    /* shrink iovec */
    if ( h->Req.iovec[0].iov_len > nh->xheaderlen +
	 sizeof( PSP_Header_Net_t )){
	h->Req.iovec[0].iov_len = nh->xheaderlen +
	    sizeof( PSP_Header_Net_t );
    }
    /* skip is always >= 0 */
    h->Req.skip = nh->xheaderlen + sizeof( PSP_Header_Net_t ) -
	h->Req.iovec[0].iov_len;

    if ( h->Req.iovec[1].iov_len > nh->datalen ){
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
void proceed_header( PSP_Header_t *header, size_t len )
{
    struct msghdr *mh;

    mh = &header->Req.msgheader;

    while ( mh->msg_iovlen && len ){
	if ( mh->msg_iov->iov_len <= len ){
	    len            -= mh->msg_iov->iov_len;
	    mh->msg_iov->iov_len = 0;
	    mh->msg_iov++;
	    mh->msg_iovlen--;
	}else{
	    ((char*)mh->msg_iov->iov_base) += len;
	    mh->msg_iov->iov_len           -= len;
	    break;
	}
    };
}


/**********************************************************************
 *
 *  Request List
 *
 **********************************************************************/

static
void InitRequestList( PSP_RecvReqList_t *rl)
{
    int i;
    rl->PostedRecvRequests =NULL;
    rl->PostedRecvRequestsTailP = &rl->PostedRecvRequests;

    
#if PSP_VER == 4
    rl->RunningRequests =NULL;
#endif
    rl->GenReqHashHead = NULL;
    rl->GenReqHashTailP = &rl->GenReqHashHead;

    for( i=0; i< PSP_MSGQ_HASHSIZE; i++){
	rl->GenReqHash[i].GenReqHead = NULL;
	rl->GenReqHash[i].GenReqTailP = &rl->GenReqHash[i].GenReqHead;
	rl->GenReqHash[i].NextHash = NULL;
	rl->GenReqHash[i].InHashList = 0;
    }
}

static inline
void AddRecvRequest( PSP_RecvReqList_t *rl, PSP_Request_t *req)
{
    req->Next = NULL;
    *rl->PostedRecvRequestsTailP = req;
    rl->PostedRecvRequestsTailP = &req->Next;
    PostedRecvReqs++;
}

static inline
PSP_Request_t * GetPostedRequest( PSP_RecvReqList_t *rl, PSP_Header_Net_t *nh,
				  int nhlen, int from )
{
    PSP_Request_t **prev_p;
    PSP_Request_t *req;

    prev_p = &rl->PostedRecvRequests;
    req    =  rl->PostedRecvRequests;

    while (req && ( ! req->cb( nh, from, req->cb_param ))){
	prev_p = &req->Next;
	req    =  req->Next;
    }

    if (req){
	// dequeue
	if (!req->Next){
	    rl->PostedRecvRequestsTailP = prev_p;
	}
	*prev_p = req->Next;
	req->Next = NULL;

	/* Copy already received data: */
	AdjustHeader( REQ_TO_HEADER( req ), nh );
	copy_to_header( REQ_TO_HEADER( req ), nh, nhlen );
	PostedRecvReqsUsed++;
    }
    return req;
}

static inline
void AddSendRequest( PSP_Port_t *port, PSP_Request_t *req)
{
#if PSP_VER == 4
    req->Next = NULL;
    *port->PostedSendRequestsTailP = req;
    port->PostedSendRequestsTailP = &req->Next;
#endif
#if PSP_VER == FE
    unsigned int dest = REQ_TO_HEADER( req )->addr.to;
    /* ToDo: Rangecheck and connect check of dest */
    req->Next = NULL;
    *port->conns[ dest ].PostedSendRequestsTailP = req;
    port->conns[ dest ].PostedSendRequestsTailP = &req->Next;
    FD_SET2( port->conns[ dest ].con_fd, &port->fds_write, &port->nfds );
#endif
}

static inline
void DelFirstSendRequest(  PSP_Port_t *port, PSP_Request_t *req)
{
    int dest = REQ_TO_HEADER( req )->addr.to;
#ifdef DEBUG
    if ( req != PostedSendRequests) {
	fprintf(stderr,"DelFirstSendRequest() error\n");
	exit(1);
    }
#endif
    DP_SR( "SFIN xh: %8d dat: %8d\n",
	   REQ_TO_HEADER_NET(req)->xheaderlen,
	   REQ_TO_HEADER_NET(req)->datalen);

    if (req->Next == NULL){ // same as: *PostedSendRequestsTailP == req
	// SendRequestQueue is now empty
	port->conns[ dest ].PostedSendRequestsTailP =
	    &port->conns[ dest ].PostedSendRequests; 
	FD_CLR( port->conns[ dest ].con_fd, &port->fds_write );
    }
    port->conns[ dest ].PostedSendRequests = req->Next;
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
    while ( ReqBH ){
	PSP_Request_t *req = GetReqBH();
	/* if (req->dcb){ alway true for ReqBH requests */
	dcbs[ndcbs].dcb = req->dcb;
	dcbs[ndcbs].dcb_param = req->dcb_param;
	dcbs[ndcbs].Req = req;
	ndcbs++;
	if (ndcbs == PSP_NDCBS){
	    break;
	}
    }
    PSP_UNLOCK;

    /* execute the done callbacks (without any lock) */
    for( i=0; i<ndcbs; i++ ){
	dcbs[i].dcb( REQ_TO_HEADER( dcbs[i].Req ), dcbs[i].dcb_param);
	dcbs[i].Req->state |= PSP_REQ_STATE_PROCESSED;
    }
    if ( ReqBH ){
	/* There are more requests left */
	PSP_LOCK;
	goto restart;
    }
}


static inline
void FinishRequest( PSP_Port_t *port, PSP_Request_t * req)
{
    unused_var( port );
    DP_SR( "%pFINI xh: %8d dat: %8d           %s\n",
	   req,
	   REQ_TO_HEADER_NET(req)->xheaderlen,
	   REQ_TO_HEADER_NET(req)->datalen,
	   dumpstr(REQ_TO_HEADER_NET(req)->xheader,16)
	);
    
    if (req->dcb)
	AddReqBH(req);
    else{
	req->state |= PSP_REQ_STATE_PROCESSED;
    }
}

static inline
void FinishRunRecvReq( PSP_Port_t *port, PSP_Request_t *req, int con )
{
    /* Deq the req */
    port->conns[ con ].RunningRecvRequest = NULL;
    FinishRequest( port, req );
}




/*********************************************
 * Generated Requests
 */
					      
static inline
PSP_Request_t *SearchGeneratedRequestFromHash( PSP_GenRecvReq_t *hash_,
					       PSP_RecvCallBack_t *cb, void* cb_param)
{					     
    PSP_Request_t *req;
    req = hash_->GenReqHead;

    while (req &&
	   ( ! cb( REQ_TO_HEADER_NET( req ),
		   REQ_TO_HEADER( req )->addr.from, cb_param ))){
	req    =  req->NextGen;
    }
    return req;
}

/* Find a request */
static inline
PSP_Request_t * SearchForGeneratedRequest(PSP_RecvReqList_t *rl,
					  PSP_RecvCallBack_t *cb,
					  void* cb_param, int from)
{
    if (from != PSP_AnySender){
	return SearchGeneratedRequestFromHash( &rl->GenReqHash[ PSP_MSGQ_HASH( from )],
					       cb, cb_param );
    }else{
	/* Search in all Hashlists */
	PSP_GenRecvReq_t *act;
	PSP_Request_t *req;

	act = rl->GenReqHashHead;
	while ( act ){
	    if ( act->GenReqHead ){
		req = SearchGeneratedRequestFromHash( act, cb, cb_param );
		if (req){
		    return req;
		}
	    }
	    act = act->NextHash;
	}
	return NULL;
    }
}

static inline
PSP_Request_t *GetGeneratedRequestFromHash( PSP_GenRecvReq_t *hash_,
					    PSP_RecvCallBack_t *cb, void* cb_param)
{					     
    PSP_Request_t **prev_p;
    PSP_Request_t *req;
    
    prev_p = &hash_->GenReqHead;
    req    =  hash_->GenReqHead;

    while (req &&
	   ( ! cb(
	       REQ_TO_HEADER_NET( req ),
	       REQ_TO_HEADER( req )->addr.from, cb_param ))){
	prev_p = &req->NextGen;
	req    =  req->NextGen;
    }
    
    if (req){
	// dequeue
	*prev_p = req->NextGen;
	if (req->NextGen == NULL){
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
    if (from != PSP_AnySender){
	return GetGeneratedRequestFromHash( &rl->GenReqHash[ PSP_MSGQ_HASH( from )],
					    cb, cb_param );
    }else{
	/* Search in all Hashlists */
	PSP_GenRecvReq_t **prev_p;
	PSP_GenRecvReq_t *act;
	PSP_Request_t *req;

	prev_p = &rl->GenReqHashHead;
	act    =  rl->GenReqHashHead;

	while ( act ){
	    if ( act->GenReqHead ){
		req = GetGeneratedRequestFromHash( act, cb, cb_param );
		if (req){
#define PSP_RECV_PREDICTION
#ifdef  PSP_RECV_PREDICTION
		    if ( rl->GenReqHashHead != act ){
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

		if ( *prev_p == NULL ){
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
    if (! h->InHashList){
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
PSP_Request_t * GenerateRequest(PSP_RecvReqList_t *rl,
				PSP_Header_Net_t *nh, int nhlen, int from)
{
    PSP_Header_t *header;
    PSP_Request_t *req;
    int packsize;

    packsize = nh->xheaderlen + nh->datalen;
    header = (PSP_Header_t *)PSP_malloc( sizeof( *header ) + packsize );
    req = &header->Req;

    DP_SR( "%pGENE xh: %8d dat: %8d from %d\n",
	   &header->Req,
	   nh->xheaderlen, nh->datalen, from
  	);

    /* initialize */

    header->Req.Next = NULL;
    header->Req.NextGen	= NULL;
    header->Req.state = PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV | PSP_REQ_STATE_RECVGEN;
    header->Req.UseCnt = 0;
    
    header->Req.iovec[0].iov_base = PSP_HEADER_NET( header );
    header->Req.iovec[0].iov_len  = PSP_HEADER_NET_LEN + nh->xheaderlen;
    header->Req.iovec[1].iov_base = ((char*)PSP_HEADER_NET( header )) +
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
    copy_to_header( header, nh, nhlen );
    
    /* Enq req to list */
    GenReqEnq( rl , req, from);
    
    req->UseCnt++;
    GenReqs++;
    return req;
}
    

static inline
void FreeGenRequest(PSP_Request_t * req)
{
    GenReqsUsed++;
    free( REQ_TO_HEADER( req ));
}




static
void CleanupRequestList( PSP_Port_t *port, PSP_RecvReqList_t *rl)
{
    PSP_Request_t *req,*nreq;

    req = rl->PostedRecvRequests;
    while(req){
	req->state =
	    PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV |
	    PSP_REQ_STATE_RECVCANCELED;
	nreq=req->Next;
	FinishRequest( port, req );
	req=nreq;
    }

#if PSP_VER == 4
    req = rl->RunningRequests;
    while(req){
	req->state =
	    PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV |
	    PSP_REQ_STATE_RECVCANCELED;
	nreq=req->Next;
	FinishRequest( port, req);
	req=nreq;
    }
#endif
    while ( (req = GetGeneratedRequest( rl, PSP_RecvAny, 0, PSP_AnySender))){
  	req->state =
  	    PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV |
  	    PSP_REQ_STATE_RECVCANCELED;
  	FinishRequest( port, req );
    }
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
PSP_Port_t *AllocPortStructInitialized( void )
{
    PSP_Port_t *port;
    port = (PSP_Port_t *)PSP_malloc( sizeof(PSP_Port_t));

    port->NextPort = NULL;
    port->port_fd = -1;
    memset( &port->portid, 0, sizeof(port->portid));
    port->portid.id.magic = PSP_PORTIDMAGIC;
        
    InitRequestList( &port->ReqList );

#if PSP_VER == 4
    port->PostedSendRequests = NULL;
    port->PostedSendRequestsTailP = &port->PostedSendRequests;
#endif    
#if PSP_VER == FE
    {
	int i;
	for( i = 0;i < PSP_FE_MAX_CONNS; i++ ){
	    port->conns[i].con_fd = -1;
	    ReadAHInit( &port->conns[i].readahead, env_readahead );
	    port->conns[i].PostedSendRequests = NULL;
	    port->conns[i].PostedSendRequestsTailP =
		&port->conns[i].PostedSendRequests;
	    port->conns[i].RunningRecvRequest = NULL;
	}
	FD_ZERO( &port->fds_read );
	FD_ZERO( &port->fds_write );
	port->nfds = 0;
	port->min_connid = PSP_FE_MAX_CONNS;
    }
#endif
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
PSP_Port_t *DelPort( PSP_Port_t *Port)
{
    PSP_Port_t *hash;
    PSP_Port_t **prev_p;

    prev_p=&Ports;
    hash  = Ports;
    
    while (hash && (hash != Port)){
	prev_p = &hash->NextPort;
	hash   =  hash->NextPort;
    }

    if (hash){
	/* Dequeue */
	*prev_p = hash->NextPort;
    }
    return hash;
}


#if PSP_VER == FE
static
void ConfigureConnection( int fd )
{
    int ret;
    int val;
#ifdef USE_SIGURGSOCK

/*
  TCP  supports  urgent  data. Urgent data is used to signal
  the receiver that some important message is  part  of  the
  data  stream  and  that  it should be processed as soon as
  possible.  To send urgent data specify the MSG_OOB  option
  to  send(2).   When  urgent  data  is received, the kernel
  sends a SIGURG signal to the reading process or  the  pro-
  cess  or  process  group  that has been set for the socket
  using  the  FIOCSPGRP  or  FIOCSETOWN  ioctls.  When   the
  SO_OOBINLINE  socket option is enabled, urgent data is put
  into the normal data stream (and can be tested for by  the
  SIOCATMARK  ioctl), otherwise it can be only received when
  the MSG_OOB flag is set for sendmsg(2).
*/
    val = 1;
    ret = setsockopt( fd, SOL_SOCKET, SO_OOBINLINE, &val, sizeof(val));

    /* The ioctl SIOCSPGRP is not nessasary, because the
       fcntl F_SETOWN set also SIOCSPGRP (but not vice versa!).
       This is nessasary to receive the signal SIGURG */
    
    val = getpid();
    ret = fcntl( fd, F_SETOWN, val );
    ret = ioctl( fd, SIOCSPGRP, &val );
#else
    unused_var( fd );
#endif

    if ( env_so_sndbuf ){
	errno = 0;
	val = env_so_sndbuf;
	ret = setsockopt( fd, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
	DPRINT( 2, "setsockopt( %d, SOL_SOCKET, SO_SNDBUF, [%d], %ld ) = %d : %s",
		fd, val, (long)sizeof(val), ret, strerror( errno ));
    }
    if ( env_so_rcvbuf ){
	errno = 0;
	val = env_so_rcvbuf;
	ret = setsockopt( fd, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
	DPRINT( 2, "setsockopt( %d, SOL_SOCKET, SO_RCVBUF, [%d], %ld ) = %d : %s",
		fd, val, (long)sizeof(val), ret, strerror( errno ));
    }
    errno = 0;

    val = env_tcp_nodelay;
    ret = setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    DPRINT( 2, "setsockopt( %d, IPPROTO_TCP, TCP_NODELAY, [%d], %ld ) = %d : %s",
	    fd, val, (long) sizeof(val), ret, strerror( errno ));
}
#endif


static
int GetUnusedCon( PSP_Port_t *port )
{
    int con;
    /* Find a free conn */
    for( con=PSP_FE_MAX_CONNS-1; con >=0  ; con--){
	if ( port->conns[con].con_fd < 0 ){
	    port->min_connid = PSP_MIN( port->min_connid, con );
	    break;
	}
    }
    return con >= 0 ? con : -1;
//    return con < PSP_FE_MAX_CONNS ? con : -1;
}

static 
void DoAccept( PSP_Port_t *port )
{
    struct sockaddr_in si;
    int len = sizeof(si);
    int con_fd;
    int con = -1;
    
    con_fd = accept( port->port_fd, (struct sockaddr*)&si, &len);
    if ( con_fd < 0 ) goto err_accept;

    con = GetUnusedCon( port );
    if ( con < 0 ) goto err_nocon;

    DPRINT( 1, "ACCEPT conn %d from %s:%d", con,
	    inetstr( ntohl( si.sin_addr.s_addr )), ntohs( si.sin_port ));
    
    port->conns[ con ].con_fd = con_fd;
    FD_SET2( con_fd, &port->fds_read, &port->nfds );
    ConfigureConnection( con_fd );

    return;
 err_nocon:
    shutdown( con_fd, 2 );
    close( con_fd );
 err_accept:
    DPRINT( 1, "REJECT conn %d from %s:%d", con,
	    inetstr( ntohl( si.sin_addr.s_addr )), ntohs( si.sin_port ));
    return;
}

static
int recvall(int fd, void *buf, int count)
{
    int len;
    int c=count;

    while (c>0){
#ifdef RECVMSG
	len = recv( fd, buf, c, 0/*MSG_NOSIGNAL | MSG_DONTWAIT*/ );
#else
	len = read( fd, buf, c );
#endif
	if (len<=0){
	    if (( len < 0 ) && ( errno == EINTR )/* || (errno == EAGAIN )*/){
		perror(__FUNCTION__);
		continue;
	    }else{
		if ( len == 0 )
		    errno = ENOTCONN;
		return len;
	    }
	}
	c-=len;
	((char*)buf)+=len;
    }
    return count;
}

/* Try to fill the readaheadbuffer with maximal max bytes by reading from
   con_fd, return the number of bytes in the buffer,
   or -1 on problems with the connection */
int ReadAHreadahead( ReadAhead_t *rh, int con_fd, int min, int max )
{
    int len;
    if ( max > rh->len ){
	rh->buf = (char*)realloc( rh->buf, max );
    }
    if ( rh->n < min ){
#ifdef RECVMSG
	len = recv( con_fd, rh->buf + rh->n,
		    max - rh->n, MSG_NOSIGNAL | MSG_DONTWAIT );
#else
	len = read( con_fd, rh->buf + rh->n,
		    max - rh->n );
#endif
	if ( len <= 0 ) {
	    if (len == 0) errno = ECONNREFUSED;
	    return -1;
	}
	rh->n += len;
    }
    return rh->n;
}

static inline
void ReadAHRemove( ReadAhead_t *rh, int d )
{
    if ( rh->n - d > 0 ){
	memmove( rh->buf, rh->buf + d, rh->n - d );
    }
    rh->n = rh->n - d;
}

static inline
void ReadAHClear( ReadAhead_t *rh )
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
int DoRecvNewMessage( PSP_Port_t *port, int con )
{
    int ret;
    PSP_Request_t *req;
    int con_fd = port->conns[con].con_fd;
    ReadAhead_t *rh = &port->conns[con].readahead;
    int hlen; /*< headerlen ( PSP_Header_Net_t + xhlen ) */
    int rlen; /*< length of the readahbuf */
    int mlen; /*< lenght of the whole message ( PSP_Header_Net_t + xhlen +data ) */
    int minrm; /*< min( rlen, mlen ) */

    /* Read at least the PSP_Header_Net_t and the xheader !!! */

    /* Read psport header */
    ret = ReadAHreadahead( rh, con_fd, sizeof( PSP_Header_Net_t ), env_readahead );
    if ( ret < (int)sizeof(PSP_Header_Net_t))
	goto unfinished;
    
    /* PSP_Header_Net_t is complete. Now read the xheader.
       With luck the first readahead call already read this data. */
    hlen = sizeof( PSP_Header_Net_t ) + ((PSP_Header_Net_t *)rh->buf)->xheaderlen;
    ret = ReadAHreadahead( rh, con_fd, hlen, hlen );
    
    if ( ret < hlen )
	goto unfinished;

    /* also the xheader is complete and maybe some data from the
       first ReadAHreadahead */

    mlen = hlen + ((PSP_Header_Net_t *)rh->buf)->datalen;
    rlen = ret;
 more:
    minrm = PSP_MIN( rlen, mlen );
    
    /* Search or generate the Request */
    req = GetPostedRequest( &port->ReqList, (PSP_Header_Net_t *)rh->buf, minrm, con );
    if (!req){
	/* receive without posted RecvRequest */
	req = GenerateRequest( &port->ReqList , (PSP_Header_Net_t *)rh->buf,
			       minrm, con );
    }

    REQ_TO_HEADER( req )->addr.from = con;

    if ( mlen <= rlen ){
	/* Message is complete */
	/* "port->conns[ con ].RunningRecvRequest = 0" already done */
	FinishRequest( port, req );
	/* remove the used data */
	ReadAHRemove( rh, mlen );
	if (rh->n >= (int)sizeof( PSP_Header_Net_t )){
	    hlen = sizeof( PSP_Header_Net_t ) + ((PSP_Header_Net_t *)rh->buf)->xheaderlen;
	    mlen = hlen + ((PSP_Header_Net_t *)rh->buf)->datalen;
	    rlen = rh->n;
	    if ( rh->n >= mlen ){
		/* There is a complet message in the readahead buffer.
		   We MUST read this now, else we will block
		   in the next select forever. */
		goto more;
	    }
	}
	return 1;
    }else{
	/* Enq running req */
	port->conns[ con ].RunningRecvRequest = req;
	/* whole readahead buffer used */
	ReadAHClear( rh );
	return 0;
    }
    
 unfinished:
    /* PSP_Header_Net_t not complete */
    if ( ret >= 0 ){
	return 1;
    }else{
	if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)){
	    DPRINT( 1, "STOPPED conn %d (recvnew : %s)", con,
		    strerror( errno ));
	    /* ToDo: Terminate connection */
	    FD_CLR( port->conns[con].con_fd, &port->fds_read );
	}
	return 1;
    }
}


static inline
void DoRecv( PSP_Port_t *port, int con )
{
    PSP_Request_t *req;
    int ret;
 trymore3:
    req = port->conns[con].RunningRecvRequest;
    if ( req ){
	/* continue this request */
	if ( req->msgheader.msg_iovlen ){
	trymore1:
#ifdef RECVMSG
	    ret = recvmsg( port->conns[con].con_fd,
			   &req->msgheader, MSG_NOSIGNAL | MSG_DONTWAIT );
#else
	    if (req->msgheader.msg_iovlen >=0 ){
		ret = read( port->conns[con].con_fd,
			    req->msgheader.msg_iov->iov_base,
			    req->msgheader.msg_iov->iov_len );
	    }else{
		ret = 0;
	    }
#endif
	    if ( ret <= 0 ) goto err_recvmsg; 
	    proceed_header( REQ_TO_HEADER( req ), ret );
	    
	    if (( req->msgheader.msg_iovlen == 0 ) &&
		( port->conns[con].RunningRecvRequest->skip == 0)){
		/* Request Finished */
		FinishRunRecvReq( port, req, con );
	    }else{
		goto trymore1;
	    }
	}else{
	trymore2:
	    /* skip the last bytes from the message (user buffer was to small)*/
	    ret = recv( port->conns[con].con_fd,
			trash,
			PSP_MIN( req->skip, TRASHSIZE ),
			MSG_NOSIGNAL | MSG_DONTWAIT );
	    if ( ret <= 0 ) goto err_recv; 
	    port->conns[con].RunningRecvRequest->skip -= ret;

	    if ( port->conns[con].RunningRecvRequest->skip == 0){
		/* Request Finished */
		FinishRunRecvReq( port, req, con );
	    }else{
		goto trymore2;
	    }
	}
    }else{
	/* New message */
	if ( !DoRecvNewMessage( port, con ) && port->conns[con].RunningRecvRequest ){
	    goto trymore3;
	}
    }

    return;
 err_recv:
 err_recvmsg:
    if (ret == 0) errno = ECONNREFUSED;
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)){
	DPRINT( 1, "STOPPED conn %d (recv : %s)", con,
		strerror( errno ));
	/* ToDo: Terminate connection */
	FD_CLR( port->conns[con].con_fd, &port->fds_read );
    }
    return;
}

static inline
void DoSend( PSP_Port_t *port, int con )
{
    PSP_Request_t *req = port->conns[con].PostedSendRequests;
    int ret;

#ifdef SENDMSG
    ret = sendmsg( port->conns[con].con_fd,
		   &req->msgheader, MSG_NOSIGNAL |
#ifdef USE_SIGURGSOCK
		   MSG_OOB|
#endif
		   MSG_DONTWAIT );
#else
    ret = writev(  port->conns[con].con_fd,
		   req->msgheader.msg_iov,
		   req->msgheader.msg_iovlen );
#endif
    
    if ( ret < 0 ) goto err_sendmsg; 

    proceed_header( REQ_TO_HEADER( req ), ret );

    if ( req->msgheader.msg_iovlen == 0 ){
	/* Request Finished */
	req->state |= PSP_REQ_STATE_PROCESSED;
	DelFirstSendRequest( port, req );
    }
    return;
 err_sendmsg:
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)){
	DPRINT( 1, "STOPPED conn %d (send : %s)", con,
		strerror( errno ));
	/* ToDo: Terminate connection */
	FD_CLR( port->conns[con].con_fd, &port->fds_write );
    }
    return;
}

static inline
int DoBackgroundWorkWait( PSP_Port_t *port, struct timeval *timeout ) 
{
    fd_set fds_read;
    fd_set fds_write;
    int nfds;
    int i;
    
    fds_read = port->fds_read;
    fds_write = port->fds_write;

    nfds = select( port->nfds, &fds_read, &fds_write, NULL, timeout );

    if ( nfds <= 0 ) return 0;

    for ( i = PSP_FE_MAX_CONNS-1; i>=port->min_connid; i-- ){
	if ( port->conns[i].con_fd >= 0 ){
	    if ( FD_ISSET( port->conns[i].con_fd, &fds_write )){
		FD_CLR( port->conns[i].con_fd, &fds_write );
//		printf("s");
		DoSend( port, i );
		if ( !(--nfds) ) return 1;
	    }	    
	    if ( FD_ISSET( port->conns[i].con_fd, &fds_read )){
		FD_CLR( port->conns[i].con_fd, &fds_read );
//		printf("r");
		DoRecv( port, i );
		if ( !(--nfds) ) return 1;
	    }
	}
    }

    if ((port->port_fd >= 0) && FD_ISSET( port->port_fd, &fds_read )){
	/* the listen socket */
	FD_CLR( port->port_fd, &fds_read );
	DoAccept( port );
    }
    return 1;
}

static inline
void DoBackgroundWork( PSP_Port_t *port )
{
    struct timeval to;
    int ret;
//    do{
	to.tv_sec = 0;
	to.tv_usec = 0;
	ret = DoBackgroundWorkWait( port, &to );
//    }while ( ret );
}

#ifdef USE_SIGURG
static
void sigurg( int signal )
{
    PSP_Port_t *port;
    unused_var( signal );
    if (!sigurglock){
	sigurgretry = 0;
	DP_CLONE(" +++++++++ SIGURG START+++++++++ \n");
	for( port = Ports; port; port = port->NextPort ){
	    DoBackgroundWork( port );
	}
	DP_CLONE(" +++++++++ SIGURG END  +++++++++ \n");
    }else{
	DP_CLONE(" +++++++++ SIGURG IGNORE  ++++++ \n");
	sigurgretry = 1;
    }	
}
#endif

static
void sigquit( int signal )
{
    PSP_Port_t *port;
    int i;
    unused_var( signal );
    printf(" +++++++++ SIGQUIT START ++++ \n");
    printf(" GenReq:%d (%d) PostedReq: %d(%d)\n",
	   GenReqs-GenReqsUsed, GenReqs,
	   PostedRecvReqs-PostedRecvReqsUsed, PostedRecvReqs);

    for ( port = Ports; port; port = port->NextPort ){
	for ( i=0; i<PSP_FE_MAX_CONNS; i++ ){
	    if ( port->conns[i].readahead.n ) {
		printf( "port: %d con: %d rhbuf: %s\n",
			port->portid.id.portno, i,
			dumpstr( port->conns[i].readahead.buf, port->conns[i].readahead.n ));
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
intgetenv( int *val, char *name )
{
    char *aval;

    aval = getenv( name );
    if ( aval ){
	*val = atoi( aval );
	DPRINT( 1, "set %s = %d", name, *val );
    } else {
	DPRINT( 2, "default %s = %d", name, *val );
    }
}

static
void init_env( void )
{
    intgetenv( &env_debug, ENV_DEBUG );
    DPRINT(1,"# Version(PSFE): %s", vcid);
    intgetenv( &env_so_sndbuf, ENV_SO_SNDBUF );
    intgetenv( &env_so_rcvbuf, ENV_SO_RCVBUF );
    intgetenv( &env_tcp_nodelay, ENV_TCP_NODELAY );
    intgetenv( &env_nobgthread, ENV_NOBGTHREAD );
    intgetenv( &env_sigquit, ENV_SIGQUIT );
    intgetenv( &env_readahead, ENV_READAHEAD );
    env_readahead = PSP_MAX( env_readahead, (int)sizeof( PSP_Header_Net_t ));
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

    if ( env_sigquit )
	signal( SIGQUIT, sigquit );

    /* Initialize Ports */
    InitPorts();
//    PSP_SetReadAhead( READAHEADXHEADLENDEFAULT );
    trash = (char*)malloc( TRASHSIZE );
    PSP_UNLOCK;
    
    return 0;
}

unsigned int PSP_UsedHW(void)
{
/*
  From pshwtypes.h:
#define PSHW_ETHERNET            0x0001
#define PSHW_MYRINET             0x0002
#define PSHW_GIGAETHERNET        0x0004
*/
    return 0x0001;
}


/**********************************************************************/
int PSP_GetNodeID(void)
/**********************************************************************/
{
    struct hostent *mhost;
    char myname[256];
    /* ToDo: This was psid code: Maybe buggy! */
    /* Lookup hostname */
    gethostname(myname, sizeof(myname));

    /* Get list of IP-addresses */
    mhost = gethostbyname(myname);

    if ( !mhost ) goto err_nohostent;

    /*
    while (*mhost->h_addr_list) {
	printf( " -Addr: %08x\n", 
		((struct in_addr *) *mhost->h_addr_list)->s_addr);
	mhost->h_addr_list++;
    }
    */
    
    return ntohl(*(int *)*mhost->h_addr_list);

 err_nohostent:
    fprintf( stderr, __FUNCTION__ "(): gethostbyname() failed\n");
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
    
    if (portno == PSP_ANYPORT){
	srandom( getpid());
	port->portid.id.portno = (uint16_t)random();
    }else{
	port->portid.id.portno = portno;
    }
#if PSP_VER == 4
    port->port_fd = socket( PF_P4S , 0, 0 );
#endif
#if PSP_VER == FE
    port->port_fd = socket( PF_INET , SOCK_STREAM, 0 );
#endif
    
    if ( port->port_fd < 0 ) goto err_nosocket;
    
    for( i=0; i<300; i++ ){
	struct sockaddr sa;
#if PSP_VER == 4
	sa.sa_family = PF_P4S;
	memcpy( sa.sa_data, &port->portid.portid, sizeof(port->portid.portid));
	ret = bind( port->port_fd, &sa, sizeof(sa));
	if (! ret ) break; /* Bind ok */
#endif
#if PSP_VER == FE
	struct sockaddr_in *si= (struct sockaddr_in *)&sa;
	si->sin_family = PF_INET;
	si->sin_port = htons(port->portid.id.portno);
	si->sin_addr.s_addr = INADDR_ANY;
	ret = bind( port->port_fd, &sa, sizeof(sa));
	if (! ret )
	    ret = listen( port->port_fd, 64 );
	if (! ret ){
	    FD_SET2( port->port_fd, &port->fds_read, &port->nfds );
	    break; /* Bind and listen ok */
	}
#endif
	if (portno != PSP_ANYPORT) break; /* Bind failed */
	/* try another number */
	port->portid.id.portno = (uint16_t)random();
    }

    if (ret) goto err_bind;

    AddPort( port );

    PSP_UNLOCK;
    return (PSP_PortH_t)port;
    
 err_bind:
    errno = -ret;
    close( port->port_fd );
 err_nosocket:
    FreePortStruct( port );
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
    FD_CLR(port->port_fd, &port->fds_read);
    port->port_fd = -1;
}



/**********************************************************************/
int PSP_ClosePort(PSP_PortH_t porth)
/**********************************************************************/
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
    
    PSP_LOCK;

    DelPort( port );
    CleanupRequestList( port, &port->ReqList );
    close( port->port_fd );
    FreePortStruct( port );

    ExecBHandUnlock();
    
    return 0;
}


int PSP_Connect_( PSP_PortH_t porth, struct sockaddr *sa, socklen_t addrlen )
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
#if PSP_VER == 4
#warning ToDo
#endif
#if PSP_VER == FE
    int con;
    int con_fd;
    /* Find a free conn */
    con = GetUnusedCon( port );
    if ( con < 0 ) goto err_nofreecon;

    /* Open the socket */
    con_fd = socket( PF_INET , SOCK_STREAM, 0 );
    if ( con_fd < 0 ) goto err_socket;

    /* Connect */
    if ( connect( con_fd, sa, addrlen ) < 0) goto err_connect;
    
    ConfigureConnection( con_fd );
    port->conns[con].con_fd = con_fd;
    FD_SET2( con_fd, &port->fds_read, &port->nfds );

    DPRINT( 1, "CONNECT conn %d to %s:%d",
	    con,
	    inetstr(ntohl( ((struct sockaddr_in*)sa)->sin_addr.s_addr)),
	    ntohs(((struct sockaddr_in*)sa)->sin_port));
    
    return con;
 err_connect:
 err_socket:
    return -1;
 err_nofreecon:
    errno = ENOMEM;
    return -1;
#endif
}


int PSP_Connect( PSP_PortH_t porth, int nodeid, int portno )
{
    struct sockaddr_in si;

    si.sin_family = PF_INET;
    si.sin_port = htons( portno );
    si.sin_addr.s_addr = htonl( nodeid );

    return PSP_Connect_( porth, (struct sockaddr *)&si, sizeof( si ));
}


/**********************************************************************/
int PSP_GetPortNo( PSP_PortH_t porth )
/**********************************************************************/
{
    PSP_Port_t *port = (PSP_Port_t *)porth;
    if (port)
	return port->portid.id.portno;
    else
	return -1;
}

static inline
PSP_RequestH_t ISendLoopback( PSP_PortH_t porth,
			      void* buf, unsigned buflen,
			      PSP_Header_t* header, unsigned xheaderlen )
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
    req = GetPostedRequest( &port->ReqList, PSP_HEADER_NET( header ),
			    sizeof( PSP_Header_Net_t ) + xheaderlen,
			    PSP_DEST_LOOPBACK );
    if (!req){
	/* receive without posted RecvRequest */
	req = GenerateRequest( &port->ReqList , PSP_HEADER_NET( header ),
			       sizeof( PSP_Header_Net_t ) + xheaderlen,
			       PSP_DEST_LOOPBACK );
    }
    
    REQ_TO_HEADER( req )->addr.from = PSP_DEST_LOOPBACK;
    
    /*
      If datalen equal zero, the request is finished.
      This is equivalent to:
      (iovec[1].iov_len == 0) && (skip == 0))
    */
    if ( buflen ){
	/* Copy the data to req */
	copy_to_header( REQ_TO_HEADER( req ), buf, buflen );
	/* Now (iovec[1].iov_len == 0) && (skip == 0)) is true */
    }
    
    FinishRequest( port, req );

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
	 == PSP_MAGICREQ_VALID){
	PSP_DPRINT(1,"WARNING: PSP_ISendCB called with used header\n");
    }
#endif
    unused_var( flags );
    DP_SR( "SEND xh: %8d dat: %8d to %d            %s\n",
	   xheaderlen, buflen, dest,
	   dumpstr(header->xheader,16 )
  	);

    if ((unsigned int)dest >= PSP_FE_MAX_CONNS){
	if ( dest == PSP_DEST_LOOPBACK ){
	    return ISendLoopback( porth, buf, buflen, header, xheaderlen );
	}else{
	    errno = EINVAL;
	    goto err_inval;
	}
    }
    
    header->Req.Next = NULL;
    header->Req.state = PSP_MAGICREQ_VALID | PSP_REQ_STATE_SEND | PSP_REQ_STATE_SENDPOSTED;
    header->Req.UseCnt = 0;
    header->Req.iovec[0].iov_base = PSP_HEADER_NET( header );
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

    if (header->Req.iovec[1].iov_len == 0){
	/* Allow datalen == 0 */
	header->Req.msgheader.msg_iovlen = 1;
    }
    
    PSP_LOCK;

    if ( port->conns[dest].con_fd < 0 ) goto err_notconnected; 
    
    AddSendRequest( port, &header->Req );
    DoBackgroundWork( port );

    ExecBHandUnlock();

    /* req not valid hereafter ! */
    
    return (PSP_RequestH_t) header;
 err_notconnected:
    errno = ECONNREFUSED;
    PSP_UNLOCK;
 err_inval:
    DPRINT( 1, "SEND to conn %d failed : %s", dest, strerror(errno));
    header->Req.state = PSP_MAGICREQ_VALID |
	PSP_REQ_STATE_SENDNOTCON | PSP_REQ_STATE_PROCESSED;
    return (PSP_RequestH_t) header;
}


int PSP_RecvAny( PSP_Header_Net_t* header, int from, void *param )
{
    unused_var( header );
    unused_var( from );
    unused_var( param );
    return 1;
}


    
int PSP_RecvFrom( PSP_Header_Net_t* header, int from, void *param )
{
    PSP_RecvFrom_Param_t *p = (PSP_RecvFrom_Param_t *)param;
    unused_var( header );
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
	 == PSP_MAGICREQ_VALID){
	PSP_DPRINT(1,"WARNING: PSP_IReceiveCB called with used header\n");
    }
#endif
    DP_SR( "%pRECV xh: %8d dat: %8d from %d %s\n",
	   &header->Req, xheaderlen, buflen, sender,
	   dumpstr(header->xheader,16)
  	);

    if (! cb )
	cb = PSP_RecvAny;

    /* Initialize the header */
    /* ToDo: Move most of the initialisation to the headerfile and
       reduce the number of parameters of this function */
    header->Req.Next = NULL;
    header->Req.state = PSP_MAGICREQ_VALID | PSP_REQ_STATE_RECV | PSP_REQ_STATE_RECVPOSTED;
    header->Req.UseCnt = 0;
    header->Req.iovec[0].iov_base = PSP_HEADER_NET( header );
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

    req = GetGeneratedRequest( &port->ReqList, cb, cb_param, sender);
    if ( req ){
	/* Message already partially arrived: copy this part. */
	AdjustHeader( header, REQ_TO_HEADER_NET( req ));
	copy_to_header( header, REQ_TO_HEADER_NET( req ),
			sizeof( PSP_Header_Net_t ) +
			REQ_TO_HEADER_NET( req )->xheaderlen +
			REQ_TO_HEADER_NET( req )->datalen -
			req->iovec[0].iov_len -
			req->iovec[1].iov_len );
	header->addr.from = REQ_TO_HEADER( req )->addr.from;
	header->Req.state = req->state;

	if (( header->addr.from != PSP_DEST_LOOPBACK ) &&
	    ( port->conns[ header->addr.from ].RunningRecvRequest == req )){
	    /* RunningRecvRequest == req means: The request is not finished */
	    /* Replace the running request */
	    port->conns[ header->addr.from ].RunningRecvRequest =
		&header->Req;
	}else{
	    /* Finish the request. Clear PSP_REQ_STATE_PROCESSED,
	       maybe we have to call a done callback before setting
	       PSP_REQ_STATE_PROCESSED. */
	    req->state &= ~PSP_REQ_STATE_PROCESSED;
	    FinishRequest( port, &header->Req );
	}
	FreeGenRequest( req );
    }else{
	// Enqueue new Request.
	AddRecvRequest( &port->ReqList, &header->Req );
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
	== PSP_MAGICREQ_VALID){
	PSP_DPRINT(1,"WARNING: PSP_IReceiveCB called with used header\n");
    }
#endif

    if (!cb) cb = PSP_RecvAny;
    
    PSP_LOCK;
    
    DoBackgroundWork( port ); /* important */
    req = SearchForGeneratedRequest(&port->ReqList,cb,cb_param,sender);
    if (req){
	memcpy( PSP_HEADER_NET( header ),
		REQ_TO_HEADER_NET( req ),
		PSP_HEADER_NET_LEN +
		PSP_MIN( xheaderlen, REQ_TO_HEADER( req )->xheaderlen ));
	header->addr.from = REQ_TO_HEADER( req )->addr.from;
	ret=1;
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

    while (!PSP_IProbeFrom(porth, header, xheaderlen, cb, cb_param, sender )){
	PSP_LOCK;
	DoBackgroundWorkWait( port, NULL );
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
	 != PSP_MAGICREQ_VALID){
	PSP_DPRINT(1,"WARNING: PSP_Test called with uninitialized request\n");
    }
#endif
    PSP_LOCK;
    DoBackgroundWork( port );
    ExecBHandUnlock();
//    PSP_UNLOCK;
    if (header->Req.state & PSP_REQ_STATE_PROCESSED){
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
	PSP_DPRINT(1,"WARNING: PSP_Wait called with uninitialized request\n");
    }
#endif

//      PSP_LOCK;
//      DoBackgroundWork( port );
//      ExecBHandUnlock();

    while (! (header->Req.state & PSP_REQ_STATE_PROCESSED)){
	PSP_LOCK;
	DoBackgroundWorkWait( port, NULL );
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
    unused_var( port );
    unused_var( request );
    if (!cancelwarn)
	fprintf(stderr,"PSP_Cancel() not implementet yet\n");
    cancelwarn = 1;
    return PSP_SUCCESS;
}


/*
  Local Variables:
  c-basic-offset: 4
  c-backslash-column: 72
  compile-command: "make"
  End:
*/
/* old  compile-command: "gcc psport.c -I../../include -Wall -W -o tmp.o" */
