/*
 * ParaStation
 *
 * Copyright (C) 2008-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include "psidutil.h"
#include "psidhw.h"


int PSID_AuthenticAMD(void)
{
#if defined __i386__ || defined __x86_64__
    unsigned int Regebx = 0, Regedx = 0, Regecx = 0;
    char* AMDID = "AuthenticAMD";

    asm (
	"xorl %%eax, %%eax\n\t"
	"cpuid\n\t"
	:       "=b" (Regebx),
		"=d" (Regedx),
		"=c" (Regecx)
	:
	: "%eax"
	);

    return (Regebx == *(unsigned int *)&AMDID[0]
	    && Regedx == *(unsigned int *)&AMDID[4]
	    && Regecx == *(unsigned int *)&AMDID[8]);
#else
    return 0;
#endif
}

long PSID_getPhysCPUs_AMD(void)
{
    /* No SMT on AMD yet */
    long virtCPUs = PSID_getVirtCPUs();

    return virtCPUs;
}
