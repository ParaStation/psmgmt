/*
 *               ParaStation3
 * psidutil.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidutil.h,v 1.11 2002/07/23 15:44:33 eicker Exp $
 *
 */
/**
 * \file
 * psidutil: Utilities for ParaStation daemon
 *
 * $Id: psidutil.h,v 1.11 2002/07/23 15:44:33 eicker Exp $
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

#include "pstask.h"

extern unsigned int PSID_HWstatus;   /* indicates which HW is present */
extern short PSID_numCPU;            /* actual number of CPUs */

void PSID_initLog(int usesyslog, FILE *logfile);

int PSID_getDebugLevel(void);

void PSID_setDebugLevel(int level);

void PSID_errlog(char *s, int level);

void PSID_errexit(char *s, int errorno);


void PSID_ReConfig(void);

void PSID_CardStop(void);

/* Performs reverse lookup (ip-addr given, determine id) */

void PSID_readconfigfile(int usesyslog);

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
