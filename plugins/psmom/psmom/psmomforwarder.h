/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_FORWARDER
#define __PS_MOM_FORWARDER

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "pscommon.h"

#include "pbsdef.h"
#include "psmomcomm.h"

typedef struct {
    char *jobid;
    char *jobname;
    char *user;
    char *group;
    char *limits;
    char *queue;
    char *sessid;
    char *exec_host;
    char *dirScripts;
    char *tmpDir;
    char *nameExt;
    char *resources_used;
    char *gpus;
    char *server;
    bool frontend;
    bool prologue;
    time_t start_time;
    int32_t exit;
    int32_t timeout;
    PStask_ID_t mainMom;
} PElogue_Data_t;

typedef struct {
    char *local;
    char *remote;
    int flag;
} Copy_Data_files_t;

typedef struct {
    char *jobid;
    char *hashname;
    char *jobowner;
    char *execuser;
    char *execgroup;
    int count;
    int direction;
    Copy_Data_files_t **files;
    ComHandle_t *com;
} Copy_Data_t;

typedef struct {
    char windowsize[QSUB_DATA_SIZE];
    char termtype[QSUB_DATA_SIZE];
    char termcontrol[QSUB_DATA_CONTROL];
} Inter_Data_t;


/**
 * @brief Convert a forwarder type into a string.
 *
 * @param type The type of the forwarder.
 *
 * @return Returns the requested string or NULL on error.
 */
char *fwType2Str(int type);

/**
 * @brief Execute a forwarder for the copy phase.
 *
 * This function must be executed in a seperate child process.
 *
 * @param info Pointer to a Copy_Data structure which holds all
 * needed information for the copy process.
 *
 * @return Returns 1 on error or the exit code of the copy child is returned.
 */
int execCopyForwarder(void *info);

/**
 * @brief Execute a forwarder for an interactive job.
 *
 * This function must be executed in a seperate child process.
 *
 * @param info A pointer to the job structure.
 *
 * @return Returns 1 on error or the exit code of the interactive child.
 */
int execInterForwarder(void *info);

/**
 * @brief Execute a prologue/epilogue forwarder.
 *
 * This function must be executed in a seperate child process.
 *
 * @param info Pointer to a PElogue_Data structure which holds all necessary
 * information for the pelogue process.
 *
 * @return Returns 1 on error or the exit code of the last executed
 * PElogue script.
 */
int execPElogueForwarder(void *info);

/**
 * @brief Start a jobscript forwarder.
 *
 * This function must be executed in a seperate child process.
 *
 * @param info A pointer to the job structure.
 *
 * @return Returns 1 on error or the exit code of the jobscript.
 */
int execJobscriptForwarder(void *info);

/**
 * @brief Kill the running forwarder child.
 *
 * Kill the running forwarder child and a nice manner. First send a SIGTERM and
 * let it cleanup a defined period of time before sending a SIGKILL.
 *
 * @return No return value.
 */
void killForwarderChild(char *reason);

#endif
