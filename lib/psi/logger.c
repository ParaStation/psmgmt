/*
 *               ParaStation3
 * logger.c
 *
 * ParaStation I/O facility
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: logger.c,v 1.20 2002/07/26 15:34:41 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: logger.c,v 1.20 2002/07/26 15:34:41 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "pscommon.h"
#include "psi.h"

#include "logger.h"

static int LOGGERexecv( const char *path, char *const argv[])
{
    int ret;
    int cnt;

    /* Try 5 times with delay 400ms = 2 sec overall */
    for (cnt=0; cnt<5; cnt++){
	ret = execv(path,argv);
	usleep(1000 * 400);
    }
    return ret;
}

static int listenport = -1;

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
    char *errstr;
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

    errstr = strerror(errno);

    fprintf(stderr, "LOGGERexecLogger(execv): %s [%d] %s", argv[0],
	    errno, errstr ? errstr : "UNKNOWN");
    perror("execv()");

    exit(1);
}
