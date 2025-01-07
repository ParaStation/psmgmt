/*
 * ParaStation
 *
 * Copyright (C) 2006-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * psaccounter: ParaStation accounting daemon
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
#include <sys/wait.h>

#include "list.h"
#include "pscommon.h"
#include "pscpu.h"
#include "pse.h"
#include "psi.h"
#include "psiinfo.h"
#include "psispawn.h"
#include "psilog.h"

#define MAX_HOSTNAME_LEN 64
#define MAX_USERNAME_LEN 64
#define MAX_GROUPNAME_LEN 64
#define EXEC_HOST_SIZE 1024
#define DEFAULT_LOG_DIR LOCALSTATEDIR "/log/psaccounter"
#define ERROR_WAIT_TIME	60

/* job structure */
typedef struct {
    char *jobname;
    char *jobid;
    char *exec_hosts;
    char user[MAX_USERNAME_LEN];
    char group[MAX_GROUPNAME_LEN];
    PStask_ID_t logger;
    PSnodes_ID_t submitHostID;
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
    uint64_t maxrss;
    uint64_t maxvsize;
    uint64_t avgrss;
    uint64_t avgvsize;
    uint64_t avgthreads;
    uint32_t threads;
} Job_t;

/* structure to save node information */
typedef struct {
    PSnodes_ID_t node;
    u_int32_t hostaddr;
    char hostname[MAX_HOSTNAME_LEN];
} Acc_Nodes_t;

/* binary tree structur */
struct t_node {
    PStask_ID_t key;
    Job_t *job;
    struct t_node *left;
    struct t_node *right;
};

/* struct for non responding jobs */
typedef struct {
    PStask_ID_t job;
    list_t next;
} Joblist_t;

/** display debug output */
/*
 * 0x010 more warning messages
 * 0x020 show process information (start,exit)
 * 0x040 show received messages
 * 0x080 very verbose output
 * 0x100 show node information on startup
 */
static int debug;

/** file pointer for the accouting files  */
static FILE *fp;
/** root of the binary tree */
static struct t_node *btroot;
/** log queue, start, delete messages, not only end messages */
static int extendedLogging = 0;
/** set post processing command for accouting log files like gzip */
static char *logPostProcessing;
/** structure for syslog */
static logger_t alogger;
/** log file for debug and error messages */
static FILE *logfile;
/** the number of nodes in the parastation cluster */
static int nrOfNodes = 0;
/** store incomplete jobs waiting for missing children */
static LIST_HEAD(dJobs);
/** store node information */
static Acc_Nodes_t *accNodes;

#define alog(...) logger_print(alogger, -1, __VA_ARGS__)
#define flog(...) logger_funcprint(alogger, __func__, -1, __VA_ARGS__)
#define awarn(...) logger_warn(alogger, -1, errno, __VA_ARGS__)
#define fwarn(...) logger_funcwarn(alogger, __func__, -1, errno, __VA_ARGS__)

/**
 * @brief Malloc with error handling
 *
 * Call malloc and handle errors.
 *
 * @param size Size in bytes to allocate
 *
 * @return Returned is a pointer to the allocated memory
 */
static void *umalloc(size_t size, const char *caller)
{
    void *ptr = malloc(size);

    if (!ptr) {
	awarn("%s: malloc()", caller);
	exit(EXIT_FAILURE);
    }
    return ptr;
}

/**
 * @brief (Un-)Block signal
 *
 * Block or unblock the signal @a sig depending on the flag @a
 * block. If @a block is true, the signal will be blocked. Otherwise
 * it will be unblocked.
 *
 * @param sig Signal to block or unblock
 *
 * @param block Flag steering the (un-)blocking of the signal
 *
 * @return Flag, if signal was blocked before. I.e. return true if
 * signal was blocked or false otherwise.
 */
static bool blockSig(int sig, bool block)
{
    sigset_t set, oldset;

    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, &oldset)) {
	fwarn("sigprocmask()");
    }

    return sigismember(&oldset, sig);
}

/**
* @brief Insert incomplete Job to Queue
*
* Insert a job where not all exit messages could be received to
* incomplete Queue.
*
* @param job Job to add
*
* @return No return value
*/
static void insertdJob(PStask_ID_t Job)
{
    Joblist_t *newJob = umalloc(sizeof(*newJob), __func__);
    newJob->job = Job;
    list_add_tail(&newJob->next, &dJobs);
}

/**
* @brief Find an incomplete Job
*
* Find the incomplete job identified by @a Job in the queue.
*
* @param job Job to find
*
* @return If @a Job is found return true; or false otherwise
*/
static bool finddJob(PStask_ID_t Job)
{
    list_t *j;
    list_for_each(j, &dJobs) {
	Joblist_t *job = list_entry(j, Joblist_t, next);
	if (job->job == Job) return true;
    }
    return false;
}

/**
* @brief Get next incomplete Job
*
* Returns the next incomplete Job.
*
* @return Returns the task id of the next job, or 0 if there are no
* more jobs
*/
static PStask_ID_t getNextdJob(void)
{
    if (list_empty(&dJobs)) return 0;

    Joblist_t *first = list_entry(dJobs.next, Joblist_t, next);
    PStask_ID_t jobid = first->job;

    list_del(&first->next);
    free(first);

    return jobid;
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
	free(tmpleaf->job->jobname);
	free(tmpleaf->job->jobid);
	free(tmpleaf->job->exec_hosts);
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
	    alog("job logger: %s child_started:%zi partition_size:%i user:%s "
		 "group:%s cmd=%s exec_hosts=%s submitHost:%s\n",
		 PSC_printTID(job->logger), job->procStartCount,
		 job->partitionSize, job->user,
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
 * @param sig Signal to handle
 */
static void sig_handler(int sig)
{
    /* flush all ouptput streams */
    fflush(NULL);

    switch (sig) {
    case SIGUSR1:
	/* dump running jobs */
	alog("Dumping known jobs:\n");
	dumpJobs(btroot);
	break;
    case SIGTERM:
	/* ignore term signals */
	if (debug & 0x010) alog("caught SIGTERM, ignoring\n");
	break;
    case SIGINT:
	if (debug &0x010) alog("caught SIGINT, exiting\n");
	exit(EXIT_SUCCESS);
    default:
	alog("Caught signal %s, ignoring\n", strsignal(sig));
    }
}

/**
 * @brief Print error message and exit
 *
 * @param err Pointer to the error message
 *
 * @param caller Pointer to calling function name
 *
 * @return No return value
 */
static void protocolError(char *err, const char *caller)
{
    alog("%s: protocol error:%s\n", caller, err);
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
    job->avgrss = 0;
    job->avgvsize = 0;
    job->avgthreads = 0;
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
    job->user[0] = '\0';
    job->group[0] = '\0';

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
static Job_t *handleComHeader(char **newptr, const char *caller, int *rank)
{
    char *ptr = *newptr;

    /* Task(logger) Id */
    PStask_ID_t logger = *(PStask_ID_t *)ptr;
    ptr += sizeof(PStask_ID_t);

    /* find the job */
    Job_t *job = findJob(logger);
    if (!job) {
	job = initNewJob(logger);
	if (!insertJob(job)) {
	    flog("(from %s) error saving job, memory problem?\n", caller);
	    exit(EXIT_FAILURE);
	}
	if (debug & 0x020) {
	    flog("(from %s) got new job, logger %s\n", caller,
		 PSC_printTID(job->logger));
	}
    }

    if (job->submitHostID == -1) {
	job->submitHostID = PSC_getID(logger);
    }

    /* current rank */
    *rank = *(int32_t *)ptr;
    ptr += sizeof(int32_t);

    /* child's uid */
    uid_t uid = *(uid_t *)ptr;
    ptr += sizeof(uid_t);

    /* set uid and username of job */
    if (job->user[0] == '\0') {
	int maxlen = MAX_USERNAME_LEN - 1;

	job->uid = uid;
	struct passwd *spasswd = getpwuid(job->uid);
	if (!spasswd) {
	    flog("(from %s) getpwuid(%d) failed\n", caller, job->uid);
	    snprintf(job->user, maxlen, "%i", job->uid);
	} else {
	    strncpy(job->user, spasswd->pw_name, maxlen);
	}
	job->user[maxlen] = '\0';
    }

    /* child's gid */
    gid_t gid = *(gid_t *)ptr;
    ptr += sizeof(gid_t);

    /* set gid and groupname of job*/
    if (job->group[0] == '\0') {
	int maxlen = MAX_GROUPNAME_LEN - 1;

	job->gid = gid;
	struct group *sgroup = getgrgid(job->gid);
	if (!sgroup) {
	    flog("(from %s) getgrgid(%d) failed\n", caller, job->gid);
	    snprintf(job->group, maxlen, "%i", job->gid);
	} else {
	    strncpy(job->group, sgroup->gr_name, maxlen);
	}
	job->group[maxlen] = '\0';
    }

    /* sanity check */
    if (job->uid != uid || job->gid != gid) {
	flog("(from %s) data mismatch: job->uid=%i new->uid=%i, job->gid=%i "
	     "new->gid=%i\n", caller, job->uid, uid, job->gid, gid);
    }

    if (debug & 0x080) {
	printf("%s (%s): rank:%i user:%i group:%i logger: %i\n", __func__,
	       caller, *rank, uid, gid, PSC_getPID(job->logger));
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
    char used_avgmem[100] = { "" };
    char used_vmem[100] = { "" };
    char used_avgvmem[100] = { "" };
    char used_threads[100] = { "" };
    char used_avgthreads[100] = { "" };
    char ljobid[500] = { "" };
    char session[100] = { "" };
    char info[100] = { "" };
    int procs;

    if (!job) {
	flog("couldn`t find job: %s\n", PSC_printTID(key));
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
	/* calc used max/avg mem */
	if (!job->invalid_pagesize) {
	    if (job->maxrss) {
		if ((rss = job->maxrss / 1024)) {
		    snprintf(used_mem, sizeof(used_mem),
			     "resources_used.mem=%ldkb ", rss);
		}
	    }
	    if (job->avgrss) {
		if ((rss = job->avgrss / 1024)) {
		    snprintf(used_avgmem, sizeof(used_avgmem),
			     "resources_used.avg_mem=%ldkb ", rss);
		}
	    }
	}

	/* show max vmem */
	if (job->maxvsize) {
	    snprintf(used_vmem, sizeof(used_vmem),
		     "resources_used.vmem=%llukb ",
		     (unsigned long long)(job->maxvsize / 1024));
	}

	/* show avg vmem */
	if (job->avgvsize) {
	    snprintf(used_avgvmem, sizeof(used_avgvmem),
		     "resources_used.avg_vmem=%llukb ",
		     (unsigned long long)(job->avgvsize / 1024));
	}

	/* show avg threads */
	if (job->avgthreads) {
	    snprintf(used_avgthreads, sizeof(used_avgthreads),
		     "resources_used.avg_threads=%llu ",
		     (unsigned long long)job->avgthreads);
	}

	/* set number of threads */
	if (job->threads) {
	    snprintf(used_threads, sizeof(used_threads),
		     "resources_used.threads=%u ", (job->threads));
	}
    }

    /* warn if we got different types of messages */
    if (!job->noextendedInfo && !job->extendedInfo) {
	flog("accountpoll disabled: nodes with different settings in job\n");
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
	    "Exit_status=%i resources_used.cput=%02i:%02i:%02i %s%s%s%s%s%s"
	    "resources_used.walltime=%02i:%02i:%02i resources_used.nprocs=%i\n",
	    chead, accNodes[job->submitHostID].hostname, job->user, job->group,
	    job->queue_time, job->queue_time, job->queue_time, job->start_time,
	    job->exec_hosts, info, job->jobname, ljobid, session,
	    job->end_time, job->exitStatus, chour, cmin, csec, used_mem,
	    used_avgmem, used_vmem, used_avgvmem, used_threads, used_avgthreads,
	    whour, wmin, wsec, procs);

    if (!deleteJob(key)) {
	flog("error deleting job %s\n", PSC_printTID(key));
    }
}

/**
 * @brief Handle a timer event.
 *
 * Handle a timer to an incomplete job.
 *
 * @return No return value.
 */
static void timer_handler(int sig)
{
    PStask_ID_t tid = getNextdJob();
    if (!tid) return;

    Job_t *job = findJob(tid);
    if (!job) {
	flog("couldn't find job %s\n", PSC_printTID(tid));
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

    /* re-start timer if jobs pending */
    if (!list_empty(&dJobs)) {
	struct itimerval timer;
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
 * @param msgptr Pointer to the message to handle.
 *
 * @param chead Header of the log entry.
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
	flog("child %zi with rank %i, pid %i cmd %s started\n",
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
	flog("couldn`t find job %s\n", PSC_printTID(logger));
	return;
    }

    /* log delete msg */
    if (extendedLogging) {
	fprintf(fp, "%s.%s\n", chead, accNodes[job->submitHostID].hostname);
    }

    /* delete job structure */
    if (!deleteJob(logger)) {
	flog("couldn`t delete job %s\n", PSC_printTID(logger));
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
	    flog("logger exited, %zi of %zi children finished\n",
		 job->countExitMsg, job->procStartCount);
	}

	if (!job->partitionSize && !job->procStartCount &&
	    (size_t) job->partitionSize > job->procStartCount &&
	    debug & 0x010) {
	    flog("requested partition is bigger then needed:"
		 " partition_size %i started_children %zi\n",
		 job->partitionSize, job->procStartCount);
	}

	/* check if all children terminated */
	if (job->countExitMsg < job->procStartCount) {
	    /* wait for children which are still running */
	    if (list_empty(&dJobs)) {
		struct itimerval timer;

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
		if (debug & 0x010) {
		    flog("waiting for all children to exit, job %s\n",
			 PSC_printTID(job->logger));
		}
	    }
	} else {
	    /* job successfully completed, log end msg */
	    if (!job->end_time) job->end_time = time(NULL);
	    printAccEndMsg(chead, job->logger);
	}
    } else {
	/* sender is not logger, should be forwarder of a child */
	int32_t exitStatus = 0;
	uint64_t pagesize = 0;
	uint64_t maxrss = 0;
	uint64_t maxvsize =  0;
	uint64_t avgrss = 0;
	uint64_t avgvsize =  0;
	uint64_t avgthreads =  0;
	uint32_t threads = 0;
	int32_t cputime = 0;
	int32_t extended = 0;

	/* check rank of child */
	if (rank < 0) {
	    protocolError("invalid rank for child", __func__);
	}

	if (debug & 0x020) {
	    flog("child %zi with rank %i forwarderpid %i finished\n",
		 job->countExitMsg + 1, rank, PSC_getPID(sender));
	}

	/* ping logger to check if it is still alive */
	PSI_kill(job->logger, 0, 1);

	/* pid */
	ptr += sizeof(pid_t);

	/* actual rusage structure */
	memcpy(&(job->rusage), ptr, sizeof(job->rusage));
	ptr += sizeof(job->rusage);

	/* pagesize */
	if ((pagesize = *(uint64_t *) ptr) < 1) {
	    flog("invalid pagesize, cannot calc mem usage\n");
	    pagesize = job->invalid_pagesize = 1;
	}
	ptr += sizeof(uint64_t);

	/* walltime used by child */
	memcpy(&walltime, ptr, sizeof(walltime));
	ptr += sizeof(walltime);
	if (walltime.tv_sec > job->walltime.tv_sec) {
	    job->walltime = walltime;
	}

	/* exit status */
	exitStatus = *(int32_t *) ptr;
	if (exitStatus != 0) job->exitStatus = exitStatus;
	ptr += sizeof(int32_t);

	/* check for extended info */
	extended = *(int32_t *) ptr;
	ptr += sizeof(int32_t);

	if (extended) {
	    job->extendedInfo = 1;

	    /* size of max used mem */
	    maxrss = *(uint64_t *) ptr;
	    job->maxrss += pagesize * maxrss;
	    ptr += sizeof(uint64_t);

	    /* size of max used vmem */
	    maxvsize = *(uint64_t *) ptr;
	    job->maxvsize += maxvsize;
	    ptr += sizeof(uint64_t);

	    /* number of threads */
	    threads = *(uint32_t *) ptr;
	    job->threads += threads;
	    ptr += sizeof(uint32_t);

	    /* session id */
	    if (!job->session) job->session = *(pid_t *) ptr;
	    ptr += sizeof(pid_t);

	    /* size of average used mem */
	    avgrss = *(uint64_t *) ptr;
	    job->avgrss += pagesize * avgrss;
	    ptr += sizeof(uint64_t);

	    /* size of average used vmem */
	    avgvsize = *(uint64_t *) ptr;
	    job->avgvsize += avgvsize;
	    ptr += sizeof(uint64_t);

	    /* number of average threads */
	    avgthreads = *(uint64_t *) ptr;
	    job->avgthreads += avgthreads;
	    ptr += sizeof(uint64_t);
	} else {
	    job->noextendedInfo = 1;
	}

	/* calculate used cputime */
	cputime =
	    job->rusage.ru_utime.tv_sec +
	    1.0e-6 * job->rusage.ru_utime.tv_usec +
	    job->rusage.ru_stime.tv_sec +
	    1.0e-6 * job->rusage.ru_stime.tv_usec;

	if (cputime < 0) {
	    flog("received invalid rusage structure from rank %i\n", rank);
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
    size_t nBytes, myBytes = PSCPU_bytesForCPUs(PSCPU_MAX);
    char sep[2] = "";
    Job_t *job;

    /* logger tid */
    logger = *(PStask_ID_t *) ptr;
    ptr += sizeof(PStask_ID_t);

    job = findJob(logger);

    if (!job) {
	flog("couldn't find job %s\n", PSC_printTID(logger));
	return;
    }

    /* skip child's uid */
    ptr += sizeof(uid_t);

    /* number of slots */
    numSlots = *(uint16_t *)ptr;
    ptr += sizeof(uint16_t);

    /* size of CPUset part */
    nBytes = *(uint16_t *)ptr;
    ptr += sizeof(uint16_t);

    if (nBytes > myBytes) {
	flog("too many CPUs: %zd > %zd for %s\n",
	     nBytes*8, myBytes*8, PSC_printTID(logger));
    }

    for (slot = 0; slot < numSlots; slot++) {
	int bufleft;
	PSCPU_set_t CPUset;
	char ctmphost[400];
	PSnodes_ID_t nodeID;
	int16_t cpuID;

	/* node_id */
	nodeID = *(PSnodes_ID_t *)ptr;
	ptr += sizeof(PSnodes_ID_t);
	if (nodeID >= nrOfNodes) {
	    flog("invalid nodeID %i (nrOfNodes %i) for %s\n",
		 nodeID, nrOfNodes, PSC_printTID(logger));
	    return;
	}

	PSCPU_clrAll(CPUset);
	PSCPU_inject(CPUset, ptr, nBytes);
	ptr += nBytes;

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
	   free(tmpjob);
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
	    flog("received %zi from %zi\n", job->countSlotMsg, job->taskSize);
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

    /* total number of children connected to logger */
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
    PStask_ID_t sender = msg->header.sender, rootTID;

    /* root task's TID, this identifies a task uniquely */
    rootTID = *(PStask_ID_t *) ptr;

    /* Create Header for all Msg */
    atime = time(NULL);
    bool blocked = blockSig(SIGALRM, true);
    ptm = localtime(&atime);
    blockSig(SIGALRM, blocked);
    strftime(ctime, sizeof(ctime), "%d/%m/%Y %H:%M:%S", ptm);

    snprintf(chead, sizeof(chead), "%s;%s;%i", ctime,
	     msg->type == PSP_ACCOUNT_QUEUE ? "Q" :
	     msg->type == PSP_ACCOUNT_START ? "S" :
	     msg->type == PSP_ACCOUNT_SLOTS ? "S" :
	     msg->type == PSP_ACCOUNT_DELETE ? "D" :
	     msg->type == PSP_ACCOUNT_END ? "E" : "?",
	     PSC_getPID(rootTID));

    if (debug & 0x040) {
	flog("received new acc msg: type:%s, sender: %s",
	     msg->type == PSP_ACCOUNT_QUEUE ? "Queue" :
	     msg->type == PSP_ACCOUNT_START ? "Start" :
	     msg->type == PSP_ACCOUNT_SLOTS ? "Slot" :
	     msg->type == PSP_ACCOUNT_DELETE ? "Delete" :
	     msg->type == PSP_ACCOUNT_CHILD ? "Child" :
	     msg->type == PSP_ACCOUNT_LOG ? "Log" :
	     msg->type == PSP_ACCOUNT_LOST ? "Lost" :
	     msg->type == PSP_ACCOUNT_END ? "End" : "?",
	     PSC_printTID(sender));
	alog(", root:%s\n", PSC_printTID(rootTID));
    }

    switch (msg->type) {
    case PSP_ACCOUNT_QUEUE:
	handleAccQueueMsg(ptr, chead);
	break;
    case PSP_ACCOUNT_START:
	handleAccStartMsg(ptr);
	break;
    case PSP_ACCOUNT_DELETE:
	handleAccDeleteMsg(chead, rootTID);
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
    case PSP_ACCOUNT_LOST:
	// ignore
	break;
    default:
	flog("unknown accounting message (type=%d)\n", msg->type);
    }
    fflush(fp);
}

/**
 * @brief Handle a PSP_CD_SIGRES message
 *
 * Handle the message @a msg of type PSP_CD_SIGRES.
 *
 * @param msg Pointer to the msg to handle
 *
 * @return No return value.
 */
static void handleSigMsg(DDErrorMsg_t * msg)
{

    if (debug & 0x040) {
	char *errstr = strerror(msg->error);
	if (!errstr) {
	    errstr = "UNKNOWN";
	}
	flog("msg from %s", PSC_printTID(msg->header.sender));
	alog(" on task %s: %s\n", PSC_printTID(msg->request), errstr);
    }

    Job_t *job = findJob(eMsg->request);
    /* check if logger replied */
    if (msg->error == ESRCH && job) {
	if (list_empty(&dJobs)) {
	    struct itimerval timer;

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
    bool blocked = blockSig(SIGALRM, true);
    tmp = localtime(&t);
    blockSig(SIGALRM, blocked);
    if (!tmp) {
	flog("localtime failed\n");
	exit(EXIT_FAILURE);
    }

    if (!strftime(filename, sizeof(filename), "%Y%m%d", tmp)) {
	flog("getting time failed\n");
	strncpy(filename, "unknown", sizeof(filename));
    }

    if ((arg_logdir && fp && !strcmp(arg_logdir, "-"))
	|| (fp && !strcmp(filename, oldfilename))) {
	return;
    }

    /* next day, open new log file */
    if (fp && strcmp(filename, oldfilename)) {
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
		flog("error executing postprocessing cmd '%s'\n", syscmd);
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
		flog("error writing to stdout\n");
	    } else {
		flog("error writing to log file: %s/%s\n", arg_logdir, filename);
	    }
	} else {
	    flog("error writing to log file: %s/%s\n", DEFAULT_LOG_DIR, filename);
	}
	exit(EXIT_FAILURE);
    }

    /* set gid from accfile to gid from accdir */
    if (!arg_logdir) {
	if (!stat(DEFAULT_LOG_DIR, &statbuf)) {
	    if (!fchown(fileno(fp), -1, statbuf.st_gid)) {
		if (debug & 0x010) flog("error changing grp on acc_file\n");
	    }
	} else {
	    flog("error stat on default logdir %s\n", DEFAULT_LOG_DIR);
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
	ssize_t ret = PSI_recvMsg((DDBufferMsg_t *)&msg, sizeof(msg),
				  -1, false);
	if (ret == -1 && errno == EINTR) continue;

	/* Problem with daemon */
	if (ret == -1 && errno != ENOMSG) {
	    alog("\n");
	    flog("daemon died, error %zi errno %i\n", ret, errno);
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
	    flog("unknown message: %x\n", msg.header.type);
	}

	if (logfile) fflush(logfile);
    }

    PSE_finalize();
}

static void printVersion(void)
{
    alog("psaccounter %s\n", PSC_getVersionStr());
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
	awarn("%s: unable to fork server process 1", __func__);
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
	awarn("%s: can´t ignore SIGHUP", __func__);
	exit(EXIT_FAILURE);
    }
    if ((pid = fork()) < 0) {
	awarn("%s: unable to fork server process 2", __func__);
	exit(EXIT_FAILURE);
    } else if (pid != 0) { /* parent */
	exit(EXIT_SUCCESS);
    }

    /* Change working dir to /tmp */
    if (chdir("/tmp") < 0) {
	awarn("%s: unable to change directory to /tmp", __func__);
	exit(EXIT_FAILURE);
    }

    /* Close all open file descriptors */
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
	awarn("%s: can´t get file limit", __func__);
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
    /* Let PSI and PSC log via syslog. Call it before PSI_initClient() */
    PSI_initLog(NULL);
}

/**
 * @brief Retrive node information.
 *
 * Retrive information (nodeID, ip-address) from all nodes in the
 * cluster.
 *
 * @return No return value.
 */
static void getNodeInformation(void)
{
    /* get number of nodes */
    nrOfNodes = PSC_getNrOfNodes();

    accNodes = umalloc(sizeof(Acc_Nodes_t) * nrOfNodes ,__func__);

    for (int n = 0; n < nrOfNodes; n++) {
	/* set node id */
	accNodes[n].node = n;

	/* get ip-address of node */
	int rc = PSI_infoUInt(-1, PSP_INFO_NODE, &n, &accNodes[n].hostaddr, false);
	if (rc || accNodes[n].hostaddr == INADDR_ANY) {
	    fwarn("getting node info failed");
	    exit(EXIT_FAILURE);
	}

	/* get hostname */
	struct sockaddr_in nodeAddr = (struct sockaddr_in) {
	    .sin_family = AF_INET,
	    .sin_port = 0,
	    .sin_addr = { .s_addr = accNodes[n].hostaddr } };
	rc = getnameinfo((struct sockaddr *)&nodeAddr, sizeof(nodeAddr),
			 accNodes[n].hostname, sizeof(accNodes[n].hostname),
			 NULL, 0, NI_NAMEREQD | NI_NOFQDN);
	if (rc) {
	    snprintf(accNodes[n].hostname, sizeof(accNodes[n].hostname),
		     "%s", inet_ntoa(nodeAddr.sin_addr));
	    flog("unable to resolve hostname from ip %s\n",
		 inet_ntoa(nodeAddr.sin_addr));
	}

	if (debug & 0x100) {
	    flog("hostname %s ip %s nodeid %i",
		 accNodes[n].hostname, inet_ntoa(nodeAddr.sin_addr), n);
	}
    }
}

int main(int argc, char *argv[])
{
    int version = 0;
    char *arg_logdir = NULL;
    char *arg_logfile = NULL;
    char *arg_coredir = NULL;
    int arg_nodaemon = 0;
    int arg_core = 0;

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
    PSC_setSigHandler(SIGALRM, &timer_handler);
    PSC_setSigHandler(SIGTERM, sig_handler);
    PSC_setSigHandler(SIGINT, sig_handler);
    PSC_setSigHandler(SIGUSR1, sig_handler);

    /* emergency logger during startup */
    alogger = logger_new(NULL, stderr);

    poptContext optCon =
	poptGetContext(NULL, argc, (const char **) argv, optionsTable, 0);
    int rc = poptGetNextOpt(optCon);
    if (rc < -1) {
	/* an error occurred during option processing */
	poptPrintUsage(optCon, stderr, 0);
	alog("%s: %s\n", poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
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

	/* re-init logger (use syslog in the meantime) */
	logger_finalize(alogger);
	alogger = logger_new("PSACC", NULL);
    }

    /* set core dir */
    if (arg_coredir) {
	if (chdir(arg_coredir) < 0) {
	    awarn("%s: chdir(%s)", __func__, arg_coredir);
	    exit(EXIT_FAILURE);
	}
    } else if (chdir("/tmp") < 0) {
	awarn("%s: chdir(/tmp)", __func__);
	exit(EXIT_FAILURE);
    }

    /* Set umask */
    umask(S_IRWXO | S_IWGRP);

    /* core dump */
    if (arg_core) {
	struct rlimit corelimit = {
	    .rlim_cur = RLIM_INFINITY,
	    .rlim_max = RLIM_INFINITY };
	setrlimit(RLIMIT_CORE, &corelimit);
    }

    /* init PSI */
    if (!PSI_initClient(TG_ACCOUNT)) {
	alog("failed to initialize PSI\n");
	exit(EXIT_FAILURE);
    }

    /* logging */
    if (arg_logfile && strcmp(arg_logfile, "-")) {
	logfile = fopen(arg_logfile, "a+");
	if (!logfile) {
	    awarn("fopen(%s)", arg_logfile);
	    exit(EXIT_FAILURE);
	}
    }

    if (arg_logfile && !strcmp(arg_logfile, "-")) logfile = stdout;

    /* re-init logger again (switch to final logfile) */
    logger_finalize(alogger);
    alogger = logger_new("PSACC", logfile);

    if (debug) alog("enabling debug mask: 0x%x\n", debug);

    /* init */
    btroot = 0;
    fp = 0;

    /* get node infos */
    getNodeInformation();

    /* main loop */
    loop(arg_logdir);

    return 0;
}
