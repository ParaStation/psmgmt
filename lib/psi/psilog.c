/*
 *               ParaStation3
 * psilog.c
 *
 * ParaStation logging facility.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psilog.c,v 1.4 2002/03/26 13:51:40 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psilog.c,v 1.4 2002/03/26 13:51:40 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "psi.h"

#include "psilog.h"

int SYSLOG_LEVEL=1;

unsigned long PSI_debugmask = 0;  /* debugmask for the local process. */

char PSI_txt[1024];            /* scratch for error log */

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
	    struct tm*  now;       /* current time */ 
	    struct timeval tv;
	    time_t t_now;

	    t_now = time(&t_now);
	    gettimeofday(&tv,0);
	    now = localtime(&t_now);

	    fprintf(stderr,"PSPlib [%lx][%2d:%02d:%02d:%03d]: %s", 
		    PSI_mytid, now->tm_hour, now->tm_min, now->tm_sec,
		    (int)tv.tv_usec/1000,s);
	}else{
	    fprintf(stderr, "PSIlib [%lx]: %s", PSI_mytid, s);
	}
    }
}
