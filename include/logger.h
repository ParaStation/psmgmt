/*
 *               ParaStation3
 * logger.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: logger.h,v 1.7 2002/02/11 12:43:13 eicker Exp $
 *
 */
/**
 * @file
 * User-functions for interaction with the ParaStation Logger.
 *
 * $Id: logger.h,v 1.7 2002/02/11 12:43:13 eicker Exp $
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

extern int stdin_fileno_backup;
extern int stdout_fileno_backup;
extern int stderr_fileno_backup;

/**
 * @brief Spans a forwarder.
 *
 * @todo
 * Spawns a forwarder and redirect stdin/stdout/stderr to forwarder.
 * stdin,stdout and stderr are backed up for later reuse.
 * The forwarder will create a channel to the logger listening
 * at logger_node on logger_port. rank is the rank of the spawned task.
 * if tty != 0 create an pty for stdin and stdout.
 *
 * spawns a forwarder connected with 2 pipes and redirects stdout and
 * stderr to this pipes. stdout and stderr are backed up for later reuse
 *
 * @param logger_node IP-address of the node where the logger listens.
 * @param logger_port Port the logger is listening on.
 * @param rank @todo
 * @param tty
 *
 * @return No return value.
 */
void LOGGERspawnforwarder(unsigned int logger_node, int logger_port,
			  int rank, int tty);

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
