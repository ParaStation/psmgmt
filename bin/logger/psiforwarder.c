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

#include "psi.h"
#include "logger.h"
#include "logmsg.h"

struct LOGGERclient_t *clients=NULL;

int verbose;
int noclients;
int maxclients=0;
fd_set myfds;

int loggersock=-1;
int id=-1;


/******************************************
 *  closelog()
 *
 * Close connection to logger. Send FINALIZE and wait for EXIT message.
 *
 */
void closelog(void)
{
    FLMsg_t msg;
    writelog(loggersock, FINALIZE, id, NULL, 0);

    readlog(loggersock, (FLBufferMsg_t *) &msg);
    if(msg.type != EXIT){
	/* Protocol messed up. Hopefully we can log anyhow. */
	printlog(loggersock, STDERR, id,
		 "PSIForwarder: PANIC!! Protocol messed up!\n");
    }

    if(loggersock>0)
	close(loggersock);
}

/******************************************
 *  sighandler(sig)
 */
void sighandler(int sig)
{
    char buf[80];

    switch(sig){
    case SIGHUP:    /* hangup, generated when terminal disconnects */
	snprintf(buf, sizeof(buf),
		 "PSIForwarder: PANIC!! No of clients left: %d\n", noclients);
	printlog(loggersock, STDERR, id, buf);
    }

    closelog();
}

/******************************************
 *  loggerconnect(u_int node, int port)
 *
 * Connect the logger listening at 'node' on 'port. Wait for INITIALIZE
 * message and set verbosity-flag correctly.
 *
 */
int loggerconnect(unsigned int node, int port)
{
    struct sockaddr_in sa;	/* socket address */
    FLInitMsg_t msg;

    if((loggersock = socket(PF_INET,SOCK_STREAM,0))<0){
	return(-1);
    }

    bzero((char *)&sa, sizeof(sa)); 
    sa.sin_family = PF_INET; 
    sa.sin_addr.s_addr = node;
    sa.sin_port = htons(port);

    if((connect(loggersock,(struct sockaddr *)&sa, sizeof(sa)))<0){
	return(-1);
    }

    readlog(loggersock, (FLBufferMsg_t *)&msg);
    if(msg.header.type != INITIALIZE){
	/* Protocol messed up. Hopefully we can log anyhow. */
	printlog(loggersock, STDERR, id,
		 "PSIForwarder: PANIC!! Protocol messed up!\n");
	closelog();

	return -1;
    }else{
	verbose=msg.verbose;
    }

    return loggersock;
}

/*********************************************************************
 * newrequest(int listen)
 *
 * accepts a new connection to a client.
 * The client sends its parameters 
 * and this routine packs them in a client_t struct
 *
 * RETURN the new fd (which is also index in the client array
 *        -1 on error
 */
int newrequest(int listen)
{
    struct sockaddr_in sa; /* socket address */ 
    int salen;
    int sock, reuse;
    char buf[80];

    salen = sizeof(sa);
    sock = accept(listen, &sa, &salen);

    if(verbose){
	int cli_port;
	char *cli_name;

	cli_name = inet_ntoa(sa.sin_addr);
        cli_port = ntohs(sa.sin_port);
	snprintf(buf, sizeof(buf),
		 "PSIforwarder: new connection from %s (%d)\n",
		 cli_name, cli_port);
	printlog(loggersock, STDERR, id, buf);
    }

    reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if(sock >= maxclients){
	/* Expand client array */
	maxclients = sock + 16;
	clients = (struct LOGGERclient_t *)
	    realloc(clients, maxclients * sizeof(struct LOGGERclient_t));
    }

    if(read(sock, &clients[sock], sizeof(struct LOGGERclient_t)) > 0){
	if(clients[sock].std==STDOUT_FILENO)
	    clients[sock].std = STDOUT;
	else
	    clients[sock].std = STDERR;
	id=PSI_getnode(clients[sock].id);
	if(verbose){
	    snprintf(buf, sizeof(buf),
		     "PSIforwarder: %lx [%d,%d] is logging his %s\n",
		     clients[sock].id, PSI_getnode(clients[sock].id),
		     PSI_getpid(clients[sock].id),
		     (clients[sock].std==STDOUT)?"STDOUT":"STDERR");
	    printlog(loggersock, STDERR, id, buf);
	}
    }else{
	close(sock);
	sock = -1;
    }

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
    char *errtxt, buf[80];

    for(fd=0;fd<FD_SETSIZE;){
	if(FD_ISSET(fd,openfds)){
	    bzero(&rfds,sizeof(rfds));
	    FD_SET(fd,&rfds);

	    tv.tv_sec=0;
	    tv.tv_usec=0;
	    if (select(FD_SETSIZE, &rfds, NULL, NULL, &tv) < 0){ 
		/* error : check if it is a wrong fd in the table */
		switch(errno){
		case EBADF :
		    snprintf(buf, sizeof(buf), "CheckFileTable(%d): EBADF"
			     " -> close socket\n", fd);
		    printlog(loggersock, STDERR, id, buf);
		    close(fd);
		    FD_CLR(fd,openfds);
		    fd++;
		    break;
		case EINTR:
		    snprintf(buf, sizeof(buf), "CheckFileTable(%d): EINTR"
			     " -> trying again\n", fd);
		    printlog(loggersock, STDERR, id, buf);
		    break;
		case EINVAL:
		    snprintf(buf, sizeof(buf), "CheckFileTable(%d): EINVAL"
			     " -> close socket\n", fd);
		    printlog(loggersock, STDERR, id, buf);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		case ENOMEM:
		    snprintf(buf , sizeof(buf), "CheckFileTable(%d): ENOMEM"
			    " -> close socket\n",fd);
		    printlog(loggersock, STDERR, id, buf);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		default:
		    errtxt=strerror(errno);
		    snprintf(buf, sizeof(buf), "CheckFileTable(%d):"
			    " unrecognized error (%d):%s\n", fd, errno,
			    errtxt?errtxt:"UNKNOWN errno");
		    printlog(loggersock, STDERR, id, buf);
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
 * does all the forwarding work. Now all tasks can connect and forward
 * output to the logger.
 *
 */
void loop(int listen)
{
    int sock;      /* client socket */
    fd_set afds;
    struct timeval mytv={2,0},atv;
    char buf[4000], obuf[120];
    int n;                       /* number of bytes received */
    int timeoutval;
    int startup=1;

    if(verbose){
	snprintf(obuf, sizeof(obuf), "PSIforwarder: listening on port %d\n",
		 listen);
	printlog(loggersock, STDERR, id, obuf);
    }

    FD_ZERO(&myfds);
    FD_SET(listen, &myfds);

    noclients = 0;
    timeoutval=0;

    /*
     * Loop until there is no connection left. Pay attention to the startup
     * phase, while no connection exists.
     */
    while(startup || (noclients > 0 && timeoutval < 10)){
	bcopy((char *)&myfds, (char *)&afds, sizeof(afds)); 
	atv = mytv;
	if(select(FD_SETSIZE, &afds, NULL, NULL, &atv) < 0){
	    snprintf(obuf, sizeof(obuf), "PSIforwarder: error on select(%d):"
		     " %s\n", errno, strerror(errno));
	    printlog(loggersock, STDERR, id, obuf);
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
		if(verbose){
		    snprintf(obuf, sizeof(obuf), "PSIforwarder: opening %d\n",
			     sock);
		    printlog(loggersock, STDERR, id, obuf);
		}
	    }
	}
	/*
	 * check the rest sockets for any outputs
	 */
	for(sock=3; sock<FD_SETSIZE; sock++)
	    if(FD_ISSET(sock, &afds) /* socket ready */
	       &&(sock != listen)){   /* not my listen socket */
		n = read(sock, buf, sizeof(buf));
		if(verbose){
		    snprintf(obuf, sizeof(obuf),
			     "PSIforwarder: got %d bytes on sock %d\n",
			     n, sock);
		    printlog(loggersock, STDERR, id, obuf);
		}
		if(n==0){
		    /* socket closed */
		    if(verbose){
			snprintf(obuf, sizeof(obuf),
				 "PSIforwarder: closing %d\n", sock);
			printlog(loggersock, STDERR, id, obuf);
		    }
		    close(sock);
		    FD_CLR(sock,&myfds);
		    noclients--;
		}else if(n<0){
		    /* ignore the error */
		    snprintf(obuf, sizeof(obuf), "PSIforwarder: read():%s\n",
			     strerror(errno));
		    printlog(loggersock, STDERR, id, obuf);
		}else{
		    /* forward it to logger */
		    writelog(loggersock, clients[sock].std,
			     PSI_getnode(clients[sock].id), buf, n);
		}
	    }
	if(!startup && noclients==0)
	    timeoutval++;
    }

    return;
}

int main( int argc, char**argv)
{
    unsigned int logger_node;
    int logger_port, listen;

    int ret;

    if(argc<4){
	exit(1);
    }
    sscanf(argv[1], "%u", &logger_node);
    sscanf(argv[2], "%d", &logger_port);
    sscanf(argv[3], "%d", &listen);

    if((ret=loggerconnect(logger_node, logger_port)) < 0){
	exit(1);
    }

    /*
     * Init clients array. Will be expanded if necessary.
    maxclients = 16;
    clients = (struct LOGGERclient_t *)
	malloc(maxclients * sizeof(struct LOGGERclient_t));

    signal(SIGHUP,sighandler);	

    /*
     * call the loop which does all the work
     */
    loop(listen);

    closelog();

    return 0;
}
