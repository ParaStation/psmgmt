/*
 * ParaStation
 *
 * Copyright (C) 2013-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "psprotocol.h"
#include "psicomm_proto.h"

#include "psilog.h"

#include "psicomm.h"

static int PSIcomm_sock = -1;

static int PSIcomm_myRank = -1;

static int PSIcomm_version = 0;

int PSIcomm_init(void)
{
    char *envStr;

    if (!PSI_logInitialized()) PSI_initLog(NULL);

    PSI_log(-1, "%s(): not yet implemented\n", __func__);

    envStr = getenv("PMI_RANK");
    if (!envStr) {
	PSI_log(-1, "%s: Unable to determine local rank\n", __func__);
	return -1;
    }
    PSIcomm_myRank = atoi(envStr);

    /* @todo create connection to local forwarder */
    return -1;
}

static int do_send(char *buf, size_t count)
{
    size_t c = count;

    if (PSIcomm_sock == -1) {
	errno = ENOTCONN;
	return -1;
    }

    //int n;
    //c = n = count; /*@todo */
    return c; /*@todo */
    /* do { */
    /* 	n = (c>sizeof(msg.buf)) ? sizeof(msg.buf) : c; */
    /* 	if (n) memcpy(msg.buf, buf, n); */
    /* 	msg.header.len = PSLog_headerSize + n; */
    /* 	n = send(daemonsock, &msg, msg.header.len, 0); */
    /* 	if (n < 0) { */
    /* 	    switch(errno) { */
    /* 	    case EAGAIN: */
    /* 	    case EINTR: */
    /* 		continue; */
    /* 		break; */
    /* 	    default: */
    /* 		return n;             /\* error, return < 0 *\/ */
    /* 	    } */
    /* 	} */
    /* 	if ( (n > 0) && (n < (int) sizeof(msg.header))) { */
    /* 	    errno = EIO; */
    /* 	    return -1; */
    /* 	} */
    /* 	c -= n - PSLog_headerSize; */
    /* 	buf += n - PSLog_headerSize; */
    /* } while (c > 0); */

    return count;
}

int PSIcomm_send(int dest_rank, int type, void *payload, size_t len)
{
    PSIcomm_Msg_t msg;

    PSI_log(-1, "%s(): not yet implemented\n", __func__);

    return -1;

    if (PSIcomm_sock == -1) {
	errno = ENOTCONN;
	return -1;
    }

    if (len > PSICOMM_MAX_SIZE) {
	errno = EMSGSIZE;
	return -1;
    }

    msg.header.type = PSP_CC_PSI_MSG;
    msg.header.sender = 0; /* will be filled by local forwarder */
    msg.header.dest = 0;   /* will be filled by local daemon/psicomm plugin */
    msg.header.len = sizeof(msg.header);

    msg.version = PSIcomm_version;
    msg.header.len += sizeof(msg.version);

    msg.src_rank = PSIcomm_myRank;
    msg.header.len += sizeof(msg.src_rank);

    msg.dest_rank = dest_rank;
    msg.header.len += sizeof(msg.dest_rank);

    msg.type = type;
    msg.header.len += sizeof(msg.type);

    PSP_putMsgBuf((DDBufferMsg_t*)&msg, "payload", len ? payload : NULL, len);

    return do_send((char*)&msg, msg.header.len);

}

static int do_recv(char *buf, size_t count)
{
    int total = 0, n;

    while(count > 0) {      /* Complete message */
	n = recv(PSIcomm_sock, buf, count, 0);
	if (n < 0) {
	    switch (errno) {
	    case EINTR:
	    case EAGAIN:
		continue;
		break;
	    default:
		return n;             /* error, return < 0 */
	    }
	} else if (n == 0) {
	    return n;
	}
	count -= n;
	total += n;
	buf += n;
    }

    return total;
}


int PSIcomm_recv(int *src_rank, int *type, void *payload, size_t *len)
{
    int total, n;
    PSIcomm_Msg_t msg;
    size_t pl_len;
    //char *buf=(char *)msg;

    PSI_log(-1, "%s(): not yet implemented\n", __func__);

    return -1;

    if (PSIcomm_sock < 0) {
	errno = EBADF;
	return -1;
    }

    /* First only try to read the header */
    total = n = do_recv((char*)&msg, sizeof(msg.header));
    if (n <= 0) return n;

    if (msg.header.len > sizeof(msg)) {
	/* protocol broken. Receive rest of message but return error */
    }

    /* Now read the rest of the message (if necessary) */
    if (msg.header.len -total) {
	total += n = do_recv(((char*)&msg)+total, msg.header.len - total);
	if (n <= 0) return n;
    }

    pl_len = msg.header.len - sizeof(msg.header) - sizeof(msg.version)
	- sizeof(msg.src_rank) - sizeof(msg.dest_rank) - sizeof(msg.type);
    /* Test if *payload is large enough */
    if (pl_len > *len) {
	errno = ENOMEM;
	return -1;
    }

    *src_rank = msg.src_rank;
    *type = msg.type;
    memcpy(msg.buf, payload, pl_len);
    // *len = pl_len; @todo

    return pl_len;

}

int PSIcomm_close(int rank)
{
    PSI_log(-1, "%s(): not yet implemented\n", __func__);

    return -1;
}

int PSIcomm_finalize(void)
{
    PSI_log(-1, "%s(): not yet implemented\n", __func__);

    return -1;
}
