/*
 * ParaStation
 *
 * Copyright (C) 2011-2020 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSPAM_SSH
#define __PSPAM_SSH

#include <stdbool.h>
#include <sys/types.h>

#include "list.h"

/** Structure holding all information concerning a specific ssh session */
typedef struct {
    list_t next;       /**< used to put into list of sessions */
    char *user;        /**< username of the ssh-session owner */
    char *rhost;       /**< remote host the ssh-session came from */
    pid_t pid;         /**< pid of the SSH forwarder */
    pid_t sid;         /**< session ID of the SSH forwarder */
    time_t startTime;  /**< time when the ssh-session was started */
} Session_t;

/**
 * @brief Add new SSH session
 *
 * Add a new SSH session identified by the username @a user, the
 * remote host @a rhost, its process ID @a sshPid and its session ID
 * @a sshSid.
 *
 * @param user Name of the user a session shall be added for
 *
 * @param rhost (Remote) host the SSH session was started from
 *
 * @param sshPid Process ID of the SSH session to add
 *
 * @param sshSID Session ID of the SSH session to add
 *
 * @return Returns the new session on success. Otherwise
 * NULL is returned.
 */
Session_t *addSession(char *user, char *rhost, pid_t sshPid, pid_t sshSid);

/**
 * @brief Search for SSH session PID belongs to
 *
 * Identify if the process with ID @a pid belongs to a SSH session
 * registered within pspam.
 *
 * @param pid Process ID of the process the check
 *
 * @return If a session was found, true is returned. Otherwise false
 * is returned.
 */
bool findSessionForPID(pid_t pid);

/**
 * @brief Remove SSH session
 *
 * Remove the SSH session identified by the username @a user, and its
 * process ID @a sshPid.
 *
 * @param user Name of the user identifying the session to remove
 *
 * @param sshPid Process ID of the SSH session to remove
 *
 * @return No return value
 */
void rmSession(char *user, pid_t sshPid);

/**
 * @brief Terminate a user's SSH sessions and free the memory
 *
 * @param user Name of the user to terminate SSH session for
 *
 * @return No return value
 */
void killSessions(char *user);

/**
 * @brief Terminate all ssh clients and free all memory
 *
 * @return No return value
 */
void clearSessionList(void);

/**
 * @brief List current sessions
 *
 * List current sessions and put all information into the buffer @a
 * buf. Upon return @a bufSize indicates the current size of @a buf.
 *
 * @param buf Buffer to write all information to
 *
 * @param bufSize Size of the buffer
 *
 * @return Pointer to buffer with updated session information
 */
char *listSessions(char *buf, size_t *bufSize);

#endif /* __PSPAM_SSH */
