#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

/*************************************************************
 * int
 * LOGGERstdconnect(u_int node, int port, int fd, PStask_t* task)
 *
 * creates a socket and connects it to the port. It signals the logger that
 * it will be used as std(STDOUT/STDERR).
 *
 * RETURN the newly created socket.
 */
int LOGGERstdconnect(unsigned int node, int port, int std, PStask_t* task)
{
    int sock;                   /* master socket to log to */
    struct sockaddr_in sa;	/* socket address */ 
    struct LOGGERclient_t mystruct;

    int delay;

    if((sock = socket(PF_INET,SOCK_STREAM,0))<0){
#ifdef DEBUG
	sprintf(PSI_txt,"LOGGERstdconnect: socket() error %d\n",errno);
	PSI_logerror(PSI_txt);
#endif
	return(-1);
    }

    delay = 1;
    setsockopt(sock, SOL_TCP, TCP_NODELAY, (void *) &delay, sizeof(delay));

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
    mystruct.std = std;

    if(task->options & TaskOption_SENDSTDHEADER){
	write(sock,&mystruct,sizeof(struct LOGGERclient_t));
    }

    dup2(sock, std);
    close(sock);

    return sock;
}

/*************************************************************
 * int LOGGERstdDevNull(int std)
 * 
 * redirect stdout/stderr to /dev/null
 *
 * RETURN 0 success
 *        -1 error errno is set.
 */
int LOGGERstdDevNull(int std)
{
    int fd;

    if((fd = open("/dev/null",O_WRONLY, S_IRUSR|S_IWUSR))<0){
#ifdef DEBUG
	sprintf(PSI_txt,"LOGGERstdDevNull: Even </dev/null> is not "
		"usable for %s!! PANIC exit (errno %d)!!",
		(std==STDOUT_FILENO)?"STDOUT":"STDERR",errno);
	PSI_logerror(PSI_txt);
#endif
	errno = EIO;
	return -1;
    }
#ifdef DEBUG
    sprintf(PSI_txt,"LOGGERredirect_std: Now using /dev/null for %s\n",
	    (std==STDOUT_FILENO)?"STDOUT":"STDERR");
    PSI_logerror(PSI_txt);
#endif
    dup2(fd, std);
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
int LOGGERredirect_std(unsigned int node, int port, PStask_t* task)
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
	if((sock = LOGGERstdconnect(node, port, STDOUT_FILENO, task))<0)
	    if(LOGGERstdDevNull(STDOUT_FILENO)<0)
		return -1;

	/* 
	 * - create a socket for stderr 
	 * - close the original stderr
	 * - redirect the original stderr to the new socket
	 */
	if(task->options & TaskOption_ONESTDOUTERR){
	    dup2(STDOUT_FILENO, STDERR_FILENO);
	}else if((sock = LOGGERstdconnect(node, port, STDERR_FILENO, task))<0)
	    if(LOGGERstdDevNull(STDERR_FILENO)<0)
		return -1;
    }else{
	int fd;           /* filedesc. of the stdout/stderr while creating */
	char stdname[40]; /* name for stdout/stderr files of spawned process*/

	/* open stdout && stderr */
	sprintf(stdname,"/tmp/%s.%d.stdout",task->argv[0],getpid());
	if((fd = open(stdname,O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR))<0){
	    char* errtxt;
	    errtxt=strerror(errno);
	    sprintf(PSI_txt,"LOGGERredirect_std: can not open new stdout "
		    " errno(%d) %s <%s>>", errno,
		    errtxt?errtxt:"UNKNOWN", stdname);
	    PSI_logerror(PSI_txt);
	    if(LOGGERstdDevNull(STDOUT_FILENO)<0)
		return -1;
	}else{
	    dup2(fd, STDOUT_FILENO);
	    close(fd);
	}
	sprintf(stdname,"/tmp/%s.%d.stderr",task->argv[0],getpid());
	if((fd = open(stdname, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR))<0){
	    char* errtxt;
	    errtxt=strerror(errno);
	    sprintf(PSI_txt,"LOGGERredirect_std: can not open new stderr "
		    " errno(%d) %s <%s>",
		    errno,errtxt?errtxt:"UNKNOWN",stdname);
	    PSI_logerror(PSI_txt);
	    if(LOGGERstdDevNull(STDERR_FILENO)<0)
		return -1;
	}else{
	    dup2(fd, STDERR_FILENO);
	    close(fd);
	}
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
int LOGGERcreateport(int *portno)
{
    struct sockaddr_in sa;	/* socket address */ 
    int sock;
    int err;                    /* error code while binding */

#ifdef DEBUG
    if(0){
	sprintf(PSI_txt,"LOGGERcreateport(%d)\n",*portno);
	PSI_logerror(PSI_txt);
    }
#endif

    if((sock = socket(AF_INET,SOCK_STREAM,0))<0){
	perror("PSIlogger: can't create socket:");
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
	perror("PSIlogger: can't bind socket:");
	return(-1);
    }

    if((listen(sock,256))<0){
	perror("PSIlogger: can't listen to socket:");
	return(-1);
    }
    *portno = ntohs(sa.sin_port);

    return sock;
}

/*********************************************************************
 * int LOGGERspawnforwarder()
 *
 * spawns a forwarder.
 * RETURN the portno of the forwarder
 */
int LOGGERspawnforwarder(unsigned int logger_node, int logger_port)
{
    int portno, listen;

    /* 
     * create to port for the forwarder to listen to. 
     */
    portno = 20000;
    listen = LOGGERcreateport(&portno);

    if(listen < 0)
	return -1;
    /* 
     * fork to forwarder
     */
    if(fork()==0){
	/*
	 *   L O G G E R 
	 */
	int i;
	char* argv[5];
	/*
	 * close all open filedesciptor except my FORWARDERSOCK
	 */
	for(i=3; i<FD_SETSIZE; i++)
	    if(i != listen)
		close(i);

	argv[0] = (char*)malloc(strlen(PSI_LookupInstalldir()) + 20);
	sprintf(argv[0],"%s/bin/psiforwarder", PSI_LookupInstalldir());
	argv[1] = (char*)malloc(10);
	sprintf(argv[1],"%u", logger_node);
	argv[2] = (char*)malloc(10);
	sprintf(argv[2],"%d", logger_port);
	argv[3] = (char*)malloc(10);
	sprintf(argv[3],"%d", listen);
	argv[4] = NULL;

	execv(argv[0], argv);

	/* usually never reached, but if execv fails try to do the logging 
	   inside this program
	*/
	exit(1);
    }
    /*
     * P A R E N T 
     */
    /*
     * close the socket which is used by forwarder to listen for connections.
     */
    close(listen);

    return portno;
}

/*********************************************************************
 * int LOGGERspawnlogger()
 *
 * spawns a logger.
 * RETURN the portno of the logger
 */
int LOGGERspawnlogger(void)
{
    int portno, listen;

    /*
     * create to port for the logger to listen to.
     */
    portno = 20000;
    listen = LOGGERcreateport(&portno);

    if(listen < 0)
	return -1;
    /* 
     * fork to logger
     */
    if(fork()==0){
	/*
	 *   L O G G E R 
	 */
	int i;
	char* argv[3];
	/*
	 * close all open filedesciptor except my std* and the LOGGERSOCK
	 */
	for(i=3; i<FD_SETSIZE; i++)
	    if(i != listen)
		close(i);

	argv[0] = (char*)malloc(strlen(PSI_LookupInstalldir()) + 20);
	sprintf(argv[0],"%s/bin/psilogger", PSI_LookupInstalldir());
	argv[1] = (char*)malloc(10);
	sprintf(argv[1],"%d", listen);
	argv[2] = NULL;

	execv(argv[0], argv);

	/* usually never reached, but if execv fails try to do the logging 
	   inside this program
	*/
	exit(1);
    }
    /*
     * P A R E N T 
     */
    /*
     * close the socket which is used by logger to listen for new connections.
     */
    close(listen);

    return portno;
}
