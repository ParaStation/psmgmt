/*
 *               ParaStation3
 * psprotocol.c
 *
 * ParaStation client-daemon and daemon-daemon high-level protocol.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psprotocol.c,v 1.3 2002/07/18 12:52:12 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psprotocol.c,v 1.3 2002/07/18 12:52:12 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "psprotocol.h"

/*
 * string identification of message IDs.
 * Nicer output for errrors and debugging.
 */
struct PSPctrlmessages_t{
    long id;
    char* message;
} PSPctrlmessages[] = {
    { PSP_CD_CLIENTCONNECT      ,"PSP_CD_CLIENTCONNECT"      },
    { PSP_CD_CLIENTESTABLISHED  ,"PSP_CD_CLIENTESTABLISHED"  },
    { PSP_CD_CLIENTREFUSED      ,"PSP_CD_CLIENTREFUSED"      },
    { PSP_CD_OLDVERSION         ,"PSP_CD_OLDVERSION"         },
    { PSP_CD_NOSPACE            ,"PSP_CD_NOSPACE"            }, 
    { PSP_CD_UIDLIMIT           ,"PSP_CD_UIDLIMIT"           },
    { PSP_CD_PROCLIMIT          ,"PSP_CD_PROCLIMIT"          }, 
    { PSP_DD_SETOPTION          ,"PSP_DD_SETOPTION"          }, 
    { PSP_DD_GETOPTION          ,"PSP_DD_GETOPTION"          }, 
    { PSP_DD_CONTACTNODE        ,"PSP_DD_CONTACTNODE"        },
   
    { PSP_CD_TASKINFOREQUEST    ,"PSP_CD_TASKINFOREQUEST"    },
    { PSP_CD_TASKINFO           ,"PSP_CD_TASKINFO"           }, 
    { PSP_CD_TASKINFOEND        ,"PSP_CD_TASKINFOEND"        }, 

    { PSP_CD_HOSTREQUEST        ,"PSP_CD_HOSTREQUEST"        },
    { PSP_CD_HOSTRESPONSE       ,"PSP_CD_HOSTRESPONSE"       },
    { PSP_CD_NODELISTREQUEST    ,"PSP_CD_NODELISTREQUEST"    },
    { PSP_CD_NODELISTRESPONSE   ,"PSP_CD_NODELISTRESPONSE"   },
    { PSP_CD_LOADREQUEST        ,"PSP_CD_LOADREQUEST"        },
    { PSP_CD_LOADRESPONSE       ,"PSP_CD_LOADRESPONSE"       },
    { PSP_CD_PROCREQUEST        ,"PSP_CD_PROCREQUEST"        },
    { PSP_CD_PROCRESPONSE       ,"PSP_CD_PROCRESPONSE"       },

    { PSP_CD_HOSTSTATUSREQUEST  ,"PSP_CD_HOSTSTATUSREQUEST"  },
    { PSP_CD_HOSTSTATUSRESPONSE ,"PSP_CD_HOSTSTATUSRESPONSE" },
    { PSP_CD_COUNTSTATUSREQUEST ,"PSP_CD_COUNTSTATUSREQUEST" },
    { PSP_CD_COUNTSTATUSRESPONSE,"PSP_CD_COUNTSTATUSRESPONSE"},
    { PSP_CD_RDPSTATUSREQUEST   ,"PSP_CD_RDPSTATUSREQUEST"   },  
    { PSP_CD_RDPSTATUSRESPONSE  ,"PSP_CD_RDPSTATUSRESPONSE"  },  
    { PSP_CD_MCASTSTATUSREQUEST ,"PSP_CD_MCASTSTATUSREQUEST" },  
    { PSP_CD_MCASTSTATUSRESPONSE,"PSP_CD_MCASTSTATUSRESPONSE"},  

    { PSP_DD_SPAWNREQUEST       ,"PSP_DD_SPAWNREQUEST"       }, 
    { PSP_DD_SPAWNSUCCESS       ,"PSP_DD_SPAWNSUCCESS"       }, 
    { PSP_DD_SPAWNFAILED        ,"PSP_DD_SPAWNFAILED"        }, 
    { PSP_DD_SPAWNFINISH        ,"PSP_DD_SPAWNFINISH"        }, 
   
    { PSP_DD_NOTIFYDEAD         ,"PSP_DD_NOTIFYDEAD"         },
    { PSP_DD_NOTIFYDEADRES      ,"PSP_DD_NOTIFYDEADRES"      },
    { PSP_DD_RELEASE            ,"PSP_DD_RELEASE"            },
    { PSP_DD_RELEASERES         ,"PSP_DD_RELEASERES"         },
    { PSP_DD_SIGNAL             ,"PSP_DD_SIGNAL"             },
    { PSP_DD_WHODIED            ,"PSP_DD_WHODIED"            },

    { PSP_DD_SYSTEMERROR        ,"PSP_DD_SYSTEMERROR"        }, 
    { PSP_DD_STATENOCONNECT     ,"PSP_DD_STATENOCONNECT"     },

    { PSP_DD_RESET              ,"PSP_DD_RESET"              },
    { PSP_DD_RESET_START_REQ    ,"PSP_DD_RESET_START_REQ"    },
    { PSP_DD_RESET_START_RESP   ,"PSP_DD_RESET_START_RESP"   },
    { PSP_DD_RESET_DORESET      ,"PSP_DD_RESET_DORESET"      },
    { PSP_DD_RESET_DONE         ,"PSP_DD_RESET_DONE"         },
    { PSP_DD_RESET_ABORT        ,"PSP_DD_RESET_ABORT"        },
    { PSP_DD_RESET_OK           ,"PSP_DD_RESET_OK"           },

    { PSP_CD_RESET              ,"PSP_CD_RESET"              },
    { PSP_CD_RESET_START_REQ    ,"PSP_CD_RESET_START_REQ"    },
    { PSP_CD_RESET_START_RESP   ,"PSP_CD_RESET_START_RESP"   },
    { PSP_CD_RESET_DORESET      ,"PSP_CD_RESET_DORESET"      },
    { PSP_CD_RESET_DONE         ,"PSP_CD_RESET_DONE"         },
    { PSP_CD_RESET_ABORT        ,"PSP_CD_RESET_ABORT"        },
    { PSP_CD_RESET_OK           ,"PSP_CD_RESET_OK"           },

    { PSP_DD_DAEMONCONNECT      ,"PSP_DD_DAEMONCONNECT"      }, 
    { PSP_DD_DAEMONESTABLISHED  ,"PSP_DD_DAEMONESTABLISHED"  }, 
    { PSP_DD_DAEMONSTOP         ,"PSP_DD_DAEMONSTOP"         }, 
    { PSP_CD_DAEMONSTOP         ,"PSP_CD_DAEMONSTOP"         }, 

    { PSP_DD_NEWPROCESS         ,"PSP_DD_NEWPROCESS"         },
    { PSP_DD_DELETEPROCESS      ,"PSP_DD_DELETEPROCESS"      }, 
    { PSP_DD_CHILDDEAD          ,"PSP_DD_CHILDDEAD"          },

    {0,NULL}
};

char *PSP_printMsg(int msgtype)
{
    static char txt[30];
    int i = 0;

    while (PSPctrlmessages[i].id && PSPctrlmessages[i].id != msgtype) {
	i++;
    }

    if (PSPctrlmessages[i].id) {
	return PSPctrlmessages[i].message;
    } else {
	snprintf(txt, sizeof(txt), "msgtype 0x%x UNKNOWN", msgtype);
	return txt;
    }
}
