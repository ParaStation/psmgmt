/*
 *               ParaStation
 * psidforwarder.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidforwarder.h,v 1.4 2004/01/15 16:22:35 eicker Exp $
 *
 */
/**
 * \file
 * Handling of all input/output forwarding between logger and client.
 *
 * $Id: psidforwarder.h,v 1.4 2004/01/15 16:22:35 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDFORWARDER_H
#define __PSIDFORWARDER_H

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief The forwarder process.
 *
 * The actual forwarder process handling all input from stdin and
 * output to stdout and stderr operations of the controlled client
 * process. Therefore the forwarder process is connected to the local
 * daemon via which all communication operations of this kind are
 * delivered.
 *
 * Furthermore it's the forwarders tasks to control the client
 * process' live and to supply post mortem failure and usage
 * information to the parent process.
 *
 * @param task Task structure describing the client process to control.
 *
 * @param daemonfd File descriptor connecting the forwarder to the
 * local daemon.
 *
 * @param stdinfd File descriptor connecting the forwarder to the
 * stdin file descriptor of the controlled client process.
 *
 * @param stdoutfd File descriptor connecting the forwarder to the
 * stdout file descriptor of the controlled client process.
 *
 * @param stderrfd File descriptor connecting the forwarder to the
 * stderr file descriptor of the controlled client process.
 *
 * @return No return value.
 */
void PSID_forwarder(PStask_t *task,
		    int daemonfd, int stdinfd, int stdoutfd, int stderrfd);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDFORWARDER_H */
