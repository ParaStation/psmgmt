/*
 * ParaStation
 *
 * Copyright (C) 2010-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_CHILD
#define __PS_MOM_CHILD

#include "list.h"
#include "psmomjob.h"
#include "psmomcomm.h"

typedef enum {
    PSMOM_CHILD_PROLOGUE = 1,
    PSMOM_CHILD_EPILOGUE,
    PSMOM_CHILD_JOBSCRIPT,
    PSMOM_CHILD_COPY,
    PSMOM_CHILD_INTERACTIVE
} PSMOM_child_types_t;

typedef struct {
    pid_t pid;			/* pid of the forwarder */
    pid_t c_pid;		/* pid of forwarder's child (e.g. jobscript) */
    pid_t c_sid;		/* sesssion id of forwarder's child */
    PSMOM_child_types_t type;	/* type of forwarder (e.g. interactive) */
    struct timeval start_time;	/* the start time of the forwarder */
    long fw_timeout;		/* forwarder's walltime timeout in seconds */
    char *jobid;		/* the PBS jobid */
    int childMonitorId;		/* timerid to montior the timeout */
    int killFlag;		/* set to 1 if we are waiting to send SIGKILL */
    int signalFlag;		/* child was canceld on request by TERM/KILL */
    ComHandle_t *sharedComm;	/* communication handle connected to child */
    struct list_head list;
} Child_t;

/** the list head of the child list */
extern Child_t ChildList;

/**
 * @brief Initialize the child list.
 *
 * @return No return value.
 */
void initChildList(void);

/**
 * @brief Convert a child type to string.
 *
 * @param type The child type to convert.
 *
 * @return Returns the requested type as string or NULL on error.
 */
char *childType2String(int type);

/**
 * @brief Delete all children.
 *
 * @return No return value.
 */
void clearChildList(void);

/**
 * @brief Add a new child.
 *
 * @param pid The process pid of the child to add.
 *
 * @param type The type of the child.
 *
 * @param jobid The corresponding jobid of the child.
 *
 * @return Returns a pointer the new created child structure or NULL on error.
 */
Child_t *addChild(pid_t pid, PSMOM_child_types_t type, char *jobid);

/**
 * @brief Delete a child which is identified by its pid.
 *
 * @param pid The pid of the child to delete.
 *
 * @return Returns 0 on error and 1 on success.
 */
int deleteChild(pid_t pid);

/**
 * @brief Find a child which is identified by its pid.
 *
 * @param pid The pid of the child to find.
 *
 * @return Returns a pointer to the child requested or NULL on error.
 */
Child_t *findChild(pid_t pid);

/**
 * @brief Find a child which is identified by a its jobid and its type.
 *
 * @param jobid The jobid of the child to find.
 *
 * @param type The type of the child to find.
 *
 * @return Returns a pointer to the child requested or NULL on error.
 */
Child_t *findChildByJobid(char *jobid, int type);

/**
 * @brief Set a new walltime timeout for a child.
 *
 * @param child The child to set the timeout for.
 *
 * @param timeout The new timeout in secons to set.
 *
 * @param grace If set to true the grace time will be added to the timeout.
 *
 * @return No return value.
 */
void setChildTimeout(Child_t *child, time_t timeout, int addGrace);

#endif  /* __PS_MOM_CHILD */
