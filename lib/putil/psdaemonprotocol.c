/*
 *               ParaStation3
 * psdaemonprotocol.c
 *
 * ParaStation daemon-daemon high-level protocol.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psdaemonprotocol.c,v 1.2 2003/07/04 07:27:17 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psdaemonprotocol.c,v 1.2 2003/07/04 07:27:17 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>

#include "psdaemonprotocol.h"

/*
 * string identification of message IDs.
 * Nicer output for errrors and debugging.
 */
static struct {
    long id;
    char *message;
} ctrlmessages[] = {
    { PSP_DD_DAEMONCONNECT    , "PSP_DD_DAEMONCONNECT"    }, 
    { PSP_DD_DAEMONESTABLISHED, "PSP_DD_DAEMONESTABLISHED"}, 
    { PSP_DD_DAEMONREFUSED    , "PSP_DD_DAEMONREFUSED"    }, 

    { PSP_DD_SENDSTOP         , "PSP_DD_SENDSTOP"         }, 
    { PSP_DD_SENDCONT         , "PSP_DD_SENDCONT"         }, 

    { PSP_DD_CHILDDEAD        , "PSP_DD_CHILDDEAD"        },

    {0,NULL}
};

char *PSDaemonP_printMsg(int msgtype)
{
    static char txt[30];
    int i = 0;

    if (msgtype < 0x0100) {
	return PSP_printMsg(msgtype);
    }

    while (ctrlmessages[i].id && ctrlmessages[i].id != msgtype) {
	i++;
    }

    if (ctrlmessages[i].id) {
	return ctrlmessages[i].message;
    } else {
	snprintf(txt, sizeof(txt), "msgtype 0x%x UNKNOWN", msgtype);
	return txt;
    }
}
