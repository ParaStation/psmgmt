/*
 *               ParaStation3
 * psidforwarder.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidforwarder.h,v 1.1 2003/02/10 13:35:31 eicker Exp $
 *
 */
/**
 * \file
 * Handling of all input/output forwarding between logger and client.
 *
 * $Id: psidforwarder.h,v 1.1 2003/02/10 13:35:31 eicker Exp $
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


void PSID_forwarder(PStask_t *task,
		    int daemonfd, int stdinfd, int stdoutfd, int stderrfd);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDFORWARDER_H */
