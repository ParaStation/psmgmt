/*
 *               ParaStation3
 * logger.c
 *
 * ParaStation I/O facility
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: logger.c,v 1.19 2002/07/03 20:33:45 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: logger.c,v 1.19 2002/07/03 20:33:45 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

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
#include <pty.h>
#include <syslog.h>

#include "pscommon.h"
#include "psi.h"
//#include "psitask.h"
//#include "psilog.h"

#include "logger.h"

pid_t logger_pid=0;


static int LOGGERexecv( const char *path, char *const argv[])
{
    int ret;
    int cnt;

    /* Try 5 times with delay 400ms = 2 sec overall */
    for (cnt=0;cnt<5;cnt++){
	ret = execv(path,argv);
	usleep(1000 * 400);
    }
    return ret;
}

void LOGGERspawnforwarder(unsigned int logger_node, int logger_port,
			  int rank, int tty)
{
    int pid;
    int stdoutfds[2], stderrfds[2];
    char *errtxt;

    /* 
     * create two pipes for the forwarder to listen to. 
     */
    if (tty){
#if 0
	/* ToDo: Receive the terminal size of the orginal tty for winsize
	 * and remove this codepart from the daemon. The forwarder should open
	 * the pty. The user process should be a child of the forwarder
	 * (like the sshd). I dont want openpty inside the psi-library
	 * because the need of -lutil  *jh*
	 */
	if (openpty(&stdoutfds[0],&stdoutfds[1],NULL,NULL,NULL)) {
	    errtxt = strerror(errno);
	    syslog(LOG_ERR, "LOGGERspawnforwarder(openpty()): [%d] %s", errno,
		   errtxt?errtxt:"UNKNOWN");
	    perror("openpty()");
	    /* fallback to socketpair */
	    tty = 0;
	}
#else
	tty=0;
#endif
    }
    if (!tty){
	if (socketpair(PF_UNIX,SOCK_STREAM,0,stdoutfds)) {
	    errtxt = strerror(errno);
	    syslog(LOG_ERR, "LOGGERspawnforwarder(pipe(stdout)): [%d] %s",
		   errno, errtxt?errtxt:"UNKNOWN");
	    perror("pipe(stdout)");
	    exit(1);
	}
    }
    if (socketpair(PF_UNIX,SOCK_STREAM,0,stderrfds)) {
        errtxt = strerror(errno);
        syslog(LOG_ERR, "LOGGERspawnforwarder(pipe(stderr)): [%d] %s", errno,
               errtxt?errtxt:"UNKNOWN");
        perror("pipe(stderr)");
	exit(1);
    }

    /* 
     * fork to forwarder
     */
    if((pid = fork())==0){
	/*
	 *   F O R W A R D E R 
	 */
	int i;
	char* argv[7];
	/*
	 * close all open filedesciptor except my pipes for reading
	 */
	for(i=1; i<FD_SETSIZE; i++)
	    if(i != stdoutfds[0] && i !=stderrfds[0])
		close(i);

	argv[0] = (char*)malloc(strlen(PSC_lookupInstalldir()) + 20);
	sprintf(argv[0], "%s/bin/psiforwarder", PSC_lookupInstalldir());
	argv[1] = (char*)malloc(10);
	sprintf(argv[1], "%u", logger_node);
	argv[2] = (char*)malloc(10);
	sprintf(argv[2], "%d", logger_port);
	argv[3] = (char*)malloc(10);
	sprintf(argv[3], "%d", rank);
	argv[4] = (char*)malloc(10);
	sprintf(argv[4], "%d", stdoutfds[0]);
	argv[5] = (char*)malloc(10);
	sprintf(argv[5], "%d", stderrfds[0]);
	argv[6] = NULL;

	LOGGERexecv(argv[0], argv);

	/*
	 * usually never reached, but if execv fails stop the whole porgram
	 */
	close(stdoutfds[0]);
	close(stderrfds[0]);

        errtxt = strerror(errno);
        syslog(LOG_ERR, "LOGGERspawnforwarder(execv): %s [%d] %s", argv[0],
	       errno, errtxt?errtxt:"UNKNOWN");
        perror("fork()");
	exit(1);
    }
    /*
     * P A R E N T 
     */

    /*
     * close the reading pipes.
     */
    close(stdoutfds[0]);
    close(stderrfds[0]);

    /*
     * check if fork() was successful
     */
    if (pid ==-1){
        char *errtxt;

	close(stdoutfds[1]);
	close(stderrfds[1]);

	errtxt = strerror(errno);
        syslog(LOG_ERR, "LOGGERspawnforwarder(fork): [%d] %s", errno,
               errtxt?errtxt:"UNKNOWN");
        perror("execv()");
	exit(1);
    }

    /*
     * redirect input and output
     */
    dup2(stdoutfds[1], STDIN_FILENO);
    dup2(stdoutfds[1], STDOUT_FILENO);
    close(stdoutfds[1]);
    dup2(stderrfds[1], STDERR_FILENO);
    close(stderrfds[1]);
}

static int listenport = -1;

int LOGGERspawnlogger(void)
{
    unsigned short portno;

    /*
     * create a port for the logger to listen to.
     */
    portno = LOGGERopenPort();

    /* 
     * fork to logger
     */
    if((logger_pid=fork())==0){
	/*
	 *   L O G G E R 
	 */
	LOGGERexecLogger();
    }
    /*
     * P A R E N T 
     */
    /*
     * close the socket which is used by logger to listen for new connections.
     */
    close(listenport);

    return portno;
}

unsigned short LOGGERopenPort(void)
{
    unsigned short defaultPortNo = 20000;
    struct sockaddr_in sa;	/* socket address */ 
    int err;                    /* error code while binding */

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

    return ntohs(sa.sin_port);
}

void LOGGERexecLogger(void)
{
    int i;
    char* argv[4];
    char *errtxt;
    /*
     * close all open filedesciptor except my std* and the LOGGERSOCK
     */
    for (i=1; i<FD_SETSIZE; i++) {
	if(i != listenport && i != PSI_msock
	   && i != STDOUT_FILENO && i != STDERR_FILENO)
	if (i != listenport) {
	    close(i);
	}
    }

    argv[0] = (char*)malloc(strlen(PSC_lookupInstalldir()) + 20);
    sprintf(argv[0],"%s/bin/psilogger", PSC_lookupInstalldir());
    argv[1] = (char*)malloc(10);
    sprintf(argv[1],"%d", listenport);
    argv[2] = (char*)malloc(10);
    sprintf(argv[2],"%d", PSI_msock);
    argv[3] = NULL;

    LOGGERexecv(argv[0], argv);

    /*
     * Usually never reached, but if execv() fails just exit. This should
     * also shutdown all spawned processes.
     */
    close(PSI_msock);
    close(listenport);

    errtxt = strerror(errno);
    syslog(LOG_ERR, "LOGGERexecLogger(execv): %s [%d] %s", argv[0],
	   errno, errtxt?errtxt:"UNKNOWN");
    perror("execv()");

    exit(1);
}
