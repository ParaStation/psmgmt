/*
 * ParaStation mcastlisten
 *
 * (C) 1999 ParTec AG Karlsruhe
 *     written by Dr. Thomas M. Warschko
 *
 * 10-12-99 V1.0: initial implementation
 *
 *
 *
 *
 *
 */

#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>

#include <netinet/in.h>
#include <netdb.h>

#define NOSOCK	-1	/* invalid socket descriptor */
 
#define MCASTSERVICE    "1889"
#define RDPPROTOCOL    "udp"
#define MGROUP 237

char errtxt[256];
static struct sockaddr_in msin;

typedef enum {CINVAL=0x0, CLOSED=0x1, SYN_SENT=0x2, SYN_RECVD=0x3, ACTIVE=0x4} RDPState;

typedef struct RDPLoad_ {
    double	load[3];	/* Load parameters of that node */
} RDPLoad;

typedef struct RDP_ConInfo_ {
    RDPState state;
    RDPLoad load;
    int	misscounter;
} RDP_ConInfo;

typedef struct Mmsg_ {
    short node;           /* Sender ID */
    short type;
    RDPState state;
    RDPLoad load;
} Mmsg;


#define NODES 129
char wheel[4] = {'|', '/', '-', '\\'};
unsigned int count[NODES];
char display[260];

void init(void)
{
    int i;

    for(i=0;i<NODES;i++){
	count[i]=0;
	display[i]='.';
    }
    for(i=NODES;i<2*NODES;i++){
	display[i]='\b';
    }
    display[258]=0;
}

int main(int argc, char *argv[])
{
    char host[80];
    char *service=MCASTSERVICE;
    char *protocol=RDPPROTOCOL;
    char *net=NULL;
    struct hostent *phe;     /* pointer to host information entry */
    struct servent *pse;     /* pointer to servive intformation entry */ 
    u_char loop;
    int reuse;
    struct ip_mreq mreq;
    struct sockaddr_in sin;  /* an internet endpoint address */ 
    struct in_addr in_sin;
    int MY_MCAST_GROUP = MGROUP;
    int mcastsock;
    fd_set rfds;
    int slen;
    Mmsg buf;
    char c;
    int errflg=0;
    int debug=0;

    optarg = NULL;
    while (!errflg && ((c = getopt(argc,argv, "p:n:m:D")) != -1)){
	switch(c){
	case 'p':
	    service = optarg; 
	    printf("using port %s\n",service);
	    break;
	case 'n':
	    net = optarg; 
	    printf("using network %s\n",net);
	    break;
	case 'm':
	    sscanf(optarg, "%d", &MY_MCAST_GROUP);
	    printf("using mcast %d\n", MY_MCAST_GROUP);
	    break;
	case 'D':
	    debug=1;
	    break;
	default:
	    errflg++;
	    break;
	}
    }

    bzero( (char *)&sin, sizeof(sin));

    /*
     * allocate a socket
     */
    if ( (mcastsock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
	sprintf(errtxt,"can't create socket (mcast)");
	perror(errtxt);
    }

    sin.sin_family = AF_INET;

    mreq.imr_multiaddr.s_addr = htonl(INADDR_UNSPEC_GROUP | MY_MCAST_GROUP);
    if(net == NULL){
	mreq.imr_interface.s_addr = INADDR_ANY; 
	sin.sin_addr.s_addr = INADDR_ANY;
    }else{
	mreq.imr_interface.s_addr = inet_addr(net); 
	sin.sin_addr.s_addr = inet_addr(net);
    }
    /*
     * map service name to port number
     */
    if((pse = getservbyname(service, protocol))){
	sin.sin_port = htons(ntohs((u_short) pse->s_port) );
    }else{
	if((sin.sin_port = htons((u_short)atoi(service))) == 0){
	    sprintf(errtxt,"can't get %s service entry", service);
	    perror(errtxt);
	}
    }

    printf("listening on port %d\n",ntohs(sin.sin_port));

    if (setsockopt(mcastsock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
		   sizeof(mreq)) == -1){
	sprintf(errtxt,"unable to join mcast group");
	perror(errtxt);
    }

    printf("using mcast addr %x\n",mreq.imr_multiaddr.s_addr);

    loop = 1; /* 0 = disable, 1 = enable (default) */
    if (setsockopt(mcastsock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
		   sizeof(loop)) == -1){
	sprintf(errtxt,"unable to disable mcast loop");
	perror(errtxt);
    }

    reuse = 1; /* 0 = disable (default), 1 = enable */
    if (setsockopt(mcastsock, SOL_SOCKET, SO_REUSEADDR, &reuse,
		   sizeof(reuse)) == -1){
	sprintf(errtxt,"unable to set reuse flag");
	perror(errtxt);
    }

    /*
     * bind the socket
     */
    if ( bind(mcastsock, (struct sockaddr *)&sin, sizeof(sin)) < 0){
	sprintf(errtxt,"can't bind mcast socket mcast addr[%x]",
		INADDR_UNSPEC_GROUP | MY_MCAST_GROUP);
	perror(errtxt);
    }

    msin.sin_family = AF_INET;
    msin.sin_addr.s_addr = htonl(INADDR_UNSPEC_GROUP | MY_MCAST_GROUP);
    msin.sin_port = sin.sin_port;

    if(gethostname(host,sizeof(host))<0){
	sprintf(errtxt,"unable to get hostname");
	perror(errtxt);
    }

    /*
     * map host name to IP address
     */
    if((phe = gethostbyname(host))){
	bcopy(phe->h_addr, (char *)&in_sin.s_addr, phe->h_length);
    }else{
	if((in_sin.s_addr = inet_addr(host)) == INADDR_NONE){
	    sprintf(errtxt,"can't get %s host entry", host);
	    perror(errtxt);
	}
    }

    init();
    printf("%s",display); fflush(stdout);
    while(1){
	FD_ZERO(&rfds);
	FD_SET(mcastsock,&rfds);
	select(mcastsock+1,&rfds,NULL,NULL, NULL);
	if(FD_ISSET(mcastsock,&rfds)){
	    slen = sizeof(sin);
	    recvfrom(mcastsock, &buf, sizeof(Mmsg), 0,
		     (struct sockaddr *)&sin, &slen);
	    count[buf.node]++;
	    display[buf.node]=(char)wheel[count[buf.node]%4];
	    if(debug){
		printf("receiving MCAST Ping from %x, type=%x state[%x]:%x"
		       " Load[%.2f|%.2f|%.2f]\n",
		       sin.sin_addr.s_addr, buf.type,buf.node,buf.state,
		       buf.load.load[0],buf.load.load[1],buf.load.load[2]);
	    }else{
		printf("%s",display); fflush(stdout);
	    }
	}else{
	    printf("select returned without anything !!\n");
	}
    }
}
