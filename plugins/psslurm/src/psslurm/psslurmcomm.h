/*
 * ParaStation
 *
 * Copyright (C) 2014 - 2015 ParTec Cluster Competence Center GmbH, Munich
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

typedef int Connection_CB_t(Slurm_Msg_t *msg);

typedef struct {
    PSnodes_ID_t *nodes;
    uint32_t nodesCount;
    uint32_t res;
    PS_DataBuffer_t body;
    Slurm_Msg_Header_t head;
} Connection_Forward_t;

typedef struct {
    PS_DataBuffer_t data;
    Connection_CB_t *cb;
    int error;
    int sock;
    time_t recvTime;
    Connection_Forward_t fw;
    struct list_head list;  /* the list header */
} Connection_t;

/* list which holds all jobs */
Connection_t ConnectionList;

void initConnectionList();
Connection_t *addConnection(int socket, Connection_CB_t *cb);
Connection_t *findConnection(int socket);
Connection_t *findConnectionEx(int socket, time_t recvTime);
void closeConnection(int socket);
void clearConnections();

void initSlurmMsg(Slurm_Msg_t *msg);
void freeSlurmMsg(Slurm_Msg_t *sMsg);
void initSlurmMsgHead(Slurm_Msg_Header_t *head);
void freeSlurmMsgHead(Slurm_Msg_Header_t *head);
int sendSlurmMsg(int sock, slurm_msg_type_t type, PS_DataBuffer_t *body);
int sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_DataBuffer_t *body);
void saveForwardedMsgRes(Slurm_Msg_t *sMsg, PS_DataBuffer_t *data,
			    uint32_t error, const char *func, const int line);

int openSlurmdSocket(int port);
const char *msgType2String(int type);
#define getBitString(ptr, bits) __getBitString(ptr, bits, __func__, __LINE__)
void __getBitString(char **ptr, char **bitStr, const char *func,
				const int line);
int tcpConnect(char *addr, char *port);
void getSockInfo(int socket, uint32_t *addr, uint16_t *port);
PSnodes_ID_t getStepLocalNodeID(Step_t *step);

int srunOpenControlConnection(Step_t *step);
int srunOpenIOConnection(Step_t *step);
int srunOpenPTY(Step_t *step);
void srunEnableIO(Step_t *step);
int srunSendIO(uint16_t type, uint16_t taskid, Step_t *step,
		char *buf, uint32_t bufLen);
int srunSendMsg(int sock, Step_t *step, slurm_msg_type_t type,
		PS_DataBuffer_t *body);
void closeAllStepConnections(Step_t *step);

#endif
