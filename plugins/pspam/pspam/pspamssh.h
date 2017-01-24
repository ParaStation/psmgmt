/*
 * ParaStation
 *
 * Copyright (C) 2011-2017 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSPAM_SSH
#define __PSPAM_SSH

#include <stdbool.h>
#include <sys/types.h>

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
 * @return If a new SSH session was added true is returned. Otherwise
 * false is returned.
 */
bool addSession(char *user, char *rhost, pid_t sshPid, pid_t sshSid);

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

#endif /* __PSPAM_SSH */
