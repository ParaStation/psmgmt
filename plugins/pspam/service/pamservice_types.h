/*
 * ParaStation
 *
 * Copyright (C) 2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PAMSERVICE_TYPES
#define __PAMSERVICE_TYPES

#include <stdbool.h>

/**
 * @brief Start PAM service
 *
 * Start a PAM service for the user @a user. In a first step this
 * creates a PAM context for this user, authenticates the user,
 * attaches a pseudoterminal, etc. In a second step it will setup a PAM
 * session for the user associated to the local process.
 *
 * This is a prerequisite to setup further PAM sessions for this user
 * in descendant processes via @ref pamserviceOpenSession().
 *
 * @param user Name of the user to start the PAM service for
 *
 * @return Return true on success or false in case of error
 */
typedef bool (pamserviceStartService_t)(char *user);

/**
 * @brief Open PAM session
 *
 * Setup a PAM session for the user @a user associated to the local
 * process. This requires that @ref pamserviceStartService() was
 * called before for this user in one of the ancestor processes.
 *
 * @param user Name of the user to create the PAM session for
 *
 * @return Return true on success or false in case of error
 */
typedef bool (pamserviceOpenSession_t)(char *user);

/**
 * @brief Stop PAM service
 *
 * Stop the PAM service that is currently active and that was created
 * via pamserviceStartService()
 *
 * @return Return true on success or false in case of error
 */
typedef bool (pamserviceStopService_t)(void);

#endif /* __PAMSERVICE_TYPES */
