#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

#include "logmsg.h"


void writelog(int sock, enum msg_type type, int node, char *buf, size_t count)
{
    FLBufferMsg_t msg;

    if(sock > 0){
	msg.header.type = type;
	msg.header.sender = node;
	if(count < 0) return;
	while(count > sizeof(msg.buf)){
	    bcopy(buf, msg.buf, sizeof(msg.buf));
	    msg.header.len = sizeof(msg.header) + sizeof(msg.buf);
	    write(sock, &msg, msg.header.len);
	    count -= sizeof(msg.buf);
	    buf += sizeof(msg.buf);
	}
	bcopy(buf, msg.buf, count);
	msg.header.len = sizeof(msg.header) + count;
	write(sock, &msg, msg.header.len);
    }
}

void printlog(int sock, enum msg_type type, int node, char *buf)
{
    writelog(sock, type, node, buf, strlen(buf));
}

int readlog(int sock, FLBufferMsg_t *msg)
{
    int total=-1, n, nleft;
    char *buf=(char *)msg;

    if(sock > 0){
	/* Fixme: What happens, if the last message sticks out of the buffer */
	/*        Hopefully this problem won't occur with RDP */
	/* For now we're filling the buffer only half in the first read(),
	   hoping the last message is smaller than sizeof(*msg)/2 */
	n=read(sock, buf, (sizeof(*msg)/2>SSIZE_MAX)?SSIZE_MAX:sizeof(*msg)/2);
	total=n;
	if(n<=0) return n;
	nleft = msg->header.len-n;
	while(nleft < 0){      /* More than one message received */
	    buf += msg->header.len;  /* Skip to next message */
	    n -= msg->header.len;
	    msg = (FLBufferMsg_t *)buf;
	    nleft += msg->header.len;
	}
	buf += n;
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
