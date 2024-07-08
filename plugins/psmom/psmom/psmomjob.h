/*
 * ParaStation
 *
 * Copyright (C) 2010-2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSMOM_JOB
#define __PSMOM_JOB

#include <stdbool.h>
#include <stdint.h>
#include <pwd.h>
#include <sys/types.h>

#include "list.h"
#include "pscommon.h"

#include "psmomcomm.h"
#include "psmomlist.h"

typedef enum {
    JOB_INIT   = 0x0001,
    JOB_QUEUED,		    /* the job was queued */
    JOB_PRESTART,	    /* job spawned, but not yet started */
    JOB_RUNNING,	    /* the user job is executed */
    JOB_PROLOGUE,	    /* the prologue is executed */
    JOB_EPILOGUE,	    /* the epilogue is executed */
    JOB_CANCEL_PROLOGUE,    /* prologue failed and is canceled */
    JOB_CANCEL_EPILOGUE,    /* epilouge failed and is canceled */
    JOB_CANCEL_INTERACTIVE, /* an interactive job failed and is canceled */
    JOB_WAIT_OBIT,	    /* send job obit failed, try again periodically */
    JOB_EXIT		    /* the job is exiting */
} JobState_t;

typedef struct {
    time_t walltime;    /* the used walltime */
    uint32_t r_chour;   /* the used cpu hours, from wait() rusage */
    uint32_t r_cmin;	/* the used cpu minutes, from wait() rusage */
    uint32_t r_csec;	/* the used cpu secons, from wait() rusage */
    uint32_t a_chour;   /* the used cpu hours, from account polling */
    uint32_t a_cmin;	/* the used cpu minutes, from account polling */
    uint32_t a_csec;	/* the used cpu secons, from account polling */
    uint64_t mem;	/* the maximal used memory */
    uint64_t vmem;	/* the maximal used virtual memory */
} Resources_t;

typedef struct {
    pid_t pid;
    int event;
    int nodeNr;
    int argc;
    char **argv;
    char *env;
    list_t list;
} Task_t;

typedef enum {
    JOB_CON_FORWARD,      /* local connection between forwarder and psmom */
    JOB_CON_X11_CLIENT,   /* connection between qsub and x11 client */
    JOB_CON_X11_LISTEN
} Job_Conn_type_t;

typedef struct {
    Job_Conn_type_t type;
    ComHandle_t *com;
    ComHandle_t *comForward;
    int sock;
    Protocol_t cType;
    int sockForward;
    Protocol_t cfType;
    char *jobid;
    struct list_head list;
} Job_Conn_t;

typedef struct {
    int prologue;
    int epilogue;
    PSnodes_ID_t id;
} Job_Node_List_t;

typedef struct {
    list_t next;            /**< used to put into list */
    char *id;		    /* the PBS jobid */
    char *hashname;	    /* filesystem compatible jobid */
    char *server;	    /* pbs_server address for the job */
    char *jobscript;	    /* filename of jobscript, empty if interactive */
    char *cookie;	    /* uniq job cookie for job recognition */
    char *user;		    /* username of the job owner */
    struct passwd passwd;   /* passwd information from the job owner */
    char *pwbuf;	    /* the buffer to save the passwd information */
    pid_t pid;		    /* the pid of the running child (e.g. jobscript) */
    pid_t sid;		    /* the sid of the running child */
    pid_t mpiexec;	    /* the pid of the last mpiexec/psilogger process */
    Data_Entry_t data;	    /* job information received from torque server */
    Data_Entry_t status;    /* status information as string e.g. walltime */
    Task_t tasks;	    /* info on additional tasks e.g. (interactive) */
    Resources_t res;	    /* all used resources (walltime, mem, cputime) */
    JobState_t state;	    /* state of the job e.g. prologue, running,..  */
    Job_Conn_t connections; /* structure with all job related connections */
    Job_Node_List_t *nodes; /* all participating nodes in the job */
    int update;		    /* flag to save request of an initial job update */
    int nrOfNodes;	    /* number of participating nodes */
    int nrOfUniqueNodes;    /* number of participating unique nodes */
    int prologueTrack;	    /* track how many prologue scripts has finished */
    int prologueExit;	    /* the max exit code of all prologue scripts */
    int epilogueTrack;	    /* track how many epilogue scripts has finished */
    int epilogueExit;	    /* the max exit code of all epilogue scripts */
    int jobscriptExit;	    /* the exit code of the jobscript */
    int qsubPort;	    /* save the qsub port for interactive jobs */
    int recovered;	    /* true if job was recovered and not running */
    int recoverTrack;	    /* track number of requests on job recover infos */
    int pelogueMonitorId;   /* timer id of the pelogue monitor */
    int signalFlag;	    /* set to last signal received from PBS server */
    char *pelogueMonStr;    /* pointer to jobid use by the pelogue timeout */
    time_t PElogue_start;
    time_t start_time;	    /* the time were the job started */
    time_t end_time;	    /* the time were the job terminated */
    PStask_t *resDelegate;  /* task struct holding resources used as delegate */
} Job_t;

extern int jobObitTimerID;

/**
 * @brief Delete all jobs.
 */
void clearJobList(void);

/**
 * @brief Add a new job.
 *
 * @param jobid The id of the job.
 *
 * @param server The correspoding PBS server of the job.
 *
 * @return Returns the new created job structure.
 */
Job_t *addJob(char *jobid, char *server);

/**
 * @brief Query job information.
 *
 * @param data Pointer to the list to query.
 *
 * @param name The name of the information to query.
 *
 * @param resource The resource of the information to query, may be NULL.
 */
char *getJobDetail(Data_Entry_t *data, char *name, char *resource);

/**
 * @brief Query job information and glue them together.
 */
int getJobDetailGlue(Data_Entry_t *data, char *name, char *buf, int buflen);

/**
 * @brief Find a job by its short cut job id.
 *
 * @param id The short cut job id of the job to find.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobByShortId(char *shortId);

/**
 * @brief Find a job by its job id.
 *
 * @param id The id of the job to find.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobById(char *id);

/**
 * @brief Find a job by its owner user name.
 *
 * @param user The user name of the job to find.
 *
 * @param state The state of the job. If 0 all states are valid.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobByUser(char *user, JobState_t state);

/**
 * @brief Find a job by its main child pid.
 *
 * @param pid The pid of the main child.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobByPid(pid_t pid);

/**
 * @brief Find a job by its logger pid.
 *
 * @param pid The pid of the logger.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobByLogger(pid_t logger);

/**
 * @brief Find a job via a connection socket.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobByCom(ComHandle_t *com, Job_Conn_type_t type);

/**
 * @brief Test if a pid belongs to a local running job.
 *
 * @param pid The pid to test.
 *
 * @return Returns the identified job or NULL on error.
 */
Job_t *findJobforPID(pid_t pid);

/**
 * @brief Delete a job.
 *
 * @param jobid The id of the job to delete.
 *
 * @return returns false on error and true on success.
 */
bool deleteJob(char *jobname);

/**
 * @brief Count the number of jobs.
 *
 * @return Returns the number of jobs.
 */
int countJobs(void);

/**
 * @brief Save information about all known jobs into a buffer
 *
 * @return Returns the buffer with the updated job information
 */
char *listJobs(void);

/**
 * @brief Check PID for allowance
 *
 * Check if the process with ID @a pid is allowed to run on the local
 * node due to the fact that it is part of a local job.
 *
 * @param pid Process ID of the process to check
 *
 * @param psAccLogger Logger associated to the process as determined
 * via psaccount
 *
 * @param reason Pointer to be update by the function with the reason
 * for allowance of this process
 *
 * @return Return true of the process is allowed to run on the local
 * node due to some local process. Otherwise false is returned.
 */
bool showAllowedJobPid(pid_t pid, pid_t sid, PStask_ID_t psAccLogger,
		       char **reason);

/**
 * @brief Clean job info
 *
 * Clean all job info involved with remote node @a id. This also calls
 * various cleanup actions of internal states.
 *
 * @param id Node ID of the remote node that died
 *
 * @return No return value.
 */
void cleanJobByNode(PSnodes_ID_t id);

/**
 * @brief Convert a job state into its string representation.
 *
 * @param state The job state to convert.
 *
 * @return Returns the convert string or NULL on error.
 */
char *jobState2String(int state);

/**
 * @brief Get a string holding all known job-ids.
 *
 * The job-ids will be separated by spaces. The memory for the job-ids will
 * be dynically allocated and must be freed after use.
 *
 * @return Returns NULL if no jobs are found or the requested
 * string.
 */
char *getJobString(void);

/**
 * @brief Extract node information from PBS job structure.
 *
 * @param job The job to set the information for.
 *
 * @return Returns 1 on success and 0 on error.
 */
int setNodeInfos(Job_t *job);

Task_t *createTask(Job_t *job);

/**
 * @brief Try to resend all obit messages for waiting jobs.
 *
 * @return No return value.
 */
void obitWaitingJobs(void);

/**
 * @brief Try to find the psmom job cookie.
 *
 * Try to find the psmom job cookie which should be
 * present in the environment of mpiexec requesting
 * the partition. This cookie will identify the job
 * definitely.
 *
 * @return Returns 0 on error and if the cookie was not
 * found else 1.
 */
int findJobCookie(char *cookie, pid_t pid);

/**
 * @brief Test if a user has running local or remote jobs.
 *
 * @param user The name of the user to test.
 *
 * @return Returns 1 if the user has running jobs, otherwise 0 is returned.
 */
int hasRunningJobs(char *user);

Job_Node_List_t *findJobNodeEntry(Job_t *job, PSnodes_ID_t id);

void setJobObitTimer(Job_t *job);
Job_Conn_t *addJobConn(Job_t *job, ComHandle_t *com, Job_Conn_type_t type);
void addJobConnF(Job_Conn_t *con, ComHandle_t *com);
ComHandle_t *getJobCom(Job_t *job, Job_Conn_type_t type);
Job_Conn_t *getJobConn(Job_t *job, Job_Conn_type_t type);
Job_Conn_t *findJobConn(Job_t *job, Job_Conn_type_t type, ComHandle_t *com);
Job_Conn_t *getJobConnByCom(ComHandle_t *com, Job_Conn_type_t type);
void closeJobConn(Job_Conn_t *con);
int closeJobConnByJob(Job_t *job, Job_Conn_type_t type, ComHandle_t *com);

/**
 * @brief Find a jobid in the job history.
 *
 * @param jobid The jobid to find.
 *
 * @return Returns 1 on success and 0 on error.
 */
int isJobIDinHistory(char *jobid);

/**
 * @brief Send a signal to one or more jobs.
 *
 * The signal must be send to the corresponding forwarder of
 * the job, which will then send the signal to the appropriate processes.
 *
 * @param signal The signal to send.
 *
 * @param reason The reason why the signal should be sent.
 *
 * @return Returns false on error and true on success.
 */
bool signalAllJobs(int signal, char *reason);

#endif /* __PSMOM_JOB */
