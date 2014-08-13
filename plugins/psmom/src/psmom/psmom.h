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

#ifndef __PS_MOM_MAIN
#define __PS_MOM_MAIN

#include "psidcomm.h"

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
int isMaster;

/** flag which will be set to 1 if we are currently in the shutdown process */
extern int doShutdown;

/** set to the maximal size of a pwnam structure */
extern long pwBufferSize;

/** set to the home directory of root */
extern char rootHome[100];

extern handlerFunc_t oldSpawnReqHandler;

/**
 * @brief Constructor for psmom library.
 *
 * @return No return value.
 */
void __attribute__ ((constructor)) startPsmom();

/**
 * @brief Destructor for psmom library.
 *
 * @return No return value.
 */
void __attribute__ ((destructor)) stopPsmom();

/**
 * @brief Initialize the psmom plugin.
 *
 * @return Returns 1 on error and 0 on success.
 */
int initialize(void);

/**
 * @brief Prepare and beginn shutdown.
 *
 * @return No return value.
 */
void finalize(void);

/**
 * @brief Free left memory, final cleanup.
 *
 * After this function we will be unloaded.
 *
 * @return No return value.
 */
void cleanup(void);

#endif
