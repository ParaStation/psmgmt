/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include "psmomrpp.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>

#include "selector.h"

#include "pbsrpp.h"
#include "psmom.h"
#include "psmomauth.h"
#include "psmomcomm.h"
#include "psmomlog.h"
#include "psmomproto.h"
#include "psmompbsserver.h"

int rppPoll(int fd, void *data)
{
    int stream = -1;
    ComHandle_t *com;

    while (1) {
	/* Warning: rpp_poll() will also return streams when internal connection
	 * timeouts occure. An rpp_read() will then fail on the stream.
	 * Reconnecting such rpp_streams seems to work fine.
	 */
	stream = rpp_poll();
	if (stream == -1 || stream == -2) break;

	if (!(com = getComHandle(stream, RPP_PROTOCOL))) {
	    mlog("%s: com handle not found for stream '%i'\n", __func__,
		    stream);
	} else {
	    if (com->isAuth) {
		handleNewData(com);
	    } else if (!(isAuthIP(com->lremoteAddr))
			    || com->remotePort > IPPORT_RESERVED) {
		mlog("%s: not authorized remote addr or port for stream '%i'\n",
		    __func__, stream);
	    } else {
		handleNewData(com);
	    }
	}
    }

    if ((rpp_io()) != 0) {
	mlog("%s: rppio error\n", __func__);
	return -1;
    }

    return 1;
}

int rppBind(int port)
{
    int fd;

    if ((fd = rpp_bind(port)) == -1) {
	return -1;
    }

    /* register fd in selector facility */
    if (!Selector_isRegistered(fd)) {
	Selector_register(fd, rppPoll, NULL);
    }

    if ((rpp_io()) == -1) {
	return -1;
    }

    return 1;
}

void rppShutdown()
{
    int i;

    /* Remove all rpp sockets from the selector facility.
     * The rpp_fd_num is located in the PBS rpp library.*/
    for (i=0; i<rpp_fd_num; i++) {
	Selector_remove(rpp_fd_array[i]);
    }

    /* close all open streams */
    rpp_shutdown();

    /* free all remaining memory */
    rpp_terminate();
}

int rppDoSend(int stream, const char *caller)
{
    if (stream < 0) {
	mlog("%s(%s): invalid stream '%i'\n", __func__, caller, stream);
	return -1;
    }

    mdbg(PSMOM_LOG_RPP, "%s(%s): stream:%i\n", __func__, caller, stream);

    if ((rpp_wcommit(stream, 1)) < 0) {
	mwarn(errno, "%s(%s): error wcommit ", __func__, caller);
	return -1;
    }

    if ((rpp_flush(stream)) < 0) {
	mwarn(errno, "%s(%s): error flush ", __func__, caller);
	return -1;
    }

    return 0;
}

int rppWrite(int stream, void *msg, size_t len, const char *caller)
{
    int ret = 0;

    if (stream < 0) {
	mlog("%s(%s): invalid stream '%i'\n", __func__, caller, stream);
	return -1;
    }

    mdbg(PSMOM_LOG_RPP, "%s(%s): stream '%i' len '%zu' msg '%s'\n",
	    __func__, caller, stream, len, (char *)msg);

    if ((ret = rpp_write(stream, msg, len)) < 0) {
	switch (errno) {
	    case 32: /* Broken pipe */
		return -1;
	    case 107: /* Transport endpoint is not connected */
	    case 110: /* Connection timed out */
		mdbg(PSMOM_LOG_RPP | PSMOM_LOG_VERBOSE, "%s(%s): lost "
			"connection stream:%i error:%i - %s, reconnecting\n",
			__func__, caller, stream, errno, strerror(errno));
		break;
	    default:
		mwarn(errno, "%s(%s): failed, stream '%i' errno '%i'", __func__,
			caller, stream, errno);
	}
	rppReconnect(stream, __func__);
    }
    return ret;
}

int rppOpen(int port, char *host)
{
    int stream = -1;
    char buf[1024];
    ComHandle_t *com;

    if (!port || !host) return -1;

    if ((stream = rpp_open(host, port, buf))< 0) {
	mlog("%s: error:%i, errmsg:%s\n", __func__, errno, buf);
	return -1;
    }

    /* register fd in selector facility */
    if (!Selector_isRegistered(rpp_getfd(stream))) {
	Selector_register(rpp_getfd(stream), rppPoll, NULL);
    }
    if ((com = getComHandle(stream, RPP_PROTOCOL))) com->isAuth = 1;

    mdbg(PSMOM_LOG_RPP, "%s: Connected to %s, port:%i\n", __func__, host, port);

    return stream;
}

int rppReconnect(int stream, const char *caller)
{
    Server_t *serv;
    ComHandle_t *com;
    int new_stream;

    if (stream < 0) {
	mlog("%s(%s) reconnect on invalid stream '%i'\n", __func__, caller,
		stream);
	return -1;
    }

    rppClose(stream);

    if (!(com = findComHandle(stream, RPP_PROTOCOL))) {
	mlog("%s(%s) communication handle not found for stream '%i'\n",
		__func__, caller, stream);
	return -1;
    }

    if (!(serv = findServer(com))) {
	/* no need to reconnect, since this is a new temp connection */
	return 0;
    }
    serv->haveConnection = 0;

    if (!com->remotePort || !com->remoteAddr[0]) {
	mlog("%s no remote port or addr for stream '%i'\n", __func__, stream);
	return -1;
    }

    if ((new_stream = rppOpen(com->remotePort, com->remoteAddr))< 0) {
	mlog("%s: reconnecting to %s:%i failed!\n", __func__, com->remoteAddr,
	    com->remotePort);
	com->socket = -1;
	return -1;
    }
    com->socket = new_stream;

    /* empty receive queue */
    if (torqueVer > 2) {
	rppEOM(stream);
    }

    /* init IS protocol */
    InitIS(com);

    return 0;
}

ssize_t rppRead(int stream, char *buffer, ssize_t len, const char *caller)
{
    int ret = 0;

    if ((ret = rpp_read(stream, buffer, len)) == -1) {
	switch (errno) {
	    case 32:  /* Broken pipe */
	    case 107: /* Transport endpoint is not connected */
	    case 110: /* Connection timed out */
		mdbg(PSMOM_LOG_RPP | PSMOM_LOG_VERBOSE, "%s: lost connection"
		    " stream:%i error:%i - %s, reconnecting\n", __func__,
		    stream, errno, strerror(errno));
		break;
	    default:
		mwarn(errno, "%s: failed, stream:%i errno:%i ", __func__,
			stream, errno);
	}
	rppReconnect(stream, __func__);
	return ret;
    }

    if (ret > -1) {
	buffer[ret] = '\0';
	mdbg(PSMOM_LOG_RPP, "%s: stream:%i len:%i msg:%s\n", __func__, stream,
	    ret, buffer);
    }

    if (ret == -2) {
	mlog("%s:nothing read\n", __func__);
	rpp_rcommit(stream, 0);
    } else {
	if (stream < 0) {
	    mlog("%s: invalid stream:%i\n", __func__, stream);
	    return -1;
	}

	mdbg(PSMOM_LOG_RPP, "%s: stream:%i\n", __func__, stream);

	if ((rpp_rcommit(stream, 1)) < 0) {
	    mlog("%s: error rcommit:%i\n", __func__, errno);
	    return -1;
	}
    }

    return ret;
}

int rppClose(int stream)
{
    int ret;

    mdbg(PSMOM_LOG_RPP, "%s: stream:%i\n", __func__, stream);

    if ((ret = rpp_close(stream)) != 0) {
	mlog("%s: failed\n", __func__);
    }
    return ret;
}

int rppFlush(int stream)
{
    int ret;

    if ((ret = rpp_flush(stream)) != 0) {
	mlog("%s: failed\n", __func__);
    }

    return ret;
}

int rppEOM(int stream)
{
    int ret;

    if ((ret = rpp_eom(stream)) != 0) {
	mdbg(PSMOM_LOG_RPP, "%s: failed\n", __func__);
    }

    return ret;
}

struct sockaddr_in* rppGetAddr(int stream)
{
    return rpp_getaddr(stream);
}
