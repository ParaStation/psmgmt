/*
 *               ParaStation3
 * pshwtypes.c
 *
 * ParaStation hardware types.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: pshwtypes.c,v 1.1 2002/07/18 12:27:32 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: pshwtypes.c,v 1.1 2002/07/18 12:27:32 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <string.h>

#include "pshwtypes.h"

char *PSHW_printType(int hwType)
{
    static char txt[80];

    txt[0] = '\0';

    if (hwType & PSHW_ETHERNET) {
	snprintf(txt, sizeof(txt), "ethernet ");
    }
    if (hwType & PSHW_MYRINET) {
	snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "myrinet ");
    }
    if (hwType & PSHW_GIGAETHERNET) {
	snprintf(txt+strlen(txt), sizeof(txt)-strlen(txt), "gigaethernet ");
    }
    if (!hwType) snprintf(txt, sizeof(txt), "none ");

    txt[strlen(txt)-1] = '\0';

    return txt;
}
