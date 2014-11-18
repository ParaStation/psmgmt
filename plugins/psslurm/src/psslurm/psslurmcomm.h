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

#ifndef __PS_SLURM_COMM
#define __PS_SLURM_COMM

#include "psslurmjob.h"
#include "plugincomm.h"
#include "slurmmsg.h"

typedef struct {
    uint16_t version;
    uint16_t flags;
    uint16_t type;
    uint32_t bodyLen;
    uint16_t forward;
    uint16_t returnList;
    uint32_t addr;
    uint16_t port;
    uid_t uid;
    gid_t gid;
} Slurm_msg_header_t;

typedef int Connection_CB_t(int, void*, size_t, int);

typedef struct {
    PS_DataBuffer_t data;
    Connection_CB_t *cb;
    int error;
    int sock;
    struct list_head list;  /* the list header */
} Connection_t;

/* list which holds all jobs */
Connection_t ConnectionList;

void initConnectionList();
Connection_t *addConnection(int socket, Connection_CB_t *cb);
Connection_t *findConnection(int socket);
void closeConnection(int socket);
void clearConnections();

int openSlurmdSocket(int port);
int sendSlurmMsg(int sock, slurm_msg_type_t type, PS_DataBuffer_t *body,
		    void *sockData);
const char *msgType2String(int type);
#define getBitString(ptr, bits) __getBitString(ptr, bits, __func__, __LINE__)
void __getBitString(char **ptr, char **bitStr, const char *func,
				const int line);
int tcpConnect(char *addr, char *port);
void getSockInfo(int socket, uint32_t *addr, uint16_t *port);

int srunOpenControlConnection(Step_t *step);
int srunOpenIOConnection(Step_t *step);
int srunOpenPTY(Step_t *step);
int srunSendIO(uint16_t type, Step_t *step, char *buf, uint32_t bufLen);
int srunSendMsg(int sock, Step_t *step, slurm_msg_type_t type,
		PS_DataBuffer_t *body);
void closeAllStepConnections(Step_t *step);
#endif
