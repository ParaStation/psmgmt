/*
 *               ParaStation
 *
 * Copyright (C) 2017 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2023-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */
/**
 * @file
 * Common definitions between pspam plugin and the actual PAM module
 */
#ifndef __PSPAM_COMMON
#define __PSPAM_COMMON

/** Maximum length of username pspam can handle (Linux limits this to 32) */
#define USERNAME_LEN 64

/** Maximum length of FQDN pspam can handle (RFC 1035 limits this to 255 */
#define HOSTNAME_LEN 256

/** abstract socket name the pspam plugin is listening to */
#define pspamSocketName "\0pspam.sock"

/** Result of the pspam plugin analysis on allowance to access */
typedef enum {
    PSPAM_RES_DENY = 0,   /**< No specific allowance -> deny access */
    PSPAM_RES_BATCH,      /**< Running batch-job -> grant access */
    PSPAM_RES_ADMIN_USER, /**< Admin user -> grant access */
    PSPAM_RES_PROLOG,     /**< Prologue still running -> deny access */
    PSPAM_RES_JAIL,	  /**< Jailing SSH processes failed -> deny access */
} PSPAMResult_t;

typedef enum {
    PSPAM_CMD_SESS_OPEN = 0,   /**< Open session request from PAM module */
    PSPAM_CMD_SESS_CLOSE,      /**< Close session info from PAM module */
} PSPAMCmd_t;

#endif /* __PSPAM_COMMON */
