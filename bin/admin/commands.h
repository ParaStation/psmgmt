/*
 *               ParaStation
 * commands.h
 *
 * Copyright (C) ParTec AG Karlsruhe
 * All rights reserved.
 *
 * $Id: commands.h,v 1.6 2003/11/26 17:19:17 eicker Exp $
 *
 */
/**
 * \file
 * Commands of the ParaStation adminstration tool
 *
 * $Id: commands.h,v 1.6 2003/11/26 17:19:17 eicker Exp $
 *
 * \author
 * Norbert Eicker <eicker@par-tec.com>
 *
 */
#ifndef __COMMANDS_H
#define __COMMANDS_H

#include "pstask.h"
#include "psprotocol.h"

#ifdef __cplusplus
extern "C" {
#if 0
} /* <- just for emacs indentation */
#endif
#endif

/** @todo Add docu */

extern char commandsversion[];

void PSIADM_sighandler(int signal);

void PSIADM_AddNode(char *nl);
void PSIADM_ShutdownNode(char *nl);
void PSIADM_HWStart(int hw, char *nl);
void PSIADM_HWStop(int hw, char *nl);

void PSIADM_NodeStat(char *nl);
void PSIADM_RDPStat(char *nl);
void PSIADM_MCastStat(char *nl);
void PSIADM_CountStat(int hw, char *nl);
void PSIADM_ProcStat(int count, int full, char *nl);
void PSIADM_LoadStat(char *nl);
void PSIADM_HWStat(char *nl);

void PSIADM_SetParam(PSP_Option_t type, PSP_Optval_t value, char *nl);
void PSIADM_ShowParam(PSP_Option_t type, char *nl);

void PSIADM_Reset(int reset_hw, char *nl);
void PSIADM_TestNetwork(int mode);
void PSIADM_KillProc(PStask_ID_t tid, int sig);

#ifdef __cplusplus
}/* extern "C" */
#endif

#endif /* __COMMANDS_H */
