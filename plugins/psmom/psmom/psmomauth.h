/*
 * ParaStation
 *
 * Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_AUTH
#define __PS_MOM_AUTH

#include "list.h"

typedef struct {
    unsigned long ipaddr;
    struct list_head list;
} Auth_t;

/** the list head of the auth list */
extern Auth_t AuthList;

void initAuthList(void);
int isAuthIP(unsigned long ipaddr);
void addAuthIP(unsigned long ipaddr);
void clearAuthList(void);

#endif  /* __PS_MOM_AUTH */
