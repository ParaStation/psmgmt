/*
 *               ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "psidutil.h"

#include "psidmsgbuf.h"

msgbuf_t *getMsgbuf(size_t len)
{
    msgbuf_t *mp = malloc(sizeof(*mp) + len);

    if (!mp) {
	PSID_warn(-1, errno, "%s: malloc()", __func__);
	return NULL;
    }

    INIT_LIST_HEAD(&mp->next);
    mp->offset = 0;

    return mp;
}

void putMsgbuf(msgbuf_t *mp)
{
    free(mp);
}
