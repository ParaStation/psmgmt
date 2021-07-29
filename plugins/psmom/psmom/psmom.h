/*
 * ParaStation
 *
 * Copyright (C) 2010-2016 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PS_MOM_MAIN
#define __PS_MOM_MAIN

/* generic torque version support */
extern int torqueVer;

/* psmom version number */
extern int version;

/* resmon protocol version number */
extern unsigned int RM_PROTOCOL_VER;

/* task manager protocol version number */
extern unsigned int TM_PROTOCOL_VER;

/* inter-manager (mom) protocol number */
extern unsigned int IM_PROTOCOL_VER;

/* inter-server protocol number */
extern unsigned int IS_PROTOCOL_VER;

/** default rm port (tcp/udp) */
extern int rmPort;

/** default mom port (tcp) */
extern int momPort;

/** flag to identify if the current process is the main psmom process */
extern int isMaster;

/** flag which will be set to 1 if we are currently in the shutdown process */
extern int doShutdown;

/** set to the maximal size of a pwnam structure */
extern long pwBufferSize;

/** set to the home directory of root */
extern char rootHome[100];

#endif  /* __PS_MOM_MAIN */
