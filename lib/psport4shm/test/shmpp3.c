/*
  (c) 2003-02-23 Jens Hauke <hauke@wtal.de>

  Wie shmpp2.c aber client simuliert im selben prozess.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <sched.h>
#include <assert.h>
#include <asm/msr.h>
#include <sys/time.h>
#include <unistd.h>


//#define sched_yield() do { ;} while(0)

#define SHM_BUFS 1
#define SHM_BUFLEN (1024 )

#define SHM_MSGTYPE_NONE 0
#define SHM_MSGTYPE_STD	 1

typedef struct shm_msg_s {
    uint32_t len;
    volatile uint32_t msg_type;
} shm_msg_t;

#define SHM_DATA(buf, len) ((char*)(&(buf).header) - len)
typedef struct shm_buf_s {
    uint8_t _data[SHM_BUFLEN];
    shm_msg_t header;
} shm_buf_t;

typedef struct shm_ctrl_s {
    volatile uint8_t	used;
} shm_ctrl_t;

typedef struct shm_com_s {
    shm_buf_t	buf[SHM_BUFS];
    shm_ctrl_t	ctrl[SHM_BUFS];
} shm_com_t;



typedef struct shm_info_s {
    shm_com_t *local_com; /* local */
    shm_com_t *remote_com; /* remote */
    int recv_cur;
    int send_cur;
    int recv_id;
} shm_info_t;

shm_info_t _shm;

#define SCALL(func) do {				\
    if ((func) < 0) {					\
	printf( #func ": %s\n", strerror(errno));	\
	exit(1);					\
    }							\
}while (0)


shm_info_t *shm_initrecv(shm_info_t *shm)
{
    int shmid;
    void *buf;
    
    SCALL(shmid = shmget(/*key*/0, sizeof(shm_com_t), IPC_CREAT | 0777));
    
    printf( "shmget()=%d\n",shmid);
    SCALL((int)(buf = shmat(shmid, 0, 0 /*SHM_RDONLY*/)));

    SCALL(shmctl(shmid, IPC_RMID, NULL)); /* remove shmid after usage */

    memset(buf, 0, sizeof(shm_com_t)); /* init */
    
    shm->recv_id = shmid;
    shm->local_com = (shm_com_t *)buf;
    shm->recv_cur = 0;
    return shm;
}
    
int shm_initsend(shm_info_t *shm, int rem_shmid)
{
    void *buf;
    SCALL((int)(buf = shmat(rem_shmid, 0, 0)));

    shm->remote_com = buf;
    shm->send_cur = 0;
    return 0;
}

unsigned long sendwhile1;
unsigned long sendwhile2;

static inline
void shm_send(shm_info_t *shm, char *buf, int len)
{
    int cur = shm->send_cur;
    unsigned long t1;
    /* wait for unused sendbuffer */
    while (shm->local_com->ctrl[cur].used) sched_yield();
    shm->local_com->ctrl[cur].used = 1;

    /* copy to sharedmem */
    rdtscl(sendwhile1);
    memcpy(SHM_DATA(shm->remote_com->buf[cur], len),
	   buf, len);

    /* Notify the new message */
    shm->remote_com->buf[cur].header.len = len;
    shm->remote_com->buf[cur].header.msg_type = SHM_MSGTYPE_STD;
    shm->send_cur = (shm->send_cur + 1) % SHM_BUFS;
    rdtscl(sendwhile2);
}

void shm_recvstart(shm_info_t *shm, char **buf, int *len)
{
    int cur = shm->recv_cur;
    while (shm->local_com->buf[cur].header.msg_type == SHM_MSGTYPE_NONE)
	sched_yield();
    *len = shm->local_com->buf[cur].header.len;
    *buf = SHM_DATA(shm->local_com->buf[cur], *len);
}

void shm_recvdone(shm_info_t *shm)
{
    int cur = shm->recv_cur;
    shm->local_com->buf[cur].header.msg_type = SHM_MSGTYPE_NONE;
    /* free buffer */
    shm->remote_com->ctrl[cur].used = 0;
    shm->recv_cur = (shm->recv_cur + 1) % SHM_BUFS;
}


#define MIN(a,b)      (((a)<(b))?(a):(b))
#define MAX(a,b)      (((a)>(b))?(a):(b))

static
char *dumpstr(void *buf, int size)
{
    static char *ret=NULL;
    char *tmp;
    int s;
    char *b;
    if (ret) free(ret);
    ret = (char *)malloc(size * 5 + 4);
    tmp = ret;
    s = size; b = (char *)buf;
    for (; s ; s--, b++){
	    tmp += sprintf(tmp, "<%02x>", (unsigned char)*b);
    }
    *tmp++ = '\'';
    s = size; b = (char *)buf;
    for (; s ; s--, b++){
	    *tmp++ = isprint(*b) ? *b: '.';
    }
    *tmp++ = '\'';
    *tmp++ = 0;
    return ret;
}

unsigned long getusec(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec*1000000+tv.tv_usec;
}


double rdts_us;
void rdts_cal(void)
{
    unsigned long t1, t2, rt1, rt2;
    t1 = getusec();
    rdtscl(rt1);
    /* usleep call kapm-idled and slowdown the cpu! */
    while (getusec() - 1000 < t1);
    rdtscl(rt2);
    t2 = getusec();

    t2 -= t1;
    rt2 -= rt1;
    rdts_us = 1.0 * t2 / rt2;
    printf("# %ld usec = %ld rdts, 1 usec = %f\n", t2, rt2, 1 / rdts_us); 
    
}


#define BUFSIZE (SHM_BUFLEN)
#define LOOPS 20
#define PPSIZE 1

int main(int argc, char **argv)
{
    shm_info_t *shm;
    shm_info_t shm1;
    shm_info_t shm2;
    int rem_shmid;
    int cpid;
    char *buf = (char *)malloc(BUFSIZE);

    memset(buf, 42, BUFSIZE);
    assert(PPSIZE <= BUFSIZE);
    assert(BUFSIZE <= SHM_BUFLEN);
    rdts_cal();
	
    shm_initrecv(&shm1);
    shm_initrecv(&shm2);
    
    shm_initsend(&shm1, shm2.recv_id);
    shm_initsend(&shm2, shm1.recv_id);

    sleep(1);
    {
	int i;
	int len;
	unsigned long times[LOOPS];

	shm = &shm1;
	/* server */

	/* Short PingPong */
	for (i = 0; i < LOOPS; i++) {
	    shm_send(shm, buf, PPSIZE);
	    {
		char *buf2;
		int len;
		shm_recvstart(&shm2, &buf2, &len);
		shm_send(&shm2, buf2, len);
		shm_recvdone(&shm2);
	    }
	    shm_recvstart(shm, &buf, &len);
	    shm_recvdone(shm);
	    rdtscl(times[i]);
	}
	shm_send(shm, NULL, 0);
	
	for (i = 0; i < LOOPS-1; i++) {
	    printf("#%3d %ld  %7.2f usec\n", i,
		   times[i+1] - times[i],
		   (times[i+1] - times[i]) * rdts_us);
	}

	printf("STime: %ld\n", sendwhile2 - sendwhile1);
	sleep(1);
    }
    
    return 0;
}


/*
 * Local Variables:
 *  compile-command: "gcc shmpp3.c -g -Wall -W -Wno-unused -o shmpp3"
 * End:
 *
 */
