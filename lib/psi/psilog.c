/*
 *               ParaStation3
 * psilog.c
 *
 * ParaStation logging facility.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psilog.c,v 1.5 2002/07/03 20:34:19 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psilog.c,v 1.5 2002/07/03 20:34:19 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "errlog.h"

#include "psilog.h"

/* Wrapper functions for logging */
void PSI_initLog(int usesyslog, FILE *logfile)
{
    if (!usesyslog && logfile) {
	int fno = fileno(logfile);

	if (fno!=STDERR_FILENO) {
	    dup2(fno, STDERR_FILENO);
	    if (fno!=STDOUT_FILENO) {
		fclose(logfile);
	    }
	}
    }

    initErrLog("PSI", usesyslog);
}

int PSI_getDebugLevel(void)
{
    return getErrLogLevel();
}

void PSI_setDebugLevel(int level)
{
    setErrLogLevel(level);
}

void PSI_errlog(char *s, int level)
{
    errlog(s, level);
}

void PSI_errexit(char *s, int errorno)
{
    errexit(s, errorno);
}
