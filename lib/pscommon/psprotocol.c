/*
 *               ParaStation3
 * psprotocol.c
 *
 * ParaStation client-daemon and daemon-daemon high-level protocol.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psprotocol.c,v 1.8 2003/07/04 07:34:19 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psprotocol.c,v 1.8 2003/07/04 07:34:19 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>

#include "psprotocol.h"

/*
 * string identification of message IDs.
 * Nicer output for errrors and debugging.
 */
static struct {
    long id;
    char *message;
} ctrlmessages[] = {
    { PSP_CD_CLIENTCONNECT    , "PSP_CD_CLIENTCONNECT"    },
    { PSP_CD_CLIENTESTABLISHED, "PSP_CD_CLIENTESTABLISHED"},
    { PSP_CD_CLIENTREFUSED    , "PSP_CD_CLIENTREFUSED"    },

    { PSP_CD_SETOPTION        , "PSP_CD_SETOPTION"        },
    { PSP_CD_GETOPTION        , "PSP_CD_GETOPTION"        },

    { PSP_CD_INFOREQUEST      , "PSP_CD_INFOREQUEST"      },
    { PSP_CD_INFORESPONSE     , "PSP_CD_INFORESPONSE"     },

    { PSP_CD_SPAWNREQUEST     , "PSP_CD_SPAWNREQUEST"     },
    { PSP_CD_SPAWNSUCCESS     , "PSP_CD_SPAWNSUCCESS"     },
    { PSP_CD_SPAWNFAILED      , "PSP_CD_SPAWNFAILED"      },
    { PSP_CD_SPAWNFINISH      , "PSP_CD_SPAWNFINISH"      },

    { PSP_CD_NOTIFYDEAD       , "PSP_CD_NOTIFYDEAD"       },
    { PSP_CD_NOTIFYDEADRES    , "PSP_CD_NOTIFYDEADRES"    },
    { PSP_CD_RELEASE          , "PSP_CD_RELEASE"          },
    { PSP_CD_RELEASERES       , "PSP_CD_RELEASERES"       },
    { PSP_CD_SIGNAL           , "PSP_CD_SIGNAL"           },
    { PSP_CD_WHODIED          , "PSP_CD_WHODIED"          },

    { PSP_CD_DAEMONSTART      , "PSP_CD_DAEMONSTART"      },
    { PSP_CD_DAEMONSTOP       , "PSP_CD_DAEMONSTOP"       },
    { PSP_CD_DAEMONRESET      , "PSP_CD_DAEMONRESET"      },

    { PSP_CC_MSG              , "PSP_CC_MSG"              },
    { PSP_CC_ERROR            , "PSP_CC_ERROR"            },

    { PSP_CD_ERROR            , "PSP_CD_ERROR"            },

    {0,NULL}
};

char *PSP_printMsg(int msgtype)
{
    static char txt[30];
    int i = 0;

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
