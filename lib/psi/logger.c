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

pid_t logger_pid=0;

int stdout_fileno_backup=-1;
int stderr_fileno_backup=-1;

static
int LOGGERexecv( const char *path, char *const argv[])
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


/*********************************************************************
 * void LOGGERspawnforwarder()
 *
 * spawns a forwarder connected with 2 pipes and redirects stdout and
 * stderr to this pipes. stdout and stderr are backed up for later reuse
 *
 * RETURN nothing
 */
void LOGGERspawnforwarder(unsigned int logger_node, int logger_port)
{
    int pid;
    int stdoutfds[2], stderrfds[2];
    char *errtxt;

    /* 
     * create two pipes for the forwarder to listen to. 
     */
    if(pipe(stdoutfds)){
        errtxt = strerror(errno);
        syslog(LOG_ERR, "LOGGERspawnforwarder(pipe(stdout)): [%d] %s", errno,
               errtxt?errtxt:"UNKNOWN");
        perror("pipe(stdout)");
	exit(1);
    }
    if(pipe(stderrfds)){;
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
	 *   L O G G E R 
	 */
	int i;
	char* argv[7];
	/*
	 * close all open filedesciptor except my pipes for reading
	 */
	for(i=1; i<FD_SETSIZE; i++)
	    if(i != stdoutfds[0] && i !=stderrfds[0])
		close(i);

	argv[0] = (char*)malloc(strlen(PSI_LookupInstalldir()) + 20);
	sprintf(argv[0],"%s/bin/psiforwarder", PSI_LookupInstalldir());
	argv[1] = (char*)malloc(10);
	sprintf(argv[1],"%u", logger_node);
	argv[2] = (char*)malloc(10);
	sprintf(argv[2],"%d", logger_port);
	argv[3] = (char*)malloc(10);
	sprintf(argv[3],"%d", PSI_myid);
	argv[4] = (char*)malloc(10);
	sprintf(argv[4],"%d", stdoutfds[0]);
	argv[5] = (char*)malloc(10);
	sprintf(argv[5],"%d", stderrfds[0]);
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
     * backup stdout and stderr for later reuse
     */
    stdout_fileno_backup=dup(STDOUT_FILENO);
    stderr_fileno_backup=dup(STDERR_FILENO);

    /*
     * redirect output
     */
    dup2(stdoutfds[1], STDOUT_FILENO);
    close(stdoutfds[1]);
    dup2(stderrfds[1], STDERR_FILENO);
    close(stderrfds[1]);
}

/*********************************************************************
 * int LOGGERspawnlogger()
 *
 * spawns a logger.
 * RETURN the portno of the logger
 */
int LOGGERspawnlogger(void)
{
    int portno, listenport;
    struct sockaddr_in sa;	/* socket address */ 
    int err;                    /* error code while binding */

    /*
     * create a port for the logger to listen to.
     */
    portno = 20000;

    if((listenport = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP))<0){
	perror("LOGGERspawnlogger: can't create socket:");
	exit(1);
    }
    memset(&sa, 0, sizeof(sa)); 
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;

    sa.sin_port = htons(portno); 

    while(((err = bind(listenport, (struct sockaddr *)&sa, sizeof(sa)))<0)
	  &&(errno == EADDRINUSE)){
	sa.sin_port = htons(ntohs(sa.sin_port)-1); 
    }
    if(err <0){
	perror("LOGGERspawnlogger: can't bind socket:");
	exit(1);
    }

    if(listen(listenport, 256)<0){
	perror("LOGGERspawnlogger: can't listen to socket:");
	exit(1);
    }
    portno = ntohs(sa.sin_port);

    /* 
     * fork to logger
     */
    if((logger_pid=fork())==0){
	/*
	 *   L O G G E R 
	 */
	int i;
	char* argv[3];
	char *errtxt;
	/*
	 * close all open filedesciptor except my std* and the LOGGERSOCK
	 */
	for(i=1; i<FD_SETSIZE; i++)
	    if(i != listenport && i != STDOUT_FILENO && i != STDERR_FILENO)
		close(i);

	argv[0] = (char*)malloc(strlen(PSI_LookupInstalldir()) + 20);
	sprintf(argv[0],"%s/bin/psilogger", PSI_LookupInstalldir());
	argv[1] = (char*)malloc(10);
	sprintf(argv[1],"%d", listenport);
	argv[2] = NULL;

	LOGGERexecv(argv[0], argv);

	/* usually never reached, but if execv fails try to do the logging 
	   inside this program
	*/
	close(listenport);

	errtxt = strerror(errno);
	syslog(LOG_ERR, "LOGGERspawnlogger(execv): %s [%d] %s", argv[0],
	       errno, errtxt?errtxt:"UNKNOWN");
	perror("execv()");
	exit(1);
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
