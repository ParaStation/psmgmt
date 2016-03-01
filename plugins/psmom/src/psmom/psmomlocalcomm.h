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

#ifndef __PS_MOM_LOCAL_COMM
#define __PS_MOM_LOCAL_COMM

#include "psmomdef.h"

#define FORWARD_BUFFER_SIZE   1024    /* the size of the buffer used to forward
					data between qsub, psmom, forward, */

typedef enum {
    FORWARDER_COPY = 0,
    FORWARDER_INTER,
    FORWARDER_PELOGUE,
    FORWARDER_JOBSCRIPT
} LocalSenderType_t;

typedef enum {
    CMD_LOCAL_HELLO = 0,
    CMD_LOCAL_SIGNAL,
    CMD_LOCAL_DEBUG,
    CMD_LOCAL_FINISH,
    CMD_LOCAL_QSUB_OUT,
    CMD_LOCAL_CHILD_START,
    CMD_LOCAL_CHILD_EXIT,
    CMD_LOCAL_CLOSE,
    CMD_LOCAL_PAM_SSH_REQ,
    CMD_LOCAL_PAM_SSH_REPLY,
    CMD_LOCAL_REQUEST_ACCOUNT,
    CMD_LOCAL_FORK_FAILED
} LocalCommandType_t;

extern int masterSocket;

ComHandle_t *openLocalConnection(void);
ssize_t localRead(int sock, char *buffer, ssize_t len, const char *caller);
int localWrite(int sock, void *msg, size_t len, const char *caller);
int localDoSend(int sock, const char *caller);
int closeLocalConnetion(int fd);
void openMasterSock(void);
void closeMasterSock(void);

#endif
