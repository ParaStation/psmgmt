/*
 *               ParaStation
 *
 * Copyright (C) 1999-2002 ParTec AG, Karlsruhe
 * Copyright (C) 2005 Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>

#include "psilog.h"

logger_t* PSI_logger;

void PSI_initLog(int usesyslog, FILE* logfile)
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

    PSI_logger = logger_init("PSI", usesyslog);
}

int32_t PSI_getDebugMask(void)
{
    return logger_getMask(PSI_logger);
}

void PSI_setDebugMask(int32_t mask)
{
    logger_setMask(PSI_logger, mask);
}
