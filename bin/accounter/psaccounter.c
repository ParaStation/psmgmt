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
 * Michael Rauh <rauh@par-tec.com>
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
#include "pscpu.h"
#include "psprotocol.h"
#include "logging.h"

#define MAX_HOSTNAME_LEN 64
#define EXEC_HOST_SIZE 1024
#define DEFAULT_LOG_DIR "/var/account"
#define ERROR_WAIT_TIME	60

/* job structure */
typedef struct {
    char hostname[MAX_HOSTNAME_LEN];
    char *jobname;
    char *jobid;
    char *exec_hosts;
    size_t exec_hosts_size;
    size_t countSlotMsg;
    PStask_ID_t logger;
    size_t taskSize;
    size_t startCount;
    int uid;
    int gid;
    int queue_time;
    int start_time;
    int end_time;
    int session;
    float cput;
    size_t countExitMsg;
    int exitStatus;
    struct rusage rusage;
    long maxrss;
    ulong maxvsize;
    ulong threads;
    int incomplete;
} Job_t;

/* binary tree structur */
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

/* globals */
FILE *fp;
struct t_node *btroot;
int extendedLogging;
int debug;
char *logPostProcessing;
logger_t *alogger;
FILE *logfile = NULL;
int edebug = 0;			/* extended debugging msg */
Joblist *dJobs;

#define alog(...) if (alogger) logger_print(alogger, -1, __VA_ARGS__)

static void insertdJob(PStask_ID_t Job)
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

static int finddJob(PStask_ID_t Job)
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

static PStask_ID_t getNextdJob()
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

static struct t_node *insertTNode(PStask_ID_t key, Job_t * job,
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

static struct t_node *findMin(struct t_node *leaf)
{
    if (!leaf) {
	return NULL;
    } else if (!leaf->left) {
	return leaf;
    } else {
	return findMin(leaf->left);
    }
}

static struct t_node *findMax(struct t_node *leaf)
{
    if (!leaf) {
	return NULL;
    } else if (!leaf->right) {
	return leaf;
    } else {
	return findMax(leaf->right);
    }
}

static struct t_node *deleteTNode(PStask_ID_t key, struct t_node *leaf)
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
	if (tmpleaf->job->jobid) {
	    free(tmpleaf->job->jobid);
	}
	if (tmpleaf->job->exec_hosts) {
	    free(tmpleaf->job->exec_hosts);
	}
	free(tmpleaf->job);
	free(tmpleaf);
    }
    return leaf;
}

static struct t_node *searchTNode(PStask_ID_t key, struct t_node *leaf)
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

static int insertJob(Job_t * job)
{
    btroot = insertTNode(job->logger, job, btroot);
    return 1;
}

static int deleteJob(PStask_ID_t key)
{
    btroot = deleteTNode(key, btroot);
    return 1;
}

static Job_t *findJob(PStask_ID_t key)
{
    struct t_node *leaf;
    leaf = searchTNode(key, btroot);
    if (leaf) {
	return leaf->job;
    } else {
	return 0;
    }
}

/**
 * @brief Signal Handler
 *
 * @param sig Signal to handle.
 */
static void sig_handler(int sig)
{
    /* ignore term signals */
    if (sig == SIGTERM) {
	if(debug) alog("Caught the term signal.\n");
    }

    if (sig == SIGINT) {
	if(debug) alog("Caught the SIGINT.\n");
	exit(0);
    }
}

/**
 * @brief Print Account End Messages.
 *
 * @param chead Header of the log entry.
 *
 * @param key The job id (equals the taskID of the logger).
 */
static void printAccEndMsg(char *chead, PStask_ID_t key)
{
    Job_t *job = findJob(key);
    int ccopy, chour, cmin, csec, wspan, whour, wmin, wsec, pagesize;
    long rss = 0;
    char used_mem[500] = { "" };
    char used_vmem[500] = { "" };
    char used_threads[500] = { "" };
    char ljobid[500] = { "" };
    char session[100] = { "" };
    char info[500] = { "" };
    struct passwd *spasswd;
    struct group *sgroup;

    if (!job) {
	alog("AccEndMsg to non existing Job:%i\n", key);
	return;
    }

    if (!(spasswd = getpwuid(job->uid))) {
	alog("%s: getting username failed, invalid user id:%i\n", __func__, job->uid);
    }
    if (!(sgroup = getgrgid(job->gid))) {
	alog("%s: getting groupname failed, invalid group id:%i\n", __func__, job->gid);
    }

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
    if (!job->start_time || job->start_time > job->end_time) {
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
    if ((pagesize = getpagesize())) {
	rss = (job->maxrss * pagesize) / 1024;
	if (rss) {
	    snprintf(used_mem, sizeof(used_mem), "resources_used.mem=%ldkb ", rss);
	}
    }

    /* calc used vmem */
    if (job->maxvsize) {
	snprintf(used_vmem, sizeof(used_vmem), "resources_used.vmem=%ldkb ", 
		    (job->maxvsize / 1024));
    }

    /* set number of threads */
    if (job->threads) {
	snprintf(used_threads, sizeof(used_threads), " resources_used.threads=%lu ", 
		    (job->threads));
    }
   
    /* set up session information */
    if (job->session) {
	snprintf(session, sizeof(session), "session=%i ", 
		    job->session);
    }
    
    /* set up job id */
    if (job->jobid && job->jobid[0] != '\0') {
	snprintf(ljobid, sizeof(ljobid), "jobid=%s ", 
		    job->jobid);
    }

    fprintf(fp,
	    "%s.%s;user=%s group=%s %sjobname=%s %squeue=batch ctime=%i qtime=%i etime=%i start=%i exec_host=%s %send=%i Exit_status=%i resources_used.cput=%02i:%02i:%02i %s%sresources_used.walltime=%02i:%02i:%02i%s\n",
	    chead, job->hostname, spasswd->pw_name, sgroup->gr_name, info, job->jobname,
	    ljobid, job->queue_time, job->queue_time, job->queue_time,
	    job->start_time, job->exec_hosts, session, job->end_time,
	    job->exitStatus, chour, cmin, csec, used_mem, used_vmem, whour, wmin,
	    wsec, used_threads);

    fflush(fp);
    if (edebug) {
	alog("Processed acc end msg, deleting job\n");
    }
    if (!deleteJob(key)) {
	alog("Error Deleting Job: Possible memory leak\n");
    }
}

/**
 * @brief Handle a timer event.
 *
 * Handle a timer to an incomplete job.
 *
 * @return No return value.
 */
static void timer_handler()
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
	/* Configure the timer to expire */
	timer.it_value.tv_sec = ERROR_WAIT_TIME;
	timer.it_value.tv_usec = 0;

	/* ... not anymore after that. */
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = 0;

	/* Start a timer. */
	setitimer(ITIMER_REAL, &timer, NULL);
    }	
}

/**
 * @brief Handle a PSP_ACCOUNT_QUEUE message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_QUEUE.
 *
 * @param chead Header of the log entry.
 *
 * @param ptr Pointer to the message to handle.
 *
 * @param key The job id (equals the taskID of the logger).
 *
 * @return No return value.
 */
static void handleAccQueueMsg(char *chead, char *ptr, PStask_ID_t logger)
{
    Job_t *job;
    struct in_addr senderIP;
    struct hostent *hostName;
    struct passwd *spasswd;
    struct group *sgroup;

    if (!(job = malloc(sizeof(Job_t)))) {
	alog("Out of memory, exiting\n");
	exit(1);
    }

    /* init job structure */
    job->queue_time = time(NULL);
    job->cput = 0;
    job->exitStatus = 0;
    job->countExitMsg = 0;
    job->countSlotMsg = 0;
    job->incomplete = 0;
    job->start_time = 0;
    job->end_time = 0;
    job->session = 0;
    job->maxrss = 0;
    job->maxvsize = 0;
    job->exec_hosts_size = EXEC_HOST_SIZE;
    job->jobname = NULL;
    job->jobid = NULL;
    job->threads = 0;
    job->startCount = 0;
    
    if (!(job->exec_hosts = malloc(EXEC_HOST_SIZE))) {
        alog("Out of memory, exiting\n");
	exit(1);
    }
    job->exec_hosts[0] = '\0';   
    
    /* Task(logger) Id */
    job->logger = logger;

    /* current rank */
    ptr += sizeof(int32_t);

    /* child's uid */
    job->uid = *(uid_t *) ptr;
    ptr += sizeof(uid_t);

    /* child's gid */
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

    if (!insertJob(job)) {
	alog("Error caching job, exiting\n");
	exit(1);
    }
    
    if (!(spasswd = getpwuid(job->uid))) {
	alog("%s: getting username failed, invalid user id:%i\n", __func__, job->uid);
    }
    if (!(sgroup = getgrgid(job->gid))) {
	alog("%s: getting groupname failed, invalid group id:%i\n", __func__, job->gid);
    }

    if (extendedLogging) {
	fprintf(fp, "%s.%s;queue=batch user=%s group=%s ctime=%i qtime=%i etime=%i\n", chead, job->hostname, 
		    spasswd->pw_name, sgroup->gr_name,
		    job->queue_time, job->queue_time, job->queue_time);
    }

    if (edebug) {
	alog("processed acc queue msg\n");
    }
}

/**
 * @brief Handle a PSP_ACCOUNT_CHILD_START message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_CHILD_START.
 *
 * @param ptr Pointer to the message to handle.
 *
 * @param key The job id (equals the taskID of the logger).
 *
 * @return No return value.
 */
static void handleAccChildStartMsg(char *ptr, PStask_ID_t key)
{
    int rank;
    char *exec_name;
    Job_t *job = findJob(key);

    if (!job) {
	alog("AccChildStartMsg to non existing Job:%i\n", key);
	return;
    }
   
    /* childs rank */
    rank = *(int32_t *) ptr;
    ptr += sizeof(int32_t);

    /* executable name */
    exec_name = ptr; 

    job->startCount++;

    if (debug) {
	alog("%s child with rank %d started:%s\n", __func__, rank, exec_name);
    }
}

/**
 * @brief Handle a PSP_ACCOUNT_DELETE message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_DELETE.
 *
 * @param chead Header of the log entry.
 *
 * @param key The job id (equals the taskID of the logger).
 *
 * @return No return value.
 */
static void handleAccDeleteMsg(char *chead, PStask_ID_t key)
{
    Job_t *job = findJob(key);
    
    if (extendedLogging) {
	fprintf(fp, "%s.%s\n", chead, job->hostname);
    }
    if (!deleteJob(key)) {
	alog("Job could not be deleted:%i\n", key);
    }

    if (edebug) {
	alog("processed acc delete msg\n");
    }
}

/**
 * @brief Handle a PSP_ACCOUNT_END message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_END.
 *
 * @param chead Header of the log entry.
 *
 * @param ptr Pointer to the message to handle.
 *
 * @param sender The task id of the sender.
 *
 * @param logger The job id (equals the taskID of the logger).
 *
 * @return No return value.
 */
static void handleAccEndMsg(char *chead, char *ptr, PStask_ID_t sender,
		     PStask_ID_t logger)
{
    Job_t *job = findJob(logger);
    int rank;

    if (!job) {
	alog("AccEndMsg to non existing Job:%i\n", logger);
	return;
    } else {
	/* only account child tasks */
	if (sender != logger) {
	    int exitStatus = 0;
	    
	    /* current rank */
	    rank = *(int32_t *) ptr;
	    ptr += sizeof(int32_t);

	    /* child's uid */
	    ptr += sizeof(uid_t);

	    /* child's gid */
	    ptr += sizeof(gid_t);

	    /* total number of childs. Only the logger knows this */
	    ptr += sizeof(int32_t);

	    /* ip address */
	    ptr += sizeof(int32_t);

	    /* actual rusage structure */
	    memcpy(&(job->rusage), ptr, sizeof(job->rusage));
	    ptr += sizeof(job->rusage);
	       
	    /* size of max used mem */
	    job->maxrss += *(long *) ptr;
	    ptr += sizeof(long);

	    /* size of max used vmem */
	    job->maxvsize += *(ulong *) ptr;
	    ptr += sizeof(ulong);
	    
	    /* number of threads */
	    job->threads += *(ulong *) ptr; 
	    ptr += sizeof(ulong);

	    /* session id */
	    if (!job->session) job->session = *(int32_t *) ptr;
	    ptr += sizeof(int32_t);
	    
	    /* exit status */
	    exitStatus = *(int32_t *) ptr;
	    if (exitStatus != 0) job->exitStatus = exitStatus;
	    ptr += sizeof(int32_t);
	}

	/* check if logger is alive */
	PSI_kill(job->logger, 0, 1);

	if (sender == logger) {

	    /* check if all childs terminated */
	    if (job->countExitMsg < job->taskSize) {
		struct itimerval timer;

		/* start timer if not yet active */
		if (!dJobs) {
		
		    /* Configure the timer to expire */
		    timer.it_value.tv_sec = ERROR_WAIT_TIME;
		    timer.it_value.tv_usec = 0;

		    /* ... not anymore after that. */
		    timer.it_interval.tv_sec = 0;
		    timer.it_interval.tv_usec = 0;

		    /* Start a timer. */
		    setitimer(ITIMER_REAL, &timer, NULL);
		}	
		
		if (!finddJob(logger)) {
		    insertdJob(logger);
		    if(debug) alog("Waiting for all childs to exit on job:%i\n",logger);
		}

	    } else {
		printAccEndMsg(chead, logger);
	    }

	} else {
	    int len = strlen(ptr);	
	    if (rank == 0) {
		/* set up jobname */
		if(!(job->jobname = malloc(len +1 ))) {
		    alog("Out of memory, exiting\n");
		    exit(1);
		}
		strncpy(job->jobname, ptr, len);
		job->jobname[len] = '\0';
		ptr += len + 1;
		
		/* set up job id */
		len = strlen(ptr);	
		if(!(job->jobid = malloc(len +1 ))) {
		    alog("Out of memory, exiting\n");
		    exit(1);
		}
		strncpy(job->jobid, ptr, len);
		job->jobid[len] = '\0';
	    }

	    job->countExitMsg++;
	    job->cput +=
		job->rusage.ru_utime.tv_sec +
		1.0e-6 * job->rusage.ru_utime.tv_usec +
		job->rusage.ru_stime.tv_sec +
		1.0e-6 * job->rusage.ru_stime.tv_usec;

	    if (job->cput < 0) {
		job->cput = 0;
	    }

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

/**
 * @brief Handle a PSP_ACCOUNT_START message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_START.
 *
 * @param ptr Pointer to the message to handle.
 *
 * @param key The job id (equals the taskID of the logger).
 *
 * @return No return value.
 */
static void handleAccStartMsg(char *ptr, PStask_ID_t key)
{
    Job_t *job = findJob(key);

    if (!job) {
	alog("AccStartMsg to non existing Job:%i\n", key);
	return;
    }

    /* current rank */
    ptr += sizeof(int32_t);

    /* child's uid */
    ptr += sizeof(uid_t);

    /* child's gid */
    ptr += sizeof(gid_t);

    /* total number of childs. Only the logger knows this */
    job->taskSize = *(int32_t *) ptr;
    ptr += sizeof(int32_t);

    job->start_time = time(NULL);

    if (edebug) {
	alog("processed acc start msg\n");
    }
}

/**
 * @brief Handle a PSP_ACCOUNT_SLOT message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_SLOT.
 *
 * @param chead Header of the log entry.
 *
 * @param msg Pointer to the msg to handle.
 *
 * @return No return value.
 */
static void handleSlotsMsg(char *chead, DDTypedBufferMsg_t * msg)
{
    char *ptr = msg->buf;
    
    PStask_ID_t logger;
    unsigned int numSlots, slot;
    size_t slotSize;
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

    slotSize = (msg->header.len - sizeof(msg->header) - sizeof(msg->type)
		- sizeof(PStask_ID_t) - sizeof(uint32_t)) / numSlots;

    job = findJob(logger);

    if (!job) {
	alog("AccSlotMsg to non existing Job:%i\n", logger);
	return;
    }

    slotSize -= sizeof(uint32_t);

    for (slot = 0; slot < numSlots; slot++) {
	struct in_addr slotIP;
	int bufleft;
	PSCPU_set_t CPUset;
	char ctmphost[400];
        
	slotIP.s_addr = *(uint32_t *) ptr;
	ptr += sizeof(uint32_t);

	switch (slotSize) {
	case sizeof(PSCPU_set_t):
	    memcpy(CPUset, ptr, sizeof(PSCPU_set_t));
	    ptr += sizeof(PSCPU_set_t);
	    break;
	default:
	    alog("Unknown slotSize %d or unknown protocoll, exiting\n", (int)slotSize);
	    exit(1);
	}

        if (job->countSlotMsg) {
            strcpy(sep, "+");
        }
	
	/* find hostname */
	hostName =
	    gethostbyaddr(&slotIP.s_addr, sizeof(slotIP.s_addr), AF_INET);
        
	if (!hostName) {
	    alog("Couldn't resolve hostname from ip:%s\n",
                 inet_ntoa(slotIP));
	    snprintf(ctmphost,sizeof(ctmphost),"%s%s/%d",
		     sep, inet_ntoa(slotIP), (int) job->countSlotMsg);
        } else {
	    snprintf(ctmphost,sizeof(ctmphost),"%s%s/%d",
		     sep, hostName->h_name, (int) job->countSlotMsg); 
        }
	
	job->countSlotMsg++;
	
	/* add hostname to exechosts */ 
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

    /* log start msg if all slot msgs are received */
    if (job->countSlotMsg == job->taskSize) {

	struct passwd *spasswd;
	struct group *sgroup;
    	
        if (!(spasswd = getpwuid(job->uid))) {
	    alog("%s: getting username failed, invalid user id:%i\n", __func__, job->uid);
	}
	if (!(sgroup = getgrgid(job->gid))) {
	    alog("%s: getting groupname failed, invalid group id:%i\n", __func__, job->gid);
	}

	if (extendedLogging) {
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

/**
 * @brief Message switch for Account Messages.
 *
 * Handle all types of PSP_ACCOUNT msgs and forward
 * them to the appropriate handle function.
 *
 * @param msg Pointer to the msg to handle.
 *
 * @return No return value.
 */
static void handleAcctMsg(DDTypedBufferMsg_t * msg)
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

/**
 * @brief Handle a PSP_CD_SIGRES message.
 *
 * Handle the message @a msg of type PSP_CD_SIGRES.
 *
 * @param msg Pointer to the msg to handle.
 *
 * @return No return value.
 */
static void handleSigMsg(DDErrorMsg_t * msg)
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
	    /* Configure the timer to expire */
	    timer.it_value.tv_sec = ERROR_WAIT_TIME;
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

/**
 * @brief Open the account log file.
 *
 * This function calculates the new log file name 
 * (YYYYMMDD), opens it and postprocess the old one. 
 *
 * @param arg_logdir The directory to write log files to.
 *
 * @return No return value.
 */
static void openAccLogFile(char *arg_logdir)
{
    char filename[200];
    static char oldfilename[200];
    char alogfile[600];
    time_t t;
    struct tm *tmp;
    struct stat statbuf;
    int ret;

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
	/* postprocess the old log file */
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
	    ret = system(syscmd);
	    if (ret == -1) {
		alog("error executing postprocessing cmd:%s\n", syscmd);
	    }
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
    
    /* set gid from accfile to gid from accdir */
    if (!arg_logdir) {
	if (!stat(DEFAULT_LOG_DIR,&statbuf)) { 
	    if (!fchown(fileno(fp),-1,statbuf.st_gid)) {
		if(debug) alog("error changing grp on acc_file\n");
	    }
	} else {
	    alog("error stat on dir %s\n",arg_logdir);
	}
    }
    strncpy(oldfilename, filename, sizeof(oldfilename));
}

/**
 * @brief Main process loop.
 *
 * Using the PSI interface this loop waits for new
 * account messages from the psid and forwards it
 * to the appropriate handle functions.
 *
 * @param arg_logdir The directory to write log files to.
 */
static void loop(char *arg_logdir)
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

static void printVersion(void)
{
    char revision[] = "$Revision$";
    fprintf(stderr, "psaccounter %s\b \n", revision+11);
}

/**
 * @brief Become a daemon.
 *
 * Do some necessary steps to become a deamon
 * process.
 *
 * @param cmd The identifier of the syslog entry.
 */
static void daemonize(const char *cmd)
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

int main(int argc, char *argv[])
{
    poptContext optCon;		/* context for parsing command-line options */
    int rc, version = 0;
    char *arg_logdir = NULL;
    char *arg_logfile = NULL;
    int arg_nodaemon = 0;

    struct poptOption optionsTable[] = {
	{"extend", 'e', POPT_ARG_NONE,
	 &extendedLogging, 0, "extended logging", "flag"},
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

    /* need to be root */
    if(getuid() != 0) {
	printf("must be started as root\n");
	exit(1);
    }

    /* Become a daemon */
    if (!arg_nodaemon) {
	daemonize("psaccounter");
    }

    /* Set umask */
    umask(S_IRWXO | S_IWGRP);
    
    /* init logger */
    alogger = logger_init("PSACC", logfile);

    /* init PSI */
    if (!PSI_initClient(TG_ACCOUNT)) {
	alog("%s", "Initialization of PSI failed\n");
	exit(1);
    }

    /* logging */
    if (arg_logfile && !!strcmp(arg_logfile, "-")) {
	logfile = fopen(arg_logfile, "a+");
    }

    if (arg_logfile && !strcmp(arg_logfile, "-")) {
	logfile = stdout;
    }

    /* init */
    btroot = 0;
    fp = 0;
    dJobs = 0;

    /* main loop */
    loop(arg_logdir);

    return 0;
}

/*
 * Local Variables:
 *  compile-command: "make psaccounter"
 * End
 */
