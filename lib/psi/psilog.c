/*
 *               ParaStation
 *
 * Copyright (C) 1999-2002 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2008 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <unistd.h>

#include "psilog.h"

logger_t* PSI_logger;

void PSI_initLog(FILE* logfile)
{
    PSI_logger = logger_init("PSI", logfile);
}

int32_t PSI_getDebugMask(void)
{
    return logger_getMask(PSI_logger);
}

void PSI_setDebugMask(int32_t mask)
{
    logger_setMask(PSI_logger, mask);
}
