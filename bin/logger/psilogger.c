/*
 *               ParaStation3
 * psilogger.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psilogger.c,v 1.15 2002/02/18 19:48:26 eicker Exp $
 *
 */
/**
 * @file
 * psilogger: Log-daemon for ParaStation I/O forwarding facility
 *
 * $Id: psilogger.c,v 1.15 2002/02/18 19:48:26 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psilogger.c,v 1.15 2002/02/18 19:48:26 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

/* DEBUG_LOGGER allows logger debuging without the daemon
 * Set arg[1] to -1 to open an tcp port (listen):
 * ./psilogger -1 2
 * And psiforwarder (localhost port rank stdin stdout)
 * ./psiforwarder 16777343 20000 0 1 2 
 */
#define DEBUG_LOGGER 1

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

/**
 * Should source and length of each message be displayed ?  (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_SOURCEPRINTF is defined.
 */
int PrependSource = 0;
/**
 * Verbosity of Forwarders (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_FORWARDERDEBUG is defined.
 */
int forw_verbose = 0;
/**
 * Verbosity of Logger (1=Yes, 0=No)
 *
 * Set in main() to 1 if environment variable PSI_LOGGERDEBUG is defined.
 */
int verbose = 0;
/** Number of connected forwarders */
int noclients;
/**
 * Set of fds, the logger listens to. Each member is connected to a forwarder
 * or to the connector socket.
 */
fd_set myfds;

int msock;

/**
 *  sighandler(signal)
 */
void sighandler(int sig)
{
    int i;
    switch(sig){
    case SIGTERM:
	if (verbose) {
	    printf("PSIlogger: No of clients: %d open sockets:", noclients);
	    for(i=0; i<FD_SETSIZE; i++)
		if(FD_ISSET(i, &myfds))
		    printf(" %d",i);
	    printf("\n");
	}
	if (msock!=-1) {
	    close(msock);
	    msock = -1;
	}
    }
    fflush(stdout);

    signal(sig, sighandler);
}

/**
 * @brief Handles connection requests from new forwarders.
 *
 * Accepts a new connection from a forwarder. The new socket is set up
 * for reuse and a @ref INITIALIZE message is sent to the forwarder.
 *
 * @param listen The socket to listen to.
 *
 * @return On success, the new fd (which is also index in the client array)
 * is returned. On error, -1 is returned, and errno is set appropriately.
 */
int newrequest(int listen)
{
    struct sockaddr_in sa; /* socket address */
    int salen;
    int sock, reuse = 1;

    salen = sizeof(sa);
    sock = accept(listen, (struct sockaddr *)&sa, &salen);
    if (sock < 0) return sock;

    if(verbose){
	int cli_port;
	char *cli_name;
	cli_name = inet_ntoa(sa.sin_addr);
	cli_port = ntohs(sa.sin_port);
	fprintf(stderr, "PSIlogger: new connection (%d) from %s (%d)\n",
		sock, cli_name, cli_port);
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* Send Init-message */
    writelog(sock, INITIALIZE, 0,
	     (char *) &forw_verbose, sizeof(forw_verbose));

    return sock;
}

/**
 * @brief Checks file table after select has failed.
 *
 * @param openfds Set of file descriptors that have to be checked.
 *
 * @return No return value.
 */
void CheckFileTable(fd_set* openfds)
{
    fd_set rfds;
    int fd;
    struct timeval tv;
    char* errtxt;

    for(fd=0;fd<FD_SETSIZE;){
	if(FD_ISSET(fd,openfds)){
	    memset(&rfds, 0, sizeof(rfds));
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

void forward_input(int std_in, int fwclient)
{
    char buf[1000];
    int len;

    len = read(std_in, buf, sizeof(buf)>SSIZE_MAX ? SSIZE_MAX : sizeof(buf));
    if (len > 0){
	writelog(fwclient,STDIN, -1, buf, len);
    } else {
	FD_CLR(std_in, &myfds);
	/* close(std_in); */ /* Don't close std_in to prevent fd=0 from use */
    }
}

/**
 * @brief The main loop
 *
 * Does all the logging work. All forwarders can connect and log via
 * the logger. Forwarders send I/O data via @ref STDOUT and @ref STDERR
 * messages.
 *
 * @param listen The socket to read from.
 *
 * @return No return value.
 */
void loop(int listen)
{
    int sock;            /* client socket */
    fd_set afds;
    struct timeval mytv={2,0}, atv;
    FLBufferMsg_t msg;
    int n;               /* number of bytes received */
    int outfd;
    int timeoutval;
    int forward_input_sock = -1; /* client socket which want stdin */
    
    if (verbose) {
	fprintf(stderr, "PSIlogger: listening on port %d\n", listen);
    }

    FD_ZERO(&myfds);
    FD_SET(listen, &myfds);

    noclients = 0;
    timeoutval=0;

    /*
     * Loop until there is no connection left. Pay attention to the startup
     * phase, while no connection exists. Thus wait at least 10 * mytv.
     */
    while ( noclients > 0 || timeoutval < 10 ) {
	memcpy(&afds, &myfds, sizeof(afds));
	atv = mytv;
	if ( select(FD_SETSIZE, &afds, NULL,NULL,&atv) < 0 ) {
	    if (errno == EINTR) {
                /* Interrupted syscall, just start again */
                continue;
	    }
	    fprintf(stderr, "PSIlogger: error on select(%d): %s\n", errno,
		    strerror(errno));
	    CheckFileTable(&myfds);
	    continue;
	}
	/*
	 * check the listen socket for any new connections
	 */
	if ( FD_ISSET(listen, &afds) ) {
	    /* a connection request on my master socket */
	    if ((sock = newrequest(listen)) > 0) {
		FD_SET(sock, &myfds);
		timeoutval = 10;
		noclients++;
		if (verbose) {
		    fprintf(stderr, "PSIlogger: opening %d\n", sock);
		}
	    }
	}
	if (FD_ISSET(STDIN_FILENO, &afds) && (forward_input_sock >= 0)) {
	    forward_input(STDIN_FILENO, forward_input_sock);
	}
	/*
	 * check the rest sockets for any outputs
	 */
	for (sock=3; sock<FD_SETSIZE; sock++) {
	    if (FD_ISSET(sock, &afds) /* socket ready */
		&&(sock != listen)) {    /* not my listen socket */
		n = readlog(sock, &msg);
		if (verbose) {
		    fprintf(stderr, "PSIlogger: Got %d bytes on sock %d\n",
			    n, sock);
		}
		if (n==0) {
		    /* socket closed */
		    fprintf(stderr, "PSIlogger: socket %d closed without"
			    " FINALIZE. This shouldn't happen...\n", sock);
		    close(sock);
		    FD_CLR(sock,&myfds);
		    noclients--;
		} else {
		    if (n<0) {
			/* ignore the error */
			perror("PSIlogger: read()");
		    } else {
			/* Analyze messages */
			outfd = STDOUT_FILENO;
			switch (msg.header.type) {
			case FINALIZE:
			    if(verbose)
				fprintf(stderr,
					"PSIlogger: closing %d on FINALIZE\n",
					sock);
			    writelog(sock, EXIT, 0, NULL, 0);
			    close(sock);
			    FD_CLR(sock,&myfds);
			    if (sock == forward_input_sock){
				/* disable input forwarding */
				FD_CLR(STDIN_FILENO,&myfds);
				forward_input_sock = -1;
			    }
			    noclients--;
			    break;
			case STDERR:
			    outfd = STDERR_FILENO;
			case STDOUT:
			    if(PrependSource){
				char prefix[30];
				if (verbose) {
				    snprintf(prefix, sizeof(prefix),
					     "[%d, %d]:", msg.header.sender,
					     msg.header.len);
				    write(outfd, prefix, strlen(prefix));
				}else{
				    if (msg.header.len - sizeof(msg.header)>0){
					snprintf(prefix, sizeof(prefix),
						 "[%d]:", msg.header.sender);
					write(outfd, prefix, strlen(prefix));
				    }
				}
			    }
			    write(outfd, msg.buf,
				  msg.header.len - sizeof(msg.header));
			    if ((msg.header.sender == 0)
				&& (forward_input_sock < 0)) {
				/* rank 0 want the input */
				forward_input_sock = sock;
				FD_SET(STDIN_FILENO,&myfds);
				if(verbose){
				    fprintf(stderr, "PSIlogger:"
					    " forward input to sock %d\n",
					    forward_input_sock);
				}
			    }
			    break;
			default:
			    fprintf(stderr,
				    "PSIlogger: Unknown message type!\n");
			}
		    }
		}
	    }
	}
	if ( noclients==0 ) {
	    timeoutval++;
	}
    }
    if ( getenv("PSI_NOMSGLOGGERDONE")==NULL ) {
	fprintf(stderr,"\nPSIlogger: done\n");
    }

    return;
}

#ifdef DEBUG_LOGGER
/*********************************************************************
 * int LOGGERopenPort()
 *
 * open the logger port.
 * RETURN the portno of the logger
 */
unsigned short LOGGERopenPort(void)
{
    unsigned short defaultPortNo = 20000;
    struct sockaddr_in sa;	/* socket address */ 
    int err;                    /* error code while binding */
    int listenport;
    /*
     * create a port for the logger to listen to.
     */
    if((listenport = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP))<0){
	perror("LOGGERopenPort: can't create socket:");
	exit(1);
    }
    memset(&sa, 0, sizeof(sa)); 
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(defaultPortNo); 

    while(((err = bind(listenport, (struct sockaddr *)&sa, sizeof(sa)))<0)
	  &&(errno == EADDRINUSE)){
	sa.sin_port = htons(ntohs(sa.sin_port)-1); 
    }
    if(err <0){
	perror("LOGGERopenPort: can't bind socket:");
	exit(1);
    }

    if(listen(listenport, 256)<0){
	perror("LOGGERopenPort: can't listen to socket:");
	exit(1);
    }

    printf("Using port %d\n",ntohs(sa.sin_port));
    return listenport;
}
#endif

/**
 * @brief The main program
 *
 * After becoming process group leader, sets global variables @ref
 * verbose, @ref forw_verbose and @ref PrependSource from environment
 * and finally calls loop().
 *
 * @param argc The number of arguments in @a argv.
 * @param argv Array of character strings containing the arguments.
 *
 * This program expects at least 1 additional argument:
 *  -# The port number it will listen to.
 *
 * @return Always returns 0.  */
int main( int argc, char**argv)
{
    int listen;

    /* become process group leader */
    // setpgid(0,0);
    // signal(SIGHUP,sighandler);
    signal(SIGTERM,sighandler);

    if ( argc < 3 ) {
	fprintf(stderr, "PSIlogger: Sorry, program must be called correctly"
		" inside an application.\n");
	exit(1);
    }
    listen = atol(argv[1]);
    msock = atol(argv[2]);

    if ( getenv("PSI_LOGGERDEBUG") != NULL ) {
	verbose=1;
	fprintf(stderr, "PSIlogger: Going to be verbose.\n");
    }

    if ( getenv("PSI_FORWARDERDEBUG") != NULL ) {
	forw_verbose=1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Forwarders will be verbose, too.\n");
	}
    }

    if ( getenv("PSI_SOURCEPRINTF") != NULL ) {
	PrependSource = 1;
	if (verbose) {
	    fprintf(stderr, "PSIlogger: Will print source-info.\n");
	}
    }

#ifdef DEBUG_LOGGER
    /* For debug: */
    if (listen < 0)
	listen = LOGGERopenPort();
#endif
    
    /* call the loop which does all the work */
    loop(listen);

    close(listen);
    if (msock!=-1) {
	close(msock);
    }

    return 0;
}
