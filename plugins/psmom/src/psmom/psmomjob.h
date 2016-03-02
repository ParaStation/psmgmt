/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * $Id$
 *
 * \author
 * Michael Rauh <rauh@par-tec.com>
 *
 */

#ifndef __PS_MOM_JOB
#define __PS_MOM_JOB

#include <pwd.h>
#include <netinet/in.h>

#include "list.h"
#include "psmomlist.h"
#include "psmomcomm.h"
#include "pscommon.h"

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
    JOB_WAIT_OBIT,	    /* send job obit failed, try it periodically again */
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
    struct list_head list;
} Task_t;

typedef enum {
    JOB_CON_FORWARD,		/* local connection between the forwarder and the psmom */
    JOB_CON_X11_CLIENT, 	/* connection between qsub and x11 client */
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
    char *id;		    /* the PBS jobid */
    char *hashname;	    /* filesystem compatible jobid */
    char *server;	    /* pbs_server address for the job */
    char *jobscript;	    /* filename of the jobscript, empty for interactive jobs */
    char *cookie;	    /* uniq job cookie for job recognition */
    char *user;		    /* username of the job owner */
    struct passwd passwd;   /* passwd information from the job owner */
    char *pwbuf;	    /* the buffer to save the passwd information */
    pid_t pid;		    /* the pid of the running child (e.g. jobscript) */
    pid_t sid;		    /* the sid of the running child */
    pid_t mpiexec;	    /* the pid of the last mpiexec/psilogger process */
    Data_Entry_t data;	    /* job information received from torque server */
    Data_Entry_t status;    /* status information as string e.g. walltime */
    Task_t tasks;	    /* information about additional tasks e.g. (interactive) */
    Resources_t res;	    /* all used resources (walltime, mem, cputime) */
    JobState_t state;	    /* the state of the job e.g. prologue, running,..  */
    Job_Conn_t connections; /* structure with all job related connections */
    Job_Node_List_t *nodes; /* all participating nodes in the job */
    int update;		    /* flag to save the request of an initial job update */
    int nrOfNodes;	    /* number of participating nodes */
    int nrOfUniqueNodes;    /* number of participating unique nodes */
    int prologueTrack;	    /* track how many prologue scripts has finished */
    int prologueExit;	    /* the max exit code of all prologue scripts */
    int epilogueTrack;	    /* track how many epilogue scripts has finished */
    int epilogueExit;	    /* the max exit code of all epilogue scripts */
    int jobscriptExit;	    /* the exit code of the jobscript */
    int qsubPort;	    /* save the qsub port for interactive jobs */
    int recovered;	    /* set to true if the job was recovered and not running */
    int recoverTrack;	    /* track how many times job recover infos were requested */
    int pelogueMonitorId;   /* timer id of the pelogue monitor */
    int signalFlag;	    /* set to the last signal received from PBS server */
    char *pelogueMonStr;    /* pointer to the jobid use by the pelogue timeout */
    time_t PElogue_start;
    time_t start_time;	    /* the time were the job started */
    time_t end_time;	    /* the time were the job terminated */
    struct list_head list;  /* the job list header */
} Job_t;

/* list which holds all jobs */
extern Job_t JobList;

extern int jobObitTimerID;

void initJobList(void);

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
 * @brief Delete a job.
 *
 * @param jobid The id of the job to delete.
 *
 * @return returns 0 on error and 1 on success.
 */
int deleteJob(char *jobname);

int countJobs(void);

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

#endif
