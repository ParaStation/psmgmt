/*
  (c) 2003-02-23 Jens Hauke <hauke@wtal.de>

  pingpong ueber shm.
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

#define SCALL(func) do {				\
    if ((func) < 0) {					\
	printf( #func ": %s\n", strerror(errno));	\
	exit(1);					\
    }							\
}while (0)

#define MIN(a,b)      (((a)<(b))?(a):(b))
#define MAX(a,b)      (((a)>(b))?(a):(b))


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


#define LOOPS 20

typedef struct shmmem_s {
    uint8_t volatile  ping;
} shmmem_t;

int main(int argc, char **argv)
{
    int shmid;
    int cpid;
    shmmem_t *shm;

    rdts_cal();

    SCALL(shmid = shmget(/*key*/0, sizeof(shmmem_t), IPC_CREAT | 0777));
    SCALL((int)(shm = shmat(shmid, 0, 0 /*SHM_RDONLY*/)));
    SCALL(shmctl(shmid, IPC_RMID, NULL)); /* remove shmid after usage */

    shm->ping = 0;

    cpid = fork();

    sleep(1);
    if (cpid) {
	int i;
	int len;
	unsigned long times[LOOPS];

	/* server */

	/* Short PingPong */
	for (i = 0; i < LOOPS; i++) {
	    shm->ping = 1;
	    while (shm->ping) sched_yield();
	    rdtscl(times[i]);
	}
	
	for (i = 0; i < LOOPS-1; i++) {
	    printf("#%3d %ld  %7.2f usec\n", i,
		   times[i+1] - times[i],
		   (times[i+1] - times[i]) * rdts_us);
	}
	sleep(1);
	kill(cpid, SIGKILL);
    } else {
	/* Short PingPong */
	while (1) {
	    while (!shm->ping) sched_yield();
	    shm->ping = 0;
	}
    }
    
    return 0;
}


/*
 * Local Variables:
 *  compile-command: "gcc shmpp4.c -g -Wall -W -Wno-unused -o shmpp4 && shmpp4"
 * End:
 *
 */
