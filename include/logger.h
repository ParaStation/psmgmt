/*
 *               ParaStation3
 * logger.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: logger.h,v 1.5 2002/02/08 10:22:44 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for interaction with the ParaStation Logger.
 *
 * $Id: logger.h,v 1.5 2002/02/08 10:22:44 eicker Exp $
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

extern pid_t logger_pid;

extern int stdout_fileno_backup;
extern int stderr_fileno_backup;

/*********************************************************************
 * void LOGGERspawnforwarder(unsigned int logger_node, int logger_port)
 *
 * spawns a forwarder connected with 2 pipes and redirects stdout and
 * stderr to this pipes. stdout and stderr are backed up for later reuse
 *
 * Spawns a forwarder and redirect stdout/stderr to forwarder.
 * stdout and stderr are backed up for later reuse.
 * The forwarder will create a channel to the logger listening
 * at logger_node on logger_port.
 *
 * RETURN nothing
 */
void LOGGERspawnforwarder(unsigned int logger_node, int logger_port);

/*********************************************************************
 * int LOGGERspawnlogger()
 *
 * spawns a logger.
 *
 * RETURN the portno of the logger
 */
int LOGGERspawnlogger(void);

unsigned short LOGGERopenPort(void);

void LOGGERexecLogger(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __LOGGER_H */
