/*
 *               ParaStation3
 * psidutil.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.h,v 1.7 2002/06/13 15:09:54 eicker Exp $
 *
 */
/**
 * \file
 * psidutil: Utilities for ParaStation daemon
 *
 * $Id: psidutil.h,v 1.7 2002/06/13 15:09:54 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDUTIL_H
#define __PSIDUTIL_H

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

#include "psitask.h"
#include "config_parsing.h"

extern int PSID_CardPresent ;    /* indicates if the card is present */


void PSID_ReConfig(int nodenr, int nrofnodes, char *license, char *module,
		   char *configfile);

void PSID_CardStop(void);

/* Performs reverse lookup (ip-addr given, determine id) */

int PSID_readconfigfile(void);

/***************************************************************************
 *       PSI_startlicenseserver()
 *
 *       starts the licenser daemon via the inetd
 */
int PSID_startlicenseserver(unsigned int hostaddr);

int PSID_execv( const char *path, char *const argv[]);

int PSID_taskspawn(PStask_t *task);     /* spawns a process with the
					   definitions in task on the
					   local node */
#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __PSIDUTIL_H */
