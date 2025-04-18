/*
 * ParaStation
 *
 * Copyright (C) 2002-2004 ParTec AG, Karlsruhe
 * Copyright (C) 2005-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psprotocol.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pscommon.h"

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

    { PSP_CD_REQUESTPART      , "PSP_CD_REQUESTPART"      },
    { PSP_CD_PARTITIONRES     , "PSP_CD_PARTITIONRES"     },
    { PSP_CD_GETRESERVATION   , "PSP_CD_GETRESERVATION"   },
    { PSP_CD_RESERVATIONRES   , "PSP_CD_RESERVATIONRES"   },
    { PSP_CD_GETSLOTS         , "PSP_CD_GETSLOTS"         },
    { PSP_CD_SLOTSRES         , "PSP_CD_SLOTSRES"         },
    { PSP_CD_FINRESERVATION   , "PSP_CD_FINRESERVATION"   },

    { PSP_CD_SENDSTOP         , "PSP_CD_SENDSTOP"         },
    { PSP_CD_SENDCONT         , "PSP_CD_SENDCONT"         },

    { PSP_CC_MSG              , "PSP_CC_MSG"              },
    { PSP_CC_ERROR            , "PSP_CC_ERROR"            },

    { PSP_PF_MSG              , "PSP_PF_MSG"              },

    { PSP_CD_UNKNOWN          , "PSP_CD_UNKNOWN"          },
    { PSP_CD_ERROR            , "PSP_CD_ERROR"            },

    {0,NULL}
};

char *PSP_printMsg(int msgtype)
{

    for (int m = 0; messages[m].name; m++) {
	if (messages[m].id == msgtype) return messages[m].name;
    }

    static char txt[30];
    snprintf(txt, sizeof(txt), "msgtype 0x%x UNKNOWN", msgtype);
    return txt;
}

int16_t PSP_resolveType(char *typeStr)
{
    if (!typeStr) return -1;

    for (int m = 0; messages[m].name; m++) {
	if (!strcmp(messages[m].name, typeStr)) return messages[m].id;
    }

    return -1;
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
    { PSP_INFO_INSTDIR,          "PSP_INFO_INSTDIR" },
    { PSP_INFO_HOST,             "PSP_INFO_HOST" },
    { PSP_INFO_NODE,             "PSP_INFO_NODE" },

    { PSP_INFO_LIST_END,         "PSP_INFO_LIST_END" },

    { PSP_INFO_LIST_HOSTSTATUS,  "PSP_INFO_LIST_HOSTSTATUS" },
    { PSP_INFO_RDPSTATUS,        "PSP_INFO_RDPSTATUS" },
    { PSP_INFO_MCASTSTATUS,      "PSP_INFO_MCASTSTATUS" },

    { PSP_INFO_COUNTHEADER,      "PSP_INFO_COUNTHEADER" },
    { PSP_INFO_COUNTSTATUS,      "PSP_INFO_COUNTSTATUS" },

    { PSP_INFO_HWNUM,            "PSP_INFO_HWNUM" },
    { PSP_INFO_HWINDEX,          "PSP_INFO_HWINDEX" },
    { PSP_INFO_HWNAME,           "PSP_INFO_HWNAME" },

    { PSP_INFO_TASKRANK,         "PSP_INFO_TASKRANK" },
    { PSP_INFO_PARENTTID,        "PSP_INFO_PARENTTID" },
    { PSP_INFO_LOGGERTID,        "PSP_INFO_LOGGERTID" },

    { PSP_INFO_LIST_VIRTCPUS,    "PSP_INFO_LIST_VIRTCPUS" },
    { PSP_INFO_LIST_PHYSCPUS,    "PSP_INFO_LIST_PHYSCPUS" },
    { PSP_INFO_LIST_HWSTATUS,    "PSP_INFO_LIST_HWSTATUS" },
    { PSP_INFO_LIST_LOAD,        "PSP_INFO_LIST_LOAD" },
    { PSP_INFO_LIST_ALLJOBS,     "PSP_INFO_LIST_ALLJOBS" },
    { PSP_INFO_LIST_NORMJOBS,    "PSP_INFO_LIST_NORMJOBS" },
    { PSP_INFO_LIST_ALLOCJOBS,   "PSP_INFO_LIST_ALLOCJOBS" },
    { PSP_INFO_LIST_EXCLUSIVE,   "PSP_INFO_LIST_EXCLUSIVE" },
    { PSP_INFO_LIST_PARTITION,   "PSP_INFO_LIST_PARTITION" },
    { PSP_INFO_LIST_MEMORY,      "PSP_INFO_LIST_MEMORY" },
    { PSP_INFO_LIST_RESNODES,    "PSP_INFO_LIST_RESNODES" },

    { PSP_INFO_CMDLINE,          "PSP_INFO_CMDLINE" },
    { PSP_INFO_RPMREV,           "PSP_INFO_RPMREV" },

    { PSP_INFO_QUEUE_SEP,        "PSP_INFO_QUEUE_SEP" },
    { PSP_INFO_QUEUE_ALLTASK,    "PSP_INFO_QUEUE_ALLTASK" },
    { PSP_INFO_QUEUE_NORMTASK,   "PSP_INFO_QUEUE_NORMTASK" },
    { PSP_INFO_QUEUE_PARTITION,  "PSP_INFO_QUEUE_PARTITION" },

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

size_t PSP_strLen(const char *str)
{
    return str ? strlen(str) + 1 : 0;
}

static bool doPutMsgBuf(DDBufferMsg_t *msg, const char *callName,
			const char *caller, const char *dataName,
			const void *data, size_t size, bool typed, bool try)
{
    size_t off;

    if (!msg) {
	PSC_log("%s: no msg provided for '%s' in %s()\n", callName,
		dataName, caller);
	return false;
    }

    /* msg->header.len might be 0 on first call */
    if (!msg->header.len) msg->header.len = DDBufferMsgOffset;
    off = msg->header.len - DDBufferMsgOffset;
    if (typed && !off) {
	/* First item to add: adapt len and offset for type member */
	size_t t_off = DDTypedBufMsgOffset - DDBufferMsgOffset;
	off += t_off;
	msg->header.len += t_off;
    }

    size_t s = size ? size : 1;
    size_t used = (sizeof(msg->buf) - off >= s) ? s : 0;

    if (!used) {
	PSC_dbg(try ? PSC_LOG_VERB : -1, "%s: data '%s' too large in %s()\n",
		callName, dataName ? dataName : "<empty>", caller);
	return false;
    }

    if (data) {
	memcpy(msg->buf+off, data, size);
    } else {
	msg->buf[off] = '\0';
    }
    msg->header.len += used;

    return true;
}

bool PSP_putMsgBufF(DDBufferMsg_t *msg, const char *caller,
		    const char *dataName, const void *data, size_t size)
{
    return doPutMsgBuf(msg, "PSP_putMsgBuf", caller, dataName,
		       data, size, false /* typed */, false /* try */);
}

bool PSP_tryPutMsgBufF(DDBufferMsg_t *msg, const char *caller,
		       const char *dataName, const void *data, size_t size)
{
    return doPutMsgBuf(msg, "PSP_tryPutMsgBuf", caller, dataName,
		       data, size, false /* typed */, true /* try */);
}

bool PSP_putTypedMsgBufF(DDTypedBufferMsg_t *msg, const char *caller,
			 const char *dataName, const void *data, size_t size)
{
    return doPutMsgBuf((DDBufferMsg_t *)msg, "PSP_putTypedMsgBuf", caller,
		       dataName, data, size, true /* typed */, false /* try */);
}

bool PSP_tryPutTypedMsgBufF(DDTypedBufferMsg_t *msg, const char *caller,
			    const char *dataName, const void *data, size_t size)
{
    return doPutMsgBuf((DDBufferMsg_t *)msg, "PSP_tryPutTypedMsgBuf", caller,
		       dataName, data, size, true /* typed */, true /* try */);
}

static bool doGetMsgBuf(DDBufferMsg_t *msg, size_t *used, const char *callName,
			const char *caller, const char *dataName, void *data,
			size_t size, bool typed, bool try)
{
    if (!msg || !used || !data) {
	PSC_log("%s: no %s provided for '%s' in %s()\n", callName,
		msg ? (used ? "data" : "used") : "msg", dataName, caller);
	return false;
    }

    size_t u = *used;
    if (typed) u += DDTypedBufMsgOffset - DDBufferMsgOffset;

    ssize_t totAvail = msg->header.len - DDBufferMsgOffset;
    if ((ssize_t)(size + u) > totAvail) {
	PSC_dbg(try ? PSC_LOG_VERB : -1,
		"%s: insufficient data for '%s' in %s()\n", callName, dataName,
		caller);
	return false;
    }

    memcpy(data, msg->buf + u, size);
    *used += size;

    return true;
}

bool PSP_tryGetMsgBufF(DDBufferMsg_t *msg, size_t *used, const char *caller,
		       const char *dataName, void *data, size_t size)
{
    return doGetMsgBuf(msg, used, "PSP_tryGetMsgBuf", caller, dataName, data,
		       size, false /* typed */, true /* try */);
}

bool PSP_getMsgBufF(DDBufferMsg_t *msg, size_t *used, const char *caller,
		    const char *dataName, void *data, size_t size)
{
    return doGetMsgBuf(msg, used, "PSP_getMsgBuf", caller, dataName, data,
		       size, false /* typed */, false /* try */);
}

bool PSP_getTypedMsgBufF(DDTypedBufferMsg_t *msg, size_t *used,
			 const char *caller, const char *dataName, void *data,
			 size_t size)
{
    return doGetMsgBuf((DDBufferMsg_t *)msg, used, "PSP_getTypedMsgBuf", caller,
		       dataName, data, size, true /* typed */, false /* try */);
}
