/*
 *      @(#)shm.c    1.00 (Karlsruhe) 10/4/95
 *
 *      written by Joachim Blum
 *
 *
 * This is the base module for the ParaStationProtocol.
 * It manages the SHareMemory.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "psi.h"

#include "psilog.h"

#ifndef DEBUGFILE
#define DEBUGFILE "/var/tmp/PSPdebug"
#endif

int SYSLOG_LEVEL=1;

unsigned long PSI_debugmask = 0;  /* debugmask for the local process. */

char PSI_txt[1024];            /* scratch for error log */

FILE* PSI_errorlog=0;

/***************************************************************************
 *      PSI_openlog()
 *
 *      Log a PSILib error message.  Prepends a string identifying the task.
 */
void 
PSI_openlog()
{
#if defined(DEBUG)
    char errorlogname[200];
    char hostname[50];
    int filenum;
    if (!PSI_errorlog){
	gethostname(hostname,sizeof(hostname));
	sprintf(errorlogname,"%s.%s.%d",
		DEBUGFILE,hostname,getpid());
	PSI_errorlog = fopen(errorlogname,"a");
	filenum=fileno(PSI_errorlog);
	if ((PSI_errorlog) && (fcntl(filenum, F_SETFD, FD_CLOEXEC) < 0)){
	    sprintf(PSI_txt, "PSI_openlog():"
		    " can't set FD_CLOEXEC on logfile <%s> errno:%d\n",
		    errorlogname, errno); 
	    PSI_logerror(PSI_txt);
	}
    }
#endif
}

/***************************************************************************
 *      PSI_closelog()
 *
 *      Log a PSILib error message.  Prepends a string identifying the task.
 */
void
PSI_closelog()
{
#if defined(DEBUG)
    if (PSI_errorlog){
	fclose(PSI_errorlog);
	PSI_errorlog = NULL;
    }
#endif
}

/***************************************************************************
 *      PSI_logerror()
 *
 *      Log a PSILib error message.  Prepends a string identifying the task.
 */
void
PSI_logerror(char *s)
{
    if (PSI_isoption(PSP_OSYSLOG)){
	SYSLOG(6,(LOG_ERR, "PSIlib [%lx]: %s", PSI_mytid, s));
    }else{
	if (PSI_isoption(PSP_OTIMESTAMP)){
	    struct tm*  now;                    /* current time */ 
	    struct timeval tv;
	    time_t t_now;

	    t_now = time(&t_now);
	    gettimeofday(&tv,0);
	    now = localtime(&t_now);

	    if (PSI_errorlog){
		fprintf(PSI_errorlog,"PSPlib [%lx][%2d:%02d:%02d:%03d]: %s", 
			PSI_mytid, now->tm_hour, now->tm_min, now->tm_sec,
			(int)tv.tv_usec/1000,s);
		fflush(PSI_errorlog);
	    }else{
		fprintf(stderr,"PSPlib [%lx][%2d:%02d:%02d:%03d]: %s", 
			PSI_mytid, now->tm_hour, now->tm_min, now->tm_sec,
			(int)tv.tv_usec/1000,s);
	    }
	}else{
	    if (PSI_errorlog){
		fprintf(PSI_errorlog, "PSPlib [%lx]: %s", PSI_mytid,s);
		fflush(PSI_errorlog);
	    }else{
		fprintf(stderr, "PSIlib [%lx]: %s", PSI_mytid,s);
	    }
	}
    }
}
