/*
 *               ParaStation
 * psidmaster.c
 *
 * Helper functions for master-node detection and actions.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psidmaster.c,v 1.1 2003/10/09 19:20:39 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psidmaster.c,v 1.1 2003/10/09 19:20:39 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include "mcast.h"

#include "pscommon.h"
#include "psprotocol.h"
#include "psdaemonprotocol.h"
#include "pspartition.h"
#include "pshwtypes.h"
#include "pstask.h"
#include "psnodes.h"
#include "hardware.h"

#include "psidutil.h"
#include "psidcomm.h"
#include "psidtask.h"

#include "psidpartition.h"

static char errtxt[256];

static unsigned short masterNode = 0;

unsigned short getMasterNode(void)
{
    return masterNode;
}

void setMasterNode(unsigned short id)
{
    masterNode = id;
}

int amMasterNode(void)
{
    return (PSC_getMyID() == masterNode);
}


