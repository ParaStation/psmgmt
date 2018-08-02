/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2018 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>

#include "psdaemonprotocol.h"

/*
 * string identification of message IDs.
 * Nicer output for errrors and debugging.
 */
static struct {
    int id;
    char *message;
} ctrlmessages[] = {
    { PSP_DD_DAEMONCONNECT    , "PSP_DD_DAEMONCONNECT"    },
    { PSP_DD_DAEMONESTABLISHED, "PSP_DD_DAEMONESTABLISHED"},
    { PSP_DD_DAEMONREFUSED    , "PSP_DD_DAEMONREFUSED"    },
    { PSP_DD_DAEMONSHUTDOWN   , "PSP_DD_DAEMONSHUTDOWN"   },

    { PSP_DD_SENDSTOP         , "PSP_DD_SENDSTOP"         },
    { PSP_DD_SENDCONT         , "PSP_DD_SENDCONT"         },
    { PSP_DD_SENDSTOPACK      , "PSP_DD_SENDSTOPACK"      },

    { PSP_DD_CHILDDEAD        , "PSP_DD_CHILDDEAD"        },
    { PSP_DD_CHILDBORN        , "PSP_DD_CHILDBORN"        },
    { PSP_DD_CHILDACK         , "PSP_DD_CHILDACK"         },
    { PSP_DD_CHILDRESREL      , "PSP_DD_CHILDRESREL"      },

    { PSP_DD_NEWCHILD         , "PSP_DD_NEWCHILD"         },
    { PSP_DD_NEWPARENT        , "PSP_DD_NEWPARENT"        },
    { PSP_DD_NEWANCESTOR      , "PSP_DD_NEWANCESTOR"      },
    { PSP_DD_ADOPTCHILDSET    , "PSP_DD_ADOPTCHILDSET"    },
    { PSP_DD_ADOPTFAILED      , "PSP_DD_ADOPTFAILED"      },
    { PSP_DD_INHERITDONE      , "PSP_DD_INHERITDONE"      },
    { PSP_DD_INHERITFAILED    , "PSP_DD_INHERITFAILED"    },

    { PSP_DD_GETPART          , "PSP_DD_GETPART"          },
    { PSP_DD_GETPARTNL        , "PSP_DD_GETPARTNL"        },
    { PSP_DD_PROVIDEPART      , "PSP_DD_PROVIDEPART"      },
    { PSP_DD_PROVIDEPARTSL    , "PSP_DD_PROVIDEPARTSL"    },
    { PSP_DD_PROVIDEPARTRP    , "PSP_DD_PROVIDEPARTRP"    },
    { PSP_DD_GETNODES         , "PSP_DD_GETNODES"         },
    { PSP_DD_GETTASKS         , "PSP_DD_GETTASKS"         },
    { PSP_DD_PROVIDETASK      , "PSP_DD_PROVIDETASK"      },
    { PSP_DD_PROVIDETASKSL    , "PSP_DD_PROVIDETASKSL"    },
    { PSP_DD_PROVIDETASKRP    , "PSP_DD_PROVIDETASKRP"    },
    { PSP_DD_CANCELPART       , "PSP_DD_CANCELPART"       },
    { PSP_DD_TASKDEAD         , "PSP_DD_TASKDEAD"         },
    { PSP_DD_TASKSUSPEND      , "PSP_DD_TASKSUSPEND"      },
    { PSP_DD_TASKRESUME       , "PSP_DD_TASKRESUME"       },
    { PSP_DD_GETRANKNODE      , "PSP_DD_GETRANKNODE"      },

    { PSP_DD_LOAD             , "PSP_DD_LOAD"             },
    { PSP_DD_ACTIVE_NODES     , "PSP_DD_ACTIVE_NODES"     },
    { PSP_DD_DEAD_NODE        , "PSP_DD_DEAD_NODES"       },
    { PSP_DD_MASTER_IS        , "PSP_DD_MASTER_IS"        },

    { PSP_DD_NODESRES         , "PSP_DD_GETNODESRES"      },
    { PSP_DD_REGISTERPART     , "PSP_DD_REGISTERPART"     },
    { PSP_DD_REGISTERPARTSL   , "PSP_DD_REGISTERPARTSL"   },
    { PSP_DD_REGISTERPARTRP   , "PSP_DD_REGISTERPARTRP"   },

    { PSP_DD_GETRESERVATION   , "PSP_DD_GETRESERVATION"   },
    { PSP_DD_GETSLOTS         , "PSP_DD_GETSLOTS"         },
    { PSP_DD_SLOTSRES         , "PSP_DD_SLOTSRES"         },

    { PSP_DD_SPAWNLOC         , "PSP_DD_SPAWNLOC"         },

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
