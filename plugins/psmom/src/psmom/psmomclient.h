/*
 * ParaStation
 *
 * Copyright (C) 2010-2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef PS_MOM_CLIENT
#define PS_MOM_CLIENT

#include "list.h"

typedef struct {
    unsigned long ipaddr;
    struct list_head list;
    time_t last_contact;
} Client_t;

/** the list head of the client list */
Client_t ClientList;

/**
 * @brief Initialize the client list.
 *
 * @return No return value.
 */
void initClientList();

/**
 * @brief Add a new client.
 *
 * @param ipaddr The ip address of the client to add.
 *
 * @return Returns a pointer the new created client structure or NULL on error.
 */
Client_t *addClient(unsigned long ipaddr);

/**
 * @brief Update a client.
 *
 * @param ipaddr The ip address of the client to update.
 *
 * @return No return value.
 */
void updateClient(unsigned long ipaddr);

#endif
