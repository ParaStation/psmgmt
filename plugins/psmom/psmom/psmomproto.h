/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_PROTO
#define __PS_MOM_PROTO

#include <stdint.h>

#include "psmomcomm.h"
#include "psmomjob.h"
#include "psmomlist.h"

int handleNewData(ComHandle_t *com);
int WriteIS(ComHandle_t *com, int cmd);
int WriteTM(ComHandle_t *com, int cmd);

/**
 * @brief Simplify sending a TM batch reply.
 *
 */
int WriteTM_Batch_Reply(ComHandle_t *com, int cmd);
int InitIS(ComHandle_t *com);

/**
 * @brief Read a TM command.
 *
 * @param com The com handle to use.
 *
 * @return Returns 1 on success and 0 on error.
 */
int ReadTM(ComHandle_t *com, int *cmd);

int sendTMJobTermination(Job_t *job);
int send_TM_Error(ComHandle_t *com, int32_t err_code, char *err_msg, int clear);
void updateInfoStruct(void);
int updateServerState(ComHandle_t *com);

/**
 * @brief Request job information from the PBS server.
 */
int requestJobInformation(Job_t *job);

/**
 * @brief Cleanup after a job has finished.
 *
 * @param job The job do the cleanup for.
 *
 * @param save When set to true output and account files will be saved in the
 * undelivered directory.
 *
 * @return Returns 0 on success and 1 on error.
 */
int jobCleanup(Job_t *job, int save);

/**
 * @brief Get the PBS node state.
 *
 * @param job The job to get the node state for.
 *
 * @param host The hostname to get the node state for (NULL for myself).
 *
 * @return Returns the requested node state or NULL on error.
 */
Data_Entry_t *getPBSNodeState(char *server, const char *host);

/**
 * @brief Set PBS node state (offline, down).
 *
 * @param job The job to set the node offline for.
 *
 * @param note The note to set, NULL will leave the note unchanged.
 *
 * @param state The node state to set (offline, down).
 *
 * @param host The hostname to set the node state for (NULL for myself).
 *
 * @return Returns 0 on success and 1 on error.
 */
int setPBSNodeState(char *server, char *note, char *state, const char *host);

/**
 * @brief Set a node offline.
 *
 * @param server The server to request the offline state.
 *
 * @param host The host to set offline, NULL for my host.
 *
 * @param note The note will be set to the node only if no other note was set
 * before. If the note is NULL the node field will not be changed.
 *
 * @return No return value.
 */
void setPBSNodeOffline(char *server, const char *host, char *note);

#endif  /* __PS_MOM_PROTO */
