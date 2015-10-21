/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PELOGUE_JOB
#define __PS_PELOGUE_JOB

#include "list.h"
#include "psnodes.h"

typedef void Pelogue_JobCb_Func_t (char *, int, int);

typedef enum {
    JOB_QUEUED,		    /* the job was queued */
    JOB_RUNNING,	    /* the user job is executed */
    JOB_PROLOGUE,	    /* the prologue is executed */
    JOB_EPILOGUE,	    /* the epilogue is executed */
    JOB_CANCEL_PROLOGUE,    /* prologue failed and is canceled */
    JOB_CANCEL_EPILOGUE,    /* epilouge failed and is canceled */
} JobState_t;

typedef struct {
    int prologue;
    int epilogue;
    PSnodes_ID_t id;
} Job_Node_List_t;

typedef struct {
    char *id;		    /* the PBS jobid */
    uid_t uid;		    /* user id of the job owner */
    gid_t gid;		    /* group of the job owner */
    pid_t pid;		    /* the pid of the running child (e.g. jobscript) */
    pid_t sid;		    /* the sid of the running child */
    Job_Node_List_t *nodes; /* all participating nodes in the job */
    int prologueTrack;	    /* track how many prologue scripts has finished */
    int prologueExit;	    /* the max exit code of all prologue scripts */
    int epilogueTrack;	    /* track how many epilogue scripts has finished */
    int epilogueExit;	    /* the max exit code of all epilogue scripts */
    int pelogueMonitorId;   /* timer id of the pelogue monitor */
    int nrOfNodes;
    int signalFlag;
    int state;
    char *plugin;
    void (*pluginCallback)(char *, int, int);
    char *scriptname;
    time_t PElogue_start;
    time_t start_time;	    /* the time were the job started */
    struct list_head list;  /* the job list header */
} Job_t;

/* list which holds all jobs */
Job_t JobList;


void initJobList();

/**
 * @brief Delete all jobs.
 *
 * @return No return value.
 */
void clearJobList();

/**
 * @brief Add a new job.
 *
 * @param jobid The id of the job.
 *
 * @return Returns the new created job structure.
 */
void *addJob(const char *plugin, const char *jobid, uid_t uid, gid_t gid,
		int nrOfNodes, PSnodes_ID_t *nodes,
		Pelogue_JobCb_Func_t *pluginCallback);

/**
 * @brief Find a job by its job id.
 *
 * @param id The id of the job to find.
 *
 * @return Returns a pointer to the job or NULL if the
 * job was not found.
 */
Job_t *findJobByJobId(const char *plugin, const char *jobid);

/**
 * @brief Delete a job.
 *
 * @param jobid The id of the job to delete.
 *
 * @return returns 0 on error and 1 on success.
 */
int deleteJob(Job_t *job);

/**
 * @brief Find a jobid in the job history.
 *
 * @param jobid The jobid to find.
 *
 * @return Returns 1 on success and 0 on error.
 */
int isJobIDinHistory(char *jobid);

Job_Node_List_t *findJobNodeEntry(Job_t *job, PSnodes_ID_t id);

const char *getPluginFromJob(char *jobid);

char *jobState2String(JobState_t state);

int countJobs();

void signalJobs(int signal, char *reason);

int isValidJobPointer(Job_t *jobPtr);

#endif
