/*
 *               ParaStation
 *
 * Copyright (C) 2011 ParTec Cluster Competence Center GmbH, Munich
 *
 * $Id$
 *
 */
/**
 * @file
 * Helper functions for environment handling.
 *
 * $Id$
 *
 * @author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __PSIDENV_H
#define __PSIDENV_H

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/**
 * @brief Initialize environment stuff
 *
 * Initialize the environment handling framework. This also registers
 * the necessary message handlers.
 *
 * @return No return value.
 */
void initEnvironment(void);

/**
 * @brief Send info on environment.
 *
 * Send information on the environment variable @a key and its values
 * currently set in the local daemon. All information about a single
 * variable (i.e. name and value) is given back in a character
 * string. If @a key is pointing to a character string with value '*'
 * information on all variables is sent.
 *
 * @param dest Task ID of process waiting for answer.
 *
 * @param key Name of the environment variable to send info about
 *
 * @return No return value.
 */
void PSID_sendEnvList(PStask_ID_t dest, char *key);


#ifdef __cplusplus
}/* extern "C" */
#endif

#endif  /* __PSIDENV_H */
