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

#ifndef __PS_SLURM_CONFIG
#define __PS_SLURM_CONFIG

#include "pluginconfig.h"

#define SPOOL_DIR LOCALSTATEDIR "/spool/parastation"

/** The plugin configuration list. */
Config_t Config;

/** The slurm configuration list. */
Config_t SlurmConfig;

/** The slurm gres configuration list. */
Config_t SlurmGresTmp;
Config_t SlurmGresConfig;

extern const ConfDef_t CONFIG_VALUES[];

int initConfig(char *filename);

#endif
