/*
 * ParaStation
 *
 * Copyright (C) 2013-2020 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2022-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifndef __PSPLUGIN_PROTOCOL
#define __PSPLUGIN_PROTOCOL

/** Various message types used by plugins */
#define PSP_PLUG_ACCOUNT	    0x0200  /**< psaccount message */
#define PSP_PLUG_PELOGUE	    0x0201  /**< pelogue message */
#define PSP_PLUG_PSSLURM	    0x0202  /**< psslurm message */
#define PSP_PLUG_PSEXEC		    0x0203  /**< psexec message */
#define PSP_PLUG_PSPMIX		    0x0204  /**< pspmix message */
#define PSP_PLUG_PSGW		    0x0205  /**< psgw message */
#define PSP_PLUG_NODEINFO	    0x0206  /**< nodeinfo message */
#define PSP_PLUG_RRCOMM		    0x0207  /**< rank routed comm message */

#endif  /* __PSPLUGIN_PROTOCOL */
