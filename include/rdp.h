/*
 * ParaStation Reliable Datagram Protocol
 *
 * file: rdp.h
 *
 * (C) 1999 ParTec AG Karlsruhe
 *     written by Dr. Thomas M. Warschko
 *
 * Dec-99 V1.0: initial implementation
 *
 */

typedef enum {CINVAL=0x0, CLOSED=0x1, SYN_SENT=0x2, SYN_RECVD=0x3, ACTIVE=0x4} RDPState;

typedef struct RDPLoad_ {
  double	load[3];	/* Load parameters of that node */
} RDPLoad;

typedef struct RDP_ConInfo_ {
  RDPState	state;
  RDPLoad	load;
  int	 	misscounter;
} RDP_ConInfo;

#define T_INFO		0x01
#define T_CLOSE		0x02
#define T_LIC		0x10
#define T_KILL		0x20

typedef struct Mmsg_ {
  short         node;           /* Sender ID */
  short         type;
  RDPState      state;
  RDPLoad       load;
} Mmsg;   

typedef struct RDP_Deadbuf_{
  int dst;
  void *buf;
  int buflen;
} RDP_Deadbuf;


/*
 * Get Info for node n
 */
void RDP_GetInfo(int n, RDP_ConInfo *info);

/*
 * Set/Get Dead Limit (-1 == Get, else Set)
 */
int RDP_DeadLimit(int limit);

/*
 * Initialize RDP
 * Parameters:  1) nodes: Nr of Nodes in the Cluster
 *              2) mgroup: Id of Multicastgroup (0 < id < 255)
 *              3) usesyslog (1=yes,0=no): Use syslog() to log error/info messages
 *              4) address of callback function to handle RDP exceptions / infos
 *                 (func == NULL allowed to prevent callback) 
 *                 callback func is called with (type, void *buf)
 */
int RDPinit(int nodes, int mgroup, int usesyslog, void (*func)(int, void*));
int RDPMCASTinit(int nodes, int mgroup, char *ifname, int interface, int usesyslog, void (*func)(int, void*));

#define RDP_NEW_CONNECTION	0x1	/* buf == nodeno */
#define RDP_LOST_CONNECTION	0x2	/* buf == nodeno */
#define RDP_PKT_UNDELIVERABLE	0x3	/* buf == (dst,*buf,buflen) */

#define RDP_LIC_LOST		0x10	/* buf == sinaddr */ 
#define RDP_LIC_SHUTDOWN	0x20	/* buf == reason */ 

#define LIC_LOST_CONECTION	0x1
#define LIC_KILL_MSG		0x2


/*
 * Shutdown RDP
 */
void RDPexit(void);

/*
 * Sent a msg[buf:len] to node <node> reliable
 */
int Rsendto(int node, void *buf, int len);

/*
 * RDP select() call
 */
int Rselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds, struct timeval *timeout);
int Mselect(int n, fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds, struct timeval *timeout);

/*
 * receive a RDP packet 
 * Parameters:	node: source node of msg (O)
 *              msg:  pointer to msg buffer (O)
 *              len:  max lenght of buffer (I)
 * Retval:	lenght of received msg (or -1 on error, errno is set)
 */
int Rrecvfrom(int *node, void *msg, int len);

/*
 * Get (local) Load values
 */
RDPLoad RDPGetMyLoad(void);

/*
 * Set DebugLevel
 */
int RDP_SetDBGLevel(int level);

/*
 * Set Log Msg
 */
void RDP_SetLogMsg(int level);

void RDP_StateInfo(int n, char *s);

