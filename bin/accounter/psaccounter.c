/*
 *               ParaStation
 *
 * Copyright (C) 2006-2007 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 *
 */
/**
 * \file
 * psaccounter: ParaStation accounting daemon
 *
 * $Id$ 
 *
 * \author
 * Rauh Michael <rauh@par-tec.com>
 * Norbert Eicker <eicker@par-tec.com>
 * Ralph Krotz <krotz@par-tec.com>
 */
#ifndef DOXYGEN_SHOULD_SKIP_THIS
static char vcid[] __attribute__ ((unused)) =
    "$Id$";
#endif				/* DOXYGEN_SHOULD_SKIP_THIS */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <popt.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "pse.h"
#include "psi.h"
#include "psiinfo.h"
#include "psispawn.h"
#include "pscommon.h"
#include "pstask.h"
#include "psprotocol.h"
#include "logging.h"

#define MAX_HOSTNAME_LEN 64
#define EXEC_HOST_SIZE 1024
#define DEFAULT_LOG_DIR "/var/account"

typedef struct {
    char hostname[MAX_HOSTNAME_LEN];
    char *jobname;
    char *exec_hosts;
    size_t exec_hosts_size;
    size_t countSlotMsg;
    PStask_ID_t logger;
    size_t taskSize;
    int uid;
    int gid;
    int queue_time;
    int start_time;
    int end_time;
    float cput;
    size_t countExitMsg;
    int exitStatus;
    struct rusage rusage;
    int incomplete;
} Job_t;


/* tree structur */
struct t_node {
    PStask_ID_t key;
    Job_t *job;
    struct t_node *left;
    struct t_node *right;
};

/* struct for non responding jobs */
typedef struct struct_list {
    PStask_ID_t job;
    struct struct_list *next;
}Joblist;

void daemonize(const char *cmd);
void sig_handler(int sig);
void handleAccQueueMsg(char *chead, char *ptr, PStask_ID_t logger);
void handleAccStartMsg(char *ptr, PStask_ID_t key);
void handleAccDelteMsg(char *chead, PStask_ID_t key);
void handleAccEndMsg(char *chead, char *ptr, PStask_ID_t sender,
		     PStask_ID_t logger);
void handleSlotsMsg(char *chead, DDTypedBufferMsg_t * msg);
void timer_handler();
void printAccEndMsg(char *chead, int TaskId);
void openAccLogFile(char *arg_logdir);

/* binary tree */
struct t_node *insertTNode(PStask_ID_t key, Job_t * job,
			   struct t_node *leaf);
struct t_node *deleteTNode(PStask_ID_t key, struct t_node *leaf);
struct t_node *searchTNode(PStask_ID_t key, struct t_node *leaf);
struct t_node *findMin(struct t_node *leaf);
struct t_node *findMax(struct t_node *leaf);

/* job struct fuctions */
int insertJob(Job_t * job);
int deleteJob(PStask_ID_t key);
Job_t *findJob(PStask_ID_t key);

/* dead jobs functions */
PStask_ID_t getNextdJob();
void insertdJob(PStask_ID_t Job);
int finddJob(PStask_ID_t Job);

/* globals */
FILE *fp;
struct t_node *btroot;
int logTorque;
int debug;
char *logPostProcessing;
logger_t *alogger;
FILE *logfile = NULL;
int edebug = 0;			/* extended debugging msg */
Joblist *dJobs;

#define alog(...) if (alogger) logger_print(alogger, -1, __VA_ARGS__)

void insertdJob(PStask_ID_t Job)
{
    if (!dJobs) { /* empty queue */ 
	if (!(dJobs = malloc(sizeof(Joblist)))) {
	    alog("Out of memory, Exiting\n");
	    exit(1);
	}
	dJobs->next = NULL;
	dJobs->job = Job;
    } else { 
	Joblist *lastJob;
	lastJob = dJobs;
	/* find last job */
	while (lastJob->next != NULL) {
	    lastJob = lastJob->next;
	}
	if (!(lastJob->next = malloc(sizeof(Joblist)))) {
	    alog("Out of memory, Exiting\n");
	    exit(1);
	}
	lastJob = lastJob->next;
	lastJob->next = NULL;
	lastJob->job = Job;
    }
}

int finddJob(PStask_ID_t Job)
{
    if (!dJobs) {
	return 0;
    } else {
	Joblist *sJob;
	sJob = dJobs;
	if (sJob->job == Job) {
	    return 1;
	}
	while (sJob->next != NULL) {
	    sJob = sJob->next;
	    if (sJob->job == Job) {
		return 1;
	    }
	}
	return 0;
    }
}

PStask_ID_t getNextdJob()
{
    if (!dJobs) {
	return 0;
    } else {
	Joblist *nextJob;
	PStask_ID_t jobid;

	nextJob = dJobs;
	jobid = dJobs->job;
	dJobs = dJobs->next;
	free(nextJob);
	return jobid;
    }
}

struct t_node *insertTNode(PStask_ID_t key, Job_t * job,
			   struct t_node *leaf)
{
    struct t_node test;
    struct t_node test2;
    struct t_node *ptr;
    struct t_node **ptrptr;

    ptr = &test;
    ptrptr = &ptr;
    *ptrptr = &test2;

    if (!leaf) {
	leaf = malloc(sizeof(struct t_node));
	if (!leaf) {
	    alog("Out of memory, exiting\n");
	    exit(1);
	}

	leaf->key = key;
	leaf->job = job;

	/* initialize the children to null */
	leaf->left = 0;
	leaf->right = 0;
    } else if (key < leaf->key) {
	leaf->left = insertTNode(key, job, leaf->left);
    } else if (key > leaf->key) {
	leaf->right = insertTNode(key, job, leaf->right);
    }
    return leaf;
}


struct t_node *findMin(struct t_node *leaf)
{
    if (!leaf) {
	return NULL;
    } else if (!leaf->left) {
	return leaf;
    } else {
	return findMin(leaf->left);
    }
}


struct t_node *findMax(struct t_node *leaf)
{
    if (!leaf) {
	return NULL;
    } else if (!leaf->right) {
	return leaf;
    } else {
	return findMax(leaf->right);
    }
}

struct t_node *deleteTNode(PStask_ID_t key, struct t_node *leaf)
{
    struct t_node *tmpleaf;
    if (!leaf) {
	return 0;
    } else if (key < leaf->key) {
	leaf->left = deleteTNode(key, leaf->left);
    } else if (key > leaf->key) {
	leaf->right = deleteTNode(key, leaf->right);
    } else if (leaf->left && leaf->right) {
        Job_t *tmpjob;
	tmpleaf = findMin(leaf->right);
	leaf->key = tmpleaf->key;
	tmpjob = leaf->job;
	leaf->job = tmpleaf->job;
	tmpleaf->job = tmpjob;
	leaf->right = deleteTNode(leaf->key, leaf->right);
    } else {
	tmpleaf = leaf;
	if (!leaf->left) {
	    leaf = leaf->right;
	} else if (!leaf->right) {
	    leaf = leaf->left;
	}
	if (tmpleaf->job->jobname) {
	    free(tmpleaf->job->jobname);
	}
	if (tmpleaf->job->exec_hosts) {
	    free(tmpleaf->job->exec_hosts);
	}
	free(tmpleaf->job);
	free(tmpleaf);
    }
    return leaf;
}

struct t_node *searchTNode(PStask_ID_t key, struct t_node *leaf)
{
    if (leaf != 0) {
	if (key == leaf->key) {
	    return leaf;
	} else if (key < leaf->key) {
	    return searchTNode(key, leaf->left);
	} else {
	    return searchTNode(key, leaf->right);
	}
    } else
	return 0;
}

int insertJob(Job_t * job)
{
    btroot = insertTNode(job->logger, job, btroot);
    return 1;
}

int deleteJob(PStask_ID_t key)
{
    btroot = deleteTNode(key, btroot);
    return 1;
}


Job_t *findJob(PStask_ID_t key)
{
    struct t_node *leaf;
    leaf = searchTNode(key, btroot);
    if (leaf) {
	return leaf->job;
    } else {
	return 0;
    }
}

void sig_handler(int sig)
{
    if (sig == SIGTERM) {
	alog("Caught the term signal, exiting\n");
	exit(0);
    }

    if (sig == SIGINT) {
	exit(0);
    }

}

void timer_handler()
{
    PStask_ID_t tid;
    struct itimerval timer;
    
    if (!(tid = getNextdJob())) {
	return;
    }
    Job_t *job = findJob(tid);

    if (!job) {
	if (debug) {
	    alog("Timer AccEndMsg to non existing Job:%i\n", tid);
	}
    } else {
	time_t atime;
	struct tm *ptm;
	char ctime[100];
	char chead[300];
        
        /* check if all childs terminated */
	if (job->countExitMsg != job->taskSize) {
	    job->incomplete = 1;
	    job->end_time = time(NULL);
	}

	/* Create Header */
	atime = time(NULL);
	ptm = localtime(&atime);
	strftime(ctime, sizeof(ctime), "%d/%m/%Y %H:%M:%S", ptm);
	snprintf(chead, sizeof(chead), "%s;%s;%i.%s", ctime, "E",
		 PSC_getPID(job->logger), job->hostname);

	/* print the msg */
	printAccEndMsg(chead, tid);
    }

    /* start timer if jobs pending */
    if (dJobs) {
	/* Configure the timer to expire after 60 sec */
	timer.it_value.tv_sec = 60;
	timer.it_value.tv_usec = 0;

	/* ... not anymore after that. */
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;

	/* Start a timer. */
	setitimer(ITIMER_REAL, &timer, NULL);
    }	
}


void printAccEndMsg(char *chead, PStask_ID_t key)
{

    Job_t *job = findJob(key);
    int ccopy, chour, cmin, csec, wspan, whour, wmin, wsec, pagesize;
    long rss;
    char used_mem[500] = { "" };
    char info[500] = { "" };
    struct passwd *spasswd;
    struct group *sgroup;

    if (!job) {
	alog("AccEndMsg to non existing Job:%i\n", key);
	return;
    }

    spasswd = getpwuid(job->uid);
    sgroup = getgrgid(job->gid);

    /* calc cputime */
    ccopy = job->cput;
    chour = ccopy / 3600;
    ccopy = ccopy % 3600;
    cmin = ccopy / 60;
    csec = ccopy % 60;

    /* calc walltime */
    if (!job->end_time) {
	job->end_time = time(NULL);
	job->incomplete = 1;
    }
    if (!job->end_time || !job->start_time || job->start_time > job->end_time) {
	whour = 0;
	wmin = 0;
	wsec = 0;
	job->incomplete = 1;
    } else {
	wspan = job->end_time - job->start_time;
	whour = wspan / 3600;
	wspan = wspan % 3600;
	wmin = wspan / 60;
	wsec = wspan % 60;
    }

    if (job->incomplete) {
	snprintf(info, sizeof(info), "info=incomplete ");
    }
    
    /* calc used mem */
    pagesize = getpagesize();
    rss = (job->rusage.ru_maxrss * pagesize) / 1024;
    if (rss) {
	snprintf(used_mem, sizeof(used_mem), "resources_used.mem=%ldkb ", rss);
    }

    fprintf(fp,
	    "%s.%s;user=%s group=%s %sjobname=%s queue=batch ctime=%i qtime=%i etime=%i start=%i exec_host=%s end=%i Exit_status=%i resources_used.cput=%02i:%02i:%02i %sresources_used.walltime=%02i:%02i:%02i\n",
	    chead, job->hostname, spasswd->pw_name, sgroup->gr_name, info, job->jobname,
	    job->queue_time, job->queue_time, job->queue_time,
	    job->start_time, job->exec_hosts, job->end_time,
	    job->exitStatus, chour, cmin, csec, used_mem, whour, wmin,
	    wsec);

    fflush(fp);
    if (edebug) {
	alog("Processed acc end msg, deleting job\n");
    }
    if (!deleteJob(key)) {
	alog("Error Deleting Job: Possible memory leak\n");
    }

}

void handleAccQueueMsg(char *chead, char *ptr, PStask_ID_t logger)
{
    Job_t *job;
    struct in_addr senderIP;
    struct hostent *hostName;

    if (!(job = malloc(sizeof(Job_t)))) {
	alog("Out of memory, exiting\n");
	exit(1);
    }

    job->cput = 0;
    job->countExitMsg = 0;
    job->countSlotMsg = 0;
    job->incomplete = 0;
    job->start_time = 0;
    job->end_time = 0;
    job->exec_hosts_size = EXEC_HOST_SIZE;
    job->jobname = NULL;
    if (!(job->exec_hosts = malloc(EXEC_HOST_SIZE))) {
        alog("Out of memory, exiting\n");
	exit(1);
    }
    job->exec_hosts[0] = '\0';   
    
    /* Task(logger) Id */
    job->logger = logger;

    /* current rank */
    ptr += sizeof(int32_t);

    /* childs uid */
    job->uid = *(uid_t *) ptr;
    ptr += sizeof(uid_t);

    /* childs gid */
    job->gid = *(gid_t *) ptr;
    ptr += sizeof(gid_t);

    /* total number of childs. Only the logger knows this */
    ptr += sizeof(int32_t);

    /* ip address */
    senderIP.s_addr = *(uint32_t *) ptr;
    hostName =
	gethostbyaddr(&senderIP.s_addr, sizeof(senderIP.s_addr), AF_INET);
    if (hostName) {
	strncpy(job->hostname, hostName->h_name, MAX_HOSTNAME_LEN - 1);
    } else {
	strcpy(job->hostname, "unknown");
	alog("Couldn't resolve hostName from ip:%s\n",
	     inet_ntoa(senderIP));
    }

    ptr += sizeof(int32_t);

    job->queue_time = time(NULL);

    if (!insertJob(job)) {
	alog("Error caching job, exiting\n");
	exit(1);
    }

    if (logTorque) {
	fprintf(fp, "%s.%s;queue=batch\n", chead, job->hostname);
    }

    if (edebug) {
	alog("processed acc queue msg\n");
    }
}


void handleAccStartMsg(char *ptr, PStask_ID_t key)
{

    Job_t *job = findJob(key);
    struct passwd *spasswd;
    struct group *sgroup;

    if (!job) {
	alog("AccStartMsg to non existing Job:%i\n", key);
	return;
    }

    /* current rank */
    ptr += sizeof(int32_t);

    /* childs uid */
    ptr += sizeof(uid_t);
    spasswd = getpwuid(job->uid);

    /* childs gid */
    ptr += sizeof(gid_t);
    sgroup = getgrgid(job->gid);

    /* total number of childs. Only the logger knows this */
    job->taskSize = *(int32_t *) ptr;
    ptr += sizeof(int32_t);

    job->start_time = time(NULL);

    if (edebug) {
	alog("processed acc start msg\n");
    }

}

void handleAccDeleteMsg(char *chead, PStask_ID_t key)
{
    Job_t *job = findJob(key);
    
    if (logTorque) {
	fprintf(fp, "%s.%s\n", chead, job->hostname);
    }
    if (!deleteJob(key)) {
	alog("Job could not be deleted:%i\n", key);
    }

    if (edebug) {
	alog("processed acc delete msg\n");
    }
}


void handleAccEndMsg(char *chead, char *ptr, PStask_ID_t sender,
		     PStask_ID_t logger)
{

    Job_t *job = findJob(logger);

    if (!job) {
	alog("AccEndMsg to non existing Job:%i\n", logger);
	return;
    } else {
	struct passwd *spasswd;
	struct group *sgroup;

	/* current rank */
	ptr += sizeof(int32_t);

	/* childs uid */
	ptr += sizeof(uid_t);
	spasswd = getpwuid(job->uid);

	/* childs gid */
	ptr += sizeof(gid_t);
	sgroup = getgrgid(job->gid);

	/* total number of childs. Only the logger knows this */
	ptr += sizeof(int32_t);

	/* ip address */
	ptr += sizeof(int32_t);

	/* actual rusage structure */
	memcpy(&(job->rusage), ptr, sizeof(job->rusage));
	ptr += sizeof(job->rusage);

	/* exit status */
	job->exitStatus = *(int32_t *) ptr;
	ptr += sizeof(int32_t);

	/* check if logger is alive */
	PSI_kill(job->logger, 0, 1);

	if (sender == logger) {

	    /* check if all childs terminated */
	    if (job->countExitMsg < job->taskSize) {
		struct itimerval timer;

		/* start timer if not yet active */
		if (!dJobs) {
		
		    /* Configure the timer to expire after 30 sec */
		    timer.it_value.tv_sec = 60;
		    timer.it_value.tv_usec = 0;

		    /* ... not anymore after that. */
		    timer.it_interval.tv_sec = 0;
		    timer.it_interval.tv_usec = 0;

		    /* Start a timer. */
		    setitimer(ITIMER_REAL, &timer, NULL);
		}	
		
		if (!finddJob(logger)) {
		    insertdJob(logger);
		    alog("Waiting for all childs to exit on job:%i\n",logger);
		}

	    } else {
		printAccEndMsg(chead, logger);
	    }

	} else {
	    int joblen = strlen(ptr);	
	    if (job->countExitMsg == 0) {
		if(!(job->jobname = malloc(joblen +1 ))) {
		    alog("Out of memory, exiting\n");
		    exit(1);
		}
		strncpy(job->jobname, ptr, joblen);
		job->jobname[joblen] = '\0';
	    }

	    job->countExitMsg++;
	    job->cput +=
		job->rusage.ru_utime.tv_sec +
		1.0e-6 * job->rusage.ru_utime.tv_usec +
		job->rusage.ru_stime.tv_sec +
		1.0e-6 * job->rusage.ru_stime.tv_usec;

	    /* all jobs terminated -> set end time */
	    if (job->countExitMsg == job->taskSize) {
		job->end_time = time(NULL);
	    }
	}

    }
    if (edebug) {
	alog("processed acc end msg\n");
    }
}


void handleAcctMsg(DDTypedBufferMsg_t * msg)
{
    char *ptr = msg->buf;
    time_t atime;
    struct tm *ptm;
    char ctime[100];
    char chead[300];
    PStask_ID_t sender = msg->header.sender, logger;

    if (edebug) {
	alog("processing acc msg\n");
    }

    /* logger's TID, this identifies a task uniquely */
    logger = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    /* Create Header for all Msg */
    atime = time(NULL);
    ptm = localtime(&atime);
    strftime(ctime, sizeof(ctime), "%d/%m/%Y %H:%M:%S", ptm);

    snprintf(chead, sizeof(chead), "%s;%s;%i", ctime,
	     msg->type == PSP_ACCOUNT_QUEUE ? "Q" :
	     msg->type == PSP_ACCOUNT_START ? "S" :
	     msg->type == PSP_ACCOUNT_SLOTS ? "S" :
	     msg->type == PSP_ACCOUNT_DELETE ? "D" :
	     msg->type == PSP_ACCOUNT_END ? "E" : "?",
	     PSC_getPID(logger));

    if (debug) {
	alog("Received new acc msg: type:%d, sender:%s, logger:%s\n",
	     msg->type, PSC_printTID(sender), PSC_printTID(logger));
    }

    switch (msg->type) {
    case PSP_ACCOUNT_QUEUE:
	handleAccQueueMsg(chead, ptr, logger);
	break;
    case PSP_ACCOUNT_START:
	handleAccStartMsg(ptr, logger);
	break;
    case PSP_ACCOUNT_DELETE:
	handleAccDeleteMsg(chead, logger);
	break;
    case PSP_ACCOUNT_END:
	handleAccEndMsg(chead, ptr, sender, logger);
	break;
    case PSP_ACCOUNT_SLOTS:
	handleSlotsMsg(chead, msg);
	break;
    default:
	alog("Unknown Accounting Message: Type=%d\n", msg->type);
    }
}

void handleSigMsg(DDErrorMsg_t * msg)
{
    struct itimerval timer;
    Job_t *job;

    if (debug) {
	char *errstr = strerror(msg->error);
	if (!errstr) {
	    errstr = "UNKNOWN";
	}
	alog("%s: msg from %s:", __func__,
	     PSC_printTID(msg->header.sender));
	alog(" task %s: %s\n", PSC_printTID(msg->request), errstr);
    }
    job = findJob(msg->request);

    /* check if logger replied */
    if (msg->error == ESRCH && job) {

	if (!dJobs) {
	    /* Configure the timer to expire after 60 sec */
	    timer.it_value.tv_sec = 60;
	    timer.it_value.tv_usec = 0;

	    /* ... not anymore after that. */
	    timer.it_interval.tv_sec = 0;
	    timer.it_interval.tv_usec = 0;

	    /* Start a timer. */
	    setitimer(ITIMER_REAL, &timer, NULL);
	}
	
	if (!finddJob(msg->request)) {
	    insertdJob(msg->request);
	    alog("logger died, error:%i\n", msg->error);
	}
    }
}


void handleSlotsMsg(char *chead, DDTypedBufferMsg_t * msg)
{
    char *ptr = msg->buf;
    
    PStask_ID_t logger;
    unsigned int numSlots, slot;
    char sep[2] = "";
    struct hostent *hostName;
    Job_t *job;

    if (edebug) {
	alog("processing slot msg\n");
    }

    logger = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);
    numSlots = *(uint32_t *) ptr;
    ptr += sizeof(uint32_t);

    job = findJob(logger);

    if (!job) {
	alog("AccSlotMsg to non existing Job:%i\n", logger);
	return;
    }

    for (slot = 0; slot < numSlots; slot++) {
	struct in_addr slotIP;
	int cpu, bufleft;
	char ctmphost[400];
        
	slotIP.s_addr = *(uint32_t *) ptr;
	ptr += sizeof(uint32_t);
	cpu = *(int32_t *) ptr;
	ptr += sizeof(int32_t);

        if (job->countSlotMsg) {
            strcpy(sep, "+");
        }
	
	job->countSlotMsg++;
	
	hostName =
	    gethostbyaddr(&slotIP.s_addr, sizeof(slotIP.s_addr), AF_INET);
        
	if (!hostName) {
	    alog("Couldn't resolve hostName from ip:%s\n",
                 inet_ntoa(slotIP));
	    snprintf(ctmphost,sizeof(ctmphost),"%s%s/%d",
		     sep, inet_ntoa(slotIP), (int) job->countSlotMsg);
        } else {
	    snprintf(ctmphost,sizeof(ctmphost),"%s%s/%d",
		     sep, hostName->h_name, (int) job->countSlotMsg); 

        }
    
        if (strlen(job->exec_hosts) + strlen(ctmphost) + 1 >
            job->exec_hosts_size) {
            char *tmpjob = job->exec_hosts;
            job->exec_hosts =
                malloc(job->exec_hosts_size + EXEC_HOST_SIZE);
            if (!job->exec_hosts) {
                alog("Out of memory, exiting\n");
                exit(1);
            }
            job->exec_hosts_size += EXEC_HOST_SIZE;
            strncpy(job->exec_hosts, tmpjob, job->exec_hosts_size);
            if (tmpjob) {
                free(tmpjob);
            }
        }
	
	bufleft = job->exec_hosts_size - strlen(job->exec_hosts); 
        char *cptr = job->exec_hosts;
        cptr += strlen(job->exec_hosts);
        snprintf(cptr, bufleft, "%s", ctmphost);
    }


    if (job->countSlotMsg == job->taskSize) {

	struct passwd *spasswd;
	struct group *sgroup;
	
        spasswd = getpwuid(job->uid);
	sgroup = getgrgid(job->gid);

	if (logTorque) {
	    fprintf(fp,
		    "%s.%s;user=%s group=%s queue=batch ctime=%i qtime=%i etime=%i start=%i exec_host=%s\n",
		    chead, job->hostname, spasswd->pw_name, sgroup->gr_name,
		    job->queue_time, job->queue_time, job->queue_time,
		    job->start_time, job->exec_hosts);
	}
	if (edebug) {
	    alog("handled all slot msgs\n");
	}
    }

}


void loop(char *arg_logdir)
{
    while (1) {
	DDTypedBufferMsg_t msg;

	int ret = PSI_recvMsg(&msg);

	if (ret < 0 && errno == EINTR) {
	    continue;
	}

	if (ret < 0 && errno != EINTR) {
	    /* Problem with daemon */
	    alog("\nError receiving messages, the daemon died: Exiting Error:%i errno:%i\n", ret, errno);
	    exit(1);
	}

	/* open log file */
	openAccLogFile(arg_logdir);

	switch (msg.header.type) {
	case PSP_CD_ACCOUNT:
	    handleAcctMsg(&msg);
	    break;
	case PSP_CD_SIGRES:
	    handleSigMsg((DDErrorMsg_t *) & msg);
	    break;
	default:
	    alog("Unknown message: %x\n", msg.header.type);
	}

	if (logfile) {
	    fflush(logfile);
	}
    }

    PSE_finalize();
}

void openAccLogFile(char *arg_logdir)
{
    char filename[200];
    static char oldfilename[200];
    char alogfile[600];
    time_t t;
    struct tm *tmp;

    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
	perror("localtime");
	exit(EXIT_FAILURE);
    }

    strftime(filename, sizeof(filename), "%Y%m%d", tmp);

    if ((arg_logdir && fp && !strcmp(arg_logdir, "-"))
	|| (fp && !strcmp(filename, oldfilename))) {
	return;
    }

    /* next day, open new log file */
    if (fp && !!strcmp(filename, oldfilename)) {
	fclose(fp);
	if (logPostProcessing) {
	    char syscmd[1024];
	    snprintf(syscmd, sizeof(syscmd), "%s ", logPostProcessing);
	    if (arg_logdir) {
		strncat(syscmd, arg_logdir, sizeof(syscmd) - strlen(syscmd) - 1);
	    } else {
		strncat(syscmd, DEFAULT_LOG_DIR,
			sizeof(syscmd) - strlen(syscmd) - 1);
	    }
	    strncat(syscmd, "/", sizeof(syscmd) - strlen(syscmd) - 1);
	    strncat(syscmd, oldfilename, sizeof(syscmd) - strlen(syscmd) - 1);
	    system(syscmd);
	}

    }

    if (arg_logdir) {
	if (!strcmp(arg_logdir, "-")) {
	    fp = stdout;
	} else {
	    snprintf(alogfile, sizeof(alogfile), "%s/%s", arg_logdir, filename);
	    fp = fopen(alogfile, "a+");
	}
    } else {
	snprintf(alogfile, sizeof(alogfile), "%s/%s", DEFAULT_LOG_DIR, filename);
	fp = fopen(alogfile, "a+");
    }

    if (!fp) {
	if (arg_logdir) {
	    if (!strcmp(arg_logdir, "-")) {
		alog("error writing to stdout, Exiting\n");
	    } else {
		alog("error writing to log file: %s/%s, Exiting\n",
		     arg_logdir, filename);
	    }
	} else {
	    alog("error writing to log file: %s/%s, Exiting\n",
		 DEFAULT_LOG_DIR, filename);
	}
	exit(1);
    }
    strncpy(oldfilename, filename, sizeof(oldfilename));

}

static void printVersion(void)
{
    char revision[] = "$Revision$";
    fprintf(stderr, "psaccounter %s\b \n", revision+11);
}

int main(int argc, char *argv[])
{
    poptContext optCon;		/* context for parsing command-line options */
    int rc, version = 0;
    char *arg_logdir = NULL;
    char *arg_logfile = NULL;
    int arg_nodaemon = 0;

    struct poptOption optionsTable[] = {
	{"extend", 'e', POPT_ARG_NONE,
	 &logTorque, 0, "extended logging", "flag"},
	{"debug", 'd', POPT_ARG_NONE,
	 &debug, 0, "output debug messages", "flag"},
	{"foreground", 'F', POPT_ARG_NONE,
	 &arg_nodaemon, 0, "don't fork into background", "flag"},
	{"logdir", 'l', POPT_ARG_STRING, &arg_logdir, 0,
	 "accouting log dir", "directory"},
        { "version", 'v', POPT_ARG_NONE, &version, 0,
          "output version information and exit", NULL},
	{"logfile", 'f', POPT_ARG_STRING, &arg_logfile, 0,
	 "log file for debug and error logging", "file"},
	{"logpro", 'p', POPT_ARG_STRING, &logPostProcessing, 0,
	 "acc log file post processing cmd", "cmd"},
	POPT_AUTOHELP {NULL, '\0', 0, NULL, 0, NULL, NULL}
    };
    
    signal(SIGALRM, &timer_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    optCon =
	poptGetContext(NULL, argc, (const char **) argv, optionsTable, 0);
    rc = poptGetNextOpt(optCon);

    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	fprintf(stderr, "%s: %s\n",
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));
	return 1;
    }

    if (version) {
	printVersion();
	return 0;
    }

    /* Become a daemon */
    if (!arg_nodaemon) {
	daemonize("psaccounter");
    }


    /* init PSI */
    if (!PSI_initClient(TG_ACCOUNT)) {
	printf("%s", "Initialization of PSI failed\n");
	exit(1);
    }

    /* logging */
    if (arg_logfile && !!strcmp(arg_logfile, "-")) {
	logfile = fopen(arg_logfile, "a+");
    }

    if (arg_logfile && !strcmp(arg_logfile, "-")) {
	logfile = stdout;
    }
    alogger = logger_init("PSACC", logfile);

    /* init */
    btroot = 0;
    fp = 0;
    dJobs = 0;

    /* main loop */
    loop(arg_logdir);

    return 0;
}


void daemonize(const char *cmd)
{
    unsigned int i;
    int pid, fd0, fd1, fd2; 
    struct sigaction sa;
    struct rlimit rl;

    /* Clear umask */
    umask(0);

    /* Become a session leader to lose TTY */
    if ((pid = fork()) < 0) {
	printf("unable to fork server process\n");
	exit(1);
    } else if (pid != 0) { /* parent */
	exit(0);
    }
    setsid();

    /* Ensure future opens won´t allocate controlling TTYs */
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGHUP, &sa, NULL) < 0) {
	printf("Can´t ignore SIGHUP");
	exit(1);
    } 
    if ((pid = fork()) < 0) {
	printf("unable to fork server process\n");
	exit(1);
    } else if (pid != 0) { /* parent */
	exit(0);
    }

    /* Change working dir to root */
    if (chdir("/") < 0) {
	printf("Unable to change directory to /\n");
	exit(1);
    }

    /* Close all open file descriptors */
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
	printf("Can´t get file limit\n");
	exit(1);
    }
    if (rl.rlim_max == RLIM_INFINITY) {
	rl.rlim_max = 1024;
    }
    for (i=0; i < rl.rlim_max; i++) {
	close(i);
    }

    /* Attach file descriptors 0, 1, 2 to /dev/null */
    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(0);
    fd2 = dup(0);

    /* Init log file */
    openlog(cmd, LOG_PID | LOG_CONS, LOG_DAEMON);
    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
	syslog(LOG_ERR, "unexpected file descriptors %d %d %d",
		fd0, fd1, fd2);
	exit(1);
    }

}



/*
 * Local Variables:
 *  compile-command: "make psaccounter"
 * End
 */
