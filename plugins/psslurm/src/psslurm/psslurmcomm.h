/*
 * ParaStation
 *
 * Copyright (C) 2014-2016 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
#ifndef __PSSLURM_COMM
#define __PSSLURM_COMM

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

void initConnectionList(void);
void clearConnections(void);

void initSlurmMsg(Slurm_Msg_t *msg);
void freeSlurmMsg(Slurm_Msg_t *sMsg);
void initSlurmMsgHead(Slurm_Msg_Header_t *head);
void freeSlurmMsgHead(Slurm_Msg_Header_t *head);
int sendSlurmMsg(int sock, slurm_msg_type_t type, PS_DataBuffer_t *body);
int sendSlurmMsgEx(int sock, Slurm_Msg_Header_t *head, PS_DataBuffer_t *body);

#define saveForwardedMsgRes(sMsg, data, error) \
	    __saveForwardedMsgRes(sMsg, data, error, __func__, __LINE__);
void __saveForwardedMsgRes(Slurm_Msg_t *sMsg, PS_DataBuffer_t *data,
			    uint32_t error, const char *func, const int line);
void handleBrokenConnection(PSnodes_ID_t nodeID);

int openSlurmdSocket(int port);
void closeSlurmdSocket(void);
const char *msgType2String(int type);
#define getBitString(ptr, bits) __getBitString(ptr, bits, __func__, __LINE__)
void __getBitString(char **ptr, char **bitStr, const char *func,
		    const int line);
int tcpConnect(char *addr, char *port);
int tcpConnectU(uint32_t addr, uint16_t port);
void getSockInfo(int socket, uint32_t *addr, uint16_t *port);

int srunOpenControlConnection(Step_t *step);
int srunOpenIOConnection(Step_t *step, int sock, char *sig);
int srunOpenPTY(Step_t *step);
void srunEnableIO(Step_t *step);
int srunSendIO(uint16_t type, uint32_t taskid, Step_t *step,
	       char *buf, uint32_t bufLen);
int srunSendIOSock(uint16_t type, uint32_t taskid, int sock,
		   char *buf, uint32_t bufLen, int *error);
int srunSendMsg(int sock, Step_t *step, slurm_msg_type_t type,
		PS_DataBuffer_t *body);
int handleSrunMsg(int sock, void *data);
void closeAllStepConnections(Step_t *step);

#endif  /* __PSSLURM_COMM */
