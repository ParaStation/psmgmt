/*
 *               ParaStation3
 * psitask.c
 *
 * ParaStation client-daemon and daemon-daemon high-level protocol.
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: psp.c,v 1.11 2002/02/19 09:35:08 eicker Exp $
 *
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__(( unused )) = "$Id: psp.c,v 1.11 2002/02/19 09:35:08 eicker Exp $";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "psi.h"

#include "psp.h"

/*----------------------------------------------------------------------*/
/* string identification of message IDs.                                */
/* Nicer output for errrors and debugging.                              */
/*----------------------------------------------------------------------*/
struct PSPctrlmessages_t{
    long id;
    char* message;
};
extern struct PSPctrlmessages_t PSPctrlmessages[];

struct PSPctrlmessages_t PSPctrlmessages[]=
{
   { PSP_CD_CLIENTCONNECT      ,"PSP_CD_CLIENTCONNECT"      },
   { PSP_CD_CLIENTESTABLISHED  ,"PSP_CD_CLIENTESTABLISHED"  },
   { PSP_CD_CLIENTREFUSED      ,"PSP_CD_CLIENTREFUSED"      },
   { PSP_CD_OLDVERSION         ,"PSP_CD_OLDVERSION"         },
   { PSP_CD_NOSPACE            ,"PSP_CD_NOSPACE"            }, 
   { PSP_CD_REMOTECONNECT      ,"PSP_CD_REMOTECONNECT"      },
   { PSP_CD_UIDLIMIT           ,"PSP_CD_UIDLIMIT"           },
   { PSP_CD_PROCLIMIT          ,"PSP_CD_PROCLIMIT"          }, 
   { PSP_DD_SETOPTION          ,"PSP_DD_SETOPTION"          }, 
   { PSP_DD_GETOPTION          ,"PSP_DD_GETOPTION"          }, 
   { PSP_DD_CONTACTNODE        ,"PSP_DD_CONTACTNODE"        },
   
   { PSP_CD_TASKINFO           ,"PSP_CD_TASKINFO"           }, 
   { PSP_CD_TASKINFOEND        ,"PSP_CD_TASKINFOEND"        }, 
   { PSP_CD_TASKLISTREQUEST    ,"PSP_CD_TASKLISTREQUEST"    }, 
   { PSP_CD_TASKINFOREQUEST    ,"PSP_CD_TASKINFOREQUEST"    },
   { PSP_CD_HOSTSTATUSREQUEST  ,"PSP_CD_HOSTSTATUSREQUEST"  },
   { PSP_CD_HOSTSTATUSRESPONSE ,"PSP_CD_HOSTSTATUSRESPONSE" },
   { PSP_CD_HOSTLISTREQUEST    ,"PSP_CD_HOSTLISTREQUEST"    },
   { PSP_CD_HOSTLISTRESPONSE   ,"PSP_CD_HOSTLISTRESPONSE"   },

   { PSP_CD_HOSTREQUEST        ,"PSP_CD_HOSTREQUEST"        },
   { PSP_CD_HOSTRESPONSE       ,"PSP_CD_HOSTRESPONSE"       },

   { PSP_CD_LOADREQ            ,"PSP_CD_LOADREQ"            },
   { PSP_CD_LOADRES            ,"PSP_CD_LOADRES"            },

   { PSP_CD_PROCREQ            ,"PSP_CD_PROCREQ"            },
   { PSP_CD_PROCRES            ,"PSP_CD_PROCRES"            },

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
   
   { PSP_DD_TASKKILL           ,"PSP_DD_TASKKILL"           }, 
   { PSP_DD_NOTIFYDEAD         ,"PSP_DD_NOTIFYDEAD"         },
   { PSP_DD_RELEASE            ,"PSP_DD_RELEASE"            },
   { PSP_DD_WHODIED            ,"PSP_DD_WHODIED"            },
   { PSP_DD_NOTIFYDEADRES      ,"PSP_DD_NOTIFYDEADRES"      },
   { PSP_DD_RELEASERES         ,"PSP_DD_RELEASERES"         },

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

char PSPctrlmsgtxt[80];

char* PSPctrlmsg(int msgtype)
{
    int i=0;
    while ((PSPctrlmessages[i].id!=0)            /* end symbol */
	   &&(PSPctrlmessages[i].id != msgtype)) {
	i++;
    }

    if (PSPctrlmessages[i].id!=0) {
	return PSPctrlmessages[i].message;
    } else {
	sprintf(PSPctrlmsgtxt,"msgtype 0x%x UNKNOWN",msgtype);
	return PSPctrlmsgtxt;
    }
}

int ClientMsgSend(void* amsg)
{
    DDMsg_t* msg = (DDMsg_t*)amsg;
    int ret = 0;

    if ((ret = write(PSI_msock, msg, msg->len))==0) {
	perror("ClientMsgSend: Lost connection to ParaStation daemon");
	exit(-1);
    }
    return ret;
}

int ClientMsgRecv(void * amsg)
{
    DDMsg_t* msg = (DDMsg_t*)amsg;
    int n;
    int count =0;
    do {
	if (count==0) {
	    n = read(PSI_msock, msg, sizeof(*msg));
	} else {
	    n = read(PSI_msock, &((char*)msg)[count], msg->len-count);
	}
	if (n>0) count+=n;
	if (n==0) {
	    perror("ClientMsgRecv: Lost connection to ParaStation daemon");
	    exit(-1);
	}
    } while ((msg->len > count) && n > 0);
    if (count==msg->len){
	return msg->len;
    } else {
	return n;
    }
}
