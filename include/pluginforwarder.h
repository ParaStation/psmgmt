/*
 * ParaStation
 *
 * Copyright (C) 2014 ParTec Cluster Competence Center GmbH, Munich
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

#ifndef __PLUGIN_LIB_FORWARDER
#define __PLUGIN_LIB_FORWARDER

typedef enum {
    CMD_LOCAL_HELLO = 0,
    CMD_LOCAL_SIGNAL,
    CMD_LOCAL_DEBUG,
    CMD_LOCAL_FINISH,
    CMD_LOCAL_QSUB_OUT,
    CMD_LOCAL_CHILD_START,
    CMD_LOCAL_CLOSE,
    CMD_LOCAL_PAM_SSH_REQ,
    CMD_LOCAL_PAM_SSH_REPLY,
    CMD_LOCAL_REQUEST_ACCOUNT,
    CMD_LOCAL_FORK_FAILED,
    CMD_LOCAL_SIGNAL_CHILD
} LocalCommandType_t;

typedef struct {
    char *pTitle;
    char *syslogID;
    char *jobid;
    char *listenSocketName;
    char *logName;
    void *userData;
    int8_t childRerun;
    int32_t timeoutChild;
    int32_t timeoutConnect;
    int32_t graceTime;
    int32_t forwarderID;
    pid_t forwarderPid;
    pid_t childPid;
    pid_t childSid;
    int stdIn[2];
    int stdOut[2];
    int stdErr[2];
    int timeoutConnectId;
    int forwarderError;
    int listenSocket;
    int controlSocket;
    void (*sigHandler)(int);
    int (*killSession)(pid_t, int);
    int (*callback)(int32_t, char *, size_t, void *);
    void (*childFunc)(void *, int);
    void (*hookForwarderInit)(void *);
    void (*hookForwarderLoop)(void *);
    int (*hookHandleSignal)(void *);
    int (*hookHandleMsg)(void *, char *, int32_t cmd);
    void (*hookChildStart)(void *, pid_t, pid_t, pid_t);
} Forwarder_Data_t;

Forwarder_Data_t *getNewForwarderData();
void destroyForwarderData(Forwarder_Data_t *data);

int startForwarder(Forwarder_Data_t *data);
int signalForwarderChild(Forwarder_Data_t *data, int signal);

#endif
