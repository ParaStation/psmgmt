/*
 * ParaStation
 *
 * Copyright (C) 2011-2013 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PS_MOM_PBS_SERVER
#define __PS_MOM_PBS_SERVER

#include <time.h>

#include "psmomcomm.h"

typedef struct {
    char *addr;
    ComHandle_t* com;
    time_t lastContact;
    int haveConnection;
    struct list_head list;
} Server_t;

/** list which holds all known PBS servers */
extern Server_t ServerList;

void initServerList(void);
int openServerConnections(void);
void clearServerList(void);
Server_t *findServer(ComHandle_t *com);
Server_t *findServerByrAddr(char *addr);

/**
 * @brief Send a status update to all pbs servers.
 *
 * @return No return value.
 */
void sendStatusUpdate(void);

#endif  /* __PS_MOM_PBS_SERVER */
