#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "logger.h"

/******************************************
 *  sighandler(signal)
 */
void sighandler(int sig)
{
    int i;
    switch(sig){
    case  SIGHUP    :    /* hangup, generated when terminal disconnects */
	printf("PSILogger: No of clients:%d open sockets:",LOGGERnoclients);
	for(i=0;i<FD_SETSIZE;i++)
	    if(FD_ISSET(i,&LOGGERmyfds))
		printf(" %d",i);
	printf("\n");
    }
    fflush(stdout);
}

int main( int argc, char**argv)
{
    int LOGGERlisten;
    int LOGGERparent;

    if(argc<2){
	printf("PSIlogger: Sorry, program must be called correctly"
	       " inside an application.\n");
	exit(1);
    }
    LOGGERlisten = atol(argv[1]);

    signal(SIGHUP,sighandler);	
    /* 
     * create a (parent<->logger) control channel 
     * for interaction 
     */
    LOGGERparent = LOGGERgetparentsock(LOGGERlisten);

    if(getenv("PSI_NOSOURCEPRINTF")==NULL){
	PrependSource = 0;
    }
    /*
     * call the logger who does all the work
     */
    LOGGERloop(LOGGERlisten,LOGGERparent);

    return 0;
}

