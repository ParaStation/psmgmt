#ifndef __LOGMSG_H
#define __LOGMSG_H

/*----------------------------------------------------------------------*/
/* Forwarder-Logger Protocol Message Types                              */
/*----------------------------------------------------------------------*/

enum msg_type {INITIALIZE, STDOUT, STDERR, FINALIZE, EXIT};

typedef struct{
    int len;
    enum msg_type type;
    int sender;
} FLMsg_t;

/* Init Message */
typedef struct{
    FLMsg_t header;   /* header of the message */
    int verbose;   /* buffer for Message */
} FLInitMsg_t;

/* Untyped Buffer Message */
typedef struct{
    FLMsg_t header;   /* header of the message */
    char buf[2048];   /* buffer for Message */
} FLBufferMsg_t;

void writelog(int sock, enum msg_type type, int node, char *buf, size_t count);

void printlog(int sock, enum msg_type type, int node, char *buf);

int readlog(int sock, FLBufferMsg_t *msg);


#endif
