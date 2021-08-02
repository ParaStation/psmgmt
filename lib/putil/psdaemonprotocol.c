/*
 * ParaStation
 *
 * Copyright (C) 2003-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdio.h>

#include "pspluginprotocol.h"

#include "psdaemonprotocol.h"

/*
 * string identification of message IDs.
 * Nicer output for errors and debugging.
 */
typedef struct {
    int id;
    char *name;
} msgString_t;

static msgString_t daemonMessages[] = {
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
    { PSP_DD_GETNODES         , "PSP_DD_GETNODES"         },
    { PSP_DD_GETTASKS         , "PSP_DD_GETTASKS"         },
    { PSP_DD_PROVIDETASK      , "PSP_DD_PROVIDETASK"      },
    { PSP_DD_PROVIDETASKSL    , "PSP_DD_PROVIDETASKSL"    },
    { PSP_DD_CANCELPART       , "PSP_DD_CANCELPART"       },
    { PSP_DD_TASKDEAD         , "PSP_DD_TASKDEAD"         },
    { PSP_DD_TASKSUSPEND      , "PSP_DD_TASKSUSPEND"      },
    { PSP_DD_TASKRESUME       , "PSP_DD_TASKRESUME"       },
    { PSP_DD_GETRANKNODE      , "PSP_DD_GETRANKNODE"      },
    { PSP_DD_PROVIDETASKRP    , "PSP_DD_PROVIDETASKRP"    },
    { PSP_DD_PROVIDEPARTRP    , "PSP_DD_PROVIDEPARTRP"    },

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
    { PSP_DD_RESCREATED       , "PSP_DD_RESCREATED"       },
    { PSP_DD_RESRELEASED      , "PSP_DD_RESRELEASED"      },

    {0,NULL}
};

static msgString_t pluginMessages[] = {
    { PSP_PLUG_PSMOM          , "PSP_PLUG_PSMOM"          },
    { PSP_PLUG_ACCOUNT        , "PSP_PLUG_ACCOUNT"        },
    { PSP_PLUG_PELOGUE        , "PSP_PLUG_PELOGUE"        },
    { PSP_PLUG_PSSLURM        , "PSP_PLUG_PSSLURM"        },
    { PSP_PLUG_PSEXEC         , "PSP_PLUG_PSEXEC"         },
    { PSP_PLUG_PSPMIX         , "PSP_PLUG_PSPMIX"         },
    { PSP_PLUG_PSGW           , "PSP_PLUG_PSGW"           },
    { PSP_PLUG_NODEINFO       , "PSP_PLUG_NODEINFO"       },
    { 0, NULL }
};

static char *printPluginMsg(int msgtype)
{
    for (int m = 0; pluginMessages[m].name; m++) {
	if (pluginMessages[m].id == msgtype) return pluginMessages[m].name;
    }

    static char txt[30];
    snprintf(txt, sizeof(txt), "msgtype %#x UNKNOWN", msgtype);
    return txt;
}

char *PSDaemonP_printMsg(int msgtype)
{
    if (msgtype < 0x0100) {
	return PSP_printMsg(msgtype);
    } else if (msgtype >= 0x0200) {
	return printPluginMsg(msgtype);
    }

    for (int m = 0; daemonMessages[m].name; m++) {
	if (daemonMessages[m].id == msgtype) return daemonMessages[m].name;
    }

    static char txt[30];
    snprintf(txt, sizeof(txt), "msgtype %#x UNKNOWN", msgtype);
    return txt;
}
