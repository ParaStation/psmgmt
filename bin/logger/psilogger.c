#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "logmsg.h"

int PrependSource = 0;
int forw_verbose = 0;
int verbose = 0;
int noclients;
fd_set myfds;

/******************************************
 *  sighandler(signal)
 */
void sighandler(int sig)
{
    int i;
    switch(sig){
    case SIGHUP:    /* hangup, generated when terminal disconnects */
	printf("PSIlogger: No of clients:%d open sockets:", noclients);
	for(i=0; i<FD_SETSIZE; i++)
	    if(FD_ISSET(i, &myfds))
		printf(" %d",i);
	printf("\n");
    }
    fflush(stdout);
}


/*********************************************************************
 * newrequest(int listen)
 *
 * accepts a new connection to a forwarder.
 * The client sends its parameters 
 * and this routine packs them in a client_t struct
 * RETURN the new fd (which is also index in the client array
 *        -1 on error
 */
int newrequest(int listen)
{
    struct sockaddr_in sa; /* socket address */ 
    int salen;
    int sock, reuse;

    salen = sizeof(sa);
    sock = accept(listen, &sa, &salen);

    if(verbose){
	int cli_port;
	char *cli_name;
	cli_name = inet_ntoa(sa.sin_addr);
        cli_port = ntohs(sa.sin_port);
	printf("PSIlogger: new connection from %s (%d)\n", cli_name, cli_port);
    }

    reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* Send Init-message */
    writelog(sock, INITIALIZE, 0,
	     (char *) &forw_verbose, sizeof(forw_verbose));

    return sock;
}

/******************************************
 *  CheckFileTable()
 */
void CheckFileTable(fd_set* openfds)
{
    fd_set rfds;
    int fd;
    struct timeval tv;
    char* errtxt;

    for(fd=0;fd<FD_SETSIZE;){
	if(FD_ISSET(fd,openfds)){
	    bzero(&rfds,sizeof(rfds));
	    FD_SET(fd,&rfds);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &rfds, (fd_set *)0, (fd_set *)0, &tv) < 0){ 
		/* error : check if it is a wrong fd in the table */
		switch(errno){
		case EBADF :
		    fprintf(stderr,"CheckFileTable(%d):"
			    " EBADF -> close socket\n",fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    fd++;
		    break;
		case EINTR:
		    fprintf(stderr,"CheckFileTable(%d):"
			    " EINTR -> trying again\n",fd);
		    break;
		case EINVAL:
		    fprintf(stderr,"CheckFileTable(%d):"
			    " EINVAL -> close socket\n", fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		case ENOMEM:
		    fprintf(stderr,"CheckFileTable(%d):"
			    " ENOMEM -> close socket\n",fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		default:
		    errtxt=strerror(errno);
		    fprintf(stderr, "CheckFileTable(%d):"
			    " unrecognized error (%d):%s\n", fd, errno,
			    errtxt?errtxt:"UNKNOWN errno");
		    fd ++;
		    break;
		}
	    }else
		fd ++;
	}else
	    fd ++;
    }
}

/*********************************************************************
 * loop(listen)
 *
 * does all the logging work. Now all forwarders can connect and log via
 * the logger.
 *
 */
void loop(int listen)
{
    int sock;      /* client socket */
    fd_set afds;
    struct timeval mytv={2,0},atv;
    FLBufferMsg_t msg;
    char *buf=(char *)&msg;
    int n;                       /* number of bytes received */
    int outfd;
    int timeoutval;
    int startup=1;

    if(verbose)
	printf("PSIlogger: listening on port %d\n", listen);

    FD_ZERO(&myfds);
    FD_SET(listen, &myfds);

    noclients = 0;
    timeoutval=0;

    /*
     * Loop until there is no connection left Pay attention to the startup
     * phase, while no connection exists.
     */
    while(startup || (noclients > 0 && timeoutval < 10)){
	bcopy((char *)&myfds, (char *)&afds, sizeof(afds)); 
	atv = mytv;
	if(select(FD_SETSIZE, &afds, NULL,NULL,&atv) < 0){
	    fprintf(stderr, "PSIlogger: error on select(%d): %s\n", errno,
		    strerror(errno));
	    CheckFileTable(&myfds);
	    continue;
	}
	/*
	 * check the listen socket for any new connections
	 */
	if(FD_ISSET(listen, &afds)){
	    /* a connection request on my master socket */
	    if((sock = newrequest(listen)) > 0){
		FD_SET(sock, &myfds);
		startup=0;
		noclients++;
		if(verbose)
		    printf("PSIlogger: opening %d\n", sock);
	    }
	}
	/*
	 * check the rest sockets for any outputs
	 */
	for(sock=3; sock<FD_SETSIZE; sock++)
	    if(FD_ISSET(sock, &afds) /* socket ready */
	       &&(sock != listen)){   /* not my listen socket */
		n = readlog(sock, &msg);
		if(verbose)
		    printf("PSIlogger: Got %d bytes on sock %d\n", n, sock); 
		if(n==0){
  		    /* socket closed */
		    fprintf(stderr, "PSIlogger: socket %d closed without"
			    " FINALIZE. This shouldn't happen...\n", sock);
		    close(sock);
		    FD_CLR(sock,&myfds);
		    noclients--;
		}else
		    if(n<0)
		    /* ignore the error */
		    perror("PSIlogger: read()");
		else{
		    /* Analyze messages */
		    buf=(char *)&msg;
		    while(n > 0){
			outfd = STDOUT_FILENO;
			switch(((FLBufferMsg_t *)buf)->header.type){
			case FINALIZE:
			    if(verbose)
				printf("PSIlogger: closing %d on FINALIZE\n",
				       sock);
			    writelog(sock, EXIT, 0, NULL, 0);
			    close(sock);
			    FD_CLR(sock,&myfds);
			    noclients--;
			    break;
			case STDERR:
			    outfd = STDERR_FILENO;
			case STDOUT:
			    if(PrependSource){
				char prefix[30];
				snprintf(prefix, sizeof(prefix), "[%d, %d]:",
					 ((FLBufferMsg_t *)buf)->header.sender,
					 ((FLBufferMsg_t *)buf)->header.len);
				write(outfd, prefix, strlen(prefix));
			    }
			    write(outfd, ((FLBufferMsg_t *)buf)->buf,
				  ((FLBufferMsg_t *)buf)->header.len
				  - sizeof(msg.header));
			    break;
			default:
			    fprintf(stderr,
				    "PSIlogger: Unknown message type!\n");
			}
			n -= ((FLBufferMsg_t *)buf)->header.len;
			buf += ((FLBufferMsg_t *)buf)->header.len;
		    }
		}
	    }
	if(!startup && noclients==0)
	    timeoutval++;
    }
    if(getenv("PSI_NOMSGLOGGERDONE")==NULL){
	fprintf(stderr,"PSIlogger: done\n");
    }

    return;
}

int main( int argc, char**argv)
{
    int listen;

    if(argc<2){
	fprintf(stderr, "PSIlogger: Sorry, program must be called correctly"
	       " inside an application.\n");
	exit(1);
    }
    listen = atol(argv[1]);

    signal(SIGHUP,sighandler);	

    if(getenv("PSI_LOGGERDEBUG")!=NULL){
	verbose=1;
    }

    if(getenv("PSI_FORWARDERDEBUG")!=NULL){
	forw_verbose=1;
    }

    if(getenv("PSI_SOURCEPRINTF")!=NULL){
	PrependSource = 1;
    }

    /*
     * call the loop which does all the work
     */
    loop(listen);

    return 0;
}
