/*
 *               ParaStation
 * commands.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: commands.h,v 1.5 2003/10/30 16:29:03 eicker Exp $
 *
 */
/**
 * \file
 * Commands of the ParaStation adminstration tool
 *
 * $Id: commands.h,v 1.5 2003/10/30 16:29:03 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __COMMANDS_H
#define __COMMANDS_H

#include "pstask.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Add docu */

extern char commandsversion[];

void PSIADM_Init(void);
void PSIADM_sighandler(int signal);

void PSIADM_AddNode(char *nl);
void PSIADM_ShutdownNode(char *nl);
void PSIADM_HWStart(int hw, char *nl);
void PSIADM_HWStop(int hw, char *nl);

void PSIADM_NodeStat(char *nl);
void PSIADM_RDPStat(char *nl);
void PSIADM_MCastStat(char *nl);
void PSIADM_CountStat(int hw, char *nl);
void PSIADM_ProcStat(char *nl, int full);
void PSIADM_LoadStat(char *nl);
void PSIADM_HWStat(char *nl);

void PSIADM_SetMaxProc(int count, char *nl);
void PSIADM_ShowMaxProc(char *nl);
void PSIADM_SetUser(int uid, char *nl);
void PSIADM_ShowUser(char *nl);
void PSIADM_SetGroup(int gid, char *nl);
void PSIADM_ShowGroup(char *nl);

void PSIADM_SetParam(int type, int value, char *nl);
void PSIADM_ShowParam(int type, char *nl);

void PSIADM_Version(void);

void PSIADM_Reset(int reset_hw, char *nl);
void PSIADM_TestNetwork(int mode);
void PSIADM_KillProc(PStask_ID_t tid, int sig);

void PSIADM_Exit(void);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __COMMANDS_H */
