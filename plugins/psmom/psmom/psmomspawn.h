/*
 * ParaStation
 *
 * Copyright (C) 2010-2019 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_SPAWN
#define __PS_MOM_SPAWN

#include <stdbool.h>
#include <pwd.h>

#include "psmomcomm.h"
#include "psmomforwarder.h"
#include "psmomjob.h"

/**
 * @brief Send a msg to all hosts in the job to start their PElogue
 * scripts.
 *
 * @param job Pointer to a job structure.
 *
 * @param prologue If set to true a prologue script is started, otherwise an
 * epilogue script is started.
 *
 * @return Returns 1 on success and 0 on error.
 */
int sendPElogueStart(Job_t *job, bool prologue);

/**
 * @brief Setup various output fds.
 *
 * @param outlog Set the stdout file.
 *
 * @param errlog Set the stderr file.
 *
 * @param stdinFD Set the stdin file descriptor.
 *
 * @param logFD Set the log file descriptor.
 *
 * @return No return value.
 */
void setOutputFDs(char *outlog, char *errlog, int stdinFD, int logFD);

/**
 * @brief Find the correct user shell.
 *
 * @param job The job to find the user shell for.
 *
 * @param login Flag which is set to true if we need a login shell.
 *
 * @param noblock If set to true we will only query non-blocking sources.
 *
 * @return Returns the request shell.
 */
char *findUserShell(Job_t *job, int login, int noblock);

/**
 * @brief Set linux resource limits.
 *
 * Here we can set ulimits/nice only, because we are already in
 * the child process.
 *
 * @return No return value.
 */
void setResourceLimits(Job_t *job);

/**
 * @brief Switch to a user including groups,gid and uid.
 *
 * @param username The name of the user to switch to.
 */
void psmomSwitchUser(char *username, struct passwd *spasswd, int saveEnv);

/**
 * @brief Spawn a copy job.
 *
 * @param data A pointer to a structure holding all the neccessary information
 * for the copy job.
 *
 * @return Returns 1 on error and 0 on success.
 */
int spawnCopyScript(Copy_Data_t *data);

/**
 * @brief Spawn an interactive job.
 *
 * Connect to the waiting qsub and execute an interactive shell,
 * forwarding all input/output in between.
 *
 * @param job Job structure which holds all neccessary information to spawn the
 * job.
 *
 * @return Returns 1 on error and 0 on success.
 */
int spawnInteractiveJob(Job_t *job);

/**
 * @brief Start an interacitve job.
 *
 * @param job Pointer to the job structure to start.
 *
 * @param com The communication handle to the waiting interactive forwarder.
 *
 * @retun No return value.
 */
void startInteractiveJob(Job_t *job, ComHandle_t *com);

/**
 * @brief Stop an interactive job.
 *
 * Release the waiting forwarder. After that the callback for the forwarder will
 * continue with the job termination.
 *
 * @param job The job to release the forwarder.
 *
 * @return No return value.
 */
void stopInteractiveJob(Job_t *job);

/**
 * @brief Spawn a jobscript.
 *
 * @param job Job structure which holds all neccessary information to spawn the
 * job.
 *
 * @return No return value.
 */
void spawnJobScript(Job_t *job);

/**
 * @brief Cleanup leftover processes/logins after a job end.
 *
 * @param user The username which was running the job.
 *
 * @return No return value.
 */
void afterJobCleanup(char *user);

/**
 * @brief Handle a failed spawn/fork call.
 *
 * We have most certainly a memory problem. So try to set myself offline with a
 * appropriate message. A new TCP/IP connection to the server will be opended,
 * but no forking is necessary.
 *
 * @return No return value.
 */
void handleFailedSpawn(void);

#endif  /* __PS_MOM_SPAWN */
