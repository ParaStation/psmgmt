/*
 * ParaStation
 *
 * Copyright (C) 1999-2002 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pscommon.h"

#include "psilog.h"

logger_t* PSI_logger = NULL;

void PSI_initLog(FILE* logfile)
{
    if (! PSC_logInitialized()) PSC_initLog(logfile);

    PSI_logger = logger_init("PSI", logfile);
    if (!PSI_logger) {
	fprintf(stderr, "%s: failed to initialize logger\n", __func__);
	exit(1);
    }
}

int PSI_logInitialized(void)
{
    return !!PSI_logger;
}

int32_t PSI_getDebugMask(void)
{
    return logger_getMask(PSI_logger);
}

void PSI_setDebugMask(int32_t mask)
{
    logger_setMask(PSI_logger, mask);
}

void PSI_finalizeLog(void)
{
    if (PSC_logInitialized()) PSC_finalizeLog();
    if (PSI_logger) logger_finalize(PSI_logger);
    PSI_logger = NULL;
}
