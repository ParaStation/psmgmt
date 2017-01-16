/*
 * ParaStation
 *
 * Copyright (C) 2011-2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PAM_SSH
#define __PS_PAM_SSH

#include "list.h"

typedef struct {
    char *user;		    /* username of the job owner */
    char *rhost;	    /* remote host of the user connected to us */
    pid_t pid;		    /* pid of the SSH forwarder */
    pid_t sid;		    /* session ID of the SSH forwarder */
    time_t start_time;	    /* time when the ssh session was started */
    struct list_head list;  /* the SSH list header */
} SSHSession_t;

/* list which holds all ssh sessions */
SSHSession_t SSHList;

/**
 * @brief Initialize the ssh list.
 *
 * @return No return value.
 */
void initSSHList(void);

/**
 * @brief Terminate all ssh clients and free all memory.
 *
 * @return No return value.
 */
void clearSSHList(void);

/**
 * @brief Add a new SSH session.
 *
 * @param user The name of the user.
 *
 * @param rhost The remote(starter) host of the SSH session.
 *
 * @param sshPid The pid of the SSH session.
 *
 * @param sshSID The sid of the SSH session.
 *
 * @return Returns the created SSH session.
 */
SSHSession_t *addSSHSession(char *user, char *rhost, pid_t sshPid,
			    pid_t sshSid);

/**
 * @brief Terminate a SSH session and free the memory.
 *
 * @param user The user to terminate the SSH session for.
 *
 * @return No return value.
 */
void delSSHSessions(char *user);

/**
 * @brief Try to find a SSH session for a pid.
 *
 * @param pid The pid to identify the SSH session.
 *
 * @return Returns found SSH session or NULL otherwise.
 */
SSHSession_t *findSSHSessionforPID(pid_t pid);

#endif
