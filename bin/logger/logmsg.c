/*
 *               ParaStation3
 * logmsg.c
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: logmsg.c,v 1.7 2002/01/23 11:28:42 eicker Exp $
 *
 */

static char vcid[] __attribute__ (( unused )) = "$Id: logmsg.c,v 1.7 2002/01/23 11:28:42 eicker Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

#include "logmsg.h"

int writelog(int sock, FLMsg_msg_t type, int node, char *buf, size_t count)
{
    /** @todo
     * This implementation is *not* correct !!
     * What happens if less then msg.header.len bytes are written ?
     */
    int n, sent = 0;
    FLBufferMsg_t msg;

    if(sock > 0){
	msg.header.type = type;
	msg.header.sender = node;
	if(count < 0) return 0;
	do {
	    n = (count>sizeof(msg.buf)) ? sizeof(msg.buf) : count;
	    memcpy(msg.buf, buf, n);
	    msg.header.len = sizeof(msg.header) + n;
	    n = write(sock, &msg, msg.header.len);
	    if (n < 0){
		if (errno == EAGAIN){
		    continue;
		} else {
		    perror("writelog()");
		    return(n);             /* error, return < 0 */
		}
	    }
	    sent += n - sizeof(msg.header);
	    count -= n - sizeof(msg.header);
	    buf += n - sizeof(msg.header);
	} while(sent < count);
    }

    return sent;
}

int printlog(int sock, FLMsg_msg_t type, int node, char *buf)
{
    return writelog(sock, type, node, buf, strlen(buf));
}

int readlog(int sock, FLBufferMsg_t *msg)
{
    int total=-1, n, nleft;
    char *buf=(char *)msg;

    if(sock > 0){
	nleft = sizeof(FLMsg_t);
	total = 0;
	while(nleft > 0){      /* Complete message */
	    n = read(sock, buf, (nleft>SSIZE_MAX)?SSIZE_MAX:nleft);
	    if (n < 0){
		if (errno == EAGAIN){
		    continue;
		} else {
		    perror("readlog()");
		    return(n);             /* error, return < 0 */
		}
	    } else if (n == 0) {
		return n;
	    }
	    nleft -= n;
	    total += n;
	    buf += n;
	}
	nleft = msg->header.len - total;
	while(nleft > 0){      /* Complete message */
	    n = read(sock, buf, (nleft>SSIZE_MAX)?SSIZE_MAX:nleft);
	    if (n < 0){
		if (errno == EAGAIN){
		    continue;
		} else {
		    perror("readlog()");
		    return(n);             /* error, return < 0 */
		}
	    }

	    nleft -= n;
	    total += n;
	    buf += n;
	}
    }else
	errno=EBADF;

    return total;
}
