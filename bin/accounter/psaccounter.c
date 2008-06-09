/*
 *               ParaStation
 *
 * Copyright (C) 2006-2008 ParTec Cluster Competence Center GmbH, Munich
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

#include <stdbool.h>
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
#define MAX_USERNAME_LEN 64
#define MAX_GROUPNAME_LEN 64
#define EXEC_HOST_SIZE 1024
#define DEFAULT_LOG_DIR "/var/account"
#define ERROR_WAIT_TIME	60

/* children struct */
typedef struct {
    PStask_ID_t pid;
    PStask_ID_t forwarderpid;
    char *cmd;
} Children_t;

/* job structure */
typedef struct {
    char *jobname;
    char *jobid;
    char *exec_hosts;
    char user[MAX_USERNAME_LEN];
    char group[MAX_GROUPNAME_LEN];
    PStask_ID_t logger;
    PStask_ID_t submitHostID;
    size_t exec_hosts_size;
    size_t countSlotMsg;
    size_t taskSize;
    size_t procStartCount;
    size_t countExitMsg;
    uid_t uid;
    gid_t gid;
    int32_t queue_time;
    int32_t start_time;
    int32_t end_time;
    int32_t session;
    int32_t exitStatus;
    int32_t loggerChildren;
    int32_t partitionSize;
    bool extendedInfo;
    bool noextendedInfo;
    bool incomplete; /* true if not all acct msg were received */
    bool invalid_pagesize; /* true we can't calc the memory */
    float cput;
    struct rusage rusage;
    struct timeval walltime;
    struct Children_t *children;
    long long maxrss;
    unsigned long maxvsize;
    unsigned long threads;
} Job_t;

/* structure to save node information */
typedef struct {
    PSnodes_ID_t node;
    u_int32_t hostaddr;
    char hostname[MAX_HOSTNAME_LEN];
    int	protoVersion;
} Acc_Nodes_t;

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
}Joblist_t;

/** display debug output */
/*
 * 0x010 more warning messages
 * 0x020 show process information (start,exit) 
 * 0x040 show received messages
 * 0x080 very verbose output 
 * 0x100 show node information on startup
 */
int debug;

/** file pointer for the accouting files  */
FILE *fp;
/** root of the binary tree */
struct t_node *btroot;
/** log queue, start, delete messages, not only end messages */
int extendedLogging = 0;
/** set post processing command for accouting log files like gzip */
char *logPostProcessing;
/** structure for syslog */
logger_t *alogger;
/** log file for debug and error messages */
FILE *logfile = NULL;
/** the number of nodes in the parastation cluster */
int nrOfNodes = 0;
/** store incomplete jobs waiting for missing children */
Joblist_t *dJobs;
/** store node information */
Acc_Nodes_t *accNodes;	    

#define alog(...) if (alogger) logger_print(alogger, -1, __VA_ARGS__)

/**
 * @brief Malloc with error handling.
 *
 * Call malloc and handle errors.
 *
 * @param size Size in bytes to allocate.
 *
 * @return Returned is a pointer to the allocated memory. 
 */
static void *umalloc(size_t size, const char *func)
{
    void *ptr;

    if (!(ptr = malloc(size))) {
	alog("%s: memory allocation failed\n", func);
	exit(EXIT_FAILURE);
    }
    return ptr;
}

/**
* @brief Insert incomplete Job to Queue.
*
* Insert a job where not all exit messages could be received to
* incomplete Queue.
* 
* @return No return value.
*/
static void insertdJob(PStask_ID_t Job)
{
    if (!dJobs) { /* empty queue */ 
	dJobs = umalloc(sizeof(Joblist_t), __func__);
	dJobs->next = NULL;
	dJobs->job = Job;
    } else { 
	Joblist_t *lastJob;
	lastJob = dJobs;
	/* find last job */
	while (lastJob->next != NULL) {
	    lastJob = lastJob->next;
	}
	lastJob->next = umalloc(sizeof(Joblist_t), __func__);
	lastJob = lastJob->next;
	lastJob->next = NULL;
	lastJob->job = Job;
    }
}

/**
* @brief Find an incomplete Job.
*
* Finds an incomplete job in the queue.
* 
* @return No return value.
*/
static int finddJob(PStask_ID_t Job)
{
    if (!dJobs) {
	return 0;
    } else {
	Joblist_t *sJob;
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

/**
* @brief Get next incomplete Job.
*
* Returns the next incomplete Job.
* 
* @return Returns the task id of the next job,
* or 0 if there are no more jobs.
*/
static PStask_ID_t getNextdJob()
{
    if (!dJobs) {
	return 0;
    } else {
	Joblist_t *nextJob;
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
    if (!leaf) {
	leaf = umalloc(sizeof(struct t_node), __func__);
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
 * @brief Dump the running jobs.
 *
 * @return No return value.
 */
static void dumpJobs(struct t_node *leaf)
{
    Job_t *job;

    if (leaf) { 
	if (leaf->job) {
	    job = leaf->job;
	    alog("job logger:%i child_started:%zi partition_size:%i user:%s "
		 "group:%s cmd=%s exec_hosts=%s submitHost:%s\n", job->logger, 
		 job->procStartCount, job->partitionSize, job->user,
		 job->group, job->jobname, job->exec_hosts, 
		 accNodes[job->submitHostID].hostname);
	}
	if (leaf->left) {
	    dumpJobs(leaf->left);
	}
	if (leaf->right) {
	    dumpJobs(leaf->right);
	}
    }
}

/**
 * @brief Signal Handler
 *
 * @param sig Signal to handle.
 */
static void sig_handler(int sig)
{
    /* flush all ouptput streams */
    fflush(NULL);

    /* dump running jobs */
    if (sig == SIGUSR1) {
	alog("Dumping known jobs:\n");
	dumpJobs(btroot);
    }

    /* ignore term signals */
    if (sig == SIGTERM) {
	if(debug & 0x010) alog("Caught SIGTERM, ignoring.\n");
    }

    if (sig == SIGINT) {
	if(debug &0x010) alog("Caught SIGINT, exiting.\n");
	exit(EXIT_SUCCESS);
    }
}

/**
 * @brief Print error message and exit.
 *
 * @param err Pointer to the error message.
 *
 * @param func Pointer to the function name.
 *
 * @return No return value.
 */
static void protocolError(char *err, const char *func)
{
	alog("%s: protocol error:%s\n", 
	     func, err);
	exit(EXIT_FAILURE);
}

/**
 * @brief Creates and init a new job object.
 *
 * @param logger The taskid of the logger.
 *
 * @return Returns a pointer to the new created job.
 */
static Job_t *initNewJob(PStask_ID_t logger)
{
    Job_t *job;
    job = umalloc(sizeof(Job_t), __func__);
    
    /* init job structure */
    job->logger = logger;
    job->queue_time = 0;
    job->cput = 0;
    job->exitStatus = 0;
    job->countExitMsg = 0;
    job->countSlotMsg = 0;
    job->incomplete = 0;
    job->start_time = 0;
    job->end_time = 0;
    job->session = 0;
    job->extendedInfo = 0;
    job->noextendedInfo = 0;
    job->maxrss = 0;
    job->maxvsize = 0;
    job->exec_hosts_size = EXEC_HOST_SIZE;
    job->submitHostID = -1;
    job->jobname = NULL;
    job->jobid = NULL;
    job->threads = 0;
    job->procStartCount = 0;
    job->invalid_pagesize = 0;
    job->walltime.tv_sec = 0;
    job->walltime.tv_usec = 0;
    job->loggerChildren = 0;
   
    job->exec_hosts = umalloc(EXEC_HOST_SIZE, __func__);
    job->exec_hosts[0] = '\0';   

    return job;
}

/**
 * @brief Skip the default msg header.
 *
 * @param ptr Pointer to the message.
 *
 * @return Returns pointer to the rest of the message. 
 */
static Job_t *handleComHeader(char **newptr, const char *func, int *rank)
{
    uid_t uid;
    gid_t gid;
    PStask_ID_t logger;
    Job_t *job;

    char *ptr = *newptr;

    /* Task(logger) Id */
    logger = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    /* find the job */
    if ((job = findJob(logger)) == NULL) {
	job = initNewJob(logger);
	if (!insertJob(job)) {
	    alog("%s (%s): error saving job, possible memory problem?\n",
		 __func__, func);
	    exit(EXIT_FAILURE);
	}
	if (debug & 0x020) {
	    alog("%s (%s): got new job, logger pid:%i from function: %s\n",
		 __func__, func, PSC_getPID(job->logger), func);
	}
    }
    
    if (job->submitHostID == -1) {
	job->submitHostID = PSC_getID(logger); 
    }
    
    /* current rank */
    *rank = *(int32_t *)ptr; 
    ptr += sizeof(int32_t);

    /* child's uid */
    uid = *(uid_t *)ptr;
    ptr += sizeof(uid_t);

    /* set uid and username of job */
    if (!job->uid) {
	int maxlen = MAX_USERNAME_LEN - 1;
	struct passwd *spasswd;
	
	job->uid = uid; 
	if (!(spasswd = getpwuid(job->uid))) {
	    alog("%s (%s): getting username failed, invalid user id:%i\n", 
		__func__, func, job->uid);
	    snprintf(job->user, maxlen, "%i", job->uid);
	} else {
	    strncpy(job->user, spasswd->pw_name, maxlen);
	}
	job->user[maxlen] = '\0';
    }

    /* child's gid */
    gid = *(gid_t *)ptr;
    ptr += sizeof(gid_t);

    /* set gid and groupname of job*/
    if (!job->gid) {
	int maxlen = MAX_GROUPNAME_LEN - 1;
	struct group *sgroup;
	
	job->gid = gid;
	if (!(sgroup = getgrgid(job->gid))) {
	    alog("%s (%s): getting groupname failed, invalid group id:%i\n", 
		 __func__, func, job->gid);
	    snprintf(job->group, maxlen, "%i", job->gid);
	} else {
	    strncpy(job->group, sgroup->gr_name, maxlen);
	}
	job->group[maxlen] = '\0';
    }

    /* sanity check */
    if (job->uid != uid || job->gid != gid) {
	alog("%s (%s): data mismatch: job->uid=%i new->uid=%i, job->gid=%i "
	     "new->gid=%i\n", __func__, func, job->uid, uid, job->gid, gid);
    }
    
    if (debug & 0x080) {
	printf("%s (%s): rank:%i user:%i group:%i logger: %i\n", __func__, func,
	       *rank, uid, gid, PSC_getPID(job->logger));
    }
   
    *newptr = ptr; 
    return job;
}

/**
 * @brief Print Account End Messages.
 *
 * @param chead Header of the log entry.
 *
 * @param key The job id (equals the taskID of the logger).
 *
 * @return No return message.
 */
static void printAccEndMsg(char *chead, PStask_ID_t key)
{
    Job_t *job = findJob(key);
    int ccopy, chour, cmin, csec, wspan, whour, wmin, wsec;
    long rss = 0;
    char used_mem[100] = { "" };
    char used_vmem[100] = { "" };
    char used_threads[100] = { "" };
    char ljobid[500] = { "" };
    char session[100] = { "" };
    char info[100] = { "" };
    int procs;

    if (!job) {
	alog("%s: couldn`t find job:%i\n", __func__, key);
	return;
    }

    /* calc cputime */
    ccopy = job->cput;
    chour = ccopy / 3600;
    ccopy = ccopy % 3600;
    cmin = ccopy / 60;
    csec = ccopy % 60;

    /* verfiy end time */
    if (!job->end_time) {
	job->end_time = time(NULL);
	job->incomplete = 1;
    }
    
    /* calc walltime */
    if (!job->start_time || job->start_time > job->end_time) {
	if (job->walltime.tv_sec == 0 && job->walltime.tv_usec == 0) {
	    whour = 0;
	    wmin = 0;
	    wsec = 0;
	    job->incomplete = 1;
	} else {
	    if (job->walltime.tv_usec != 0) job->walltime.tv_sec++;
		wspan = job->walltime.tv_sec;
		whour = wspan / 3600;
		wspan = wspan % 3600;
		wmin = wspan / 60;
		wsec = wspan % 60;
	}
    } else {
	wspan = job->end_time - job->start_time;
	whour = wspan / 3600;
	wspan = wspan % 3600;
	wmin = wspan / 60;
	wsec = wspan % 60;
    }

    if (!job->noextendedInfo) {
	
	/* calc used mem */
	if (!job->invalid_pagesize) {
	    rss = job->maxrss / 1024;
	    if (rss) {
		snprintf(used_mem, sizeof(used_mem), 
			 "resources_used.mem=%ldkb ", rss);
	    }
	}
	
	/* calc used vmem */
	if (job->maxvsize) {
	    snprintf(used_vmem, sizeof(used_vmem), "resources_used.vmem=%ldkb ",
		     (job->maxvsize / 1024));
	}

	/* set number of threads */
	if (job->threads) {
	    snprintf(used_threads, sizeof(used_threads), 
		     "resources_used.threads=%lu ", (job->threads));
	}
    }

    /* warn if we got different types of messages */
    if (!job->noextendedInfo && !job->extendedInfo) {
	alog("%s: accountpoll disabled: nodes with different settings in job\n",
	     __func__);
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
    
    if (!job->jobname) {
	job->jobname = strdup("unknown");
    }

    if (!job->start_time && job->walltime.tv_sec != 0 && job->end_time) {
	if (job->walltime.tv_usec != 0) job->walltime.tv_sec++;
	job->start_time = job->end_time - job->walltime.tv_sec;		
    }

    if (!job->queue_time) job->queue_time = job->start_time;

    /* number of processes */
    if (job->procStartCount == 0) {
	procs = (int) job->countExitMsg;
	if (procs < job->loggerChildren) procs = job->loggerChildren;
    } else {
	procs = (int) job->procStartCount;
    }

    if (job->incomplete) {
	snprintf(info, sizeof(info), "info=incomplete ");
    }
  
    fprintf(fp,
	    "%s.%s;user=%s group=%s queue=batch ctime=%i " 
	    "qtime=%i etime=%i start=%i exec_host=%s %sjobname=%s %s%send=%i "
	    "Exit_status=%i resources_used.cput=%02i:%02i:%02i %s%s%s"
	    "resources_used.walltime=%02i:%02i:%02i resources_used.nprocs=%i\n",
	    chead, accNodes[job->submitHostID].hostname, job->user, job->group, 
	    job->queue_time, job->queue_time, job->queue_time, job->start_time, 
	    job->exec_hosts, info, job->jobname, ljobid, session, 
	    job->end_time, job->exitStatus, chour, cmin, csec, used_mem, 
	    used_vmem, used_threads, whour, wmin, wsec, 
	    procs);

    if (!deleteJob(key)) {
	alog("%s: error deleting job:%i\n", __func__, key);
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
    Job_t *job;
    
    
    if (!(tid = getNextdJob())) {
	return;
    }

    if (!(job = findJob(tid))) {
	alog("%s: couldn't find job:%i\n", __func__, tid);
    } else {
	time_t atime;
	struct tm *ptm;
	char ctime[100];
	char chead[300];
        
        /* check if all children terminated */
	if (job->countExitMsg != job->procStartCount) {
	    job->incomplete = 1;
	    job->end_time = time(NULL);
	}

	/* Create Header */
	atime = time(NULL);
	ptm = localtime(&atime);
	strftime(ctime, sizeof(ctime), "%d/%m/%Y %H:%M:%S", ptm);
	snprintf(chead, sizeof(chead), "%s;%s;%i.%s", ctime, "E",
		 PSC_getPID(job->logger), accNodes[job->submitHostID].hostname);

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
static void handleAccQueueMsg(char *msgptr, char *chead)
{
    Job_t *job;
    int rank;
    char *ptr = msgptr;

    /* handle default header */
    job = handleComHeader(&ptr, __func__, &rank); 

    /* check loggers rank */
    if (rank != -1) {
	protocolError("invalid rank for logger", __func__);	
    }

    job->queue_time = time(NULL);

    /* size of the requested partition */
    job->partitionSize = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* log queue msg */
    if (extendedLogging) {
	fprintf(fp, "%s.%s;user=%s group=%s queue=batch ctime=%i qtime=%i "
		    "etime=%i\n", 
		chead, accNodes[job->submitHostID].hostname, job->user, 
		job->group, job->queue_time, job->queue_time, job->queue_time);
    }
}

/**
 * @brief Handle a PSP_ACCOUNT_CHILD message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_CHILD.
 *
 * @param ptr Pointer to the message to handle.
 *
 * @return No return value.
 */
static void handleAccChildMsg(char *msgptr, PStask_ID_t sender)
{
    int rank;
    char *exec_name;
    Job_t *job;
    char *ptr = msgptr;

    /* handle default header */
    job = handleComHeader(&ptr, __func__, &rank); 
    
    /* check rank of child */
    if (rank < 0) {
	protocolError("invalid rank for child", __func__);	
    }

    /* executable name */
    exec_name = ptr; 

    /* set up jobname */
    if (rank == 0) {
	job->jobname = strdup(exec_name);
    }

    job->procStartCount++;

    if (debug & 0x020) {
	alog("%s child %zi with rank:%i, pid:%i cmd:%s started\n", __func__, 
	     job->procStartCount, rank, PSC_getPID(sender), exec_name);
    }
}

/**
 * @brief Handle a PSP_ACCOUNT_DELETE message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_DELETE.
 *
 * @param chead Header of the log entry.
 *
 * @param logger The job id (equals the taskID of the logger).
 *
 * @return No return value.
 */
static void handleAccDeleteMsg(char *chead, PStask_ID_t logger)
{
    Job_t *job = findJob(logger);

    if (!job) {
	alog("%s: couldn`t find job:%i\n", __func__, logger);
	return;
    }

    /* log delete msg */
    if (extendedLogging) {
	fprintf(fp, "%s.%s\n", chead, accNodes[job->submitHostID].hostname);
    }

    /* delete job structure */
    if (!deleteJob(logger)) {
	alog("%s: couldn`t delete job:%i\n", __func__, logger);
	return;
    }
}

/**
 * @brief Handle a PSP_ACCOUNT_END message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_END.
 *
 * @param ptr Pointer to the message to handle.
 *
 * @param chead Header of the log entry.
 *
 * @param sender The task id of the sender.
 *
 * @return No return value.
 */
static void handleAccEndMsg(char *msgptr, char *chead, PStask_ID_t sender)
{
    int rank;
    struct timeval walltime;
    Job_t *job;
    char *ptr = msgptr;
    
    /* handle default header */
    job = handleComHeader(&ptr, __func__, &rank); 
    
    /* logger is exiting, job should be finished */
    if (sender == job->logger) {

	/* check loggers rank */
	if (rank != -1) {
	    protocolError("invalid rank for logger", __func__);	
	}
	
	/* total number of children. Only the logger knows this */
	job->loggerChildren = *(int32_t *)ptr;
	ptr += sizeof(int32_t);
	
	/* walltime used by logger */
	memcpy(&walltime, ptr, sizeof(walltime));
	ptr += sizeof(walltime);
	if (walltime.tv_sec > job->walltime.tv_sec) {
	    job->walltime = walltime;
	}

	if (debug & 0x020) {
	    alog("%s: logger exited, %zi of %zi children finished\n", __func__,
		 job->countExitMsg, job->procStartCount);
	}

	if (!job->partitionSize && !job->procStartCount && 
	    (size_t) job->partitionSize > job->procStartCount && 
	    debug & 0x010) {
	    alog("%s: requested partition is bigger then needed: "
		 "partition_size:%i started_children:%zi\n", __func__,
		 job->partitionSize, job->procStartCount);
	}

	/* check if all children terminated */
	if (job->countExitMsg < job->procStartCount) {
	    struct itimerval timer;
	    
	    /* wait for children which are still running */
	    if (!dJobs) {
	    
		/* set error wait time */
		timer.it_value.tv_sec = ERROR_WAIT_TIME;
		timer.it_value.tv_usec = 0;

		/* ... not anymore after that. */
		timer.it_interval.tv_sec = 0;
		timer.it_interval.tv_usec = 0;

		setitimer(ITIMER_REAL, &timer, NULL);
	    }	
	    
	    /* insert the job into error waiting queue */ 
	    if (!finddJob(job->logger)) {
		insertdJob(job->logger);
		if(debug & 0x010) {
		    alog("%s: waiting for all children to exit, job:%i\n",
			 __func__, job->logger);
		}
	    }
	} else {
	    /* job successfully completed, log end msg */
	    if (!job->end_time) job->end_time = time(NULL);
	    printAccEndMsg(chead, job->logger);
	}
    } else {
	/* sender is not logger, should be forwarder of a child */
	int exitStatus = 0;
	int pagesize = 0;
	int maxrss = 0;
	int maxvsize =  0;
	int threads = 0;
	int cputime = 0;
	
	/* check rank of child */
	if (rank < 0) {
	    protocolError("invalid rank for child", __func__);	
	}

	if (debug & 0x020) {
	    alog("%s: child %zi with rank:%i forwarderpid:%i finished\n", 
	    __func__, job->countExitMsg + 1, rank, PSC_getPID(sender));
	}

	/* ping logger to check if it is still alive */
	PSI_kill(job->logger, 0, 1);
	
	/* actual rusage structure */
	memcpy(&(job->rusage), ptr, sizeof(job->rusage));
	ptr += sizeof(job->rusage);
	
	/* pagesize */
	if ((pagesize = *(uint64_t *) ptr) < 1) {
	    alog("%s: invalid pagesize, cannot calc mem usage\n", 
		 __func__);
	    pagesize = job->invalid_pagesize = 1;
	}
	ptr += sizeof(uint64_t);
	
	/* size of max used mem */
	maxrss = *(uint64_t *) ptr;
	job->maxrss += pagesize * maxrss;
	ptr += sizeof(uint64_t);

	/* size of max used vmem */
	maxvsize = *(uint64_t *) ptr;
	job->maxvsize += maxvsize;
	ptr += sizeof(uint64_t);
	
	/* walltime used by child */
	memcpy(&walltime, ptr, sizeof(walltime));
	ptr += sizeof(walltime);
	if (walltime.tv_sec > job->walltime.tv_sec) {
	    job->walltime = walltime;
	}
	
	/* number of threads */
	threads = *(uint32_t *) ptr; 
	job->threads += threads;
	ptr += sizeof(uint32_t);
	
	/* set flags to monitor extended and non extended messages */
	if (!pagesize || !maxrss || !maxvsize || !threads) {
	    job->noextendedInfo = 1;
	} 
	if (pagesize || maxrss || maxvsize || threads) {
	    job->extendedInfo = 1;
	}
	
	/* session id */
	if (!job->session) job->session = *(int32_t *) ptr;
	ptr += sizeof(int32_t);
	
	/* exit status */
	exitStatus = *(int32_t *) ptr;
	if (exitStatus != 0) job->exitStatus = exitStatus;
	ptr += sizeof(int32_t);

	/* calculate used cputime */
	cputime =
	    job->rusage.ru_utime.tv_sec +
	    1.0e-6 * job->rusage.ru_utime.tv_usec +
	    job->rusage.ru_stime.tv_sec +
	    1.0e-6 * job->rusage.ru_stime.tv_usec;
	
	if (cputime < 0) {
	    alog("%s: received invalid rusage structure from rank:%i\n", 
		 __func__, rank);
	    job->incomplete = 1;
	} else {
	    job->cput += cputime;
	}

	job->countExitMsg++;
	
	/* all jobs terminated -> set end time */
	if (job->countExitMsg == job->procStartCount && 
	    job->procStartCount > 0) {
	    job->end_time = time(NULL);
	}
    }
}

/**
 * @brief Handle a PSP_ACCOUNT_START message.
 *
 * Handle the message @a msg of type PSP_ACCOUNT_START.
 *
 * @param ptr Pointer to the message to handle.
 *
 * @return No return value.
 */
static void handleAccStartMsg(char *msgptr)
{
    Job_t *job;
    int rank;
    int tasksize;
    char *ptr = msgptr;
   
    /* handle default header */
    job = handleComHeader(&ptr, __func__, &rank); 

    /* check rank of the logger */
    if (rank != -1) {
	protocolError("invalid logger rank", __func__);
    }
    
    /* total number of possible children . */
    if ((tasksize = *(int32_t *) ptr) < 1) {
	protocolError("invalid taskSize", __func__);
    }
    job->taskSize = (size_t) tasksize;
    ptr += sizeof(int32_t);
    
    job->start_time = time(NULL);
    if (!job->queue_time) job->queue_time = time(NULL);
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
static void handleAccSlotsMsg(char *chead, DDTypedBufferMsg_t * msg)
{
    char *ptr = msg->buf;
    
    PStask_ID_t logger;
    unsigned int numSlots, slot;
    size_t slotSize;
    char sep[2] = "";
    Job_t *job;

    /* logger tid */
    logger = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);
    
    job = findJob(logger);

    if (!job) {
	alog("%s: couldn't find job:%i\n", __func__, logger);
	return;
    }
    
    /* number of Slots */
    numSlots = *(uint32_t *) ptr;
    ptr += sizeof(uint32_t);
    
    slotSize = (msg->header.len - sizeof(msg->header) - sizeof(msg->type)
		- sizeof(PStask_ID_t) - sizeof(uint32_t)) / numSlots;
    
    /* - nodeID */
    slotSize -= sizeof(PSnodes_ID_t);

    for (slot = 0; slot < numSlots; slot++) {
	int bufleft;
	PSCPU_set_t CPUset;
	char ctmphost[400];
	PSnodes_ID_t nodeID;
	int16_t cpuID;
       
	/* node_id */
	if ((nodeID = *(PSnodes_ID_t *) ptr) >= nrOfNodes) {
	    alog("%s got invalid nodeID:%i nrOfNodes:%i logger:%i\n", __func__,
		 nodeID, nrOfNodes, logger);
	    return;
	}
	ptr += sizeof(PSnodes_ID_t);


	switch (slotSize) {
	case sizeof(PSCPU_set_t):
	    /* cpuset of node */
	    memcpy(CPUset, ptr, sizeof(PSCPU_set_t));
	    ptr += sizeof(PSCPU_set_t);
	    break;
	default:
	    protocolError("unknown slotSize", __func__);
	}

	/* find cpu we are running on */
	cpuID = PSCPU_first(CPUset, PSCPU_MAX);

        if (job->countSlotMsg) strcpy(sep, "+");
	
	snprintf(ctmphost,sizeof(ctmphost),"%s%s/%d", sep, 
		 accNodes[nodeID].hostname, cpuID); 
	
	job->countSlotMsg++;
	
	/* add hostname to exechosts */ 
        if (strlen(job->exec_hosts) + strlen(ctmphost) + 1 >
            job->exec_hosts_size) {
            char *tmpjob = job->exec_hosts;
            job->exec_hosts =
                umalloc(job->exec_hosts_size + EXEC_HOST_SIZE, __func__);
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

	if (extendedLogging) {
	    fprintf(fp,
		    "%s.%s;user=%s group=%s queue=batch ctime=%i qtime=%i "
		    "etime=%i start=%i exec_host=%s\n",
		    chead, accNodes[job->submitHostID].hostname, job->user, 
		    job->group, job->queue_time, job->queue_time, 
		    job->queue_time, job->start_time, job->exec_hosts);
	}
    } else {
	if (debug & 0x040) {
	    alog("%s: received %zi from %zi msg\n", __func__,
		 job->countSlotMsg, job->taskSize);
	}
    }
}

static void handleAccLogMsg(char *msgptr)
{
    int rank;
    char *ptr = msgptr;
    Job_t *job;
   
    /* handle default header */
    job = handleComHeader(&ptr, __func__, &rank); 

    /* total number of childs connected to logger */
    job->loggerChildren = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* set up job id */
    if (!job->jobid) {
	job->jobid = strdup(ptr);
    }
    ptr += strlen(ptr)+1;
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

    /* logger's TID, this identifies a task uniquely */
    logger = *(PStask_ID_t *) ptr;

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

    if (debug & 0x040) {
	alog("%s: received new acc msg: type:%s, sender:%d, logger:%d\n",
	     __func__, 
	     msg->type == PSP_ACCOUNT_QUEUE ? "Queue" :
	     msg->type == PSP_ACCOUNT_START ? "Start" :
	     msg->type == PSP_ACCOUNT_SLOTS ? "Slot" :
	     msg->type == PSP_ACCOUNT_DELETE ? "Delete" :
	     msg->type == PSP_ACCOUNT_CHILD ? "Child" :
	     msg->type == PSP_ACCOUNT_LOG ? "Log" :
	     msg->type == PSP_ACCOUNT_END ? "End" : "?",
	     PSC_getPID(sender), PSC_getPID(logger));
    }

    switch (msg->type) {
    case PSP_ACCOUNT_QUEUE:
	handleAccQueueMsg(ptr, chead);
	break;
    case PSP_ACCOUNT_START:
	handleAccStartMsg(ptr);
	break;
    case PSP_ACCOUNT_DELETE:
	handleAccDeleteMsg(chead, logger);
	break;
    case PSP_ACCOUNT_END:
	handleAccEndMsg(ptr, chead, sender);
	break;
    case PSP_ACCOUNT_SLOTS:
	handleAccSlotsMsg(chead, msg);
	break;
    case PSP_ACCOUNT_CHILD:
	handleAccChildMsg(ptr, sender);
	break;
    case PSP_ACCOUNT_LOG:
	handleAccLogMsg(ptr);
	break;
    default:
	alog("%s: unknown accounting message: type=%d\n", __func__, msg->type);
    }
    fflush(fp);
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

    if (debug & 0x040) {
	char *errstr = strerror(msg->error);
	if (!errstr) {
	    errstr = "UNKNOWN";
	}
	alog("%s: msg from %s:", __func__,
	     PSC_printTID(msg->header.sender));
	alog("task %s: %s\n", PSC_printTID(msg->request), errstr);
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
    if (!(tmp = localtime(&t))) {
	alog("%s: localtime failed\n", __func__);
	exit(EXIT_FAILURE);
    }

    if (!(strftime(filename, sizeof(filename), "%Y%m%d", tmp))) {
	alog("%s: getting time failed\n", __func__);
	strncpy(filename, "unknown", sizeof(filename));
    }

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
		strncat(syscmd, arg_logdir, sizeof(syscmd) - strlen(syscmd) 
			- 1);
	    } else {
		strncat(syscmd, DEFAULT_LOG_DIR,
			sizeof(syscmd) - strlen(syscmd) - 1);
	    }
	    strncat(syscmd, "/", sizeof(syscmd) - strlen(syscmd) - 1);
	    strncat(syscmd, oldfilename, sizeof(syscmd) - strlen(syscmd) - 1);
	    ret = system(syscmd);
	    if (ret == -1) {
		alog("%s: error executing postprocessing cmd:%s\n", __func__, 
		     syscmd);
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
	snprintf(alogfile, sizeof(alogfile), "%s/%s", DEFAULT_LOG_DIR, 
		 filename);
	fp = fopen(alogfile, "a+");
    }

    if (!fp) {
	if (arg_logdir) {
	    if (!strcmp(arg_logdir, "-")) {
		alog("%s: error writing to stdout\n", __func__);
	    } else {
		alog("%s: error writing to log file: %s/%s\n",
		     __func__, arg_logdir, filename);
	    }
	} else {
	    alog("%s: error writing to log file: %s/%s\n",
		 __func__, DEFAULT_LOG_DIR, filename);
	}
	exit(EXIT_FAILURE);
    }
    
    /* set gid from accfile to gid from accdir */
    if (!arg_logdir) {
	if (!stat(DEFAULT_LOG_DIR,&statbuf)) { 
	    if (!fchown(fileno(fp),-1,statbuf.st_gid)) {
		if(debug & 0x010) alog("%s: error changing grp on acc_file\n", 
			       __func__);
	    }
	} else {
	    alog("%s: error stat on dir %s\n", __func__, arg_logdir);
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

	/* Problem with daemon */
	if (ret < 0 && errno != EINTR) {
	    alog("\n%s: daemon died, error:%i errno:%i\n", 
		 __func__, ret, errno);
	    exit(EXIT_FAILURE);
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
	    alog("%s: unknown message: %x\n", __func__, msg.header.type);
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
	fprintf(stderr, "%s: unable to fork server process\n", __func__);
	exit(EXIT_FAILURE);
    } else if (pid != 0) { /* parent */
	exit(EXIT_SUCCESS);
    }
    setsid();

    /* Ensure future opens won´t allocate controlling TTYs */
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGHUP, &sa, NULL) < 0) {
	fprintf(stderr, "%s: can´t ignore SIGHUP\n", __func__);
	exit(EXIT_FAILURE);
    } 
    if ((pid = fork()) < 0) {
	fprintf(stderr, "%s: unable to fork server process\n", __func__);
	exit(EXIT_FAILURE);
    } else if (pid != 0) { /* parent */
	exit(EXIT_SUCCESS);
    }

    /* Change working dir to root */
    if (chdir("/tmp") < 0) {
	fprintf(stderr, "%s: unable to change directory to /\n", __func__);
	exit(EXIT_FAILURE);
    }

    /* Close all open file descriptors */
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
	fprintf(stderr, "%s: can´t get file limit\n", __func__);
	exit(EXIT_FAILURE);
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
	syslog(LOG_ERR, "%s: unexpected file descriptors %d %d %d\n",
		__func__, fd0, fd1, fd2);
	exit(EXIT_FAILURE);
    }
}
/**
* @brief Retrive node information.
*
* Retrive information (nodeID, ip-address) from all nodes
* in the cluster.
* 
* @return No return value.
*/
static void getNodeInformation()
{
    int n, ret, maxlen;
    struct in_addr senderIP;
    struct hostent *hostName;
    PSP_Option_t opt = PSP_OP_DAEMONPROTOVERSION;
    PSP_Optval_t val;

    /* get number of nodes */
    if (PSI_infoInt(-1, PSP_INFO_NROFNODES, NULL, &nrOfNodes, 0)) {
	alog("%s: error getting number of nodes\n", __func__);
	exit(EXIT_FAILURE);
    }

    accNodes = umalloc(sizeof(Acc_Nodes_t) * nrOfNodes ,__func__);
   
    for (n=0; n<nrOfNodes; n++) {
	accNodes[n].protoVersion = 0;
	
	/* set node id */
	accNodes[n].node = n;	

	/* get ip-address of node */
	ret = PSI_infoUInt(-1, PSP_INFO_NODE, &n, &accNodes[n].hostaddr, 0);
	if (ret || (accNodes[n].hostaddr == INADDR_ANY)) {
	    alog("%s: getting node info failed, errno:%i\n", __func__, errno);
	    exit(EXIT_FAILURE);
	}

	/* get hostname */
	senderIP.s_addr = accNodes[n].hostaddr;
	maxlen = MAX_HOSTNAME_LEN - 1;
	hostName =
	    gethostbyaddr(&senderIP.s_addr, sizeof(senderIP.s_addr), AF_INET);
	if (hostName) {
	    strncpy(accNodes[n].hostname, hostName->h_name, maxlen);
	} else {
	    strncpy(accNodes[n].hostname, inet_ntoa(senderIP), maxlen);
	    alog("%s: couldn't resolve hostname from ip:%s\n",
		 __func__, inet_ntoa(senderIP));
	}
	accNodes[n].hostname[maxlen] = '\0';
	
	/* get daemon protocoll version */
	if ((PSI_infoOption(n, 1, &opt, &val, 0)) == -1 ) {
	    if (debug & 0x010) {
		alog("%s: error getting protocol version for node:%s\n", 
		     __func__, accNodes[n].hostname);
	    }
	    val = 0;
	} else {
	    accNodes[n].protoVersion = val;

	    /* make sure we have at least protocol version 401 */
	    if (val < 401) {
		alog("%s: need deamon protocol >= 401, please update "
		     "node:%i\n", __func__, n);
		exit(EXIT_FAILURE);
	    }
	}
	
	if (debug & 0x100) {
	    alog("%s: hostname:%s ip:%s nodeid:%i protocol:%i\n", __func__,
		 accNodes[n].hostname, inet_ntoa(senderIP), n, val);
	}
    }
}

int main(int argc, char *argv[])
{
    poptContext optCon;		/* context for parsing command-line options */
    int rc, version = 0;
    char *arg_logdir = NULL;
    char *arg_logfile = NULL;
    char *arg_coredir = NULL;
    int arg_nodaemon = 0;
    int arg_core = 0;
    struct rlimit corelimit;
    
    struct poptOption optionsTable[] = {
	{"extend", 'e', POPT_ARG_NONE,
	 &extendedLogging, 0, "extended logging", "flag"},
	{"debug", 'd', POPT_ARG_INT,
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
	{"dumpcore", 'c', POPT_ARG_NONE, &arg_core, 0, 
	 "dump core files unlimited (default to /tmp)", "flag"},
	{"coredir", 0, POPT_ARG_STRING, &arg_coredir, 0,
	 "directory to save coredump files", "dir"},
	POPT_AUTOHELP {NULL, '\0', 0, NULL, 0, NULL, NULL}
    };
    signal(SIGALRM, &timer_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGUSR1, sig_handler);

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
    
    /* set core dir */
    if (arg_coredir) {
        if(chdir(arg_coredir) < 0) {
            fprintf(stderr, "%s: unable to change directory to %s\n", __func__,
                    arg_coredir);
            exit(EXIT_FAILURE);
        }
    } else {
        if (chdir("/tmp") < 0) {
            fprintf(stderr, "%s: unable to change directory to /tmp\n",
                    __func__);
            exit(EXIT_FAILURE);
        }
    }

    /* Set umask */
    umask(S_IRWXO | S_IWGRP);

    /* core dump */
    if (arg_core) {
	corelimit.rlim_cur = RLIM_INFINITY;
	corelimit.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &corelimit);
    }

    /* init PSI */
    if (!PSI_initClient(TG_ACCOUNT)) {
	alog("%s: Initialization of PSI failed\n", __func__);
	exit(EXIT_FAILURE);
    }

    /* logging */
    if (arg_logfile && !!strcmp(arg_logfile, "-")) {
	if (!(logfile = fopen(arg_logfile, "a+"))) {
	    alog("%s: error opening logfile:%s\n", __func__, arg_logfile);
	    exit(EXIT_FAILURE);
	}
    }

    if (arg_logfile && !strcmp(arg_logfile, "-")) {
	logfile = stdout;
    }
    
    /* init logger */
    alogger = logger_init("PSACC", logfile);

    if (debug) {
	alog("Enabling debug mask: 0x%x\n", debug);
    }

    /* init */
    btroot = 0;
    fp = 0;
    dJobs = 0;

    /* get node infos */
    getNodeInformation();

    /* main loop */
    loop(arg_logdir);

    return 0;
}

/*
 * Local Variables:
 *  compile-command: "make psaccounter"
 * End
 */
