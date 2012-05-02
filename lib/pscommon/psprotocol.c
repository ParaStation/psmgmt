/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__((used)) =
    "$Id$";
#endif /* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>

#include "pscommon.h"

#include "psprotocol.h"

/*
 * string identification of message IDs.
 * Nicer output for errrors and debugging.
 */
static struct {
    int id;
    char *name;
} messages[] = {
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
    { PSP_CD_SPAWNREQ         , "PSP_CD_SPAWNREQ"         },
    { PSP_CD_ACCOUNT          , "PSP_CD_ACCOUNT"          },

    { PSP_CD_NOTIFYDEAD       , "PSP_CD_NOTIFYDEAD"       },
    { PSP_CD_NOTIFYDEADRES    , "PSP_CD_NOTIFYDEADRES"    },
    { PSP_CD_RELEASE          , "PSP_CD_RELEASE"          },
    { PSP_CD_RELEASERES       , "PSP_CD_RELEASERES"       },
    { PSP_CD_SIGNAL           , "PSP_CD_SIGNAL"           },
    { PSP_CD_WHODIED          , "PSP_CD_WHODIED"          },
    { PSP_CD_SIGRES           , "PSP_CD_SIGRES"           },

    { PSP_CD_DAEMONSTART      , "PSP_CD_DAEMONSTART"      },
    { PSP_CD_DAEMONSTOP       , "PSP_CD_DAEMONSTOP"       },
    { PSP_CD_DAEMONRESET      , "PSP_CD_DAEMONRESET"      },
    { PSP_CD_HWSTART          , "PSP_CD_HWSTART"          },
    { PSP_CD_HWSTOP           , "PSP_CD_HWSTOP"           },
    { PSP_CD_PLUGIN           , "PSP_CD_PLUGIN"           },
    { PSP_CD_PLUGINRES        , "PSP_CD_PLUGINRES"        },
    { PSP_CD_ENV              , "PSP_CD_ENV"              },
    { PSP_CD_ENVRES           , "PSP_CD_ENVRES"           },

    { PSP_CD_CREATEPART       , "PSP_CD_CREATEPART"       },
    { PSP_CD_CREATEPARTNL     , "PSP_CD_CREATEPARTNL"     },
    { PSP_CD_PARTITIONRES     , "PSP_CD_PARTITIONRES"     },
    { PSP_CD_GETNODES         , "PSP_CD_GETNODES"         },
    { PSP_CD_NODESRES         , "PSP_CD_NODESRES"         },
    { PSP_CD_GETRANKNODE      , "PSP_CD_GETRANKNODE"      },

    { PSP_CC_MSG              , "PSP_CC_MSG"              },
    { PSP_CC_ERROR            , "PSP_CC_ERROR"            },

    { PSP_CD_ERROR            , "PSP_CD_ERROR"            },

    {0,NULL}
};

char *PSP_printMsg(int msgtype)
{
    static char txt[30];
    int i = 0;

    while (messages[i].name && messages[i].id != msgtype) {
	i++;
    }

    if (messages[i].name) {
	return messages[i].name;
    } else {
	snprintf(txt, sizeof(txt), "msgtype 0x%x UNKNOWN", msgtype);
	return txt;
    }
}

/*
 * string identification of info IDs.
 * Nicer output for errrors and debugging.
 */
static struct {
    PSP_Info_t id;
    char *name;
} infos[] = {
    { PSP_INFO_UNKNOWN,          "PSP_INFO_UNKNOWN" },
    { PSP_INFO_NROFNODES,        "PSP_INFO_NROFNODES" },
    { PSP_INFO_INSTDIR,          "PSP_INFO_INSTDIR" },
    { PSP_INFO_DAEMONVER,        "PSP_INFO_DAEMONVER" },
    { PSP_INFO_HOST,             "PSP_INFO_HOST" },
    { PSP_INFO_NODE,             "PSP_INFO_NODE" },
    { PSP_INFO_NODELIST,         "PSP_INFO_NODELIST" },
    { PSP_INFO_PARTITION,        "PSP_INFO_PARTITION" },

    { PSP_INFO_LIST_TASKS,       "PSP_INFO_LIST_TASK" },
    { PSP_INFO_LIST_END,         "PSP_INFO_LIST_END" },

    { PSP_INFO_LIST_HOSTSTATUS,  "PSP_INFO_LIST_HOSTSTATUS" },
    { PSP_INFO_RDPSTATUS,        "PSP_INFO_RDPSTATUS" },
    { PSP_INFO_MCASTSTATUS,      "PSP_INFO_MCASTSTATUS" },

    { PSP_INFO_COUNTHEADER,      "PSP_INFO_COUNTHEADER" },
    { PSP_INFO_COUNTSTATUS,      "PSP_INFO_COUNTSTATUS" },

    { PSP_INFO_HWNUM,            "PSP_INFO_HWNUM" },
    { PSP_INFO_HWINDEX,          "PSP_INFO_HWINDEX" },
    { PSP_INFO_HWNAME,           "PSP_INFO_HWNAME" },

    { PSP_INFO_RANKID,           "PSP_INFO_RANKID" },
    { PSP_INFO_TASKSIZE,         "PSP_INFO_TASKSIZE" },
    { PSP_INFO_TASKRANK,         "PSP_INFO_TASKRANK" },

    { PSP_INFO_PARENTTID,        "PSP_INFO_PARENTTID" },
    { PSP_INFO_LOGGERTID,        "PSP_INFO_LOGGERTID" },

    { PSP_INFO_LIST_VIRTCPUS,    "PSP_INFO_LIST_VIRTCPUS" },
    { PSP_INFO_LIST_PHYSCPUS,    "PSP_INFO_LIST_PHYSCPUS" },
    { PSP_INFO_LIST_HWSTATUS,    "PSP_INFO_LIST_HWSTATUS" },
    { PSP_INFO_LIST_LOAD,        "PSP_INFO_LIST_LOAD" },
    { PSP_INFO_LIST_ALLJOBS,     "PSP_INFO_LIST_ALLJOBS" },
    { PSP_INFO_LIST_NORMJOBS,    "PSP_INFO_LIST_NORMJOBS" },
    { PSP_INFO_LIST_ALLTASKS,    "PSP_INFO_LIST_ALLTASKS" },
    { PSP_INFO_LIST_NORMTASKS,   "PSP_INFO_LIST_NORMTASKS" },
    { PSP_INFO_LIST_ALLOCJOBS,   "PSP_INFO_LIST_ALLOCJOBS" },
    { PSP_INFO_LIST_EXCLUSIVE,   "PSP_INFO_LIST_EXCLUSIVE" },

    { PSP_INFO_CMDLINE,          "PSP_INFO_CMDLINE" },
    { PSP_INFO_RPMREV,           "PSP_INFO_RPMREV" },

    { PSP_INFO_QUEUE_SEP,        "PSP_INFO_QUEUE_SEP" },
    { PSP_INFO_QUEUE_ALLTASK,    "PSP_INFO_QUEUE_ALLTASK" },
    { PSP_INFO_QUEUE_NORMTASK,   "PSP_INFO_QUEUE_NORMTASK" },
    { PSP_INFO_QUEUE_PARTITION,  "PSP_INFO_QUEUE_PARTITION" },

    { PSP_INFO_LIST_PARTITION,   "PSP_INFO_LIST_PARTITION" },
    { PSP_INFO_LIST_MEMORY,      "PSP_INFO_LIST_MEMORY" },
    { PSP_INFO_LIST_RESPORTS,    "PSP_INFO_LIST_RESPORTS" },

    { PSP_INFO_QUEUE_PLUGINS,    "PSP_INFO_QUEUE_PLUGINS" },

    { PSP_INFO_STARTTIME,        "PSP_INFO_STARTTIME" },

    { PSP_INFO_STARTUPSCRIPT,    "PSP_INFO_STARTUPSCRIPT" },
    { PSP_INFO_NODEUPSCRIPT,     "PSP_INFO_NODEUPSCRIPT" },
    { PSP_INFO_NODEDOWNSCRIPT,   "PSP_INFO_NODEDOWNSCRIPT" },

    { PSP_INFO_QUEUE_ENVS,       "PSP_INFO_QUEUE_ENVS" },

    {0,NULL}
};

char *PSP_printInfo(PSP_Info_t infotype)
{
    static char txt[30];
    int i = 0;

    while (infos[i].name && infos[i].id != infotype) {
	i++;
    }

    if (infos[i].name) {
	return infos[i].name;
    } else {
	snprintf(txt, sizeof(txt), "infotype 0x%x UNKNOWN", infotype);
	return txt;
    }
}

size_t PSP_strLen(char *str)
{
    return str ? strlen(str) + 1 : 0;
}

int PSP_putMsgBuf(DDBufferMsg_t *msg, char *dataName, void *data, size_t size)
{
    size_t off = msg->header.len - sizeof(msg->header);
    size_t used;

    if (data) {
	used = (sizeof(msg->buf) - off >= size) ? size : 0;
	if (used) memcpy(msg->buf+off, data, size);
    } else {
	used = 1;
	msg->buf[off] = '\0';
    }
    if (!used) {
	PSC_log(-1, "%s: data '%s' too large for buffer\n", __func__, dataName);
	return 0;
    }
    msg->header.len += used;

    return 1;
}

int PSP_putTypedMsgBuf(DDTypedBufferMsg_t *msg, char *dataName, void *data,
		       size_t size)
{
    size_t off = msg->header.len - sizeof(msg->header) - sizeof(msg->type);
    size_t used;

    if (data) {
	used = (sizeof(msg->buf) - off >= size) ? size : 0;
	if (used) memcpy(msg->buf+off, data, size);
    } else {
	used = 1;
	msg->buf[off] = '\0';
    }
    if (!used) {
	PSC_log(-1, "%s: data '%s' too large for buffer\n", __func__, dataName);
	return 0;
    }
    msg->header.len += used;

    return 1;
}
