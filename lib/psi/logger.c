#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#include "psi.h"
#include "psitask.h"
#include "psilog.h"

#include "logger.h"

struct LOGGERclient_t{
    long id;
    int std;
    int logfd;
};

struct LOGGERclient_t LOGGERclients[128];

long LOGGERportno = -1;
int LOGGERsocket = -1;
int LOGGERstdout = 1;
int LOGGERstderr = 2;
int PrependSource = 0;

fd_set LOGGERmyfds;
int LOGGERnoclients;

/*************************************************************
 * int
 * LOGGERstdconnect(u_long port,int MyOut,PStask_t* task)
 *
 * creates a socket and connects it to the port. It signals the logger that
 * it will be used as MyOut(STDOUT/STDERR).
 * RETURN the newly created socket.
 */
int
LOGGERstdconnect(unsigned int node, int port, int MyOut, PStask_t* task)
{
    int sock;     /* master socket to listen to.*/
    struct sockaddr_in sa;	/* socket address */ 
    struct LOGGERclient_t mystruct;

    FILE *tmpout;

    if((sock = socket(PF_INET,SOCK_STREAM,0))<0){
	perror("PSPlogger: can't create socket:");
#ifdef DEBUG
	sprintf(PSI_txt,"LOGGERstdconnect: socket() error %d\n",errno);
	PSI_logerror(PSI_txt);
#endif
	return(-1);
    }

    bzero((char *)&sa, sizeof(sa)); 
    sa.sin_family = PF_INET; 
    sa.sin_addr.s_addr = node;
    sa.sin_port = htons(port);

    if((connect(sock,(struct sockaddr *)&sa, sizeof(sa)))<0){
	perror("Logger: can't connect socket:");
#ifdef DEBUG
	sprintf(PSI_txt,"LOGGERstdconnect: connect() error %d\n",errno);
	PSI_logerror(PSI_txt);
#endif
	return(-1);
    }

    mystruct.id = PSI_gettid(-1,getpid());
    mystruct.std = MyOut;
    mystruct.logfd = 0;

    if(task->options & TaskOption_SENDSTDHEADER)
	write(sock,&mystruct,sizeof(struct LOGGERclient_t));

    dup2(sock, MyOut);
    close(sock);

    return sock;
}

/*************************************************************
 * int
 * LOGGERstdDevNull(int std)
 * 
 * redirect stdout/stderr to /dev/null
 * RETURN 0 success
 *        -1 error errno is set.
 */
int
LOGGERstdDevNull(int std)
{
    int fd;

    if((fd = open("/dev/null",O_WRONLY, S_IRUSR|S_IWUSR))<0){
	sprintf(PSI_txt,"LOGGERstdDevNull: Even </dev/null> is not "
		"usable for %s!! PANIC exit (errno %d)!!",
		(std==1)?"STDOUT":"STDERR",errno);
	PSI_logerror(PSI_txt);
	errno = EIO;
	return -1;
    }
    sprintf(PSI_txt,"LOGGERredirect_std: Now using /dev/null for %s\n",
	    (std==1)?"STDOUT":"STDERR");
    PSI_logerror(PSI_txt);
    close(std);
    dup(fd);
    close(fd);
    return 0;
}


/*************************************************************
 * int
 * LOGGERredirect_std(unsigned int node, int port, PStask_t* task)
 * 
 * redirect stdout/stderr to sockets connected to port
 * RETURN 0 success
 *        -1 error errno is set.
 */
int
LOGGERredirect_std(unsigned int node, int port, PStask_t* task)
{
    int sock;     /* master socket to listen to.*/

    if(port>0){
	/* logging should take place to an remote logger */
	/* 
	 * - create a socket for stdout 
	 * - close the original stdout 
	 * - redirect the original stdout to the new socket
	 */
	fflush(stdout);
	fflush(stderr);
	if((sock = LOGGERstdconnect(node, port, 1, task))<0)
	    if(LOGGERstdDevNull(1)<0)
		return -1;

	/* 
	 * - create a socket for stderr 
	 * - close the original stderr
	 * - redirect the original stderr to the new socket
	 */
	if(task->options & TaskOption_ONESTDOUTERR){
	    int dup2ret=0;
	    close(2);   /* close stderr */
	    dup2ret = fcntl(1, F_DUPFD, 2);
	}else if((sock = LOGGERstdconnect(node, port, 2, task))<0)
	    if(LOGGERstdDevNull(2)<0)
		return -1;
    }else{
	/* TODO: use /dev/null for any output */

	int fd;           /* filedesc. of the stdout/stderr while creating */
	char stdname[40]; /* name for stdout/stderr files of spawned process*/

	/* open stdout && stderr */
	sprintf(stdname,"/tmp/%s.%d.stdout",task->argv[0],getpid());
	if((fd = open(stdname,O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR))<0){
	    char* errtxt;
	    errtxt=strerror(errno);
	    sprintf(PSI_txt,"LOGGERredirect_std: can not open new stdout "
		    " errno(%d) %s <%s>>",
		    errno,errtxt?errtxt:"UNKNOWN",stdname);
	    PSI_logerror(PSI_txt);
	    if(LOGGERstdDevNull(1)<0)
		return -1;
	}else{
	    close(1);
	    dup(fd);
	    close(fd);
	}
	sprintf(stdname,"/tmp/%s.%d.stderr",task->argv[0],getpid());
	if((fd = open(stdname, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR))<0){
	    char* errtxt;
	    errtxt=strerror(errno);
	    sprintf(PSI_txt,"LOGGERredirect_std:"
		    " can not open new stdout  errno(%d) %s <%s>",
		    errno,errtxt?errtxt:"UNKNOWN",stdname);
	    PSI_logerror(PSI_txt);

	    if((fd = open("/dev/null",O_WRONLY, S_IRUSR|S_IWUSR))<0){
		sprintf(PSI_txt,"LOGGERredirect_std: Even </dev/null> is not "
			"usable !! PANIC exit!!");
		PSI_logerror(PSI_txt);
		errno = EIO;
		return -1;
	    }
	    sprintf(PSI_txt,"LOGGERredirect_std: Now using /dev/null");
	    PSI_logerror(PSI_txt);
	}
	close(2);
	dup(fd);
    }
    return 0;
}

/************************************************************************
 * LOGGERcreateport(int * portno)
 *  creates an INET port for the logger to listen to.
 *  it trys to bind to the portno and decrements this portno if a bind is
 *  not possible. 
 *  RETURN -1 on error; socketfd of success
 *         portno is set to the real portno
 */
int
LOGGERcreateport(long * portno)
{
    struct sockaddr_in sa;	/* socket address */ 
    int sock;
    int err;                    /* error code while binding */

#ifdef DEBUG
    if(0){
	sprintf(PSI_txt,"LOGGERcreateport(%ld)\n",*portno);
	PSI_logerror(PSI_txt);
    }
#endif

    if((sock = socket(AF_INET,SOCK_STREAM,0))<0){
	perror("PSPlogger: can't create socket:");
	return(-1);
    }
    bzero((char *)&sa, sizeof(sa)); 
    sa.sin_family = AF_INET; 
    sa.sin_addr.s_addr = INADDR_ANY;

    sa.sin_port = htons(*portno); 

    while(((err = bind(sock,(struct sockaddr *)&sa, sizeof(sa)))<0)
	  &&(errno == EADDRINUSE)){
	sa.sin_port = htons(ntohs(sa.sin_port)-1); 
    }
    if(err <0){
	perror("PSPlogger: can't bind socket:");
	return(-1);
    }

    if((listen(sock,256))<0){
	perror("PSPlogger: can't listen to socket:");
	return(-1);
    }
    *portno = ntohs(sa.sin_port);

    return sock;
}

/*********************************************************************
 * LOGGERgetparentsock(int listensock)
 *
 * waits util the parent connects to this socket.
 * This socket is needed for (logger<->parent) control
 */
int 
LOGGERgetparentsock(int listensock)
{
   struct sockaddr sa;	/* socket address */ 
   int salen;
   int sock;

#ifdef DEBUG
   if(0)
     {
       sprintf(PSI_txt,"LOGGERgetparentsock(%d)\n",listensock);
       PSI_logerror(PSI_txt);
     }
#endif

   salen = sizeof(sa);
   sock = accept(listensock, &sa,&salen);
   
   /* TODO setup the protocol*/
   /* TODO setup the initial stdout/stderr */
   return sock;
}

/*********************************************************************
 * LOGGERparentrequest(int sock)
 *
 * get the message from the parent and react in that way
 */
int 
LOGGERparentrequest(int sock)
{
    char buffer[4096];
    int n;

#ifdef DEBUG
    if(0){
	sprintf(PSI_txt,"LOGGERparentrequest(%d)\n",sock);
	PSI_logerror(PSI_txt);
    }
#endif

    n = read(sock,buffer,sizeof(buffer));

    /* TODO (parent<-> logger) protocol specification */
    return n;
}

/*********************************************************************
 * LOGGERgetloggersock(int portno)
 *
 * it connects to the logger. This function is only called by the parent.
 * This socket is needed for (logger<->parent) control
 */
int 
LOGGERgetloggersock(long portno)
{
    int sock;     /* master socket to listen to.*/
    struct sockaddr_in sa;	/* socket address */ 

#ifdef DEBUG
    if(0){
	sprintf(PSI_txt,"LOGGERgetloggersock(%lx[%ld,%ld])\n",
		portno,portno>>16,portno&0xffff);
	PSI_logerror(PSI_txt);
    }
#endif

    if((sock = socket(AF_INET,SOCK_STREAM,0))<0){
	perror("Loggerparent: can't create socket:");
	return(-1);
    }

    bzero((char *)&sa, sizeof(sa)); 
    sa.sin_family = AF_INET; 
    sa.sin_addr.s_addr = INADDR_ANY;

    sa.sin_port = htons(portno & 0xffff); 

    if((connect(sock,(struct sockaddr *)&sa, sizeof(sa)))<0){
	perror("Logger: can't connect socket:");
	return(-1);
    }

    /* TODO setup the protocol*/
    /* TODO setup the initial stdout/stderr */

    return sock;
}

/*********************************************************************
 * LOGGERnewrequest(int LOGGERlisten)
 *
 * accepts a new connection to a client.
 * The client sends its parameters 
 * and this routine packs them in a LOGGERclient_t struct
 * RETURN the new fd (which is also index in the LOGGERclient array
 *        -1 on error
 */
int
LOGGERnewrequest(int LOGGERlisten, int verbose)
{
    struct sockaddr_in sa; /* socket address */ 
    int salen;
    int sock;

#ifdef DEBUG
    if(0){
	sprintf(PSI_txt,"LOGGERnewrequest(%d)\n",LOGGERlisten);
	PSI_logerror(PSI_txt);
    }
#endif

    salen = sizeof(sa);
    sock = accept(LOGGERlisten, &sa,&salen);
    if(verbose){
	int cli_port;
	char *cli_name;
	cli_name = inet_ntoa(sa.sin_addr);
        cli_port = ntohs(sa.sin_port);
	printf("PSPlogger: new connection from %s (%d)\n", cli_name, cli_port);
    }
    if(read(sock,&LOGGERclients[sock],sizeof(struct LOGGERclient_t))>0){
#ifdef DEBUG
	sprintf(PSI_txt,"PSPlogger: %ld is logging his %s\n",
		LOGGERclients[sock].id,
		(LOGGERclients[sock].std==1)?"STDOUT":"STDERR");
	PSI_logerror(PSI_txt);
#endif
	if(LOGGERclients[sock].std==1)
	    LOGGERclients[sock].logfd = LOGGERstdout;
	else
	    LOGGERclients[sock].logfd = LOGGERstderr;
	if(verbose){
	    printf("PSPlogger: %lx [%d,%d] is logging his %s\n",
		   LOGGERclients[sock].id,
		   PSI_getnode(LOGGERclients[sock].id),
		   PSI_getpid(LOGGERclients[sock].id),
		   (LOGGERclients[sock].std==1)?"STDOUT":"STDERR");
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
void LOGGER_CheckFileTable(fd_set* openfds)
{
    fd_set rfds;
    int fd;
    struct timeval tv;

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
		    /* if(SHM_isoption(PSP_ODEBUG))*/
		    fprintf(stderr,"LOGGER_CheckFileTable(%d):"
			    " EBADF -> close socket\n",fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    fd++;
		    break;
		case EINTR:
		    fprintf(stderr,"LOGGER_CheckFileTable(%d):"
			    " EINTR -> trying again\n",fd);
		    break;
		case EINVAL:
		    fprintf(stderr,"CheckFileTable(%d):"
			    " PANIC filenumber is wrong. Close socket!\n", fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		case ENOMEM:
		    fprintf(stderr,"CheckFileTable(%d):"
			    " PANIC not enough memory. Good bye!\n",fd);
		    close(fd);
		    FD_CLR(fd,openfds);
		    break;
		default:
		{
		    char* errtxt;
		    errtxt=strerror(errno);
		    fprintf(stderr, "CheckFileTable(%d):"
			    " unrecognized error (%d):%s\n", fd, errno,
			    errtxt?errtxt:"UNKNOWN errno");
		    fd ++;
		    break;
		}
		}
	    }else
		fd ++;	    
	}else
	    fd ++;
    }
}

/*********************************************************************
 * LOGGERloop(LOGGERlisten,LOGGERparent)
 *
 * does all the logging work. The connection to the parent is already
 * installed. Now childs can connect and log out via the logger.
 */
int
LOGGERloop(int LOGGERlisten,int LOGGERparent)
{
    int sock;      /* client socket */
    fd_set afds;
    struct timeval mytv={2,0},atv;
    char buf[4000];
    char buf2[4094];
    int n;                       /* number of bytes received */
    int timeoutval;
    int verbose=0;

#ifdef DEBUG
    if(0){
	sprintf(PSI_txt,"PSPlogger(%d,%d)\n",LOGGERlisten,LOGGERparent);
	PSI_logerror(PSI_txt);
    }
#endif

    if(getenv("PSI_LOGGERDEBUG")!=NULL){
	verbose=1;
    }

    if(verbose)
	printf("PSPlogger: LOGGERlisten:%d  LOGGERparent:%d\n",
	       LOGGERlisten, LOGGERparent);

    FD_ZERO(&LOGGERmyfds);
    FD_SET(LOGGERlisten,&LOGGERmyfds);
    FD_SET(LOGGERparent,&LOGGERmyfds);

    LOGGERnoclients = 1;  /* the parent */

    timeoutval=0;
    /*
     * Loop until there is no connection left
     */
    while(LOGGERnoclients>0 && timeoutval<10){
	bcopy((char *)&LOGGERmyfds, (char *)&afds, sizeof(afds)); 
	atv = mytv;
	if(select(FD_SETSIZE,&afds,NULL,NULL,&atv)<0){
	    sprintf(PSI_txt,"PSPlogger:error on select(%d)\n",errno);
	    PSI_logerror(PSI_txt);
	    if(verbose)
		printf("PSPlogger:error on select(%d)\n",errno);
	    LOGGER_CheckFileTable(&LOGGERmyfds);
	    continue;
	}
	/*
	 * check the parent socket for any control msgs
	 */
	if((LOGGERparent>0) && (FD_ISSET(LOGGERparent,&afds))){
	    /* 
	     * a control request from the parent. 
	     * Maybe I have to change any of my settings 
	     */
	    if((LOGGERparentrequest(LOGGERparent))==0){
		close(LOGGERparent);
		FD_CLR(LOGGERparent,&LOGGERmyfds);
		FD_CLR(LOGGERparent,&afds);
		LOGGERparent = -1;
		LOGGERnoclients--;
	    }
	}
	/*
	 * check the listen socket for any new connections
	 */
	if(FD_ISSET(LOGGERlisten,&afds)){
	    /* a connection request on my master socket */
	    if((sock = LOGGERnewrequest(LOGGERlisten, verbose))>0){
		FD_SET(sock,&LOGGERmyfds);
		LOGGERnoclients++;
		if(verbose)
		    printf("PSPlogger: opening %d\n", sock);
	    }
	}
	/*
	 * check the rest sockets for any outputs
	 */
	for(sock=3;sock<FD_SETSIZE;sock++)
	    if(FD_ISSET(sock,&afds) /* socket ready */
	       &&(sock != LOGGERparent)   /* not my parent socket */
	       &&(sock != LOGGERlisten)){   /* not my listen socket */
		n = read(sock,buf,sizeof(buf));
		if(verbose)
		    printf("Got %d bytes on sock %d\n", n, sock); 
		buf[n] = 0x0;
		if(n==0){
		    /* socket closed */
		    if(verbose)
			printf("PSPlogger: closing %d\n", sock);
		    close(sock);
		    FD_CLR(sock,&LOGGERmyfds);
		    LOGGERnoclients--;
		}else if(n<0)
		    /* ignore the error */
		    perror("PSPlogger:read()");
		else{
		    /* print it out */
		    if(PrependSource){
			sprintf(buf2,"%8lx[%4d,%4d]%s:%s",
				LOGGERclients[sock].id,
				PSI_getnode(LOGGERclients[sock].id),
				PSI_getpid(LOGGERclients[sock].id),
				(LOGGERclients[sock].std==1)?"STDOUT":"STDERR",
				buf);
			n+=26;
		    }else
			sprintf(buf2,buf);		       
		    write(LOGGERclients[sock].std,buf2,n);
		}
	    }
	if(LOGGERnoclients==0)
	    timeoutval++;
    }
    if(getenv("PSI_NOMSGLOGGERDONE")==NULL){
	fprintf(stderr,"PSPlogger: done\n");
    }
    exit(1);
}

/*********************************************************************
 * long
 * LOGGERspawnlogger()
 *
 * spawns a logger and creates a channel to it.
 * RETURN the portno of the logger
 */
long
LOGGERspawnlogger()
{
    int LOGGERlisten;
    int LOGGERparent;

    int LOGGERID;
    int verbose=0;

    if(getenv("PSI_LOGGERDEBUG")!=NULL){
	verbose=1;
    }

    if(LOGGERportno>0)
	return LOGGERportno;
    /* 
     * create to port for the logger to listen to. 
     */
    LOGGERportno = 20000;
    LOGGERlisten = LOGGERcreateport(&LOGGERportno);

    if(LOGGERlisten<0){
	LOGGERportno = -1;
	return -1;
    }
    /* 
     * fork to logger
     */
    if((LOGGERID = fork())==0){
	int i;
	char* argv[3];
	/*
	 *   L O G G E R 
	 */
	/*
	 * close all open filedesciptor except my std* and the LOGGERSOCK
	 */
	for(i=3;i<FD_SETSIZE;i++)
	    if(i!=LOGGERlisten)
		close(i);

	argv[0] = (char*)malloc(strlen(PSI_LookupInstalldir()) + 20);
	sprintf(argv[0],"%s/bin/psilogger", PSI_LookupInstalldir());
	argv[1] = (char*)malloc(10);
	sprintf(argv[1],"%d",LOGGERlisten);
	argv[2]=NULL;

	execv(argv[0],argv);

	/* usually never reached, but if execv fails try to do the logging 
	   inside this program
	*/

	/* 
	 * create a (parent<->logger) control channel 
	 * for interaction 
	 */
	LOGGERparent = LOGGERgetparentsock(LOGGERlisten);

	/*
	 * call the logger who does all the work
	 */
	LOGGERloop(LOGGERlisten,LOGGERparent);
    }
    /*
     * P A R E N T 
     */
    /*
     * close the socket which is used by logger to listen for new connections 
     */
    close(LOGGERlisten);
    /* 
     * create a (parent<->logger) control channel 
     * for interaction 
     */
    LOGGERsocket = LOGGERgetloggersock(LOGGERportno);

    if(LOGGERsocket<0){
	LOGGERportno = -1;
	return -1;
    }
    if(verbose)
	printf("PSPlogger: listening on port %ld\n", LOGGERportno);

    return LOGGERportno;
}
