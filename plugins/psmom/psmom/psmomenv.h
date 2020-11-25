/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_ENVIRONMENT
#define __PS_MOM_ENVIRONMENT

#include "list.h"
#include "psmomjob.h"

typedef struct {
    char *var;		    /* an environment string in format 'name=value' */
    struct list_head list;  /* the env list header */
} Env_t;

/* list which holds users environment variables */
extern Env_t EnvList;

/**
 * @brief Initialize the environment list.
 *
 * @return No return value.
 */
void initEnvList(void);

/**
 * @brief Add an environment string to the list.
 *
 * Add an environment string in the format 'name=value' to the list.
 *
 * @param envStr The string to add.
 *
 * @return Returns the added list entry.
 */
Env_t *addEnv(char *envStr);

/**
 * @brief Get an environment array holding a values from the environment list.
 *
 * @param count An integer which will receive the number of array entries.
 *
 * @return Returns the requested environment array.
 */
char **getEnvArray(int* count);

/**
 * @brief Get the value from a environment entry.
 *
 * @param name The name of the entry.
 *
 * @return Returns the requested value or NULL on error.
 */
char *getEnvValue(char *name);

/**
 * @brief Set all environment variables in the environment list.
 *
 * Use putenv() to transfer all environment variables from the list to the
 * current environment.
 *
 * @return No return value.
 */
void setEnvVars(void);

/**
 * @brief Setup the PBS job environment.
 *
 * Use different information sources to setup a proper PBS job environment. The
 * environment is saved to the environment list. The list can be transformed
 * into an array using getEnvArray() or written to the current environment using
 * setEnvVars().
 *
 * @param job The job the setup the environment for.
 *
 * @param interactive Flag to indicate if the job is interactive.
 *
 * @return No return value.
 */
void setupPBSEnv(Job_t *job, int interactive);

#endif  /* __PS_MOM_ENVIRONMENT */
