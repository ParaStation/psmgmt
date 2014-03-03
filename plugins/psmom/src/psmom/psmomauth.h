/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_MOM_AUTH
#define __PS_MOM_AUTH

#include "list.h"

typedef struct {
    unsigned long ipaddr;
    struct list_head list;
} Auth_t;

/** the list head of the auth list */
Auth_t AuthList;

void initAuthList();
int isAuthIP(unsigned long ipaddr);
void addAuthIP(unsigned long ipaddr);
void clearAuthList();

#endif
