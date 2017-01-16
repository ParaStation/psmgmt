/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PS_PAM_USER
#define __PS_PAM_USER

#include <list.h>

typedef struct {
    char *name;
    char *plugin;
    int state;
    struct list_head list;
} User_t;

User_t UserList;

void initUserList(void);
void addUser(char *username, char *plugin, int state);
User_t *findUser(char *username, char *plugin);
void deleteUser(char *username, char *plugin);
void clearUserList(void);
void setState(char *username, char *plugin, int state);

void psPamAddUser(char *username, char *plugin, int state);
void psPamDeleteUser(char *username, char *plugin);
void psPamSetState(char *username, char *plugin, int state);

#endif
